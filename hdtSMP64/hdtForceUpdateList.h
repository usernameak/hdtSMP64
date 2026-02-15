#pragma once

#include <string>
#include <unordered_set>

#include "../hdtSSEUtils/FrameworkUtils.h"

namespace hdt {
	class ForceUpdateList {
		 struct NodeList {
			std::unordered_set<RE::BSFixedString> nodes;
			std::unordered_set<RE::BSFixedString> nodes_mov;
		};

	public:
		static ForceUpdateList* GetSingleton();

		int isAmong(const RE::BSFixedString &node_name);

	private:
		ForceUpdateList();

		NodeList m_list;
	};
}