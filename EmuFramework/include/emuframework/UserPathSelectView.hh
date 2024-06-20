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

#include <emuframework/EmuApp.hh>
#include <emuframework/EmuAppHelper.hh>
#include <emuframework/FilePicker.hh>
#include <imagine/gui/TableView.hh>
#include <imagine/gui/MenuItem.hh>
#include <imagine/util/container/ArrayList.hh>
#include <format>

namespace EmuEx
{

class UserPathSelectView : public TableView, public EmuAppHelper
{
public:
	UserPathSelectView(UTF16Convertible auto &&name, ViewAttachParams attach, FS::PathString initialDir,
		std::invocable<CStringView> auto &&onPathChange):
		TableView{IG_forward(name), attach, item},
		selectFolder
		{
			"选择文件夹", attach,
			[=](View &view, const Input::Event &e)
			{
				auto fPicker = view.makeView<FilePicker>(FSPicker::Mode::DIR, EmuSystem::NameFilterFunc{}, e);
				auto &thisView = asThis(view);
				fPicker->setPath(thisView.searchDir, e);
				fPicker->setOnSelectPath(
					[=](FSPicker &picker, CStringView path, std::string_view displayName, const Input::Event &e)
					{
						onPathChange(path);
						picker.popTo();
						picker.dismissPrevious();
						picker.dismiss();
					});
				thisView.app().pushAndShowModalView(std::move(fPicker), e);
			}
		},
		sameAsContent
		{
			"与游戏一致", attach,
			[=](View &view)
			{
				onPathChange(optionUserPathContentToken);
				view.dismiss();
			}
		},
		sameAsSaves
		{
			"与存档一致", attach,
			[=](View &view)
			{
				onPathChange("");
				view.dismiss();
			}
		},
		searchDir{initialDir}
	{
		item.emplace_back(&selectFolder);
		item.emplace_back(&sameAsContent);
		item.emplace_back(&sameAsSaves);
	};

	void appendItem(TextMenuItem &i) { item.emplace_back(&i); }

protected:
	TextMenuItem selectFolder;
	TextMenuItem sameAsContent;
	TextMenuItem sameAsSaves;
	StaticArrayList<MenuItem*, 4> item;
	FS::PathString searchDir;

	static UserPathSelectView &asThis(View &view) { return static_cast<UserPathSelectView&>(view); }
};

inline FS::FileString userPathToDisplayName(IG::ApplicationContext ctx, std::string_view userPathStr)
{
	if(userPathStr.size())
	{
		if(userPathStr == optionUserPathContentToken)
			return "内容文件夹";
		else
			return ctx.fileUriDisplayName(userPathStr);
	}
	else
	{
		return "保存文件夹";
	}
}

inline auto cheatsMenuName(IG::ApplicationContext ctx, std::string_view userPath)
{
	return std::format("作弊器: {}", std::string_view{userPathToDisplayName(ctx, userPath)});
}

inline auto patchesMenuName(IG::ApplicationContext ctx, std::string_view userPath)
{
	return std::format("补丁: {}", std::string_view{userPathToDisplayName(ctx, userPath)});
}

inline auto palettesMenuName(IG::ApplicationContext ctx, std::string_view userPath)
{
	return std::format("调色板: {}", std::string_view{userPathToDisplayName(ctx, userPath)});
}

}
