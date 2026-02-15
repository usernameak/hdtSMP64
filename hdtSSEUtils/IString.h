#pragma once

#include "../hdtSSEUtils/Ref.h"
#include <type_traits>

namespace hdt
{
	// Unified Const String Class
	class IString
	{
	public:
		virtual ~IString() {}
		virtual void retain() = 0;
		virtual void release() = 0;

		virtual const char* cstr() const = 0;
		virtual size_t size() const = 0;
	};

	template <typename T>
	struct RefImpl;

	template <std::derived_from<IString> T>
	struct RefImpl<T>
	{
		inline static void retain(IString* str) { str->retain(); }
		inline static void release(IString* str) { str->release(); }
	};
}
