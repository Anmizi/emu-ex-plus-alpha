#pragma once

/*  This file is part of Saturn.emu.

	Saturn.emu is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Saturn.emu is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Saturn.emu.  If not, see <http://www.gnu.org/licenses/> */

#include <emuframework/EmuApp.hh>
#include "MainSystem.hh"

namespace EmuEx
{

class SaturnApp final: public EmuApp
{
public:
	SaturnSystem saturnSystem;

	SaturnApp(ApplicationInitParams initParams, ApplicationContext &ctx):
		EmuApp{initParams, ctx}, saturnSystem{ctx} {}

	auto &system() { return saturnSystem;  }
	const auto &system() const { return saturnSystem;  }
};

using MainApp = SaturnApp;

}
