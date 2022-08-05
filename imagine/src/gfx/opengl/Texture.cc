/*  This file is part of Imagine.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Imagine.  If not, see <http://www.gnu.org/licenses/> */

#define LOGTAG "GLTexture"
#include <imagine/gfx/RendererCommands.hh>
#include <imagine/gfx/RendererTask.hh>
#include <imagine/gfx/Renderer.hh>
#include <imagine/gfx/Texture.hh>
#include <imagine/base/Error.hh>
#include <imagine/util/ScopeGuard.hh>
#include <imagine/util/utility.h>
#include <imagine/util/math/int.hh>
#include <imagine/util/bit.hh>
#include <imagine/data-type/image/PixmapSource.hh>
#include "utils.hh"
#include <cstdlib>
#include <algorithm>

#ifndef GL_TEXTURE_SWIZZLE_R
#define GL_TEXTURE_SWIZZLE_R 0x8E42
#endif

#ifndef GL_TEXTURE_SWIZZLE_G
#define GL_TEXTURE_SWIZZLE_G 0x8E43
#endif

#ifndef GL_TEXTURE_SWIZZLE_B
#define GL_TEXTURE_SWIZZLE_B 0x8E44
#endif

#ifndef GL_TEXTURE_SWIZZLE_A
#define GL_TEXTURE_SWIZZLE_A 0x8E45
#endif

#ifndef GL_TEXTURE_SWIZZLE_RGBA
#define GL_TEXTURE_SWIZZLE_RGBA 0x8E46
#endif

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#endif

#ifndef GL_RGB5
#define GL_RGB5 0x8050
#endif

namespace IG::Gfx
{

static int makeUnpackAlignment(uintptr_t addr)
{
	// find best alignment with lower 3 bits
	static constexpr int map[]
	{
		8, 1, 2, 1, 4, 1, 2, 1
	};
	return map[addr & 7];
}

static int unpackAlignForAddrAndPitch(const void *srcAddr, uint32_t pitch)
{
	int alignmentForAddr = makeUnpackAlignment((uintptr_t)srcAddr);
	int alignmentForPitch = makeUnpackAlignment(pitch);
	if(alignmentForAddr < alignmentForPitch)
	{
		/*logMsg("using lowest alignment of address %p (%d) and pitch %d (%d)",
			srcAddr, alignmentForAddr, pitch, alignmentForPitch);*/
	}
	return std::min(alignmentForPitch, alignmentForAddr);
}

static GLenum makeGLDataType(IG::PixelFormatID format)
{
	switch(format)
	{
		case PIXEL_RGBA8888:
		case PIXEL_BGRA8888:
			if constexpr(!Config::Gfx::OPENGL_ES)
			{
				return GL_UNSIGNED_INT_8_8_8_8_REV;
			} [[fallthrough]];
		case PIXEL_RGB888:
		case PIXEL_I8:
		case PIXEL_IA88:
		case PIXEL_A8:
			return GL_UNSIGNED_BYTE;
		case PIXEL_RGB565:
			return GL_UNSIGNED_SHORT_5_6_5;
		case PIXEL_RGBA5551:
			return GL_UNSIGNED_SHORT_5_5_5_1;
		case PIXEL_RGBA4444:
			return GL_UNSIGNED_SHORT_4_4_4_4;
		default: bug_unreachable("format == %d", format);
	}
}

static GLenum makeGLFormat(const Renderer &r, IG::PixelFormatID format)
{
	switch(format)
	{
		case PIXEL_I8:
			return r.support.luminanceFormat;
		case PIXEL_IA88:
			return r.support.luminanceAlphaFormat;
		case PIXEL_A8:
			return r.support.alphaFormat;
		case PIXEL_RGB888:
		case PIXEL_RGB565:
			return GL_RGB;
		case PIXEL_RGBA8888:
		case PIXEL_RGBA5551:
		case PIXEL_RGBA4444:
			return GL_RGBA;
		case PIXEL_BGRA8888:
			assert(r.support.hasBGRPixels);
			return GL_BGRA;
		default: bug_unreachable("format == %d", format);
	}
}

static GLenum makeGLESInternalFormat(const Renderer &r, IG::PixelFormatID format)
{
	if(Config::envIsIOS && format == PIXEL_BGRA8888) // Apple's BGRA extension loosens the internalformat match requirement
		return GL_RGBA;
	return makeGLFormat(r, format); // OpenGL ES manual states internalformat always equals format
}

static GLenum makeGLSizedInternalFormat(const Renderer &r, IG::PixelFormatID format, bool isSrgb)
{
	switch(format)
	{
		case PIXEL_BGRA8888:
		case PIXEL_RGBA8888:
			return isSrgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
		case PIXEL_RGB565:
			return Config::Gfx::OPENGL_ES ? GL_RGB565 : GL_RGB5;
		case PIXEL_RGBA5551:
			return GL_RGB5_A1;
		case PIXEL_RGBA4444:
			return GL_RGBA4;
		case PIXEL_I8:
			return r.support.luminanceInternalFormat;
		case PIXEL_IA88:
			return r.support.luminanceAlphaInternalFormat;
		case PIXEL_A8:
			return r.support.alphaInternalFormat;
		default: bug_unreachable("format == %d", format);
	}
}

static int makeGLInternalFormat(const Renderer &r, PixelFormatID format, bool isSrgb)
{
	return Config::Gfx::OPENGL_ES ? makeGLESInternalFormat(r, format)
		: makeGLSizedInternalFormat(r, format, isSrgb);
}

static TextureType typeForPixelFormat(PixelFormatID format)
{
	return (format == PIXEL_A8) ? TextureType::T2D_1 :
		(format == PIXEL_IA88) ? TextureType::T2D_2 :
		TextureType::T2D_4;
}

static TextureConfig configWithLoadedImagePixmap(PixmapDesc desc, bool makeMipmaps, const TextureSampler *compatSampler)
{
	TextureConfig config{desc};
	config.setWillGenerateMipmaps(makeMipmaps);
	config.setCompatSampler(compatSampler);
	return config;
}

static ErrorCode loadImageSource(Texture &texture, Data::PixmapSource img, bool makeMipmaps)
{
	auto imgPix = img.pixmapView();
	uint32_t writeFlags = makeMipmaps ? Texture::WRITE_FLAG_MAKE_MIPMAPS : 0;
	if(imgPix)
	{
		//logDMsg("writing image source pixmap to texture");
		texture.write(0, imgPix, {}, writeFlags);
	}
	else
	{
		auto lockBuff = texture.lock(0);
		if(!lockBuff) [[unlikely]]
			return {ENOMEM};
		//logDMsg("writing image source into texture pixel buffer");
		img.write(lockBuff.pixmap());
		texture.unlock(lockBuff, writeFlags);
	}
	return {};
}

MutablePixmapView LockedTextureBuffer::pixmap() const
{
	return pix;
}

IG::WindowRect LockedTextureBuffer::sourceDirtyRect() const
{
	return srcDirtyRect;
}

LockedTextureBuffer::operator bool() const
{
	return (bool)pix;
}

Texture::Texture(RendererTask &r, TextureConfig config):
	GLTexture{r}
{
	init(r, config);
}

Texture::Texture(RendererTask &r, IG::Data::PixmapSource img, const TextureSampler *compatSampler, bool makeMipmaps):
	GLTexture{r}
{
	init(r, configWithLoadedImagePixmap(img.pixmapView().desc(), makeMipmaps, compatSampler));
	loadImageSource(*static_cast<Texture*>(this), img, makeMipmaps);
}

TextureConfig GLTexture::baseInit(RendererTask &r, TextureConfig config)
{
	if(config.willGenerateMipmaps() && !r.renderer().support.hasImmutableTexStorage)
	{
		// when using glGenerateMipmaps exclusively, there is no need to define
		// all texture levels with glTexImage2D beforehand
		config.setLevels(1);
	}
	return config;
}

void GLTexture::init(RendererTask &r, TextureConfig config)
{
	config = baseInit(r, config);
	static_cast<Texture*>(this)->setFormat(config.pixmapDesc(), config.levels(), config.colorSpace(), config.compatSampler());
}

void destroyGLTextureRef(RendererTask &task, TextureRef texName)
{
	logMsg("deleting texture:0x%X", texName);
	task.run(
		[texName]()
		{
			glDeleteTextures(1, &texName);
		});
}

int Texture::bestAlignment(PixmapView p)
{
	return unpackAlignForAddrAndPitch(p.data(), p.pitchBytes());
}

bool GLTexture::canUseMipmaps(const Renderer &r) const
{
	return r.support.textureSizeSupport.supportsMipmaps(pixDesc.w(), pixDesc.h());
}

bool Texture::canUseMipmaps() const
{
	return GLTexture::canUseMipmaps(renderer());
}

GLenum GLTexture::target() const
{
	return Config::Gfx::OPENGL_TEXTURE_TARGET_EXTERNAL && type_ == TextureType::T2D_EXTERNAL ?
			GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D;
}

bool Texture::generateMipmaps()
{
	if(!texName()) [[unlikely]]
	{
		logErr("called generateMipmaps() on uninitialized texture");
		return false;
	}
	if(!canUseMipmaps())
		return false;
	task().run(
		[&r = std::as_const(renderer()), texName = texName()]()
		{
			glBindTexture(GL_TEXTURE_2D, texName);
			logMsg("generating mipmaps for texture:0x%X", texName);
			r.support.generateMipmaps(GL_TEXTURE_2D);
		});
	updateLevelsForMipmapGeneration();
	return true;
}

int Texture::levels() const
{
	return levels_;
}

ErrorCode Texture::setFormat(PixmapDesc desc, int levels, ColorSpace colorSpace, const TextureSampler *compatSampler)
{
	assumeExpr(desc.w());
	assumeExpr(desc.h());
	if(renderer().support.textureSizeSupport.supportsMipmaps(desc.w(), desc.h()))
	{
		if(!levels)
			levels = fls(static_cast<unsigned>(desc.w() | desc.h()));
	}
	else
	{
		levels = 1;
	}
	SamplerParams samplerParams = compatSampler ? compatSampler->samplerParams() : SamplerParams{};
	if(renderer().support.hasImmutableTexStorage)
	{
		bool isSrgb = renderer().supportedColorSpace(desc.format(), colorSpace) == ColorSpace::SRGB;
		task().runSync(
			[=, &r = std::as_const(renderer()), &texNameRef = texName_.get()](GLTask::TaskContext ctx)
			{
				auto texName = makeGLTextureName(texNameRef);
				texNameRef = texName;
				ctx.notifySemaphore();
				glBindTexture(GL_TEXTURE_2D, texName);
				auto internalFormat = makeGLSizedInternalFormat(r, desc.format(), isSrgb);
				logMsg("texture:0x%X storage size:%dx%d levels:%d internal format:%s %s",
					texName, desc.w(), desc.h(), levels, glImageFormatToString(internalFormat),
					desc.format() == IG::PIXEL_BGRA8888 ? "write format:BGRA" : "");
				runGLChecked(
					[&]()
					{
						r.support.glTexStorage2D(GL_TEXTURE_2D, levels, internalFormat, desc.w(), desc.h());
					}, "glTexStorage2D()");
				setSwizzleForFormatInGL(r, desc.format(), texName);
				setSamplerParamsInGL(r, samplerParams);
			});
	}
	else
	{
		bool remakeTexName = levels != levels_; // make new texture name whenever number of levels changes
		task().GLTask::run(
			[=, &r = std::as_const(renderer()), &texNameRef = texName_.get(), currTexName = texName()](GLTask::TaskContext ctx)
			{
				auto texName = currTexName; // a copy of texName_ is passed by value for the async case to avoid accessing this->texName_
				if(remakeTexName)
				{
					texName = makeGLTextureName(texName);
					texNameRef = texName;
					ctx.notifySemaphore();
				}
				glBindTexture(GL_TEXTURE_2D, texName);
				auto format = makeGLFormat(r, desc.format());
				auto dataType = makeGLDataType(desc.format());
				auto internalFormat = makeGLInternalFormat(r, desc.format(), false);
				logMsg("texture:0x%X storage size:%dx%d levels:%d internal format:%s image format:%s:%s %s",
					texName, desc.w(), desc.h(), levels, glImageFormatToString(internalFormat),
					glImageFormatToString(format), glDataTypeToString(dataType),
					desc.format() == IG::PIXEL_BGRA8888 && internalFormat != GL_BGRA ? "write format:BGRA" : "");
				int w = desc.w(), h = desc.h();
				for(auto i : iotaCount(levels))
				{
					runGLChecked(
						[&]()
						{
							glTexImage2D(GL_TEXTURE_2D, i, internalFormat, w, h, 0, format, dataType, nullptr);
						}, "glTexImage2D()");
					w = std::max(1, (w / 2));
					h = std::max(1, (h / 2));
				}
				setSwizzleForFormatInGL(r, desc.format(), texName);
				if(remakeTexName)
					setSamplerParamsInGL(r, samplerParams);
			}, remakeTexName);
	}
	updateFormatInfo(desc, levels);
	return {};
}

void GLTexture::bindTex(RendererCommands &cmds) const
{
	if(!texName()) [[unlikely]]
	{
		logErr("called bindTex() on uninitialized texture");
		return;
	}
	cmds.glcBindTexture(target(), texName());
}

void Texture::writeAligned(int level, PixmapView pixmap, IG::WP destPos, int assumeAlign, uint32_t writeFlags)
{
	//logDMsg("writing pixmap %dx%d to pos %dx%d", pixmap.x, pixmap.y, destPos.x, destPos.y);
	if(!texName()) [[unlikely]]
	{
		logErr("called writeAligned() on uninitialized texture");
		return;
	}
	auto &r = renderer();
	assumeExpr(destPos.x + pixmap.w() <= size(level).x);
	assumeExpr(destPos.y + pixmap.h() <= size(level).y);
	assumeExpr(pixmap.format().bytesPerPixel() == pixDesc.format().bytesPerPixel());
	if(!assumeAlign)
		assumeAlign = unpackAlignForAddrAndPitch(pixmap.data(), pixmap.pitchBytes());
	if((uintptr_t)pixmap.data() % (uintptr_t)assumeAlign != 0)
	{
		bug_unreachable("expected data from address %p to be aligned to %u bytes", pixmap.data(), assumeAlign);
	}
	auto hasUnpackRowLength = r.support.hasUnpackRowLength;
	bool makeMipmaps = writeFlags & WRITE_FLAG_MAKE_MIPMAPS && canUseMipmaps();
	if(hasUnpackRowLength || !pixmap.isPadded())
	{
		task().run(
			[=, &r = std::as_const(r), texName = texName()]()
			{
				glBindTexture(GL_TEXTURE_2D, texName);
				glPixelStorei(GL_UNPACK_ALIGNMENT, assumeAlign);
				if(hasUnpackRowLength)
					glPixelStorei(GL_UNPACK_ROW_LENGTH, pixmap.pitchPixels());
				GLenum format = makeGLFormat(r, pixmap.format());
				GLenum dataType = makeGLDataType(pixmap.format());
				runGLCheckedVerbose(
					[&]()
					{
						glTexSubImage2D(GL_TEXTURE_2D, level, destPos.x, destPos.y,
							pixmap.w(), pixmap.h(), format, dataType, pixmap.data());
					}, "glTexSubImage2D()");
				if(makeMipmaps)
				{
					logMsg("generating mipmaps for texture:0x%X", texName);
					r.support.generateMipmaps(GL_TEXTURE_2D);
				}
			}, !(writeFlags & WRITE_FLAG_ASYNC));
		if(makeMipmaps)
		{
			updateLevelsForMipmapGeneration();
		}
	}
	else
	{
		// must copy to buffer without extra pitch pixels
		logDMsg("texture:%u needs temporary buffer to copy pixmap with width:%d pitch:%d", texName(), pixmap.w(), pixmap.pitchPixels());
		IG::WindowRect lockRect{{}, pixmap.size()};
		lockRect += destPos;
		auto lockBuff = lock(level, lockRect);
		if(!lockBuff) [[unlikely]]
		{
			logErr("error getting buffer for writeAligned()");
			return;
		}
		assumeExpr(pixmap.format().bytesPerPixel() == lockBuff.pixmap().format().bytesPerPixel());
		lockBuff.pixmap().write(pixmap);
		unlock(lockBuff, writeFlags);
	}
}

void Texture::write(int level, PixmapView pixmap, IG::WP destPos, uint32_t commitFlags)
{
	writeAligned(level, pixmap, destPos, bestAlignment(pixmap), commitFlags);
}

void Texture::clear(int level)
{
	auto lockBuff = lock(level, BUFFER_FLAG_CLEARED);
	if(!lockBuff) [[unlikely]]
	{
		logErr("error getting buffer for clear()");
		return;
	}
	unlock(lockBuff);
}

LockedTextureBuffer Texture::lock(int level, uint32_t bufferFlags)
{
	return lock(level, {{}, size(level)}, bufferFlags);
}

LockedTextureBuffer Texture::lock(int level, IG::WindowRect rect, uint32_t bufferFlags)
{
	if(!texName()) [[unlikely]]
	{
		logErr("called lock() on uninitialized texture");
		return {};
	}
	assumeExpr(rect.x2  <= size(level).x);
	assumeExpr(rect.y2 <= size(level).y);
	const auto bufferBytes = pixDesc.format().pixelBytes(rect.xSize() * rect.ySize());
	char *data;
	if(bufferFlags & BUFFER_FLAG_CLEARED)
		data = (char*)std::calloc(1, bufferBytes);
	else
		data = (char*)std::malloc(bufferBytes);
	if(!data) [[unlikely]]
	{
		logErr("failed allocating %u bytes for pixel buffer", bufferBytes);
		return {};
	}
	MutablePixmapView pix{{rect.size(), pixDesc.format()}, data};
	return {data, pix, rect, level, true};
}

void Texture::unlock(LockedTextureBuffer lockBuff, uint32_t writeFlags)
{
	if(!lockBuff) [[unlikely]]
		return;
	if(lockBuff.pbo())
	{
		assert(renderer().support.hasPBOFuncs);
	}
	bool makeMipmaps = writeFlags & WRITE_FLAG_MAKE_MIPMAPS && canUseMipmaps();
	if(makeMipmaps)
	{
		updateLevelsForMipmapGeneration();
	}
	task().run(
		[&r = std::as_const(renderer()), pix = lockBuff.pixmap(), bufferOffset = lockBuff.bufferOffset(),
		 texName = texName(), destPos = IG::WP{lockBuff.sourceDirtyRect().x, lockBuff.sourceDirtyRect().y},
		 pbo = lockBuff.pbo(), level = lockBuff.level(),
		 shouldFreeBuffer = lockBuff.shouldFreeBuffer(), makeMipmaps]()
		{
			glBindTexture(GL_TEXTURE_2D, texName);
			glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignForAddrAndPitch(nullptr, pix.pitchBytes()));
			if(pbo)
			{
				assumeExpr(r.support.hasUnpackRowLength);
				glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
				glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
				r.support.glFlushMappedBufferRange(GL_PIXEL_UNPACK_BUFFER, (GLintptr)bufferOffset, pix.bytes());
			}
			else
			{
				if(r.support.hasUnpackRowLength)
					glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
			}
			GLenum format = makeGLFormat(r, pix.format());
			GLenum dataType = makeGLDataType(pix.format());
			runGLCheckedVerbose(
				[&]()
				{
					glTexSubImage2D(GL_TEXTURE_2D, level, destPos.x, destPos.y,
						pix.w(), pix.h(), format, dataType, bufferOffset);
				}, "glTexSubImage2D()");
			if(pbo)
			{
				glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
			}
			else if(shouldFreeBuffer)
			{
				std::free(pix.data());
			}
			if(makeMipmaps)
			{
				logMsg("generating mipmaps for texture:0x%X", texName);
				r.support.generateMipmaps(GL_TEXTURE_2D);
			}
		});
}

IG::WP Texture::size(int level) const
{
	assert(levels_);
	int w = pixDesc.w(), h = pixDesc.h();
	for(auto i : iotaCount(level))
	{
		w = std::max(1, (w / 2));
		h = std::max(1, (h / 2));
	}
	return {(int)w, (int)h};
}

PixmapDesc Texture::pixmapDesc() const
{
	return pixDesc;
}

void Texture::setCompatTextureSampler(const TextureSampler &compatSampler)
{
	if(renderer().support.hasSamplerObjects)
		return;
	task().run(
		[&r = std::as_const(renderer()), texName = texName(), params = compatSampler.samplerParams()]()
		{
			GLTextureSampler::setTexParamsInGL(texName, GL_TEXTURE_2D, params);
		});
}

static CommonProgram commonProgramForMode(TextureType type, EnvMode mode)
{
	switch(mode)
	{
		case EnvMode::REPLACE:
			switch(type)
			{
				case TextureType::T2D_1 : return CommonProgram::TEX_ALPHA_REPLACE;
				case TextureType::T2D_2 : return CommonProgram::TEX_REPLACE;
				case TextureType::T2D_4 : return CommonProgram::TEX_REPLACE;
				#ifdef CONFIG_GFX_OPENGL_TEXTURE_TARGET_EXTERNAL
				case TextureType::T2D_EXTERNAL : return CommonProgram::TEX_EXTERNAL_REPLACE;
				#endif
				default:
					bug_unreachable("no default program for texture type:%d", std::to_underlying(type));
			}
		case EnvMode::MODULATE:
			switch(type)
			{
				case TextureType::T2D_1 : return CommonProgram::TEX_ALPHA;
				case TextureType::T2D_2 : return CommonProgram::TEX;
				case TextureType::T2D_4 : return CommonProgram::TEX;
				#ifdef CONFIG_GFX_OPENGL_TEXTURE_TARGET_EXTERNAL
				case TextureType::T2D_EXTERNAL : return CommonProgram::TEX_EXTERNAL;
				#endif
				default:
					bug_unreachable("no default program for texture type:%d", (int)type);
			}
		default:
			bug_unreachable("no default program for texture mode:%d", std::to_underlying(mode));
	}
}

bool Texture::compileDefaultProgram(EnvMode mode) const
{
	return renderer().makeCommonProgram(commonProgramForMode(type_, mode));
}

bool Texture::compileDefaultProgramOneShot(EnvMode mode) const
{
	auto compiled = compileDefaultProgram(mode);
	if(compiled)
		renderer().autoReleaseShaderCompiler();
	return compiled;
}

void Texture::useDefaultProgram(RendererCommands &cmds, EnvMode mode, const Mat4 *modelMat) const
{
	renderer().useCommonProgram(cmds, commonProgramForMode(type_, mode), modelMat);
}

void Texture::useDefaultProgram(RendererCommands &cmds, EnvMode mode, Mat4 modelMat) const
{
	useDefaultProgram(cmds, mode, &modelMat);
}

Texture::operator bool() const
{
	return texName();
}

Renderer &Texture::renderer() const
{
	return GLTexture::renderer();
}

RendererTask &Texture::task() const
{
	return GLTexture::task();
}

Texture::operator TextureSpan() const
{
	return {this};
}

GLuint GLTexture::texName() const
{
	return texName_.get();
}

RendererTask *GLTexture::taskPtr() const
{
	return texName_.get_deleter().rTaskPtr;
}

Renderer &GLTexture::renderer() const
{
	return task().renderer();
}

RendererTask &GLTexture::task() const
{
	assumeExpr(taskPtr());
	return *taskPtr();
}

static void verifyCurrentTexture2D(TextureRef tex)
{
	if(!Config::DEBUG_BUILD)
		return;
	GLint realTexture = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &realTexture);
	if(tex != (GLuint)realTexture)
	{
		bug_unreachable("out of sync, expected %u but got %u, TEXTURE_2D", tex, realTexture);
	}
}

void GLTexture::setSwizzleForFormatInGL(const Renderer &r, PixelFormatID format, GLuint tex)
{
	if(r.support.useFixedFunctionPipeline || !r.support.hasTextureSwizzle)
		return;
	verifyCurrentTexture2D(tex);
	const GLint swizzleMaskRGBA[] {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
	const GLint swizzleMaskIA88[] {GL_RED, GL_RED, GL_RED, GL_GREEN};
	const GLint swizzleMaskA8[] {GL_ONE, GL_ONE, GL_ONE, GL_RED};
	if constexpr((bool)Config::Gfx::OPENGL_ES)
	{
		auto &swizzleMask = (format == PIXEL_IA88) ? swizzleMaskIA88
				: (format == PIXEL_A8) ? swizzleMaskA8
				: swizzleMaskRGBA;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, swizzleMask[0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, swizzleMask[1]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, swizzleMask[2]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, swizzleMask[3]);
	}
	else
	{
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, (format == PIXEL_IA88) ? swizzleMaskIA88
				: (format == PIXEL_A8) ? swizzleMaskA8
				: swizzleMaskRGBA);
	}
}

void GLTexture::setSamplerParamsInGL(const Renderer &r, SamplerParams params, GLenum target)
{
	if(r.support.hasSamplerObjects || !params.magFilter)
		return;
	GLTextureSampler::setTexParamsInGL(target, params);
}

void GLTexture::updateFormatInfo(PixmapDesc desc, int8_t levels, GLenum target)
{
	assert(levels);
	levels_ = levels;
	pixDesc = desc;
	#ifdef CONFIG_GFX_OPENGL_SHADER_PIPELINE
	if(Config::Gfx::OPENGL_TEXTURE_TARGET_EXTERNAL && target == GL_TEXTURE_EXTERNAL_OES)
		type_ = TextureType::T2D_EXTERNAL;
	else
		type_ = typeForPixelFormat(desc.format());
	#endif
}

#ifdef __ANDROID__
void GLTexture::initWithEGLImage(EGLImageKHR eglImg, PixmapDesc desc, SamplerParams samplerParams, bool isMutable)
{
	auto &r = renderer();
	if(r.support.hasEGLTextureStorage() && !isMutable)
	{
		task().runSync(
			[=, &r = std::as_const(r), &texNameRef = texName_.get(), formatID = (IG::PixelFormatID)desc.format()](GLTask::TaskContext ctx)
			{
				auto texName = makeGLTextureName(texNameRef);
				texNameRef = texName;
				glBindTexture(GL_TEXTURE_2D, texName);
				if(eglImg)
				{
					logMsg("setting immutable texture:%d with EGL image:%p", texName, eglImg);
					runGLChecked(
						[&]()
						{
							r.support.glEGLImageTargetTexStorageEXT(GL_TEXTURE_2D, (GLeglImageOES)eglImg, nullptr);
						}, "glEGLImageTargetTexStorageEXT()");
				}
				ctx.notifySemaphore();
				setSwizzleForFormatInGL(r, formatID, texName);
				setSamplerParamsInGL(r, samplerParams);
			});
	}
	else
	{
		task().runSync(
			[=, &r = std::as_const(r), &texNameRef = texName_.get(), formatID = (IG::PixelFormatID)desc.format()](GLTask::TaskContext ctx)
			{
				auto texName = texNameRef;
				bool madeTexName = false;
				if(!texName) [[unlikely]] // texture storage is mutable, only need to make name once
				{
					glGenTextures(1, &texName);
					texNameRef = texName;
					madeTexName = true;
				}
				glBindTexture(GL_TEXTURE_2D, texName);
				if(eglImg)
				{
					logMsg("setting texture:%d with EGL image:%p", texName, eglImg);
					runGLChecked(
						[&]()
						{
							glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)eglImg);
						}, "glEGLImageTargetTexture2DOES()");
				}
				ctx.notifySemaphore();
				setSwizzleForFormatInGL(r, formatID, texName);
				if(madeTexName)
					setSamplerParamsInGL(r, samplerParams);
			});
	}
	updateFormatInfo(desc, 1);
}

void GLTexture::updateWithEGLImage(EGLImageKHR eglImg)
{
	task().GLTask::run(
		[=, texName = texName()](GLTask::TaskContext ctx)
		{
			glBindTexture(GL_TEXTURE_2D, texName);
			assumeExpr(eglImg);
			runGLChecked(
				[&]()
				{
					glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)eglImg);
				}, "glEGLImageTargetTexture2DOES()");
		});
}

#endif

void GLTexture::updateLevelsForMipmapGeneration()
{
	if(!renderer().support.hasImmutableTexStorage)
	{
		// all possible levels generated by glGenerateMipmap
		levels_ = fls(static_cast<unsigned>(pixDesc.w() | pixDesc.h()));
	}
}

LockedTextureBuffer GLTexture::lockedBuffer(void *data, int pitchBytes, uint32_t bufferFlags)
{
	auto &tex = *static_cast<Texture*>(this);
	IG::WindowRect fullRect{{}, tex.size(0)};
	MutablePixmapView pix{tex.pixmapDesc(), data, {pitchBytes, MutablePixmapView::Units::BYTE}};
	if(bufferFlags & Texture::BUFFER_FLAG_CLEARED)
		pix.clear();
	return {nullptr, pix, fullRect, 0, false};
}

bool TextureSizeSupport::supportsMipmaps(int imageX, int imageY) const
{
	return imageX && imageY &&
		(nonPow2CanMipmap || (IG::isPowerOf2(imageX) && IG::isPowerOf2(imageY)));
}

}
