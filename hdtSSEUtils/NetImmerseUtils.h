#pragma once

#include "Ref.h"

namespace hdt
{
	inline RE::NiNode* castNiNode(RE::NiAVObject* obj) { return obj ? obj->AsNode() : nullptr; }
	inline RE::BSTriShape* castBSTriShape(RE::NiAVObject* obj) { return obj ? obj->AsTriShape() : nullptr; }
	inline RE::BSDynamicTriShape* castBSDynamicTriShape(RE::NiAVObject* obj) { return obj ? obj->AsDynamicTriShape() : nullptr; }

	RE::NiNode* addParentToNode(RE::NiNode* node, const char* name);

	RE::NiAVObject* findObject(RE::NiAVObject* obj, const RE::BSFixedString& name);
	RE::NiNode* findNode(RE::NiNode* obj, const RE::BSFixedString& name);

	inline float length(const RE::NiPoint3& a) { return a.Length(); }
	inline float distance(const RE::NiPoint3& a, const RE::NiPoint3& b) { return length(a - b); }

	template <typename T>
	struct RefImpl;

	template <std::derived_from<RE::NiRefObject> T>
	struct RefImpl<T> {
		inline static void retain(RE::NiRefObject* object) { object->IncRefCount(); }
		inline static void release(RE::NiRefObject* object) { object->DecRefCount(); }
	};

	std::string readAllFile(const char* path);
	std::string readAllFile2(const char* path);

	void updateTransformUpDown(RE::NiAVObject* obj, bool dirty);
}
