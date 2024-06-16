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

#include <emuframework/InputManagerView.hh>
#include <emuframework/ButtonConfigView.hh>
#include <emuframework/EmuApp.hh>
#include <emuframework/EmuViewController.hh>
#include <emuframework/AppKeyCode.hh>
#include <emuframework/EmuOptions.hh>
#include <emuframework/viewUtils.hh>
#include "../InputDeviceData.hh"
#include <imagine/gui/TextEntry.hh>
#include <imagine/gui/TextTableView.hh>
#include <imagine/gui/AlertView.hh>
#include <imagine/base/ApplicationContext.hh>
#include <imagine/gfx/RendererCommands.hh>
#include <imagine/bluetooth/BluetoothAdapter.hh>
#include <imagine/util/ScopeGuard.hh>
#include <imagine/util/format.hh>
#include <imagine/util/variant.hh>
#include <imagine/util/bit.hh>
#include <imagine/logger/logger.h>

namespace EmuEx
{

constexpr SystemLogger log{"InputManagerView"};
static const char *confirmDeleteDeviceSettingsStr = "从配置文件中删除设备设置？保留任何正在使用的关键配置文件";
static const char *confirmDeleteProfileStr = "从配置文件中删除配置文件？使用该配置文件的设备将恢复为默认配置文件";

IdentInputDeviceView::IdentInputDeviceView(ViewAttachParams attach):
	View(attach),
	text{attach.rendererTask, "按下任何输入设备上的按键，进入其配置菜单", &defaultFace()},
	quads{attach.rendererTask, {.size = 1}} {}

void IdentInputDeviceView::place()
{
	quads.write(0, {.bounds = displayRect().as<int16_t>()});
	text.compile({.maxLineSize = int(viewRect().xSize() * 0.95f)});
}

bool IdentInputDeviceView::inputEvent(const Input::Event& e, ViewInputEventParams)
{
	return e.visit(overloaded
	{
		[&](const Input::MotionEvent &e)
		{
			if(e.released())
			{
				dismiss();
				return true;
			}
			return false;
		},
		[&](const Input::KeyEvent &e)
		{
			if(e.pushed())
			{
				auto del = onIdentInput;
				dismiss();
				del(e);
				return true;
			}
			return false;
		}
	});
}

void IdentInputDeviceView::draw(Gfx::RendererCommands&__restrict__ cmds, ViewDrawParams) const
{
	using namespace IG::Gfx;
	auto &basicEffect = cmds.basicEffect();
	cmds.set(BlendMode::OFF);
	basicEffect.disableTexture(cmds);
	cmds.setColor({.4, .4, .4});
	cmds.drawQuad(quads, 0);
	basicEffect.enableAlphaTexture(cmds);
	text.draw(cmds, viewRect().center(), C2DO, ColorName::WHITE);
}

InputManagerView::InputManagerView(ViewAttachParams attach,
	InputManager &inputManager_):
	TableView{"外接手柄设置", attach, item},
	inputManager{inputManager_},
	deleteDeviceConfig
	{
		"删除保存的设备设置", attach,
		[this](TextMenuItem &item, View &, const Input::Event &e)
		{
			auto &savedInputDevs = inputManager.savedInputDevs;
			if(!savedInputDevs.size())
			{
				app().postMessage("没有保存的设备设置");
				return;
			}
			auto multiChoiceView = makeViewWithName<TextTableView>(item, savedInputDevs.size());
			for(auto &ePtr : savedInputDevs)
			{
				multiChoiceView->appendItem(InputDeviceData::makeDisplayName(ePtr->name, ePtr->enumId),
					[this, deleteDeviceConfigPtr = ePtr.get()](const Input::Event &e)
					{
						pushAndShowModal(makeView<YesNoAlertView>(confirmDeleteDeviceSettingsStr,
							YesNoAlertView::Delegates
							{
								.onYes = [this, deleteDeviceConfigPtr]
								{
									log.info("deleting device settings for:{},{}",
										deleteDeviceConfigPtr->name, deleteDeviceConfigPtr->enumId);
									auto ctx = appContext();
									for(auto &devPtr : ctx.inputDevices())
									{
										auto &inputDevConf = inputDevData(*devPtr).devConf;
										if(inputDevConf.hasSavedConf(*deleteDeviceConfigPtr))
										{
											log.info("removing from active device");
											inputDevConf.setSavedConf(inputManager, nullptr);
											break;
										}
									}
									std::erase_if(inputManager.savedInputDevs, [&](auto &ptr){ return ptr.get() == deleteDeviceConfigPtr; });
									dismissPrevious();
								}
							}), e);
					});
			}
			pushAndShow(std::move(multiChoiceView), e);
		}
	},
	deleteProfile
	{
		"删除已保存的按键配置", attach,
		[this](TextMenuItem &item, View &, const Input::Event &e)
		{
			auto &customKeyConfigs = inputManager.customKeyConfigs;
			if(!customKeyConfigs.size())
			{
				app().postMessage("没有保存配置");
				return;
			}
			auto multiChoiceView = makeViewWithName<TextTableView>(item, customKeyConfigs.size());
			for(auto &ePtr : customKeyConfigs)
			{
				multiChoiceView->appendItem(ePtr->name,
					[this, deleteProfilePtr = ePtr.get()](const Input::Event &e)
					{
						pushAndShowModal(makeView<YesNoAlertView>(confirmDeleteProfileStr,
							YesNoAlertView::Delegates
							{
								.onYes = [this, deleteProfilePtr]
								{
									log.info("deleting profile:{}", deleteProfilePtr->name);
									inputManager.deleteKeyProfile(appContext(), deleteProfilePtr);
									dismissPrevious();
								}
							}), e);
					});
			}
			pushAndShow(std::move(multiChoiceView), e);
		}
	},
	rescanOSDevices
	{
		"重新扫描操作系统输入设备", attach,
		[this](const Input::Event &e)
		{
			appContext().enumInputDevices();
			int devices = 0;
			auto ctx = appContext();
			for(auto &e : ctx.inputDevices())
			{
				if(e->map() == Input::Map::SYSTEM)
					devices++;
			}
			app().postMessage(2, false, std::format("{} OS devices present", devices));
		}
	},
	identDevice
	{
		"自动检测设备", attach,
		[this](const Input::Event &e)
		{
			auto identView = makeView<IdentInputDeviceView>();
			identView->onIdentInput =
				[this](const Input::Event &e)
				{
					auto dev = e.device();
					if(dev)
					{
						pushAndShowDeviceView(*dev, e);
					}
				};
			pushAndShowModal(std::move(identView), e);
		}
	},
	generalOptions
	{
		"通用选项", attach,
		[this](const Input::Event &e)
		{
			pushAndShow(makeView<InputManagerOptionsView>(&app().viewController().inputView), e);
		}
	},
	deviceListHeading
	{
		"单个设备设置", attach,
	}
{
	inputManager.onUpdateDevices = [this]()
	{
		popTo(*this);
		auto selectedCell = selected;
		loadItems();
		highlightCell(selectedCell);
		place();
		show();
	};
	deleteDeviceConfig.setActive(inputManager.savedInputDevs.size());
	deleteProfile.setActive(inputManager.customKeyConfigs.size());
	loadItems();
}

InputManagerView::~InputManagerView()
{
	inputManager.onUpdateDevices = {};
}

void InputManagerView::loadItems()
{
	auto ctx = appContext();
	item.clear();
	item.reserve(16);
	item.emplace_back(&identDevice);
	item.emplace_back(&generalOptions);
	item.emplace_back(&deleteDeviceConfig);
	item.emplace_back(&deleteProfile);
	doIfUsed(rescanOSDevices, [&](auto &mItem)
	{
		if(ctx.androidSDK() >= 12 && ctx.androidSDK() < 16)
			item.emplace_back(&mItem);
	});
	item.emplace_back(&deviceListHeading);
	inputDevName.clear();
	inputDevName.reserve(ctx.inputDevices().size());
	for(auto &devPtr : ctx.inputDevices())
	{
		auto &devItem = inputDevName.emplace_back(inputDevData(*devPtr).displayName, attachParams(),
			[this, &dev = *devPtr](const Input::Event &e)
			{
				pushAndShowDeviceView(dev, e);
			});
		if(devPtr->hasKeys() && !devPtr->isPowerButton())
		{
			item.emplace_back(&devItem);
		}
		else
		{
			log.info("not adding device:{} to list", devPtr->name());
		}
	}
}

void InputManagerView::onShow()
{
	TableView::onShow();
	deleteDeviceConfig.setActive(inputManager.savedInputDevs.size());
	deleteProfile.setActive(inputManager.customKeyConfigs.size());
}

void InputManagerView::pushAndShowDeviceView(const Input::Device &dev, const Input::Event &e)
{
	pushAndShow(makeViewWithName<InputManagerDeviceView>(inputDevData(dev).displayName, *this, dev, inputManager), e);
}

InputManagerOptionsView::InputManagerOptionsView(ViewAttachParams attach, EmuInputView *emuInputView_):
	TableView{"常规输入选项", attach, item},
	mogaInputSystem
	{
		"支持 MOGA 控制器", attach,
		app().mogaManagerIsActive(),
		[this](BoolMenuItem &item)
		{
			if(!app().mogaManagerIsActive() && !appContext().packageIsInstalled("com.bda.pivot.mogapgp"))
			{
				app().postMessage(8, "从 Google Play 安装 MOGA Pivot 应用程序，使用您的 MOGA Pocket。 "
					"对于 MOGA Pro 或更新版本，请将开关设置为模式 B，然后在 Android 蓝牙设置应用程序中进行配对。.");
				return;
			}
			app().setMogaManagerActive(item.flipBoolValue(*this), true);
		}
	},
	notifyDeviceChange
	{
		"设备更改时发出通知", attach,
		app().notifyOnInputDeviceChange,
		[this](BoolMenuItem &item)
		{
			app().notifyOnInputDeviceChange = item.flipBoolValue(*this);
		}
	},
	bluetoothHeading
	{
		"应用内蓝牙选项", attach,
	},
	keepBtActive
	{
		"在后台保持连接", attach,
		app().keepBluetoothActive,
		[this](BoolMenuItem &item)
		{
			app().keepBluetoothActive = item.flipBoolValue(*this);
		}
	},
	btScanSecsItem
	{
		{"2secs",  attach, MenuItem::Config{.id = 2}},
		{"4secs",  attach, MenuItem::Config{.id = 4}},
		{"6secs",  attach, MenuItem::Config{.id = 6}},
		{"8secs",  attach, MenuItem::Config{.id = 8}},
		{"10secs", attach, MenuItem::Config{.id = 10}}
	},
	btScanSecs
	{
		"扫描时间", attach,
		MenuId{app().bluetoothAdapter.scanSecs},
		btScanSecsItem,
		MultiChoiceMenuItem::Config
		{
			.defaultItemOnSelect = [this](TextMenuItem &item) { app().bluetoothAdapter.scanSecs = item.id; }
		}
	},
	btScanCache
	{
		"缓存扫描结果", attach,
		app().bluetoothAdapter.useScanCache,
		[this](BoolMenuItem &item)
		{
			app().bluetoothAdapter.useScanCache = item.flipBoolValue(*this);
		}
	},
	altGamepadConfirm
	{
		"交换确认/取消键", attach,
		app().swappedConfirmKeys(),
		[this](BoolMenuItem &item)
		{
			app().setSwappedConfirmKeys(item.flipBoolValue(*this));
		}
	},
	emuInputView{emuInputView_}
{
	if constexpr(MOGA_INPUT)
	{
		item.emplace_back(&mogaInputSystem);
	}
	item.emplace_back(&altGamepadConfirm);
	#if 0
	if(Input::hasTrackball())
	{
		item.emplace_back(&relativePointerDecel);
	}
	#endif
	if(appContext().hasInputDeviceHotSwap())
	{
		item.emplace_back(&notifyDeviceChange);
	}
	if(used(bluetoothHeading))
	{
		item.emplace_back(&bluetoothHeading);
		if(used(keepBtActive))
		{
			item.emplace_back(&keepBtActive);
		}
		if(used(btScanSecs))
		{
			item.emplace_back(&btScanSecs);
		}
		if(used(btScanCache))
		{
			item.emplace_back(&btScanCache);
		}
	}
}

class ProfileSelectMenu : public TextTableView
{
public:
	using ProfileChangeDelegate = DelegateFunc<void (std::string_view profile)>;

	ProfileChangeDelegate onProfileChange{};

	ProfileSelectMenu(ViewAttachParams attach, Input::Device &dev, std::string_view selectedName, const InputManager &mgr):
		TextTableView
		{
			"按键配置",
			attach,
			mgr.customKeyConfigs.size() + 8 // reserve space for built-in configs
		}
	{
		for(auto &confPtr : mgr.customKeyConfigs)
		{
			auto &conf = *confPtr;
			if(conf.desc().map == dev.map())
			{
				if(selectedName == conf.name)
				{
					activeItem = textItem.size();
				}
				textItem.emplace_back(conf.name, attach,
					[this, &conf](const Input::Event &e)
					{
						auto del = onProfileChange;
						dismiss();
						del(conf.name);
					});
			}
		}
		for(const auto &conf : EmuApp::defaultKeyConfigs())
		{
			if(dev.map() != conf.map)
				continue;
			if(selectedName == conf.name)
				activeItem = textItem.size();
			textItem.emplace_back(conf.name, attach,
				[this, &conf](const Input::Event &e)
				{
					auto del = onProfileChange;
					dismiss();
					del(conf.name);
				});
		}
	}
};

static bool customKeyConfigsContainName(auto &customKeyConfigs, std::string_view name)
{
	return std::ranges::find_if(customKeyConfigs, [&](auto &confPtr){ return confPtr->name == name; }) != customKeyConfigs.end();
}

InputManagerDeviceView::InputManagerDeviceView(UTF16String name, ViewAttachParams attach,
	InputManagerView &rootIMView_, const Input::Device &dev, InputManager &inputManager_):
	TableView{std::move(name), attach, item},
	inputManager{inputManager_},
	rootIMView{rootIMView_},
	playerItems
	{
		[&]
		{
			DynArray<TextMenuItem> items{EmuSystem::maxPlayers + 1uz};
			items[0] = {"Multiple", attach, {.id = InputDeviceConfig::PLAYER_MULTI}};
			for(auto i : iotaCount(EmuSystem::maxPlayers))
			{
				items[i + 1] = {playerNumStrings[i], attach, {.id = i}};
			}
			return items;
		}()
	},
	player
	{
		"Player", attach,
		MenuId{inputDevData(dev).devConf.player()},
		playerItems,
		{
			.defaultItemOnSelect = [this](TextMenuItem &item)
			{
				auto playerVal = item.id;
				bool changingMultiplayer = (playerVal == InputDeviceConfig::PLAYER_MULTI && devConf.player() != InputDeviceConfig::PLAYER_MULTI) ||
					(playerVal != InputDeviceConfig::PLAYER_MULTI && devConf.player() == InputDeviceConfig::PLAYER_MULTI);
				devConf.setPlayer(inputManager, playerVal);
				devConf.save(inputManager);
				if(changingMultiplayer)
				{
					loadItems();
					place();
					show();
				}
				else
					onShow();
			}
		},
	},
	loadProfile
	{
		u"", attach,
		[this](const Input::Event &e)
		{
			auto profileSelectMenu = makeView<ProfileSelectMenu>(devConf.device(),
				devConf.keyConf(inputManager).name, inputManager);
			profileSelectMenu->onProfileChange =
					[this](std::string_view profile)
					{
						log.info("set key profile:{}", profile);
						devConf.setKeyConfName(inputManager, profile);
						onShow();
					};
			pushAndShow(std::move(profileSelectMenu), e);
		}
	},
	renameProfile
	{
		"重命名配置文件", attach,
		[this](const Input::Event &e)
		{
			if(!devConf.mutableKeyConf(inputManager))
			{
				app().postMessage(2, "无法重命名内置配置文件");
				return;
			}
			pushAndShowNewCollectValueInputView<const char*>(attachParams(), e, "输入名称", devConf.keyConf(inputManager).name,
				[this](CollectTextInputView &, auto str)
				{
					if(customKeyConfigsContainName(inputManager.customKeyConfigs, str))
					{
						app().postErrorMessage("另一个配置文件已经在使用这个名称");
						postDraw();
						return false;
					}
					devConf.mutableKeyConf(inputManager)->name = str;
					onShow();
					postDraw();
					return true;
				});
		}
	},
	newProfile
	{
		"新建配置文件", attach,
		[this](const Input::Event &e)
		{
			pushAndShowModal(makeView<YesNoAlertView>(
				"创建新的配置文件？将复制当前配置文件中的所有按键。",
				YesNoAlertView::Delegates
				{
					.onYes = [this](const Input::Event &e)
					{
						pushAndShowNewCollectValueInputView<const char*>(attachParams(), e, "输入名称", "",
							[this](CollectTextInputView &, auto str)
							{
								if(customKeyConfigsContainName(inputManager.customKeyConfigs, str))
								{
									app().postErrorMessage("另一个配置文件已经在使用这个名称");
									return false;
								}
								devConf.setKeyConfCopiedFromExisting(inputManager, str);
								log.info("创建新配置文件:{}", devConf.keyConf(inputManager).name);
								onShow();
								postDraw();
								return true;
							});
					}
				}), e);
		}
	},
	deleteProfile
	{
		"删除配置文件", attach,
		[this](const Input::Event &e)
		{
			if(!devConf.mutableKeyConf(inputManager))
			{
				app().postMessage(2, "无法删除内置配置文件");
				return;
			}
			pushAndShowModal(makeView<YesNoAlertView>(confirmDeleteProfileStr,
				YesNoAlertView::Delegates
				{
					.onYes = [this]
					{
						auto conf = devConf.mutableKeyConf(inputManager);
						if(!conf)
						{
							bug_unreachable("确认删除只读按键配置，这种情况绝不应该发生");
						}
						log.info("deleting profile:{}", conf->name);
						inputManager.deleteKeyProfile(appContext(), conf);
					}
				}), e);
		}
	},
	iCadeMode
	{
		"iCade模式", attach,
		inputDevData(dev).devConf.iCadeMode(),
		[this](BoolMenuItem &item, const Input::Event &e)
		{
			if constexpr(Config::envIsIOS)
			{
				confirmICadeMode();
			}
			else
			{
				if(!item.boolValue())
				{
					pushAndShowModal(makeView<YesNoAlertView>(
						"该模式允许从兼容 iCade 的蓝牙设备输入信息, 如果这不是 iCade，就不要启用", "开启", "取消",
						YesNoAlertView::Delegates{.onYes = [this]{ confirmICadeMode(); }}), e);
				}
				else
					confirmICadeMode();
			}
		}
	},
	consumeUnboundKeys
	{
		"处理未绑定按键", attach,
		inputDevData(dev).devConf.shouldHandleUnboundKeys,
		[this](BoolMenuItem &item, const Input::Event &e)
		{
			devConf.shouldHandleUnboundKeys = item.flipBoolValue(*this);
			devConf.save(inputManager);
		}
	},
	joystickAxisStick1Keys
	{
		"摇杆1作为十字键", attach,
		inputDevData(dev).devConf.joystickAxesAsKeys(Input::AxisSetId::stick1),
		[this](BoolMenuItem &item, const Input::Event &e)
		{
			devConf.setJoystickAxesAsKeys(Input::AxisSetId::stick1, item.flipBoolValue(*this));
			devConf.save(inputManager);
		}
	},
	joystickAxisStick2Keys
	{
		"摇杆2作为十字键", attach,
		inputDevData(dev).devConf.joystickAxesAsKeys(Input::AxisSetId::stick2),
		[this](BoolMenuItem &item, const Input::Event &e)
		{
			devConf.setJoystickAxesAsKeys(Input::AxisSetId::stick2, item.flipBoolValue(*this));
			devConf.save(inputManager);
		}
	},
	joystickAxisHatKeys
	{
		"POV hat作为十字键", attach,
		inputDevData(dev).devConf.joystickAxesAsKeys(Input::AxisSetId::hat),
		[this](BoolMenuItem &item, const Input::Event &e)
		{
			devConf.setJoystickAxesAsKeys(Input::AxisSetId::hat, item.flipBoolValue(*this));
			devConf.save(inputManager);
		}
	},
	joystickAxisTriggerKeys
	{
		"L/R 触发器为 L2/R2", attach,
		inputDevData(dev).devConf.joystickAxesAsKeys(Input::AxisSetId::triggers),
		[this](BoolMenuItem &item, const Input::Event &e)
		{
			devConf.setJoystickAxesAsKeys(Input::AxisSetId::triggers, item.flipBoolValue(*this));
			devConf.save(inputManager);
		}
	},
	joystickAxisPedalKeys
	{
		"Brake/Gas 作为 L2/R2", attach,
		inputDevData(dev).devConf.joystickAxesAsKeys(Input::AxisSetId::pedals),
		[this](BoolMenuItem &item, const Input::Event &e)
		{
			devConf.setJoystickAxesAsKeys(Input::AxisSetId::pedals, item.flipBoolValue(*this));
			devConf.save(inputManager);
		}
	},
	categories{"动作分类", attach},
	options{"选项", attach},
	joystickSetup{"操纵杆坐标轴设置", attach},
	devConf{inputDevData(dev).devConf}
{
	loadProfile.setName(std::format("配置: {}", devConf.keyConf(inputManager).name));
	renameProfile.setActive(devConf.mutableKeyConf(inputManager));
	deleteProfile.setActive(devConf.mutableKeyConf(inputManager));
	loadItems();
}

void InputManagerDeviceView::addCategoryItem(const KeyCategory &cat)
{
	auto &catItem = inputCategory.emplace_back(cat.name, attachParams(),
		[this, &cat](const Input::Event &e)
		{
			pushAndShow(makeView<ButtonConfigView>(rootIMView, cat, devConf), e);
		});
	item.emplace_back(&catItem);
}

void InputManagerDeviceView::loadItems()
{
	auto &dev = devConf.device();
	item.clear();
	auto categoryCount = EmuApp::keyCategories().size();
	bool hasJoystick = dev.motionAxes().size();
	auto joystickItemCount = hasJoystick ? 9 : 0;
	item.reserve(categoryCount + joystickItemCount + 12);
	inputCategory.clear();
	inputCategory.reserve(categoryCount + 1);
	if(EmuSystem::maxPlayers > 1)
	{
		item.emplace_back(&player);
	}
	item.emplace_back(&loadProfile);
	item.emplace_back(&categories);
	addCategoryItem(appKeyCategory);
	for(auto &cat : EmuApp::keyCategories())
	{
		if(cat.multiplayerIndex && devConf.player() != InputDeviceConfig::PLAYER_MULTI)
			continue;
		addCategoryItem(cat);
	}
	item.emplace_back(&options);
	item.emplace_back(&newProfile);
	item.emplace_back(&renameProfile);
	item.emplace_back(&deleteProfile);
	if(hasICadeInput && (dev.map() == Input::Map::SYSTEM && dev.hasKeyboard()))
	{
		item.emplace_back(&iCadeMode);
	}
	if constexpr(Config::envIsAndroid)
	{
		item.emplace_back(&consumeUnboundKeys);
	}
	if(hasJoystick)
	{
		item.emplace_back(&joystickSetup);
		if(dev.motionAxis(Input::AxisId::X))
			item.emplace_back(&joystickAxisStick1Keys);
		if(dev.motionAxis(Input::AxisId::Z))
			item.emplace_back(&joystickAxisStick2Keys);
		if(dev.motionAxis(Input::AxisId::HAT0X))
			item.emplace_back(&joystickAxisHatKeys);
		if(dev.motionAxis(Input::AxisId::LTRIGGER))
			item.emplace_back(&joystickAxisTriggerKeys);
		if(dev.motionAxis(Input::AxisId::BRAKE))
			item.emplace_back(&joystickAxisPedalKeys);
	}
}

void InputManagerDeviceView::onShow()
{
	TableView::onShow();
	loadProfile.compile(std::format("配置: {}", devConf.keyConf(inputManager).name));
	bool keyConfIsMutable = devConf.mutableKeyConf(inputManager);
	renameProfile.setActive(keyConfIsMutable);
	deleteProfile.setActive(keyConfIsMutable);
}

void InputManagerDeviceView::confirmICadeMode()
{
	devConf.setICadeMode(iCadeMode.flipBoolValue(*this));
	devConf.save(inputManager);
	onShow();
	app().defaultVController().setPhysicalControlsPresent(appContext().keyInputIsPresent());
}

}
