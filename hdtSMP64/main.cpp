#include "pch.h"

#include "ActorManager.h"
#include "config.h"
#include "EventDebugLogger.h"
#include "hdtSkyrimPhysicsWorld.h"
#include "Hooks.h"
#include "Offsets.h"
#include "HookEvents.h"
#include "PluginInterfaceImpl.h"

#include <numeric>

#include <shlobj_core.h>

#ifdef CUDA
#include "hdtSkinnedMesh/hdtCudaInterface.h"
#include "hdtSkinnedMesh/hdtFrameTimer.h"
#endif

#include "WeatherManager.h"

#include <spdlog/sinks/basic_file_sink.h>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

namespace hdt
{
	constexpr uint32_t hdtSMP64Version = 200500; // patch version + 10^2 * minor version + 10^5 * major version

	EventDebugLogger g_eventDebugLogger;

	class FreezeEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		FreezeEventHandler()
		{
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* evn, RE::BSTEventSource<RE::MenuOpenCloseEvent>* dispatcher) override
		{
			auto *mm = RE::UI::GetSingleton();

			if (evn && evn->opening && evn->menuName == "Loading Menu" || evn->menuName == "RaceSex Menu")
			{
				spdlog::trace("Loading menu/racesexmenu detected, scheduling physics reset on world un-suspend.");
				SkyrimPhysicsWorld::get()->suspend(true);
			}

			if (evn && !evn->opening && evn->menuName == "RaceSex Menu")
			{
				spdlog::trace("Racemenu closed, reloading meshes.");
				ActorManager::instance()->onEvent(*evn);
			}

			return RE::BSEventNotifyControl::kContinue;
		}
	} g_freezeEventHandler;

	void checkOldPlugins()
	{
		auto framework = GetModuleHandleA("hdtSSEFramework");
		auto physics = GetModuleHandleA("hdtSSEPhysics");
		auto hh = GetModuleHandleA("hdtSSEHighHeels");

		if (physics)
		{
			MessageBox(nullptr, TEXT(
				"hdtSSEPhysics.dll is loaded. This is an older verson of HDT-SMP and conflicts with hdtSMP64.dll. Please remove it."),
				TEXT("hdtSMP64"), MB_OK);
		}

		if (framework && !hh)
		{
			MessageBox(nullptr, TEXT(
				"hdtSSEFramework.dll is loaded but hdtSSEHighHeels.dll is not being used. You no longer need hdtSSEFramework.dll with this version of SMP. Please remove it."),
				TEXT("hdtSMP64"), MB_OK);
		}
	}

	static void DumpNodeChildren(RE::NiAVObject* node, uint32_t indent = 0)
	{
		spdlog::info("{:{}s}{} {{{}}} {{{:X}}} [{:f}, {:f}, {:f}]", "", indent, node->GetRTTI()->name, node->name.c_str(), (uintptr_t)node, node->world.translate.x, node->world.translate.y, node->world.translate.z);
		if (node->extraDataSize > 0)
		{
			indent += 4;
			for (uint16_t i = 0; i < node->extraDataSize; i++)
			{
				spdlog::info("{:{}s}{} {{{}}} {{{:X}}}", "", indent, node->extra[i]->GetRTTI()->name, node->extra[i]->name.c_str(), (uintptr_t)node->extra[i]);
			}
			indent -= 4;
		}

		RE::NiNode* niNode = node->AsNode();
		if (niNode && niNode->children.size() > 0)
		{
			indent += 4;
			for (int i = 0; i < niNode->children.size(); i++)
			{
				RE::NiAVObject* object = niNode->children[i].get();
				if (!object)
					continue;

				RE::NiNode* childNode = object->AsNode();
				RE::BSGeometry* geometry = object->AsGeometry();
				if (geometry)
				{
					spdlog::info("{:{}s}{} {{{}}} {{{:X}}} [{:f}, {:f}, {:f}] - Geometry", "", indent, object->GetRTTI()->name, object->name.c_str(), (uintptr_t)object, geometry->world.translate.x, geometry->world.translate.y, geometry->world.translate.z);
					if (geometry->skinInstance && geometry->skinInstance->skinData)
					{
						indent += 4;
						for (int i = 0; i < geometry->skinInstance->skinData->bones; i++)
						{
							auto *bone = geometry->skinInstance->bones[i];
							spdlog::info("{:{}s}Bone {} - {{{}}} {{{}}} {{{:X}}} [{:f}, {:f}, {:f}]", "", indent, i, bone->GetRTTI()->name, bone->name.c_str(), (uintptr_t)bone, bone->world.translate.x, bone->world.translate.y, bone->world.translate.z);
						}
						indent -= 4;
					}

					RE::BSShaderProperty *shaderProperty = netimmerse_cast<RE::BSShaderProperty *>(geometry->properties[RE::BSGeometry::States::kEffect].get());
					if (shaderProperty)
					{
						RE::BSLightingShaderProperty* lightingShader = netimmerse_cast<RE::BSLightingShaderProperty *>(shaderProperty);
						if (lightingShader)
						{
							RE::BSLightingShaderMaterial* material = static_cast<RE::BSLightingShaderMaterial*>(lightingShader->material);

							indent += 4;
							for (int i = 0; i < RE::BSTextureSet::Textures::kTotal; ++i)
							{
								const char* texturePath = material->textureSet->GetTexturePath((RE::BSTextureSet::Textures::Texture)i);
								if (!texturePath)
								{
									continue;
								}

								spdlog::info("{:{}s}Texture {} - {}", "", indent, i, texturePath);
							}
							spdlog::info("{:{}s}Flags - {:016X}", "", indent, lightingShader->flags.underlying());
							indent -= 4;
						}
					}
				}
				else if (childNode)
				{
					DumpNodeChildren(childNode, indent);
				}
				else
				{
					spdlog::info("{:{}s}{} {{{}}} {{{:X}}} [{:f}, {:f}, {:f}]", "", indent, object->GetRTTI()->name, object->name.c_str(), (uintptr_t)object, object->world.translate.x, object->world.translate.y, object->world.translate.z);
				}
			}
			indent -= 4;
		}
	}

	void SMPDebug_PrintDetailed(bool includeItems)
	{
		static std::map<ActorManager::SkeletonState, const char *> stateStrings =
		{ { ActorManager::SkeletonState::e_InactiveNotInScene, "Not in scene"},
			{ActorManager::SkeletonState::e_InactiveUnseenByPlayer, "Unseen by player"},
			{ActorManager::SkeletonState::e_InactiveTooFar, "Deactivated for performance"},
			{ActorManager::SkeletonState::e_ActiveIsPlayer, "Is player character"},
			{ActorManager::SkeletonState::e_ActiveNearPlayer, "Is near player"} };

		auto skeletons = ActorManager::instance()->getSkeletons();
		std::vector<int>order(skeletons.size());
		std::iota(order.begin(), order.end(), 0);
		std::sort(order.begin(), order.end(), [&](int a, int b) { return skeletons[a].state < skeletons[b].state; });

		for (int i : order)
		{
			auto& skeleton = skeletons[i];

			RE::TESObjectREFR* skelOwner = nullptr;
			RE::TESFullName* ownerName = nullptr;

			if (skeleton.skeleton->userData)
			{
				skelOwner = skeleton.skeleton->userData;
				if (skelOwner->GetBaseObject())
					ownerName = skelOwner->GetBaseObject()->As<RE::TESFullName>();
			}

			RE::ConsoleLog::GetSingleton()->Print("[HDT-SMP] %s skeleton - owner %s (refr formid %08x, base formid %08x) - %s",
				skeleton.state > ActorManager::SkeletonState::e_SkeletonActive ? "active" : "inactive",
				ownerName ? ownerName->fullName.c_str() : "unk_name",
				skelOwner ? skelOwner->formID : 0x00000000,
				skelOwner && skelOwner->GetBaseObject() ? skelOwner->GetBaseObject()->formID : 0x00000000,
				stateStrings[skeleton.state]
			);

			if (includeItems)
			{
				for (auto armor : skeleton.getArmors())
				{
					RE::ConsoleLog::GetSingleton()->Print("[HDT-SMP] -- tracked armor addon %s, %s",
						armor.armorWorn->name.c_str(),
						armor.state() != ActorManager::ItemState::e_NoPhysics
						? armor.state() == ActorManager::ItemState::e_Active
						? "has active physics system"
						: "has inactive physics system"
						: "has no physics system");

					if (armor.state() != ActorManager::ItemState::e_NoPhysics)
					{
						for (auto mesh : armor.meshes())
							RE::ConsoleLog::GetSingleton()->Print("[HDT-SMP] ---- has collision mesh %s", mesh->m_name->cstr());
					}
				}

				if (skeleton.head.headNode)
				{
					for (auto headPart : skeleton.head.headParts)
					{
						RE::ConsoleLog::GetSingleton()->Print("[HDT-SMP] -- tracked headpart %s, %s",
							headPart.headPart->name.c_str(),
							headPart.state() != ActorManager::ItemState::e_NoPhysics
							? headPart.state() == ActorManager::ItemState::e_Active
							? "has active physics system"
							: "has inactive physics system"
							: "has no physics system");

						if (headPart.state() != ActorManager::ItemState::e_NoPhysics)
						{
							for (auto mesh : headPart.meshes())
								RE::ConsoleLog::GetSingleton()->Print("[HDT-SMP] ---- has collision mesh %s", mesh->m_name->cstr());
						}
					}
				}
			}

		}
	}

	bool SMPDebug_Execute(
		const RE::SCRIPT_PARAMETER* a_paramInfo,
		RE::SCRIPT_FUNCTION::ScriptData* a_scriptData,
		RE::TESObjectREFR* a_thisObj,
		RE::TESObjectREFR* a_containingObj,
		RE::Script* a_scriptObj,
		RE::ScriptLocals* a_locals,
		double& a_result,
		std::uint32_t& a_opcodeOffsetPtr)
	{
		auto *stringChunk = a_scriptData->GetStringChunk();
		std::string str = stringChunk->GetString();
		
		if (str == "reset")
		{
			RE::ConsoleLog::GetSingleton()->Print("running full smp reset");
			hdt::loadConfig();
			SkyrimPhysicsWorld::get()->resetTransformsToOriginal();
			const RE::MenuOpenCloseEvent e { RE::BSFixedString(), false };
			ActorManager::instance()->onEvent(e);
			SkyrimPhysicsWorld::get()->resetSystems();
			return true;
		}
#ifdef CUDA
		if (str == "gpu")
		{
			CudaInterface::enableCuda = !CudaInterface::enableCuda;
			if (CudaInterface::instance()->hasCuda())
			{
				RE::ConsoleLog::GetSingleton()->Print("CUDA collision enabled");
			}
			else
			{
				RE::ConsoleLog::GetSingleton()->Print("CUDA collision disabled");
			}
			return true;
		}
		if (str == "timing")
		{
			FrameTimer::instance()->reset(200);
			RE::ConsoleLog::GetSingleton()->Print("Started frame timing");
			return true;
		}
#endif
		if (str == "dumptree")
		{
			if (a_thisObj)
			{
				RE::ConsoleLog::GetSingleton()->Print("dumping targeted reference's node tree");
				DumpNodeChildren(a_thisObj->Get3D1(false));
			}
			else
			{
				RE::ConsoleLog::GetSingleton()->Print("error: you must target a reference to dump their node tree");
			}

			return true;
		}
		if (str == "detail")
		{
			SMPDebug_PrintDetailed(true);
			return true;
		}
		if (str == "list")
		{
			SMPDebug_PrintDetailed(false);
			return true;
		}
		if (str == "on")
		{
			SkyrimPhysicsWorld::get()->disabled = false;
			{
				RE::ConsoleLog::GetSingleton()->Print("HDT-SMP enabled");
			}
			return true;
		}
		if (str == "off")
		{
			SkyrimPhysicsWorld::get()->disabled = true;
			{
				RE::ConsoleLog::GetSingleton()->Print("HDT-SMP disabled");
			}
			return true;
		}

		if (str == "QueryOverride") {
			RE::ConsoleLog::GetSingleton()->Print(hdt::Override::OverrideManager::GetSingleton()->queryOverrideData().c_str());
			return true;
		}

		auto skeletons = ActorManager::instance()->getSkeletons();

		size_t activeSkeletons = 0;
		size_t armors = 0;
		size_t headParts = 0;
		size_t activeArmors = 0;
		size_t activeHeadParts = 0;
		size_t activeCollisionMeshes = 0;

		for (auto skeleton : skeletons)
		{
			if (skeleton.state > ActorManager::SkeletonState::e_SkeletonActive)
				activeSkeletons++;

			for (const auto armor : skeleton.getArmors())
			{
				armors++;

				if (armor.state() == ActorManager::ItemState::e_Active)
				{
					activeArmors++;

					activeCollisionMeshes += armor.meshes().size();
				}
			}

			if (skeleton.head.headNode)
			{
				for (const auto headpart : skeleton.head.headParts)
				{
					headParts++;

					if (headpart.state() == ActorManager::ItemState::e_Active)
					{
						activeHeadParts++;

						activeCollisionMeshes += headpart.meshes().size();
					}
				}
			}
		}

		RE::ConsoleLog::GetSingleton()->Print("[HDT-SMP] tracked skeletons: %d", skeletons.size());
		RE::ConsoleLog::GetSingleton()->Print("[HDT-SMP] active skeletons: %d", activeSkeletons);
		RE::ConsoleLog::GetSingleton()->Print("[HDT-SMP] tracked armor addons: %d", armors);
		RE::ConsoleLog::GetSingleton()->Print("[HDT-SMP] tracked head parts: %d", headParts);
		RE::ConsoleLog::GetSingleton()->Print("[HDT-SMP] active armor addons: %d", activeArmors);
		RE::ConsoleLog::GetSingleton()->Print("[HDT-SMP] active head parts: %d", activeHeadParts);
		RE::ConsoleLog::GetSingleton()->Print("[HDT-SMP] active collision meshes: %d", activeCollisionMeshes);
		return true;
	}

	int filterException(int code, PEXCEPTION_POINTERS ex)
	{
		spdlog::critical("SEH exception caught while loading FSMP plugin into SKSE.");
		if (code == -529697949)
		{
			spdlog::critical("This exception occurs when a system process, application, or file fails to open, or your system lacks some necessary redistributable packages like Visual C++ extensions.\
						It can be caused by Damaged or Corrupt system files, Missing files in the registry, Improper configuration of system files, Conflict with third-party programs.\
						Please see https://www.elevenforum.com/t/0xe06d7363-error-which-fix-to-use.8382/post-201372. SEH exception code: %x", code);
			return EXCEPTION_EXECUTE_HANDLER;
		}
		else
		{
			spdlog::critical("Contact DaydreamingDay on the FSMP discord server, and provide him with this SEH exception code: %x. The discord invite is on the Nexus FSMP description page.", code);
			return EXCEPTION_EXECUTE_HANDLER;
		}
	}

	/* This function is the most prone to SEH exceptions. */
	static bool enclosedLoadConfig(const SKSE::LoadInterface *skse)
	{
		__try
		{
			hdt::loadConfig();
		}
		__except (hdt::filterException(GetExceptionCode(), GetExceptionInformation()))
		{
			spdlog::critical("A fatal exception has occurred thrown while reading FSMP's configs.xml");
			return false;
		}
		return true;
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse) {
#ifdef ANNIVERSARY_EDITION
	auto path = SKSE::log::log_directory();
	if (path) {
		*path /= "hdtSMP64.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);

		spdlog::set_default_logger(log);
		spdlog::info("hdtSMP64 {}.{}.{}", SKSE::GetPluginVersion().major(), SKSE::GetPluginVersion().minor(), SKSE::GetPluginVersion().patch());
	}
#endif // ANNIVERSARY_EDITION

#ifdef _DEBUG
	MessageBoxW(nullptr, L"Attach debugger NOW (or just click OK if you don't want to)", L"HDT-SMP", MB_OK);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
#endif

	SKSE::InitInfo initInfo;
	initInfo.trampoline = true;
	initInfo.trampolineSize = 1024 * 3;

	SKSE::Init(skse, initInfo);

	hdt::g_frameEventDispatcher.addListener(hdt::ActorManager::instance());
	hdt::g_frameEventDispatcher.addListener(hdt::SkyrimPhysicsWorld::get());
	hdt::g_frameSyncEventDispatcher.addListener(hdt::SkyrimPhysicsWorld::get());
	hdt::g_shutdownEventDispatcher.addListener(hdt::ActorManager::instance());
	hdt::g_shutdownEventDispatcher.addListener(hdt::SkyrimPhysicsWorld::get());
	hdt::g_armorAttachEventDispatcher.addListener(hdt::ActorManager::instance());
	hdt::g_armorDetachEventDispatcher.addListener(hdt::ActorManager::instance());
	hdt::g_skinSingleHeadGeometryEventDispatcher.addListener(hdt::ActorManager::instance());
	hdt::g_skinAllHeadGeometryEventDispatcher.addListener(hdt::ActorManager::instance());

	hdt::hookAll();

	hdt::g_pluginInterface.init(skse);

	const auto messageInterface = SKSE::GetMessagingInterface();
	if (messageInterface)
	{
		const auto cameraDispatcher = static_cast<RE::BSTEventSource<SKSE::CameraEvent>*>(messageInterface->
			GetEventDispatcher(SKSE::MessagingInterface::Dispatcher::kCameraEvent));

		if (cameraDispatcher)
			cameraDispatcher->AddEventSink(hdt::SkyrimPhysicsWorld::get());

		messageInterface->RegisterListener("SKSE", [](SKSE::MessagingInterface::Message* msg)
			{
				if (msg && msg->type == SKSE::MessagingInterface::kInputLoaded)
				{
					if (RE::UI* mm = RE::UI::GetSingleton())
						mm->AddEventSink(&hdt::g_freezeEventHandler);
					hdt::checkOldPlugins();

					// I think we only have _DEBUG now...
#ifdef DEBUG
					hdt::g_armorAttachEventDispatcher.addListener(&hdt::g_eventDebugLogger);
					GetEventDispatcherList()->unk1B8.AddEventSink(&hdt::g_eventDebugLogger);
					GetEventDispatcherList()->unk840.AddEventSink(&hdt::g_eventDebugLogger);
#endif
				}

				// If we receive a SaveGame message, we serialize our data and save it in our dedicated save files.
				if (msg && msg->type == SKSE::MessagingInterface::kSaveGame)
				{
					auto data = hdt::Override::OverrideManager::GetSingleton()->Serialize();
					if (!data.str().empty()) {
						std::string save_name = reinterpret_cast<char*>(msg->data);
						std::ofstream ofs(OVERRIDE_SAVE_PATH + save_name + ".dhdt", std::ios::out);
						if (ofs && ofs.is_open())
							ofs << data.str();
					}
				}

				// If we receive a PreLoadGame message, we take our data in our dedicated save files and deserialize it.
				if (msg && msg->type == SKSE::MessagingInterface::kPreLoadGame)
				{
					std::string save_name = reinterpret_cast<char*>(msg->data);
					save_name = save_name.substr(0, save_name.find_last_of("."));

					std::ifstream ifs(OVERRIDE_SAVE_PATH + save_name + ".dhdt", std::ios::in);
					if (ifs && ifs.is_open())
					{
						std::stringstream data;
						data << ifs.rdbuf();
						hdt::Override::OverrideManager::GetSingleton()->Deserialize(data);
					}
				}

				//Send our public interface to registered plugins
				if (msg && msg->type == SKSE::MessagingInterface::kPostPostLoad)
				{
					hdt::g_pluginInterface.onPostPostLoad();
				}
			});
	}

	RE::SCRIPT_FUNCTION* hijackedCommand = RE::SCRIPT_FUNCTION::LocateConsoleCommand("ShowRenderPasses");
	if (hijackedCommand)
	{
		static RE::SCRIPT_PARAMETER params[1];
		params[0].paramType = RE::SCRIPT_PARAM_TYPE::kChar;
		params[0].paramName = "String (optional)";
		params[0].optional = 1;

		RE::SCRIPT_FUNCTION cmd = *hijackedCommand;
		cmd.functionName = "SMPDebug";
		cmd.shortName = "smp";
		cmd.helpString = "smp <reset>";
		cmd.referenceFunction = false;
		cmd.SetParameters(params);
		cmd.executeFunction = hdt::SMPDebug_Execute;
		cmd.editorFilter = false;

		REL::WriteSafeData(hijackedCommand, cmd);
	}

	hdt::papyrus::RegisterAllFunctions(SKSE::GetPapyrusInterface());

	if (!hdt::enclosedLoadConfig(skse)) return false;

	if (hdt::SkyrimPhysicsWorld::get()->m_enableWind) {
		spdlog::info("Wind enabled");
		std::thread t(hdt::WeatherCheck);
		t.detach();
	}

	return true;
}
