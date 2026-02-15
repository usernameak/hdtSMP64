#include "pch.h"

#include "Hooks.h"
#include "HookEvents.h"
#include "Offsets.h"
#include <xbyak/xbyak.h>
#include "ActorManager.h"

#include <windows.h>
#include <detours.h>

namespace hdt
{
	// BEGIN: class BSFaceGenNiNode

	typedef void(*PFNBSFaceGenNiNode_SkinAllGeometry)(RE::BSFaceGenNiNode* self, RE::NiNode* a_skeleton, RE::BSGeometry* a_geometry, char a_unk);
	typedef void(*PFNBSFaceGenNiNode_SkinSingleGeometry)(RE::BSFaceGenNiNode* self, RE::NiNode* a_skeleton, RE::BSGeometry* a_geometry, RE::BSTriShape* a_trishape);

	static REL::Relocation<PFNBSFaceGenNiNode_SkinAllGeometry> BSFaceGenNiNode_SkinAllGeometry{ REL::ID{26986} };
	static REL::Relocation<PFNBSFaceGenNiNode_SkinSingleGeometry> BSFaceGenNiNode_SkinSingleGeometry{ REL::ID{26987} };

	static PFNBSFaceGenNiNode_SkinAllGeometry BSFaceGenNiNode_SkinAllGeometry_Trampoline;
	static PFNBSFaceGenNiNode_SkinSingleGeometry BSFaceGenNiNode_SkinSingleGeometry_Trampoline;

	static void BSFaceGenNiNode_SkinSingleGeometry_Hook(RE::BSFaceGenNiNode* self, RE::NiNode* a_skeleton, RE::BSGeometry* a_geometry, RE::BSTriShape* a_trishape);

	static void BSFaceGenNiNode_ProcessHeadPart(RE::BSFaceGenNiNode* self, RE::BGSHeadPart* headPart, RE::NiNode* a_skeleton)
	{
		if (headPart)
		{
			RE::NiAVObject* headNode = self->GetObjectByName(headPart->formEditorID);
			if (headNode)
			{
				RE::BSGeometry* headGeo = headNode->AsGeometry();
				if (headGeo)
					BSFaceGenNiNode_SkinSingleGeometry_Hook(self, a_skeleton, headGeo, nullptr);
			}
			for (uint32_t p = 0; p < headPart->extraParts.size(); p++)
			{
				RE::BGSHeadPart* extraPart = headPart->extraParts[p];
				if (extraPart)
					BSFaceGenNiNode_ProcessHeadPart(self, extraPart, a_skeleton);
			}
		}
	}

	static void BSFaceGenNiNode_SkinAllGeometryCalls(RE::BSFaceGenNiNode* self, RE::NiNode* a_skeleton, RE::BSGeometry* a_geometry, char a_unk)
	{
		bool needRegularCall = true;
		if (ActorManager::instance()->skeletonNeedsParts(a_skeleton))
		{
			RE::Actor* actor = a_skeleton->userData->As<RE::Actor>();
			if (actor)
			{
				RE::TESNPC* actorBase = actor->GetBaseObject()->As<RE::TESNPC>();
				uint32_t numHeadParts = 0;
				RE::BGSHeadPart** Headparts = nullptr;
				if (actorBase->HasOverlays()) {
					numHeadParts = actorBase->GetNumBaseOverlays();
					Headparts = actorBase->GetBaseOverlays();
				}
				else {
					numHeadParts = actorBase->numHeadParts;
					Headparts = actorBase->headParts;
				}
				if (Headparts)
				{
					for (uint32_t i = 0; i < numHeadParts; i++) {
						if (Headparts[i]) {
							BSFaceGenNiNode_ProcessHeadPart(self, Headparts[i], a_skeleton);
						}
					}
				}
				if (a_skeleton->userData && a_skeleton->userData->formID == 0x14)
					needRegularCall = false;
			}
		}

		if (needRegularCall)
			BSFaceGenNiNode_SkinAllGeometry_Trampoline(self, a_skeleton, a_geometry, a_unk);
	}

	static void BSFaceGenNiNode_SkinSingleGeometry_Hook(RE::BSFaceGenNiNode* self, RE::NiNode* a_skeleton, RE::BSGeometry* a_geometry, RE::BSTriShape* a_trishape)
	{
		const char* name = "";

		RE::FormID formId = 0x0;
		if (a_skeleton->userData && a_skeleton->userData->GetBaseObject())
		{
			auto *bname = a_skeleton->userData->GetBaseObject()->As<RE::TESFullName>();
			if (bname)
				name = bname->GetFullName();

			auto *bnpc = a_skeleton->userData->GetBaseObject()->As<RE::TESNPC>();
			if (bnpc && bnpc->faceNPC)
				formId = bnpc->faceNPC->formID;
		}
		spdlog::info("SkinSingleGeometry {} {} - {}, (formid {:08x} base form {:08x} head template form {:08x})",
			a_skeleton->name.c_str(), a_skeleton->children.size(), name,
			a_skeleton->userData ? a_skeleton->userData->formID : 0x0,
			a_skeleton->userData ? a_skeleton->userData->GetBaseObject()->formID : 0x0,
			formId);

		SkinSingleHeadGeometryEvent e;
		e.skeleton = a_skeleton;
		e.geometry = a_geometry;
		e.headNode = self;
		g_skinSingleHeadGeometryEventDispatcher.dispatch(e);
	}

	static void BSFaceGenNiNode_SkinAllGeometry_Hook(RE::BSFaceGenNiNode* self, RE::NiNode* a_skeleton, RE::BSGeometry* a_geometry, char a_unk)
	{
		const char* name = "";
		uint32_t formId = 0x0;
		if (a_skeleton->userData && a_skeleton->userData->GetBaseObject())
		{
			auto bname = a_skeleton->userData->GetBaseObject()->As<RE::TESFullName>();
			if (bname)
				name = bname->GetFullName();
			auto bnpc = a_skeleton->userData->GetBaseObject()->As<RE::TESNPC>();
			if (bnpc && bnpc->faceNPC)
				formId = bnpc->faceNPC->formID;
		}
		spdlog::info("SkinAllGeometry {} {} - {}, (formid {:08x} base form {:08x} head template form {:08x})",
			a_skeleton->name.c_str(), static_cast<uint32_t>(a_skeleton->children.size()), name,
			a_skeleton->userData ? a_skeleton->userData->formID : 0x0,
			a_skeleton->userData ? a_skeleton->userData->GetBaseObject()->formID : 0x0, formId);

		SkinAllHeadGeometryEvent e;
		e.skeleton = a_skeleton;
		e.headNode = self;
		g_skinAllHeadGeometryEventDispatcher.dispatch(e);

#ifdef ANNIVERSARY_EDITION
		BSFaceGenNiNode_SkinAllGeometryCalls(self, a_skeleton, a_geometry, a_unk);
#else
		BSFaceGenNiNode_SkinAllGeometry_Trampoline(self, a_skeleton, a_geometry, a_unk);
#endif

		e.hasSkinned = true;
		g_skinAllHeadGeometryEventDispatcher.dispatch(e);
	}

	void BSFaceGenNiNode_SetHooks()
	{
		BSFaceGenNiNode_SkinSingleGeometry_Trampoline = BSFaceGenNiNode_SkinSingleGeometry.get();
		DetourAttach((void**)&BSFaceGenNiNode_SkinSingleGeometry_Trampoline, &BSFaceGenNiNode_SkinSingleGeometry_Hook);

		BSFaceGenNiNode_SkinAllGeometry_Trampoline = BSFaceGenNiNode_SkinAllGeometry.get();
		DetourAttach((void**)&BSFaceGenNiNode_SkinAllGeometry_Trampoline, &BSFaceGenNiNode_SkinAllGeometry_Hook);

		// .text:00000001403D88D4                 cmp     ebx, 8
		// patch 8 -> 7
		// The same for AE/SE/VR.
		static REL::Relocation<uintptr_t> writeSingleGeometryBug(REL::ID{ 26987 }, 0x96);
		writeSingleGeometryBug.write(0x7);

		// bone limit workaround

		static REL::Relocation<uintptr_t> boneLimit(REL::ID{ 24836 }, 0x75);

		struct BSFaceGenExtraModelData_BoneCount_Code : Xbyak::CodeGenerator
		{
			BSFaceGenExtraModelData_BoneCount_Code() : CodeGenerator(64)
			{
				Xbyak::Label j_Out;

#ifndef ANNIVERSARY_EDITION
				mov(esi, ptr[rax + 0x58]);
				cmp(esi, 9);
				jl(j_Out);
				mov(esi, 8);
#else
				mov(ebp, ptr[rax + 0x58]);
				cmp(ebp, 9);
				jl(j_Out);
				mov(ebp, 8);
#endif // !ANNIVERSARY_EDITION

				L(j_Out);
				jmp(ptr[rip]);
				dq(boneLimit.get() + 0x7);
			}
		};


		BSFaceGenExtraModelData_BoneCount_Code code;
		REL::GetTrampoline().write_jmp5(boneLimit.get(), uintptr_t(REL::GetTrampoline().allocate(code)));

	}

	void BSFaceGenNiNode_RemoveHooks()
	{
		DetourDetach((void**)&BSFaceGenNiNode_SkinAllGeometry_Trampoline, &BSFaceGenNiNode_SkinAllGeometry_Hook);
		DetourDetach((void**)&BSFaceGenNiNode_SkinSingleGeometry_Trampoline, &BSFaceGenNiNode_SkinSingleGeometry_Hook);
	}

	// END  : class BSFaceGenNiNode
	
	// --------------------------
	 
	// BEGIN: class BipedAnim

	typedef RE::NiAVObject *(*PFNBipedAnim_AttachArmor)(
		RE::BipedAnim* self,
		RE::NiNode* armor,
		RE::NiNode* skeleton,
		void* unk3,
		char unk4,
		char unk5,
		void* unk6
	);

	static REL::Relocation<PFNBipedAnim_AttachArmor> BipedAnim_AttachArmor{ RELOCATION_ID(15535, 15712) };

	PFNBipedAnim_AttachArmor BipedAnim_AttachArmor_Trampoline;

	static RE::NiAVObject* BipedAnim_AttachArmor_Hook(
		RE::BipedAnim *self,
		RE::NiNode* armor, 
		RE::NiNode* skeleton,
		void* unk3, 
		char unk4, 
		char unk5, 
		void* unk6)
	{
		ArmorAttachEvent event;
		event.armorModel = armor;
		event.skeleton = skeleton;
		g_armorAttachEventDispatcher.dispatch(event);

		auto ret = BipedAnim_AttachArmor_Trampoline(self, armor, skeleton, unk3, unk4, unk5, unk6);

		if (ret) {
			event.attachedNode = ret;
			event.hasAttached = true;
			g_armorAttachEventDispatcher.dispatch(event);
		}

		return ret;
	}

	void BipedAnim_SetHooks()
	{
		BipedAnim_AttachArmor_Trampoline = BipedAnim_AttachArmor.get();
		DetourAttach((void**)&BipedAnim_AttachArmor_Trampoline, &BipedAnim_AttachArmor_Hook);
	}

	void BipedAnim_RemoveHooks()
	{
		DetourDetach((void **)&BipedAnim_AttachArmor_Trampoline, &BipedAnim_AttachArmor_Hook);
	}


	// END  : class BipedAnim

	// --------------------------

	// BEGIN: class ActorEquipManager

	typedef bool(*PFNActorEquipManager_UnequipObject)(
		RE::ActorEquipManager* self,
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		std::uint32_t a_count,
		const RE::BGSEquipSlot* a_slot,
		bool a_queueEquip,
		bool a_forceEquip,
		bool a_playSounds,
		bool a_applyNow,
		const RE::BGSEquipSlot* a_slotToReplace
	);

	static REL::Relocation<PFNActorEquipManager_UnequipObject> ActorEquipManager_UnequipObject{ RELOCATION_ID(37945, 38901) };

	PFNActorEquipManager_UnequipObject ActorEquipManager_UnequipObject_Trampoline;

	static bool ActorEquipManager_UnequipObject_Hook(
		RE::ActorEquipManager *self,
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		std::uint32_t a_count,
		const RE::BGSEquipSlot* a_slot,
		bool a_queueEquip,
		bool a_forceEquip,
		bool a_playSounds,
		bool a_applyNow,
		const RE::BGSEquipSlot* a_slotToReplace
	)
	{
		ArmorDetachEvent event;
		event.actor = a_actor;
		g_armorDetachEventDispatcher.dispatch(event);

		bool ret = ActorEquipManager_UnequipObject_Trampoline(
			self,
			a_actor, 
			a_object, 
			a_extraData,
			a_count,
			a_slot,
			a_queueEquip, 
			a_forceEquip,
			a_playSounds, 
			a_applyNow,
			a_slotToReplace
		);

		event.hasDetached = true;
		g_armorDetachEventDispatcher.dispatch(event);

		return ret;
	}

	static void ActorEquipManager_SetHooks()
	{
		ActorEquipManager_UnequipObject_Trampoline = ActorEquipManager_UnequipObject.get();
		DetourAttach((void**)&ActorEquipManager_UnequipObject_Trampoline, &ActorEquipManager_UnequipObject_Hook);
	}

	static void ActorEquipManager_RemoveHooks()
	{
		DetourDetach((void**)&ActorEquipManager_UnequipObject_Trampoline, &ActorEquipManager_UnequipObject_Hook);
	}

	// END  : class ActorEquipManager

	// --------------------------

	// BEGIN: class Main

	typedef void (*PFNMain_Frame)(RE::Main* self);
	typedef void (*PFNMain_FrameSync)(uint64_t param_1); // this is static

	static REL::Relocation<PFNMain_Frame> Main_Frame{ REL::ID{36564} };
	static REL::Relocation<PFNMain_FrameSync> Main_FrameSync{ REL::ID{19865} };

	static PFNMain_Frame Main_Frame_Trampoline;
	static PFNMain_FrameSync Main_FrameSync_Trampoline;

	static void Main_Frame_Hook(RE::Main *self)
	{
		Main_Frame_Trampoline(self);

		if (self->quitGame)
		{
			g_shutdownEventDispatcher.dispatch(ShutdownEvent());
		}
		else
		{
			FrameEvent e;
			e.gamePaused = self->freezeTime;
			g_frameEventDispatcher.dispatch(e);
		}
	}

	static void Main_FrameSync_Hook(uint64_t param_1)
	{
		Main_FrameSync_Trampoline(param_1);

		g_frameSyncEventDispatcher.dispatch(FrameSyncEvent());
	}

	static void Main_SetHooks()
	{
		DetourAttach((void**)Main_Frame_Trampoline, &Main_Frame_Hook);
		DetourAttach((void**)Main_FrameSync_Trampoline, &Main_FrameSync_Hook);
	}

	static void Main_RemoveHooks()
	{
		DetourDetach((void**)Main_Frame_Trampoline, &Main_Frame_Hook);
		DetourDetach((void**)Main_FrameSync_Trampoline, &Main_FrameSync_Hook);
	}

	// END  : class Main

	void hookAll()
	{
		DetourRestoreAfterWith();
		DetourTransactionBegin();

		Main_SetHooks();
		BipedAnim_SetHooks();
		ActorEquipManager_SetHooks();
		BSFaceGenNiNode_SetHooks();

		DetourTransactionCommit();
	}

	void unhookAll()
	{
		DetourRestoreAfterWith();
		DetourTransactionBegin();

		BSFaceGenNiNode_RemoveHooks();
		ActorEquipManager_RemoveHooks();
		BipedAnim_RemoveHooks();
		Main_RemoveHooks();

		DetourTransactionCommit();
	}
}
