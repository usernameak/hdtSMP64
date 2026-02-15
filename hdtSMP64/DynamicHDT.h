#pragma once

#include <functional>
#include <string>
#include <stdexcept>

#include "ActorManager.h"
#include "config.h"
#include "EventDebugLogger.h"
#include "hdtSkyrimPhysicsWorld.h"
#include "Hooks.h"
#include "HookEvents.h"
#include "dhdtOverrideManager.h"
#include "dhdtPapyrusFunctions.h"

constexpr auto PAPYRUS_CLASS_NAME = "DynamicHDT";

constexpr auto OVERRIDE_SAVE_PATH = "Data/SKSE/Plugins/hdtOverrideSaves/";

namespace hdt {
	namespace util {
		uint32_t splitArmorAddonFormID(const RE::BSFixedString &nodeName);

		std::string UInt32toString(uint32_t formID);

		void transferCurrentPosesBetweenSystems(hdt::SkyrimSystem* src, hdt::SkyrimSystem* dst);
	}
}
