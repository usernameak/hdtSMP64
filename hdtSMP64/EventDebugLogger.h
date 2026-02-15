#pragma once

#include "HookEvents.h"
#include "IEventListener.h"

namespace hdt
{
	class EventDebugLogger
		: public IEventListener<ArmorAttachEvent>
		, public RE::BSTEventSink<RE::TESCellAttachDetachEvent>
		, public RE::BSTEventSink<RE::TESMoveAttachDetachEvent>
	{
	protected:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESCellAttachDetachEvent* evn,
			RE::BSTEventSource<RE::TESCellAttachDetachEvent>* dispatcher) override;
		RE::BSEventNotifyControl ProcessEvent(const RE::TESMoveAttachDetachEvent* evn,
			RE::BSTEventSource<RE::TESMoveAttachDetachEvent>* dispatcher) override;

		void onEvent(const ArmorAttachEvent&) override;
	};
}
