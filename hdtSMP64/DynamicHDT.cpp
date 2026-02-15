#include "DynamicHDT.h"
#include "hdtSkyrimSystem.h"
#include "hdtSkinnedMesh/hdtSkinnedMeshSystem.h"

uint32_t hdt::util::splitArmorAddonFormID(const RE::BSFixedString &nodeName)
{
	std::string_view sub = std::string_view(nodeName).substr(1, 8);
	uint32_t armorAddon = 0;
	auto result = std::from_chars(sub.data(), sub.data() + sub.size(), armorAddon, 16);
	if (result.ec != std::errc{} && result.ptr != sub.data() + sub.size())
		return 0;
	return armorAddon;
}

std::string hdt::util::UInt32toString(uint32_t formID)
{
	char buffer[16];
	sprintf_s(buffer, "%08X", formID);
	return std::string(buffer);
}

RE::BSFixedString _deprefix(const RE::BSFixedString& str_with_prefix) {
	std::string str_no_prefix = std::string(str_with_prefix.data(), str_with_prefix.size());
	if (str_no_prefix.find("hdtSSEPhysics_AutoRename_") == 0) {
		str_no_prefix = str_no_prefix.substr(str_no_prefix.find(' ') + 1);
	}
	return str_no_prefix;
}

bool _match_name(const RE::BSFixedString& a, const RE::BSFixedString& b) {
	return _deprefix(a) == _deprefix(b);
}

void hdt::util::transferCurrentPosesBetweenSystems(hdt::SkyrimSystem* src, hdt::SkyrimSystem* dst)
{
	for (auto& b1 : src->getBones()) {
		if (!b1)continue;
		for (auto& b2 : dst->getBones()) {
			if (!b2)continue;
			if (_match_name(b1->m_name, b2->m_name)) {
				b2->m_rig.setWorldTransform(b1->m_rig.getWorldTransform());
				b2->m_rig.setAngularVelocity(b1->m_rig.getAngularVelocity());
				b2->m_rig.setLinearVelocity(b1->m_rig.getLinearVelocity());
				break;
			}
		}
	}
}
