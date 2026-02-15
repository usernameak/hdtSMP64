#pragma once

#include "EventDispatcherImpl.h"

namespace hdt
{
	struct SkinAllHeadGeometryEvent
	{
		RE::NiNode* skeleton = nullptr;
		RE::BSFaceGenNiNode* headNode = nullptr;
		bool hasSkinned = false;
	};

	struct SkinSingleHeadGeometryEvent
	{
		RE::NiNode* skeleton = nullptr;
		RE::BSFaceGenNiNode* headNode = nullptr;
		RE::BSGeometry* geometry = nullptr;
	};

	struct ArmorAttachEvent
	{
		RE::NiNode* armorModel = nullptr;
		RE::NiNode* skeleton = nullptr;
		RE::NiAVObject* attachedNode = nullptr;
		bool hasAttached = false;
	};

	struct ArmorDetachEvent
	{
		RE::Actor* actor = nullptr;
		bool hasDetached = false;
	};

	struct FrameEvent
	{
		bool gamePaused;
	};

	struct FrameSyncEvent
	{
	};

	struct ShutdownEvent
	{
	};

	extern EventDispatcherImpl<FrameEvent> g_frameEventDispatcher;
	extern EventDispatcherImpl<FrameSyncEvent> g_frameSyncEventDispatcher;
	extern EventDispatcherImpl<ShutdownEvent> g_shutdownEventDispatcher;
	extern EventDispatcherImpl<ArmorAttachEvent> g_armorAttachEventDispatcher;
	extern EventDispatcherImpl<ArmorDetachEvent> g_armorDetachEventDispatcher;
	extern EventDispatcherImpl<SkinAllHeadGeometryEvent> g_skinAllHeadGeometryEventDispatcher;
	extern EventDispatcherImpl<SkinSingleHeadGeometryEvent> g_skinSingleHeadGeometryEventDispatcher;
}
