/*  This file is part of EmuFramework.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with EmuFramework.  If not, see <http://www.gnu.org/licenses/> */

#include <emuframework/FilePathOptionView.hh>
#include <emuframework/EmuApp.hh>
#include <emuframework/FilePicker.hh>
#include <emuframework/UserPathSelectView.hh>
#include <emuframework/EmuOptions.hh>
#include <imagine/base/ApplicationContext.hh>
#include <imagine/gui/TextTableView.hh>
#include <imagine/gui/AlertView.hh>
#include <imagine/fs/FS.hh>
#include <imagine/util/format.hh>
#include "../pathUtils.hh"
#include <imagine/logger/logger.h>

namespace EmuEx
{

constexpr SystemLogger log{"FilePathOptionView"};

static FS::FileString savePathStrToDisplayName(IG::ApplicationContext ctx, std::string_view savePathStr)
{
	if(savePathStr.size())
	{
		if(savePathStr == optionSavePathDefaultToken)
			return "应用文件夹";
		else
			return ctx.fileUriDisplayName(savePathStr);
	}
	else
	{
		return "内容文件夹";
	}
}

static auto savesMenuName(IG::ApplicationContext ctx, std::string_view savePath)
{
	return std::format("保存: {}", savePathStrToDisplayName(ctx, savePath));
}

static auto screenshotsMenuName(IG::ApplicationContext ctx, std::string_view userPath)
{
	return std::format("截图: {}", userPathToDisplayName(ctx, userPath));
}

FilePathOptionView::FilePathOptionView(ViewAttachParams attach, bool customMenu):
	TableView{"文件路径选项", attach, item},
	savePath
	{
		savesMenuName(appContext(), system().userSaveDirectory()), attach,
		[this](const Input::Event &e)
		{
			auto multiChoiceView = makeViewWithName<TextTableView>("保存", 4);
			multiChoiceView->appendItem("选择文件夹",
				[this](const Input::Event &e)
				{
					auto fPicker = makeView<FilePicker>(FSPicker::Mode::DIR, EmuSystem::NameFilterFunc{}, e);
					auto userSavePath = system().userSaveDirectory();
					fPicker->setPath(userSavePath.size() && userSavePath != optionSavePathDefaultToken ? userSavePath
						: app().contentSearchPath, e);
					fPicker->setOnSelectPath(
						[this](FSPicker &picker, CStringView path, std::string_view displayName, const Input::Event &e)
						{
							if(!hasWriteAccessToDir(path))
							{
								app().postErrorMessage("此文件夹缺少写入访问权限");
								return;
							}
							system().setUserSaveDirectory(path);
							onSavePathChange(path);
							picker.popTo();
							dismissPrevious();
							picker.dismiss();
						});
					pushAndShowModal(std::move(fPicker), e);
				});
			multiChoiceView->appendItem("与内容相同",
				[this](View &view)
				{
					system().setUserSaveDirectory("");
					onSavePathChange("");
					view.dismiss();
				});
			multiChoiceView->appendItem("应用文件夹",
				[this](View &view)
				{
					system().setUserSaveDirectory(optionSavePathDefaultToken);
					onSavePathChange(optionSavePathDefaultToken);
					view.dismiss();
				});
			multiChoiceView->appendItem("旧版游戏数据文件夹",
				[this](View &view, const Input::Event &e)
				{
					pushAndShowModal(makeView<YesNoAlertView>(
						std::format("Please select the \"Game Data/{}\" folder from an old version of the app to use its existing saves "
							"and convert it to a regular save path (this is only needed once)", system().shortSystemName()),
						YesNoAlertView::Delegates
						{
							.onYes = [this](const Input::Event &e)
							{
								auto fPicker = makeView<FilePicker>(FSPicker::Mode::DIR, EmuSystem::NameFilterFunc{}, e);
								fPicker->setPath("");
								fPicker->setOnSelectPath(
									[this](FSPicker &picker, CStringView path, std::string_view displayName, const Input::Event &e)
									{
										auto ctx = appContext();
										if(!hasWriteAccessToDir(path))
										{
											app().postErrorMessage("此文件夹缺少写入访问权限");
											return;
										}
										if(ctx.fileUriDisplayName(path) != system().shortSystemName())
										{
											app().postErrorMessage(std::format("请选择 {} 文件夹", system().shortSystemName()));
											return;
										}
										EmuApp::updateLegacySavePath(ctx, path);
										system().setUserSaveDirectory(path);
										onSavePathChange(path);
										picker.popTo();
										dismissPrevious();
										picker.dismiss();
									});
								pushAndShowModal(std::move(fPicker), e);
							}
						}), e);
				});
			pushAndShow(std::move(multiChoiceView), e);
			postDraw();
		}
	},
	screenshotPath
	{
		screenshotsMenuName(appContext(), app().userScreenshotPath), attach,
		[this](const Input::Event &e)
		{
			pushAndShow(makeViewWithName<UserPathSelectView>("截图", app().screenshotDirectory(),
				[this](CStringView path)
				{
					log.info("设置截图路径:{}", path);
					app().userScreenshotPath = path;
					screenshotPath.compile(screenshotsMenuName(appContext(), path));
				}), e);
		}
	}
{
	if(!customMenu)
	{
		loadStockItems();
	}
}

void FilePathOptionView::loadStockItems()
{
	item.emplace_back(&savePath);
	item.emplace_back(&screenshotPath);
}

void FilePathOptionView::onSavePathChange(std::string_view path)
{
	if(path == optionSavePathDefaultToken)
	{
		app().postMessage(4, false, std::format("应用文件夹:\n{}", system().fallbackSaveDirectory()));
	}
	savePath.compile(savesMenuName(appContext(), path));
}

}
