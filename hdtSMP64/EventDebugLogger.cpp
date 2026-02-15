#include "pch.h"

#include "EventDebugLogger.h"

namespace hdt
{
	RE::BSEventNotifyControl EventDebugLogger::ProcessEvent(const RE::TESCellAttachDetachEvent* evn,
		RE::BSTEventSource<RE::TESCellAttachDetachEvent>* dispatcher)
	{
		if (evn && evn->reference && evn->reference->formType == RE::Character::FORMTYPE)
		{
			spdlog::trace("Received TESCellAttachDetachEvent(formID {:08X}, name {}, attached={}).", evn->reference->formID,
				evn->reference->GetBaseObject()->GetFormEditorID(), evn->attached ? "true" : "false");
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl EventDebugLogger::ProcessEvent(const RE::TESMoveAttachDetachEvent* evn,
	                                           RE::BSTEventSource<RE::TESMoveAttachDetachEvent>* dispatcher)
	{
		if (evn && evn->movedRef && evn->movedRef->formType == RE::Character::FORMTYPE)
		{
			spdlog::trace("Received TESMoveAttachDetachEvent(formID {:08X}, name {}, attached={}).", evn->movedRef->formID,
			          evn->movedRef->GetBaseObject()->GetFormEditorID(), evn->isCellAttached ? "true" : "false");
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	void EventDebugLogger::onEvent(const ArmorAttachEvent& e)
	{
		spdlog::trace(
			"Received ArmorAttachEvent(armorModel={} ({:016X}), skeleton={} ({:016X}), attachedNode={} ({:016X}), hasAttached={}).",
			e.armorModel ? e.armorModel->name.c_str() : "null",
			(uintptr_t)e.armorModel,
			e.skeleton ? e.skeleton->name.c_str() : "null",
			(uintptr_t)e.skeleton,
			e.attachedNode ? e.attachedNode->name.c_str() : "null",
			(uintptr_t)e.attachedNode,
			static_cast<uintptr_t>(e.hasAttached) ? "true" : "false");
	}
}
