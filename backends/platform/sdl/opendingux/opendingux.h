/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef PLATFORM_SDL_OPENDINGUX_H
#define PLATFORM_SDL_OPENDINGUX_H

#include "backends/platform/sdl/sdl.h"

class OSystem_SDL_Opendingux : public OSystem_SDL {
public:
	void init() override;
	void initBackend() override;
	bool hasFeature(Feature f) override;
	void setFeatureState(Feature f, bool enable) override;
	bool getFeatureState(Feature f) override;
	Common::HardwareInputSet *getHardwareInputSet() override;
	Common::KeymapperDefaultBindings *getKeymapperDefaultBindings() override;

protected:
	Common::String getDefaultConfigFileName() override;
	Common::String getDefaultLogFileName() override;
};

#endif
