#pragma once

// 60 (FMD)
class BSFaceGenModelExtraData : public RE::NiExtraData
{
public:
	RE::BSFaceGenModel* m_model;	// 18
	RE::BSFixedString bones[8];		// 20
};

static_assert(sizeof(BSFaceGenModelExtraData) == 0x60);

class NiStreamHelper {
	std::byte m_raw[sizeof(RE::NiStream)];

public:
	NiStreamHelper() : m_raw{} {
		using func_t = RE::NiStream* (*)(RE::NiStream*);
		REL::ID offset(70324);
		REL::Relocation<func_t> func{ offset };
		func(get());
	}

	~NiStreamHelper() {
		get()->~NiStream();
	}

	RE::NiStream* get() {
		return reinterpret_cast<RE::NiStream*>(&m_raw);
	}

	RE::NiStream* operator->() {
		return get();
	}
};

inline float GetGameStepRealTime()
{
	static REL::Relocation<float*> seconds{ RELOCATION_ID(523661, 410200) };
	return *seconds;
}
