#pragma once

#include <algorithm>
#include <random>
#include "hdtSkyrimPhysicsWorld.h"

// All weather code borrowed from Shizof. https://www.nexusmods.com/skyrimspecialedition/mods/24486
// 0x2C8  

namespace hdt {
	void WeatherCheck();

	RE::NiPoint3* getWindDirection();
}
