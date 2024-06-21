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

#include <emuframework/GUIOptionView.hh>
#include <emuframework/EmuApp.hh>
#include <emuframework/EmuViewController.hh>
#include <emuframework/EmuOptions.hh>
#include <emuframework/viewUtils.hh>
#include <imagine/base/ApplicationContext.hh>
#include <imagine/gfx/Renderer.hh>
#include <format>

namespace EmuEx
{

static constexpr bool USE_MOBILE_ORIENTATION_NAMES = Config::envIsAndroid || Config::envIsIOS;
static const char *landscapeName = USE_MOBILE_ORIENTATION_NAMES ? "横屏" : "90° 左转";
static const char *landscape2Name = USE_MOBILE_ORIENTATION_NAMES ? "横屏 2" : "90° 右转";
static const char *portraitName = USE_MOBILE_ORIENTATION_NAMES ? "竖屏" : "标准";
static const char *portrait2Name = USE_MOBILE_ORIENTATION_NAMES ? "竖屏 2" : "反转";

GUIOptionView::GUIOptionView(ViewAttachParams attach, bool customMenu):
	TableView{"用户界面选项", attach, item},
	pauseUnfocused
	{
		"未聚焦时暂停", attach,
		app().pauseUnfocused,
		[this](BoolMenuItem &item)
		{
			app().pauseUnfocused = item.flipBoolValue(*this);
		}
	},
	fontSizeItem
	{
		{"2",  attach, {.id = 2000}},
		{"3",  attach, {.id = 3000}},
		{"4",  attach, {.id = 4000}},
		{"5",  attach, {.id = 5000}},
		{"6",  attach, {.id = 6000}},
		{"7",  attach, {.id = 7000}},
		{"8",  attach, {.id = 8000}},
		{"9",  attach, {.id = 9000}},
		{"10", attach, {.id = 10000}},
		{"自定义值", attach,
			[this](const Input::Event &e)
			{
				pushAndShowNewCollectValueRangeInputView<float, 2, 10>(attachParams(), e, "输入 2.0 到 10.0", "",
					[this](CollectTextInputView &, auto val)
					{
						int scaledIntVal = val * 1000.0;
						app().setFontSize(scaledIntVal);
						fontSize.setSelected(MenuId{scaledIntVal}, *this);
						dismissPrevious();
						return true;
					});
				return false;
			}, {.id = defaultMenuId}
		},
	},
	fontSize
	{
		"字体大小", attach,
		MenuId{app().fontSize},
		fontSizeItem,
		{
			.onSetDisplayString = [this](auto idx, Gfx::Text &t)
			{
				t.resetString(std::format("{:g}", app().fontSize / 1000.));
				return true;
			},
			.defaultItemOnSelect = [this](TextMenuItem &item) { app().setFontSize(item.id); }
		},
	},
	notificationIcon
	{
		"暂停的应用程序图标", attach,
		app().showsNotificationIcon,
		[this](BoolMenuItem &item)
		{
			app().showsNotificationIcon = item.flipBoolValue(*this);
		}
	},
	statusBarItem
	{
		{"关闭",    attach, MenuItem::Config{.id = InEmuTristate::Off}},
		{"在模拟时", attach, MenuItem::Config{.id = InEmuTristate::InEmu}},
		{"开启",     attach, MenuItem::Config{.id = InEmuTristate::On}}
	},
	statusBar
	{
		"隐藏状态栏", attach,
		MenuId(InEmuTristate(app().hidesStatusBar.value())),
		statusBarItem,
		MultiChoiceMenuItem::Config
		{
			.defaultItemOnSelect = [this](TextMenuItem &item) { app().setHideStatusBarMode(InEmuTristate(item.id.val)); }
		},
	},
	lowProfileOSNavItem
	{
		{"关闭",    attach, MenuItem::Config{.id = InEmuTristate::Off}},
		{"在模拟时", attach, MenuItem::Config{.id = InEmuTristate::InEmu}},
		{"开启",     attach, MenuItem::Config{.id = InEmuTristate::On}}
	},
	lowProfileOSNav
	{
		"暗淡的用户界面", attach,
		MenuId(InEmuTristate(app().lowProfileOSNav.value())),
		lowProfileOSNavItem,
		MultiChoiceMenuItem::Config
		{
			.defaultItemOnSelect = [this](TextMenuItem &item) { app().setLowProfileOSNavMode(InEmuTristate(item.id.val)); }
		},
	},
	hideOSNavItem
	{
		{"关闭",    attach, MenuItem::Config{.id = InEmuTristate::Off}},
		{"在模拟时", attach, MenuItem::Config{.id = InEmuTristate::InEmu}},
		{"开启",     attach, MenuItem::Config{.id = InEmuTristate::On}}
	},
	hideOSNav
	{
		"隐藏系统导航栏", attach,
		MenuId(InEmuTristate(app().hidesOSNav.value())),
		hideOSNavItem,
		MultiChoiceMenuItem::Config
		{
			.defaultItemOnSelect = [this](TextMenuItem &item) { app().setHideOSNavMode(InEmuTristate(item.id.val)); }
		},
	},
	idleDisplayPowerSave
	{
		"在游戏时允许屏幕超时", attach,
		app().idleDisplayPowerSave,
		[this](BoolMenuItem &item)
		{
			app().setIdleDisplayPowerSave(item.flipBoolValue(*this));
		}
	},
	navView
	{
		"标题栏", attach,
		app().showsTitleBar,
		[this](BoolMenuItem &item)
		{
			app().setShowsTitleBar(item.flipBoolValue(*this));
		}
	},
	backNav
	{
		"标题栏导航", attach,
		attach.viewManager.needsBackControl,
		[this](BoolMenuItem &item)
		{
			manager().needsBackControl = item.flipBoolValue(*this);
			app().viewController().setShowNavViewBackButton(manager().needsBackControl);
			app().viewController().placeElements();
		}
	},
	systemActionsIsDefaultMenu
	{
		"默认菜单", attach,
		app().systemActionsIsDefaultMenu,
		"最后使用", "游戏菜单",
		[this](BoolMenuItem &item)
		{
			app().systemActionsIsDefaultMenu = item.flipBoolValue(*this);
		}
	},
	showBundledGames
	{
		"显示压缩内容", attach,
		app().showsBundledGames,
		[this](BoolMenuItem &item)
		{
			app().setShowsBundledGames(item.flipBoolValue(*this));
		}
	},
	showBluetoothScan
	{
		"显示蓝牙菜单项", attach,
		app().showsBluetoothScan,
		[this](BoolMenuItem &item)
		{
			app().setShowsBluetoothScanItems(item.flipBoolValue(*this));
		}
	},
	showHiddenFiles
	{
		"显示隐藏文件", attach,
		app().showHiddenFilesInPicker,
		[this](BoolMenuItem &item)
		{
			app().showHiddenFilesInPicker = item.flipBoolValue(*this);
		}
	},
	maxRecentContent
	{
		"最大最近游戏项目", std::to_string(app().recentContent.maxRecentContent), attach,
		[this](const Input::Event &e)
		{
			pushAndShowNewCollectValueRangeInputView<int, 1, 100>(attachParams(), e,
				"输入 1 到 100", std::to_string(app().recentContent.maxRecentContent),
				[this](CollectTextInputView &, auto val)
				{
					app().recentContent.maxRecentContent = val;
					maxRecentContent.set2ndName(std::to_string(val));
					return true;
				});
		}
	},
	orientationHeading
	{
		"屏幕方向", attach
	},
	menuOrientationItem
	{
		{"自动",         attach, {.id = Orientations{}}},
		{landscapeName,  attach, {.id = Orientations{.landscapeRight = 1}}},
		{landscape2Name, attach, {.id = Orientations{.landscapeLeft = 1}}},
		{portraitName,   attach, {.id = Orientations{.portrait = 1}}},
		{portrait2Name,  attach, {.id = Orientations{.portraitUpsideDown = 1}}},
	},
	menuOrientation
	{
		"在菜单中", attach,
		MenuId{uint8_t(app().menuOrientation.value())},
		menuOrientationItem,
		{
			.defaultItemOnSelect = [this](TextMenuItem &item) { app().setMenuOrientation(std::bit_cast<Orientations>(uint8_t(item.id))); }
		},
	},
	emuOrientationItem
	{
		{"自动",         attach, {.id = Orientations{}}},
		{landscapeName,  attach, {.id = Orientations{.landscapeRight = 1}}},
		{landscape2Name, attach, {.id = Orientations{.landscapeLeft = 1}}},
		{portraitName,   attach, {.id = Orientations{.portrait = 1}}},
		{portrait2Name,  attach, {.id = Orientations{.portraitUpsideDown = 1}}},
	},
	emuOrientation
	{
		"游戏时", attach,
		MenuId{uint8_t(app().emuOrientation.value())},
		emuOrientationItem,
		{
			.defaultItemOnSelect = [this](TextMenuItem &item) { app().setEmuOrientation(std::bit_cast<Orientations>(uint8_t(item.id))); }
		},
	},
	layoutBehindSystemUI
	{
		"应用界面全屏显示", attach,
		app().doesLayoutBehindSystemUI(),
		[this](BoolMenuItem &item)
		{
			app().setLayoutBehindSystemUI(item.flipBoolValue(*this));
		}
	},
	setWindowSize
	{
		"设置窗口大小", attach,
		[this](const Input::Event &e)
		{
			pushAndShowNewCollectValuePairRangeInputView<int, 320, 8192, 240, 8192>(attachParams(), e,
				"Input Width & Height", "",
				[this](CollectTextInputView &, auto val)
				{
					app().emuWindow().setSize({val.first, val.second});
					return true;
				});
		}
	},
	toggleFullScreen
	{
		"切换全屏", attach,
		[this]{ app().emuWindow().toggleFullScreen(); }
	}
{
	if(!customMenu)
	{
		loadStockItems();
	}
}

void GUIOptionView::loadStockItems()
{
	if(used(pauseUnfocused))
	{
		item.emplace_back(&pauseUnfocused);
	}
	if(app().canShowNotificationIcon(appContext()))
	{
		item.emplace_back(&notificationIcon);
	}
	if(used(navView))
	{
		item.emplace_back(&navView);
	}
	if(ViewManager::needsBackControlIsMutable)
	{
		item.emplace_back(&backNav);
	}
	item.emplace_back(&systemActionsIsDefaultMenu);
	item.emplace_back(&fontSize);
	if(used(setWindowSize))
	{
		item.emplace_back(&setWindowSize);
	}
	if(used(toggleFullScreen))
	{
		item.emplace_back(&toggleFullScreen);
	}
	item.emplace_back(&idleDisplayPowerSave);
	if(used(lowProfileOSNav))
	{
		item.emplace_back(&lowProfileOSNav);
	}
	if(used(hideOSNav))
	{
		item.emplace_back(&hideOSNav);
	}
	if(used(statusBar))
	{
		item.emplace_back(&statusBar);
	}
	if(used(layoutBehindSystemUI) && appContext().hasTranslucentSysUI())
	{
		item.emplace_back(&layoutBehindSystemUI);
	}
	if(EmuSystem::hasBundledGames)
	{
		item.emplace_back(&showBundledGames);
	}
	if(used(showBluetoothScan))
		item.emplace_back(&showBluetoothScan);
	item.emplace_back(&showHiddenFiles);
	item.emplace_back(&maxRecentContent);
	item.emplace_back(&orientationHeading);
	item.emplace_back(&emuOrientation);
	item.emplace_back(&menuOrientation);
}

}
