#include "pch.h"

#include "WeatherManager.h"
#include "ActorManager.h"
#include "hdtSkyrimPhysicsWorld.h"
#include "hdtDefaultBBP.h"
#include <cinttypes>
#include "Offsets.h"
#include "SkyrimExtras.h"

namespace hdt
{
	ActorManager::ActorManager()
	{
	}

	ActorManager::~ActorManager()
	{
	}

	ActorManager* ActorManager::instance()
	{
		static ActorManager s;
		return &s;
	}

	IDStr ActorManager::armorPrefix(ActorManager::IDType id)
	{
		char buffer[128];
		sprintf_s(buffer, "hdtSSEPhysics_AutoRename_Armor_%08X ", id);
		return IDStr(buffer);
	}

	IDStr ActorManager::headPrefix(ActorManager::IDType id)
	{
		char buffer[128];
		sprintf_s(buffer, "hdtSSEPhysics_AutoRename_Head_%08X ", id);
		return IDStr(buffer);
	}

	inline bool isFirstPersonSkeleton(RE::NiNode* npc)
	{
		if (!npc) return false;
		return findNode(npc, "Camera1st [Cam1]") ? true : false;
	}

	RE::NiNode* getNpcNode(RE::NiNode* skeleton)
	{
		// TODO: replace this with a generic skeleton fixing configuration option
		// hardcode an exception for lurker skeletons because they are made incorrectly
		auto shouldFix = false;
		if (skeleton->userData && skeleton->userData->GetBaseObject())
		{
			RE::TESNPC *npcForm = skeleton->userData->GetBaseObject()->As<RE::TESNPC>();
			if (npcForm && npcForm->race
				&& !strcmp(npcForm->race->skeletonModels[0].GetModel(), "Actors\\DLC02\\BenthicLurker\\Character Assets\\skeleton.nif"))
				shouldFix = true;
		}
		return findNode(skeleton, shouldFix ? "NPC Root [Root]" : "NPC");
	}

	void ActorManager::fixArmorNameMaps()
	{
		auto& skeletons = instance()->getSkeletons();
		for (auto& skeleton : skeletons)
			if (skeleton.mustFixOneArmorMap)
			{
				auto& armors = skeleton.getArmors();
				for (auto& armor : armors)
				{
					if (armor.mustFixNameMap)
					{
						if (armor.armorWorn)
						{
							std::string armorNewMeshName(armor.armorWorn->name);
							if (!armorNewMeshName.empty() && armor.armorCurrentMeshName.compare(armorNewMeshName) != 0)
							{
								auto& armorNameMap = armor.physicsFile.second;
								hdt::DefaultBBP::NameMap tempNameMap;
								for (auto& [setName, set] : armorNameMap)
									// ... and we found the old mesh name in the armor nameMap,...
									if (armor.armorCurrentMeshName.compare(setName) == 0)
									{
										// We add the new mesh name to the list of mesh names for the original mesh name (sic).
										set.insert({ armorNewMeshName });
										// We plan a new entry in the armor nameMap.
										tempNameMap.insert({ armorNewMeshName, { armorNewMeshName } });
										// This armor is fixed.
										armor.mustFixNameMap = false;
										armor.armorCurrentMeshName = armorNewMeshName;
									}
								// We add the planned entries.
								for (auto& [setName, set] : tempNameMap)
									armorNameMap.insert({ setName, set });
							}
						}
					}
				}
				skeleton.mustFixOneArmorMap = false;
			}
	}

	void ActorManager::onEvent(const ArmorAttachEvent& e)
	{
		// No armor is ever attached to a lurker skeleton, thus we don't need to test.
		if (e.skeleton == nullptr || !findNode(e.skeleton, "NPC"))
		{
			return;
		}

		std::lock_guard<decltype(m_lock)> l(m_lock);
		if (m_shutdown) return;

		fixArmorNameMaps();

		auto& skeleton = getSkeletonData(e.skeleton);
		if (e.hasAttached)
		{
			// Check override data for current armoraddon
			if (e.skeleton->userData)
			{
				auto actor_formID = e.skeleton->userData->formID;
				if (actor_formID) {
					auto old_physics_file = skeleton.getArmors().back().physicsFile.first;
					std::string physics_file_path_override = hdt::Override::OverrideManager::GetSingleton()->checkOverride(actor_formID, old_physics_file);
					if (!physics_file_path_override.empty()) {
						//Console_Print("[DynamicHDT] -- ArmorAddon %s is overridden ", e.attachedNode->m_name);
						skeleton.getArmors().back().physicsFile.first = physics_file_path_override;
					}
				}
			}


			skeleton.attachArmor(e.armorModel, e.attachedNode);
		}
		else
		{
			skeleton.addArmor(e.armorModel);
		}
	}

	void ActorManager::onEvent(const ArmorDetachEvent& e)
	{
		if (!e.actor || !e.hasDetached || !instance()->m_disableSMPHairWhenWigEquipped)
			return;

		std::lock_guard<decltype(m_lock)> l(m_lock);
		if (m_shutdown) return;

		fixArmorNameMaps();

		Skeleton* s = get3rdPersonSkeleton(e.actor);
		setHeadActiveIfNoHairArmor(e.actor, s);
	}

	// @brief To avoid calculating headparts when they're hidden by a wig,
	// we mark the head as not active when there is an armor on hair or long hair slots.
	// We do this during the attach/detach armor events, and on the events leading to scanning the head.
	// Then when checking which skeletons are active to calculate the frame,
	// we only allow the activation of headparts that are on active heads.
	// @param Actor * actor is expected not null.
	void ActorManager::setHeadActiveIfNoHairArmor(RE::Actor* actor, Skeleton* skeleton)
	{
		bool worn = false;

		auto *containerChanges = actor->extraList.GetByType<RE::ExtraContainerChanges>();
		if (containerChanges && containerChanges->changes) {
			for (RE::InventoryEntryData* entry : *containerChanges->changes->entryList) {
				if (!entry->IsWorn())
					continue;

				auto *bip = entry->GetObject()->As<RE::BGSBipedObjectForm>();
				if (!bip)
					continue;

				worn |= bip->bipedModelData.bipedObjectSlots.any(
					RE::BIPED_MODEL::BipedObjectSlot::kHair, 
					RE::BIPED_MODEL::BipedObjectSlot::kLongHair
				);

				if (worn)
					break;
			}
		}

		if (skeleton)
			skeleton->head.isActive = !worn;
	}

	// @brief This happens on a closing RaceSex menu, and on 'smp reset'.
	void ActorManager::onEvent(const RE::MenuOpenCloseEvent&)
	{
		// The ActorManager members are protected from parallel events by ActorManager.m_lock.
		std::lock_guard<decltype(m_lock)> l(m_lock);
		if (m_shutdown) return;

		spdlog::trace("Processing MenuOpenCloseEvent.");

		fixArmorNameMaps();

		setSkeletonsActive();

		for (auto& i : m_skeletons)
			i.reloadMeshes();
	}

	void ActorManager::onEvent(const FrameEvent& e)
	{
		std::lock_guard<decltype(m_lock)> l(m_lock);

		fixArmorNameMaps();

		setSkeletonsActive(true);
	}

	//NiAVObject* Actor::CalculateLOS_1405FD2C0(Actor *aActor, NiPoint3 *aTargetPosition, NiPoint3 *aRayHitPosition, float aViewCone)
	//Used to ray cast from the actor. Will return nonNull if it hits something with position at aTargetPosition.
	//Pass in 2pi to aViewCone to ignore LOS of actor.
	typedef RE::NiAVObject* (*_Actor_CalculateLOS)(RE::Actor* aActor, RE::NiPoint3* aTargetPosition, RE::NiPoint3* aRayHitPosition, float aViewCone);
	REL::Relocation<_Actor_CalculateLOS> Actor_CalculateLOS(REL::ID(37770));

	inline RE::NiNode* ActorManager::getCameraNode()
	{
#ifdef SKYRIMVR
		// Camera info taken from Shizof's cpbc under MIT. https://www.nexusmods.com/skyrimspecialedition/mods/21224?tab=files
		if (!RE::PlayerCharacter::GetSingleton()->loadedData)
			return nullptr;
		return RE::PlayerCharacter::GetSingleton()->loadedData->data3D->AsNode();
#else
		return RE::PlayerCamera::GetSingleton()->cameraRoot.get();
#endif
	}

	// @brief This function is called by different events, with different locking needs, and is therefore extracted from the events.
	void ActorManager::setSkeletonsActive(const bool updateMetrics)
	{
		if (m_shutdown) return;

		// We get the player character and its cell.
		// TODO Isn't there a more performing way to find the PC?? A singleton? And if it's the right way, why isn't it in utils functions?
		const auto &playerCharacter = std::find_if(m_skeletons.begin(), m_skeletons.end(), [](Skeleton& s) { return s.isPlayerCharacter(); });
		auto playerCell = (playerCharacter != m_skeletons.end() && playerCharacter->skeleton->parent) ? playerCharacter->skeleton->parent->parent : nullptr;

		const auto cameraNode = getCameraNode();
		if (!cameraNode)
			return;
		// We get the camera, its position and orientation.
		const auto cameraTransform = cameraNode->world;
		const auto cameraPosition = cameraTransform.translate;
		const auto cameraOrientation = cameraTransform.rotate * RE::NiPoint3(0., 1., 0.); // The camera matrix is relative to the world.
		this->m_cameraPositionDuringFrame = cameraPosition;

		std::for_each(m_skeletons.begin(), m_skeletons.end(), [&](Skeleton& skel)
			{
				skel.calculateDistanceAndOrientationDifferenceFromSource(cameraPosition, cameraOrientation);
			});

		// We sort the skeletons depending on the angle and distance.
		std::sort(m_skeletons.begin(), m_skeletons.end(),
			[](auto&& a_lhs, auto&& a_rhs) {
				auto cr = a_rhs.m_cosAngleFromCameraDirectionTimesSkeletonDistance;
				auto cl = a_lhs.m_cosAngleFromCameraDirectionTimesSkeletonDistance;
				auto dr = a_rhs.m_distanceFromCamera2;
				auto dl = a_lhs.m_distanceFromCamera2;
				return
				// If one of the skeletons is at distance zero (1st person player) from the camera
				(btFuzzyZero(dl) || btFuzzyZero(dr))
				// then it is first.
				? (dl < dr)

				// If one of the skeletons is exacly on the side of the camera (cos = 0)
				: (btFuzzyZero(cl) || btFuzzyZero(cr))
				// then it is last.
				? abs(cl) > abs(cr)

				// If both are on the same side of the camera (product of cos > 0):
				// we want first the smallest angle (so the highest cosinus), and the smallest distance,
				// so we want the smallest distance / cosinus.
				// cl = cosinus * distance, dl = distance� => distance / cosinus = dl/cl
				// So we want dl/cl < dr/cr.
				// Moreover, this test manages the case where one of the skeletons is behind the camera and the other in front of the camera too;
				// the one behind the camera is last (the one with cos(angle) = cr < 0).
				: (dl * cr < dr * cl);
			});

		// We set which skeletons are active and we count them.
		activeSkeletons = 0;
		for (auto& i : m_skeletons)
		{
			if (i.skeleton->GetRefCount() == 1)
			{
				i.clear();
				i.skeleton = nullptr;
			}
			else if (i.hasPhysics && i.updateAttachedState(playerCell, activeSkeletons >= maxActiveSkeletons)) {
				activeSkeletons++;
				//check wind obstructions
				const auto world = SkyrimPhysicsWorld::get();
				const auto wind = getWindDirection();
				if (world->m_enableWind && wind && !(btFuzzyZero(wind->Length()))) {
					const auto owner = i.skeletonOwner ? i.skeletonOwner->As<RE::Actor>() : nullptr;
					if (owner) {
						auto windray = *wind * -1; // reverse wind raycast to find obstruction
						RE::NiPoint3 hitLocation;
						//Raycast for object in direction of wind
						const auto object = Actor_CalculateLOS(owner, &windray, &hitLocation, 6.28);
						if (object) { //object found
							auto diff = (owner->GetPosition() - hitLocation);
							diff.z = 0;	//remove z component difference
							const auto dist = diff.Length();
							// wind is a linear reduction, with a minimum floor since objects may have a minimum distance
							// windfactor = 0 when dist <= m_distanceForNoWind, = 1 when dist >= m_distanceForMaxWind, and is linear with dist between these 2 values.
							const auto windFactor = std::clamp((dist - world->m_distanceForNoWind) / (world->m_distanceForMaxWind - world->m_distanceForNoWind), 0.f, 1.f);
							if (!btFuzzyZero(windFactor - i.getWindFactor())) {
								spdlog::trace("{} blocked by {} with distance {:2.2g}; setting windFactor {:2.2g}.", i.name(), object->name.c_str(), dist, windFactor);
								i.updateWindFactor(windFactor);
							}
						}
					}
					else {
						spdlog::trace("{} is active skeleton, but failed to cast to Actor, no wind obstruction check possible.", i.name());
					}
				}
			}
		}

		m_skeletons.erase(
			std::remove_if(m_skeletons.begin(), m_skeletons.end(), [](Skeleton& i) { return !i.skeleton; }),
			m_skeletons.end());

		for (auto& i : m_skeletons)
		{
			i.cleanArmor();
			i.cleanHead();
		}

		const auto world = SkyrimPhysicsWorld::get();

		// We share the same doMetrics condition here and in hdtSkyrimPhysicsWorld to avoid any gap between both.
		// The evaluation is done here rather than in hdtSkyrimPhysicsWorld because this event is called first.
		world->m_doMetrics = updateMetrics &&                    // do not do metrics on a MenuOpenCloseEvent
			                 !world->isSuspended() &&            // do not do metrics while paused
		                     frameCount++ % world->min_fps == 0; // check every min-fps frames (i.e., a stable 60 fps should wait for 1 second)

		if (world->m_doMetrics)
		{
			const auto averageProcessingTimeInMainLoop = world->m_averageSMPProcessingTimeInMainLoop;
			// 30% of processing time is in hdt per profiling;
			// Setting it higher provides more time for hdt processing and can activate more skeletons.
			const auto target_time = world->m_timeTick * world->m_percentageOfFrameTime;
			auto averageTimePerSkeletonInMainLoop = 0.f;
			if (activeSkeletons > 0) {
				averageTimePerSkeletonInMainLoop = averageProcessingTimeInMainLoop / activeSkeletons;
			}
			spdlog::debug("msecs/activeSkeleton {:2.2g} activeSkeletons/maxActive/total {}/{}/{} processTimeInMainLoop/targetTime {:2.2g}/{:2.2g}", averageTimePerSkeletonInMainLoop, activeSkeletons, maxActiveSkeletons, m_skeletons.size(), averageProcessingTimeInMainLoop, target_time);
			if (m_autoAdjustMaxSkeletons) {
				maxActiveSkeletons += target_time > averageProcessingTimeInMainLoop ? 2 : -2;
				// clamp the value to the m_maxActiveSkeletons value
				maxActiveSkeletons = std::clamp(maxActiveSkeletons, 1, m_maxActiveSkeletons);
				frameCount = 1;
			}
			else if (maxActiveSkeletons != m_maxActiveSkeletons)
				maxActiveSkeletons = m_maxActiveSkeletons;
		}
	}

	void ActorManager::onEvent(const ShutdownEvent&)
	{
		m_shutdown = true;
		std::lock_guard<decltype(m_lock)> l(m_lock);

		m_skeletons.clear();
	}

	void ActorManager::onEvent(const SkinSingleHeadGeometryEvent& e)
	{
		// This case never happens to a lurker skeleton, thus we don't need to test.
		auto npc = findNode(e.skeleton, "NPC");
		if (!npc) return;

		std::lock_guard<decltype(m_lock)> l(m_lock);
		if (m_shutdown) return;

		fixArmorNameMaps();

		auto& skeleton = getSkeletonData(e.skeleton);
		skeleton.npc = getNpcNode(e.skeleton);

		skeleton.processGeometry(e.headNode, e.geometry);

		auto headPartIter = std::find_if(skeleton.head.headParts.begin(), skeleton.head.headParts.end(),
			[e](const Head::HeadPart& p)
			{
				return p.headPart == e.geometry;
			});

		if (headPartIter != skeleton.head.headParts.end())
		{
			if (headPartIter->origPartRootNode)
			{
				spdlog::trace("Renaming nodes in original part {} back.", headPartIter->origPartRootNode->name.c_str());

				for (auto& entry : skeleton.head.renameMap)
				{
					// This case never happens to a lurker skeleton, thus we don't need to test.
					auto node = findNode(headPartIter->origPartRootNode, entry.second->cstr());
					if (node)
					{
						spdlog::trace("Rename node {} -> {}.", entry.second->cstr(), entry.first->cstr());
						node->name = entry.first->cstr();
					}
				}
			}
			headPartIter->origPartRootNode = nullptr;
		}

		if (!skeleton.head.isFullSkinning)
			skeleton.scanHead();
	}

	void ActorManager::onEvent(const SkinAllHeadGeometryEvent& e)
	{
		// This case never happens to a lurker skeleton, thus we don't need to test.
		auto npc = findNode(e.skeleton, "NPC");
		if (!npc) return;

		std::lock_guard<decltype(m_lock)> l(m_lock);
		if (m_shutdown) return;

		fixArmorNameMaps();

		auto& skeleton = getSkeletonData(e.skeleton);
		skeleton.npc = npc;
		if (e.skeleton->userData)
			skeleton.skeletonOwner = RE::TESObjectREFRPtr(e.skeleton->userData);

		if (e.hasSkinned)
		{
			skeleton.scanHead();
			skeleton.head.isFullSkinning = false;
			if (skeleton.head.npcFaceGeomNode)
			{
				spdlog::trace("NPC face geometry no longer needed, clearing reference.");
				skeleton.head.npcFaceGeomNode = nullptr;
			}
		}
		else
		{
			skeleton.head.isFullSkinning = true;
		}
	}

	void ActorManager::PhysicsItem::setPhysics(Ref<SkyrimSystem>& system, bool active)
	{
		clearPhysics();
		m_physics = system;
		if (active)
		{
			SkyrimPhysicsWorld::get()->addSkinnedMeshSystem(m_physics);
		}
	}

	void ActorManager::PhysicsItem::clearPhysics()
	{
		if (state() == ItemState::e_Active)
		{
			m_physics->m_world->removeSkinnedMeshSystem(m_physics);
		}
		m_physics = nullptr;
	}

	ActorManager::ItemState ActorManager::PhysicsItem::state() const
	{
		return m_physics ? (m_physics->m_world ? ItemState::e_Active : ItemState::e_Inactive) : ItemState::e_NoPhysics;
	}

	const std::vector<Ref<SkinnedMeshBody>>& ActorManager::PhysicsItem::meshes() const
	{
		return m_physics->meshes();
	}

	void ActorManager::PhysicsItem::updateActive(bool active)
	{
		if (active && state() == ItemState::e_Inactive)
		{
			SkyrimPhysicsWorld::get()->addSkinnedMeshSystem(m_physics);
		}
		else if (!active && state() == ItemState::e_Active)
		{
			m_physics->m_world->removeSkinnedMeshSystem(m_physics);
		}
	}

	void ActorManager::PhysicsItem::setWindFactor(float a_windFactor)
	{
		if (m_physics)
			m_physics->m_windFactor = a_windFactor;
	}

	std::vector<ActorManager::Skeleton>& ActorManager::getSkeletons()
	{
		return m_skeletons;
	}

	bool ActorManager::skeletonNeedsParts(RE::NiNode* skeleton)
	{
		return !isFirstPersonSkeleton(skeleton);
		/*
		auto iter = std::find_if(m_skeletons.begin(), m_skeletons.end(), [=](Skeleton& i)
		{
			return i.skeleton == skeleton;
		});
		if (iter != m_skeletons.end())
		{
			return (iter->head.headNode == 0);
		}
		*/
	}

	ActorManager::Skeleton& ActorManager::getSkeletonData(RE::NiNode* skeleton)
	{
		auto iter = std::find_if(m_skeletons.begin(), m_skeletons.end(), [=](Skeleton& i)
			{
				return i.skeleton == skeleton;
			});
		if (iter != m_skeletons.end())
		{
			return *iter;
		}
		if (!isFirstPersonSkeleton(skeleton))
		{
			auto ownerIter = std::find_if(m_skeletons.begin(), m_skeletons.end(), [=](Skeleton& i)
				{
					return !isFirstPersonSkeleton(i.skeleton) && i.skeletonOwner && skeleton->userData && i.skeletonOwner.get() ==
						skeleton->userData;
				});
			if (ownerIter != m_skeletons.end())
			{
				spdlog::trace("New skeleton found for formid {:08x}.", skeleton->userData->formID);
				ownerIter->cleanHead(true);
			}
		}
		m_skeletons.push_back(Skeleton());
		m_skeletons.back().skeleton = skeleton;
		return m_skeletons.back();
	}

	ActorManager::Skeleton* ActorManager::get3rdPersonSkeleton(RE::Actor* actor)
	{
		for (auto& i : m_skeletons)
		{
			const auto owner = i.skeletonOwner ? i.skeletonOwner->As<RE::Actor>() : nullptr;
			if (actor == owner && i.skeleton && !isFirstPersonSkeleton(i.skeleton))
				return &i;
		}
		return 0;
	}

	void ActorManager::Skeleton::doSkeletonMerge(RE::NiNode* dst, RE::NiNode* src, IString* prefix,
		std::unordered_map<IDStr, IDStr>& map)
	{
		for (auto &srcChildAV : src->children) {
			auto *srcChild = srcChildAV->AsNode();
			if (!srcChild) continue;

			if (srcChild->name.empty())
			{
				doSkeletonMerge(dst, srcChild, prefix, map);
				continue;
			}

			// FIXME: This was previously only in doHeadSkeletonMerge.
			// But surely non-head skeletons wouldn't have this anyway?
			if (srcChild->name == "BSFaceGenNiNodeSkinned")
			{
				spdlog::trace("Skipping facegen ninode in skeleton merge.");
				continue;
			}

			// TODO check it's not a lurker skeleton
			auto dstChild = findNode(dst, srcChild->name);
			if (dstChild)
			{
				doSkeletonMerge(dstChild, srcChild, prefix, map);
			}
			else
			{
				dst->AttachChild(cloneNodeTree(srcChild, prefix, map), false);
			}
		}
	}

	// TODO: move to smart pointers to not leak memory
	RE::NiNode* ActorManager::Skeleton::cloneNodeTree(RE::NiNode* src, IString* prefix, std::unordered_map<IDStr, IDStr>& map)
	{
		RE::NiCloningProcess c{};
		c.copyType = 1; // COPY_EXACT
		c.scale = RE::NiPoint3(1.f, 1.f, 1.f);
		auto ret = static_cast<RE::NiNode*>(src->CreateClone(c));
		src->ProcessClone(c);

		// FIXME: cloneHeadNodeTree just did this for ret, not both. Don't know if that matters. Armor parts need it on both.
		renameTree(src, prefix, map);
		renameTree(ret, prefix, map);

		return ret;
	}

	void ActorManager::Skeleton::renameTree(RE::NiNode* root, IString* prefix, std::unordered_map<IDStr, IDStr>& map)
	{
		std::string newName(prefix->cstr(), prefix->size());
		newName += root->name;
		if (map.insert(std::make_pair<IDStr, IDStr>(root->name.c_str(), newName)).second)
			spdlog::trace("Rename Bone {} -> {}.", root->name.c_str(), newName.c_str());
		root->name = newName;

		for (RE::NiPointer<RE::NiAVObject> &child : root->children)
		{
			if (auto *childNode = child->AsNode())
				renameTree(childNode, prefix, map);
		}
	}

	void ActorManager::Skeleton::doSkeletonClean(RE::NiNode* dst, IString* prefix)
	{
		for (int i = dst->children.size() - 1; i >= 0; --i)
		{
			auto child = castNiNode(dst->children[i].get());
			if (!child) continue;

			if (0 == strncmp(child->name.c_str(), prefix->cstr(), prefix->size()))
			{
				dst->DetachChildAt(i++);
			}
			else
			{
				doSkeletonClean(child, prefix);
			}
		}
	}

	// returns the name of the skeleton owner
	std::string ActorManager::Skeleton::name()
	{
		if (skeleton->userData && skeleton->userData->GetBaseObject()) {
			auto bname = skeleton->userData->GetBaseObject()->As<RE::TESFullName>();
			if (bname)
				return bname->fullName.c_str();
		}
		return "";
	}

	void ActorManager::Skeleton::addArmor(RE::NiNode* armorModel)
	{
		IDType id = armors.size() ? armors.back().id + 1 : 0;
		auto prefix = armorPrefix(id);
		// FIXME we probably could simplify this by using findNode as surely we don't merge Armors with lurkers skeleton?
		npc = getNpcNode(skeleton);
		auto physicsFile = DefaultBBP::instance()->scanBBP(armorModel);

		armors.push_back(Armor());
		armors.back().id = id;
		armors.back().prefix = prefix;
		armors.back().physicsFile = physicsFile;

		doSkeletonMerge(npc, armorModel, prefix, armors.back().renameMap);
	}

	void ActorManager::Skeleton::attachArmor(RE::NiNode* armorModel, RE::NiAVObject* attachedNode)
	{
		if (armors.size() == 0 || armors.back().hasPhysics())
			spdlog::info("Not attaching armor - no record or physics already exists");

		Armor& armor = armors.back();

		// The name of the attachedNode provided here will have been changed by the Skyrim exe between this event and the next.
		armor.armorWorn = attachedNode;
		// That's why we set here the need to fix this armor in fixArmorNameMaps() (see its comment)
		// to avoid this name change breaking processes like 'smp reset' when looking for the armor name in the armor nameMap.
		armor.armorCurrentMeshName = attachedNode->name.c_str();
		armor.mustFixNameMap = true;
		mustFixOneArmorMap = true;

		if (!isFirstPersonSkeleton(skeleton))
		{
			std::unordered_map<IDStr, IDStr> renameMap = armor.renameMap;
			// FIXME we probably could simplify this by using findNode as surely we don't attach Armors to lurkers skeleton?
			auto system = SkyrimSystemCreator().createOrUpdateSystem(getNpcNode(skeleton), attachedNode, &armor.physicsFile, std::move(renameMap), nullptr);

			if (system)
			{
				armor.setPhysics(system, isActive);
				hasPhysics = true;
			}
		}

		if (instance()->m_disableSMPHairWhenWigEquipped && skeleton && skeleton->userData)
		{
			RE::Actor* actor = skeleton->userData->As<RE::Actor>();
			if (actor)
				ActorManager().setHeadActiveIfNoHairArmor(actor, this);
		}
	}

	void ActorManager::Skeleton::cleanArmor()
	{
		for (auto& i : armors)
		{
			if (!i.armorWorn) continue;
			if (i.armorWorn->parent) continue;

			i.clearPhysics();
			if (npc) doSkeletonClean(npc, i.prefix);
			i.prefix = nullptr;
		}

		armors.erase(std::remove_if(armors.begin(), armors.end(), [](Armor& i) { return !i.prefix; }), armors.end());
	}

	void ActorManager::Skeleton::cleanHead(bool cleanAll)
	{
		for (auto& headPart : head.headParts)
		{
			if (!headPart.headPart->parent || cleanAll)
			{
				if (cleanAll)
					spdlog::trace("Cleaning headpart {} due to clean all.", headPart.headPart->name.c_str());
				else
					spdlog::trace("Headpart {} disconnected.", headPart.headPart->name.c_str());

				auto renameIt = this->head.renameMap.begin();

				while (renameIt != this->head.renameMap.end())
				{
					bool erase = false;

					if (headPart.renamedBonesInUse.count(renameIt->first) != 0)
					{
						auto findNode = this->head.nodeUseCount.find(renameIt->first);
						if (findNode != this->head.nodeUseCount.end())
						{
							findNode->second -= 1;
							spdlog::trace("Decrementing use count by 1, it is now {}.", findNode->second);
							if (findNode->second <= 0)
							{
								spdlog::trace("Node no longer in use, cleaning from skeleton.");
								auto removeObj = findObject(npc, renameIt->second->cstr());
								if (removeObj)
								{
									spdlog::trace("Found node {}, removing.", removeObj->name.c_str());
									auto parent = removeObj->parent;
									if (parent)
									{
										parent->DetachChild(removeObj);
										removeObj->DecRefCount();
									}
								}
								this->head.nodeUseCount.erase(findNode);
								erase = true;
							}
						}
					}

					if (erase)
						renameIt = this->head.renameMap.erase(renameIt);
					else
						++renameIt;
				}

				headPart.headPart = nullptr;
				headPart.origPartRootNode = nullptr;
				headPart.clearPhysics();
				headPart.renamedBonesInUse.clear();
			}
		}

		head.headParts.erase(std::remove_if(head.headParts.begin(), head.headParts.end(),
			[](Head::HeadPart& i) { return !i.headPart; }), head.headParts.end());
	}

	void ActorManager::Skeleton::clear()
	{
		std::for_each(armors.begin(), armors.end(), [](Armor& armor) { armor.clearPhysics(); });
		SkyrimPhysicsWorld::get()->removeSystemByNode(npc);
		cleanHead();
		head.headParts.clear();
		head.headNode = nullptr;
		armors.clear();
	}

	void ActorManager::Skeleton::calculateDistanceAndOrientationDifferenceFromSource(RE::NiPoint3 sourcePosition, RE::NiPoint3 sourceOrientation)
	{
		if (isPlayerCharacter())
		{
			m_distanceFromCamera2 = 0.f;
			return;
		}

		auto pos = position();
		if (!pos.has_value())
		{
			m_distanceFromCamera2 = (std::numeric_limits<float>::max)();
			return;
		}

		// We calculate the vector between camera and the skeleton feets.
		const auto camera2SkeletonVector = pos.value() - sourcePosition;
		// This is the distance (squared) between the camera and the skeleton feets.
		m_distanceFromCamera2 = camera2SkeletonVector.x * camera2SkeletonVector.x + camera2SkeletonVector.y * camera2SkeletonVector.y + camera2SkeletonVector.z * camera2SkeletonVector.z;
		// This is |camera2SkeletonVector|*cos(angle between both vectors)
		m_cosAngleFromCameraDirectionTimesSkeletonDistance = camera2SkeletonVector.x * sourceOrientation.x + camera2SkeletonVector.y * sourceOrientation.y + camera2SkeletonVector.z * sourceOrientation.z;
	}

	// Is called to print messages only
	bool ActorManager::Skeleton::checkPhysics()
	{
		hasPhysics = false;
		std::for_each(armors.begin(), armors.end(), [=](Armor& armor) {
			if (armor.state() != ItemState::e_NoPhysics)
				hasPhysics = true;
			});
		if (!hasPhysics)
			std::for_each(head.headParts.begin(), head.headParts.end(), [=](Head::HeadPart& headPart) {
			if (headPart.state() != ItemState::e_NoPhysics)
				hasPhysics = true;
				});
		spdlog::info("{} isDrawn: {}", name(), hasPhysics);

		return hasPhysics;
	}

	bool ActorManager::Skeleton::isActiveInScene() const
	{
		// TODO: do this better
		// When entering/exiting an interior, NPCs are detached from the scene but not unloaded, so we need to check two levels up.
		// This properly removes exterior cell armors from the physics world when entering an interior, and vice versa.
		return skeleton->parent && skeleton->parent->parent && skeleton->parent->parent->parent;
	}

	bool ActorManager::Skeleton::isPlayerCharacter() const
	{
		// TODO: why do it both ways?
		constexpr uint32_t playerFormID = 0x14;
		return skeletonOwner.get() == RE::PlayerCharacter::GetSingleton() || (skeleton->userData && skeleton->userData->formID == playerFormID);
	}

	bool ActorManager::Skeleton::isInPlayerView()
	{
		// This function is called only when the skeleton isn't the player character.
		// This might change in the future; in that case this test will have to be enabled.
		//if (isPlayerCharacter())
		//	return true;

		// We always enable the skeletons that are just around the camera.
		// It's useful if for example the skeleton origin is very near, behind the camera,
		// but some parts or the skeleton are in front of the camera and need to be animated.
		auto i = ActorManager::instance();
		float minDistance = i->m_minCullingDistance;
		if (m_distanceFromCamera2 < minDistance * minDistance)
			return true;

		// We don't enable the skeletons behind the camera or on its side.
		if (m_cosAngleFromCameraDirectionTimesSkeletonDistance <= 0)
			return false;

		// We enable only the skeletons that can see the PC or the camera
		const auto owner = this->skeletonOwner ? this->skeletonOwner->As<RE::Actor>() : nullptr;
		if (owner) {
			RE::NiPoint3 hitLocation;
			const auto object = Actor_CalculateLOS(owner, &(i->m_cameraPositionDuringFrame), &hitLocation, 6.28);
			return object ? false : true; // If object, we hit something on the path
		}
		return true; // should never happen, a skeleton without owner?
	}

	std::optional<RE::NiPoint3> ActorManager::Skeleton::position() const
	{
		if (npc)
		{
			// This works for lurker skeletons.
			auto rootNode = findNode(npc, "NPC Root [Root]");
			if (rootNode) return rootNode->world.translate;
		}
		return {};
	}

	void ActorManager::Skeleton::updateWindFactor(float a_windFactor)
	{
		this->currentWindFactor = a_windFactor;
		std::for_each(armors.begin(), armors.end(), [=](Armor& armor) { armor.setWindFactor(a_windFactor); });
		std::for_each(head.headParts.begin(), head.headParts.end(), [=](Head::HeadPart& headPart) { headPart.setWindFactor(a_windFactor); });
	}

	float ActorManager::Skeleton::getWindFactor()
	{
		return this->currentWindFactor;
	}

	bool ActorManager::Skeleton::updateAttachedState(const RE::NiNode* playerCell, bool deactivate = false)
	{
		// 1- Skeletons that aren't active in any scene are always detached, unless they are in the
		// same cell as the player character (workaround for issue in Ancestor Glade).
		// 2- Player character is always attached.
		// 3- Otherwise, attach only if both the camera and this skeleton have a position,
		// the distance between them is below the threshold value,
		// and the angle difference between the camera orientation and the skeleton orientation is below the threshold value.
		isActive = false;
		state = SkeletonState::e_InactiveNotInScene;

		if (deactivate)
			state = SkeletonState::e_InactiveTooFar;
		else if (isActiveInScene() || skeleton->parent && skeleton->parent->parent == playerCell)
		{
			if (isPlayerCharacter())
			{
				// That setting defines whether we don't set the PC skeleton as active
				// when it is in 1st person view, to avoid calculating physics uselessly.
				if (!(instance()->m_disable1stPersonViewPhysics // disabling?
					&& RE::PlayerCamera::GetSingleton()->currentState == RE::PlayerCamera::GetSingleton()->cameraStates[0])) // 1st person view
				{
					isActive = true;
					state = SkeletonState::e_ActiveIsPlayer;
				}
			}
			else if (isInPlayerView())
			{
				isActive = true;
				state = SkeletonState::e_ActiveNearPlayer;
			}
			else
				state = SkeletonState::e_InactiveUnseenByPlayer;
		}

		// We update the activity state of armors and head parts, and add and remove SkinnedMeshSystems to these parts in consequence.
		// We set headparts as not active if the head isn't active (for example because it's hidden by a wig).
		std::for_each(armors.begin(), armors.end(), [=](Armor& armor) { armor.updateActive(isActive); });
		const bool isHeadActive = head.isActive;
		std::for_each(head.headParts.begin(), head.headParts.end(), [=](Head::HeadPart& headPart) { headPart.updateActive(isHeadActive && isActive); });
		return isActive;
	}

	void ActorManager::Skeleton::reloadMeshes()
	{
		for (auto& i : armors)
		{
			i.clearPhysics();

			if (!isFirstPersonSkeleton(skeleton))
			{
				std::unordered_map<IDStr, IDStr> renameMap = i.renameMap;
				auto system = SkyrimSystemCreator().createOrUpdateSystem(npc, i.armorWorn, &i.physicsFile, std::move(renameMap), nullptr);

				if (system)
				{
					i.setPhysics(system, isActive);
					hasPhysics = true;
				}
			}
		}
		scanHead();
	}

	void ActorManager::Skeleton::scanHead()
	{
		if (isFirstPersonSkeleton(this->skeleton))
		{
			spdlog::trace("Not scanning head of first person skeleton.");
			return;
		}

		if (!this->head.headNode)
		{
			spdlog::trace("Actor has no head node.");
			return;
		}

		std::unordered_set<std::string> physicsDupes;

		if (instance()->m_disableSMPHairWhenWigEquipped && skeleton)
		{
			// the what... looking up the form while having a pointer to the very form?
			// TESForm* form = LookupFormByID(skeleton->userData->formID);
			RE::Actor* actor = skeleton->userData ? skeleton->userData->As<RE::Actor>() : nullptr;
			if (actor)
				ActorManager().setHeadActiveIfNoHairArmor(actor, this);
		}

		for (auto& headPart : this->head.headParts)
		{
			// always regen physics for all head parts
			headPart.clearPhysics();

			if (headPart.physicsFile.first.empty())
			{
				spdlog::trace("No physics file for headpart {}.", headPart.headPart->name.c_str());
				continue;
			}

			if (physicsDupes.count(headPart.physicsFile.first))
			{
				spdlog::trace("Previous head part generated physics system for file {}, skipping.",
					headPart.physicsFile.first.c_str());
				continue;
			}

			std::unordered_map<IDStr, IDStr> renameMap = this->head.renameMap;

			spdlog::trace("Try create system for headpart {} physics file {}.", headPart.headPart->name.c_str(),
				headPart.physicsFile.first);
			physicsDupes.insert(headPart.physicsFile.first);
			auto system = SkyrimSystemCreator().createOrUpdateSystem(npc, this->head.headNode, &headPart.physicsFile, std::move(renameMap), nullptr);

			if (system)
			{
				spdlog::trace("Success.");
				headPart.setPhysics(system, isActive);
				hasPhysics = true;
			}
		}
	}

	typedef bool (*_TESNPC_GetFaceGeomPath)(RE::TESNPC* a_npc, char* a_buf);
	REL::Relocation<_TESNPC_GetFaceGeomPath> TESNPC_GetFaceGeomPath(REL::ID(24726));

	void ActorManager::Skeleton::processGeometry(RE::BSFaceGenNiNode* headNode, RE::BSGeometry* geometry)
	{
		if (this->head.headNode && this->head.headNode != headNode)
		{
			spdlog::trace("Completely new head attached to skeleton, clearing tracking.");
			for (auto& headPart : this->head.headParts)
			{
				headPart.clearPhysics();
				headPart.headPart = nullptr;
				headPart.origPartRootNode = nullptr;
			}

			this->head.headParts.clear();

			if (npc)
				doSkeletonClean(npc, this->head.prefix);

			this->head.prefix = nullptr;
			this->head.headNode = nullptr;
			this->head.renameMap.clear();
			this->head.nodeUseCount.clear();
		}

		// clean swapped out headparts
		cleanHead();

		this->head.headNode = headNode;
		++this->head.id;
		this->head.prefix = headPrefix(this->head.id);

		auto it = std::find_if(this->head.headParts.begin(), this->head.headParts.end(),
			[geometry](const Head::HeadPart& p)
			{
				return p.headPart == geometry;
			});

		if (it != this->head.headParts.end())
		{
			spdlog::trace("Geometry is already added as head part.");
			return;
		}

		this->head.headParts.push_back(Head::HeadPart());

		head.headParts.back().headPart = geometry;
		head.headParts.back().clearPhysics();

		// Skinning
		spdlog::trace("Skinning geometry to skeleton.");

		if (!geometry->skinInstance || !geometry->skinInstance->skinData)
		{
			spdlog::error("Geometry is missing skin instance - how?");
			return;
		}

		auto fmd = static_cast<BSFaceGenModelExtraData*>(geometry->GetExtraData("FMD"));

		RE::BSGeometry* origGeom = nullptr;
		RE::NiGeometry* origNiGeom = nullptr;

		if (fmd && fmd->m_model && fmd->m_model->modelMeshData && fmd->m_model->modelMeshData->faceNode)
		{
			spdlog::trace("Original part node found via facegen extra model data.");
			auto *origRootNode = fmd->m_model->modelMeshData->faceNode->AsNode();
			head.headParts.back().physicsFile = DefaultBBP::instance()->scanBBP(origRootNode);
			head.headParts.back().origPartRootNode = origRootNode;
			for (auto &child : origRootNode->children)
			{
				if (auto *geo = child->AsGeometry())
				{
					origGeom = geo;
					break;
				}
			}
		}
		else
		{
			spdlog::trace("No facegen extra model data available, loading original facegeometry.");
			if (!head.npcFaceGeomNode)
			{
				if (skeleton->userData && skeleton->userData->GetBaseObject())
				{
					auto *npc = skeleton->userData->GetBaseObject()->As<RE::TESNPC>();
					if (npc)
					{
						char filePath[MAX_PATH];
						if (TESNPC_GetFaceGeomPath(npc, filePath))
						{
							spdlog::trace("Loading facegeometry from path {}.", filePath);

							NiStreamHelper stream;

							RE::BSResourceNiBinaryStream binaryStream(filePath);
							if (!binaryStream.good())
							{
								spdlog::error("Somehow NPC facegeometry was not found.");
							}
							else
							{
								stream->Load1(&binaryStream);
								if (!stream->topObjects.empty())
								{
									auto *rootFadeNode = stream->topObjects[0]->AsFadeNode();
									if (rootFadeNode)
									{
										spdlog::trace("NPC facegeometry root fadeNode found.");
										head.npcFaceGeomNode = RE::NiPointer{ rootFadeNode };
									}
									else
									{
										spdlog::trace("NPC facegeometry root wasn't fadeNode as expected.");
									}
								}
							}
						}
					}
				}
			}
			else
				spdlog::trace("Using cached facegeometry.");
			if (head.npcFaceGeomNode)
			{
				head.headParts.back().physicsFile = DefaultBBP::instance()->scanBBP(head.npcFaceGeomNode.get());
				auto obj = findObject(head.npcFaceGeomNode.get(), geometry->name);
				if (obj)
				{
					auto ob = obj->AsGeometry();
					if (ob) origGeom = ob;
					else {
						auto on = obj->AsNiGeometry();
						if (on) origNiGeom = on;
					}
				}
			}
		}

		bool hasMerged = false;
		bool hasRenames = false;

		for (int boneIdx = 0; boneIdx < geometry->skinInstance->skinData->bones; boneIdx++)
		{
			RE::BSFixedString boneName;

			// skin the way the game does via FMD
			if (boneIdx <= 7)
			{
				if (fmd)
					boneName = fmd->bones[boneIdx];
			}

			if (!*boneName.c_str())
			{
				if (origGeom)
				{
					boneName = origGeom->skinInstance->bones[boneIdx]->name;
				}
				else if (origNiGeom)
				{
					boneName = origNiGeom->spSkinInstance->bones[boneIdx]->name;
				}
			}

			auto renameIt = this->head.renameMap.find(boneName.c_str());

			if (renameIt != this->head.renameMap.end())
			{
				spdlog::trace("Found renamed bone {} -> {}.", boneName.c_str(), renameIt->second->cstr());
				boneName = renameIt->second->cstr();
				hasRenames = true;
			}

			auto boneNode = findNode(this->npc, boneName);

			if (!boneNode && !hasMerged)
			{
				spdlog::trace("Bone not found on skeleton, trying skeleton merge.");
				if (this->head.headParts.back().origPartRootNode)
				{
					doSkeletonMerge(npc, head.headParts.back().origPartRootNode, head.prefix, head.renameMap);
				}
				else if (this->head.npcFaceGeomNode)
				{
					// Facegen data doesn't have any tree structure to the skeleton. We need to make any new
					// nodes children of the head node, so that they move properly when there's no physics.
					// This case never happens to a lurker skeleton, thus we don't need to test.
					auto headNode = findNode(head.npcFaceGeomNode.get(), "NPC Head [Head]");
					if (headNode)
					{
						RE::NiTransform invTransform = headNode->local.Invert();
						for (int i = 0; i < head.npcFaceGeomNode->children.size(); ++i)
						{
							RE::NiPointer<RE::NiAVObject> child{ head.npcFaceGeomNode->children[i] };
							// This case never happens to a lurker skeleton, thus we don't need to test.
							if (child && !findNode(npc, child->name))
							{
								child->local = invTransform * child->local;
								head.npcFaceGeomNode->DetachChildAt(i);
								headNode->AttachChild(child.get(), false);
							}
						}
					}
					doSkeletonMerge(npc, this->head.npcFaceGeomNode.get(), head.prefix, head.renameMap);
				}
				hasMerged = true;

				auto postMergeRenameIt = this->head.renameMap.find(boneName.c_str());

				if (postMergeRenameIt != this->head.renameMap.end())
				{
					spdlog::trace("Found renamed bone {} -> {}.", boneName.c_str(), postMergeRenameIt->second->cstr());
					boneName = postMergeRenameIt->second->cstr();
					hasRenames = true;
				}

				boneNode = findNode(this->npc, boneName);
			}

			if (!boneNode)
			{
				spdlog::error("Bone {} not found after skeleton merge, geometry cannot be fully skinned.", boneName.c_str());
				continue;
			}

			geometry->skinInstance->bones[boneIdx] = boneNode;
			geometry->skinInstance->boneWorldTransforms[boneIdx] = &boneNode->world;
		}

		geometry->skinInstance->rootParent = headNode;

		if (hasRenames)
		{
			for (auto& entry : head.renameMap)
			{
				if ((this->head.headParts.back().origPartRootNode && findObject(this->head.headParts.back().origPartRootNode, entry.first->cstr())) ||
					(this->head.npcFaceGeomNode && findObject(this->head.npcFaceGeomNode.get(), entry.first->cstr())))
				{
					auto findNode = this->head.nodeUseCount.find(entry.first);
					if (findNode != this->head.nodeUseCount.end())
					{
						findNode->second += 1;
						spdlog::trace("Incrementing use count by 1, it is now {}.", findNode->second);
					}
					else
					{
						this->head.nodeUseCount.insert(std::make_pair(entry.first, 1));
						spdlog::trace("First use of bone, count 1.");
					}
					head.headParts.back().renamedBonesInUse.insert(entry.first);
				}
			}
		}

		spdlog::trace("Done skinning part.");
	}
}
