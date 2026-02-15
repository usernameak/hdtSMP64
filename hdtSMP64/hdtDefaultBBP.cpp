#include "hdtDefaultBBP.h"

#include "XmlReader.h"

#include <clocale>

#include "../hdtSSEUtils/NetImmerseUtils.h"

#include <algorithm>

namespace hdt
{
	DefaultBBP* DefaultBBP::instance()
	{
		static DefaultBBP s;
		return &s;
	}

	DefaultBBP::PhysicsFile DefaultBBP::scanBBP(RE::NiNode* scan)
	{
		auto* stringData = scan->GetExtraData<RE::NiStringExtraData>("HDT Skinned Mesh Physics Object");
		if (stringData)
		{
			return { stringData->value, defaultNameMap(scan) };
		}

		return scanDefaultBBP(scan);
	}

	DefaultBBP::DefaultBBP()
	{
		loadDefaultBBPs();
	}

	void DefaultBBP::loadDefaultBBPs()
	{
		auto path = "SKSE/Plugins/hdtSkinnedMeshConfigs/defaultBBPs.xml";

		auto loaded = readAllFile(path);
		if (loaded.empty()) return;

		// Store original locale
		char saved_locale[32];
		strcpy_s(saved_locale, std::setlocale(LC_NUMERIC, nullptr));

		// Set locale to en_US
		std::setlocale(LC_NUMERIC, "en_US");

		XMLReader reader((uint8_t*)loaded.data(), loaded.size());

		reader.nextStartElement();
		if (reader.GetName() != "default-bbps")
			return;

		while (reader.Inspect())
		{
			if (reader.GetInspected() == Xml::Inspected::StartTag)
			{
				if (reader.GetName() == "map")
				{
					try
					{
						auto shape = reader.getAttribute("shape");
						auto file = reader.getAttribute("file");
						bbpFileList.insert(std::make_pair(shape, file));
					}
					catch (...)
					{
						spdlog::warn("defaultBBP({},{}) : invalid map", reader.GetRow(), reader.GetColumn());
					}
					reader.skipCurrentElement();
				}
				else if (reader.GetName() == "remap")
				{
					auto target = reader.getAttribute("target");
					Remap remap = { target, { }, { } };
					while (reader.Inspect())
					{
						if (reader.GetInspected() == Xml::Inspected::StartTag)
						{
							if (reader.GetName() == "source")
							{
								int priority = 0;
								try
								{
									priority = reader.getAttributeAsInt("priority");
								}
								catch (...) {}
								auto source = reader.readText();
								remap.entries.insert({ priority, source });
							}
							else if (reader.GetName() == "requires")
							{
								auto req = reader.readText();
								remap.required.insert(req);
							}
							else
							{
								spdlog::warn("defaultBBP({},{}) : unknown element", reader.GetRow(), reader.GetColumn());
								reader.skipCurrentElement();
							}
						}
						else if (reader.GetInspected() == Xml::Inspected::EndTag)
						{
							break;
						}
					}
					remaps.push_back(remap);
				}
				else
				{
					spdlog::warn("defaultBBP({},{}) : unknown element", reader.GetRow(), reader.GetColumn());
					reader.skipCurrentElement();
				}
			}
			else if (reader.GetInspected() == Xml::Inspected::EndTag)
				break;
		}

		// Restore original locale
		std::setlocale(LC_NUMERIC, saved_locale);
	}

	DefaultBBP::PhysicsFile DefaultBBP::scanDefaultBBP(RE::NiNode* armor)
	{
		static std::mutex s_lock;
		std::lock_guard<std::mutex> l(s_lock);

		if (bbpFileList.empty()) return { "", {} };

		auto remappedNames = DefaultBBP::instance()->getNameMap(armor);

		auto it = std::find_if(bbpFileList.begin(), bbpFileList.end(), [&](const std::pair<std::string, std::string>& e)
			{ return remappedNames.find(e.first) != remappedNames.end(); });
		return { it == bbpFileList.end() ? "" : it->second, remappedNames };
	}

	DefaultBBP::NameMap DefaultBBP::getNameMap(RE::NiNode* armor)
	{
		DefaultBBP::NameMap nameMap = defaultNameMap(armor);

		for (auto remap : remaps)
		{
			bool doRemap = true;
			for (auto req : remap.required)
			{
				if (nameMap.find(req) == nameMap.end())
				{
					doRemap = false;
				}
			}
			if (doRemap)
			{
				auto start = std::find_if(remap.entries.rbegin(), remap.entries.rend(), [&](const RemapEntry& e)
					{ return nameMap.find(e.second) != nameMap.end(); });
				auto end = std::find_if(start, remap.entries.rend(), [&](const RemapEntry& e)
					{ return e.first != start->first; });
				if (start != remap.entries.rend())
				{
					const auto &s = nameMap.insert({ remap.name, { } }).first;
					std::for_each(start, end, [&](const RemapEntry& e)
						{
							auto it = nameMap.find(e.second);
							if (it != nameMap.end())
							{
								std::for_each(it->second.begin(), it->second.end(), [&](const std::string& name)
									{
										s->second.insert(name);
									});
							}
						});
				}
			}
		}
		return nameMap;
	}

	DefaultBBP::NameMap DefaultBBP::defaultNameMap(RE::NiNode* armor)
	{
		std::unordered_map<std::string, std::unordered_set<std::string> > nameMap;
		// This case never happens to a lurker skeleton, thus we don't need to test.
		auto skinned = findNode(armor, "BSFaceGenNiNodeSkinned");
		if (skinned)
		{
			for (auto& child : skinned->children)
			{
				auto tri = child->AsTriShape();
				if (!tri || tri->name.empty()) continue;
				nameMap.insert({ tri->name.c_str(), {tri->name.c_str()}});
			}
		}
		for (auto& child : armor->children)
		{
			auto tri = child->AsTriShape();
			if (!tri || tri->name.empty()) continue;
			nameMap.insert({ tri->name.c_str(), {tri->name.c_str()} });
		}
		return nameMap;
	}
}
