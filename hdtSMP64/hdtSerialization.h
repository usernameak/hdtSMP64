#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <iostream>

namespace hdt {
	class SerializerBase;

	extern std::vector<SerializerBase*> g_SerializerList;

	class SerializerBase {
	public:
		SerializerBase() {};
		~SerializerBase() {};
		virtual uint32_t StorageName() = 0;
		virtual uint32_t FormatVersion() = 0;

		virtual void SaveData(SKSE::SerializationInterface*) = 0;
		virtual void ReadData(SKSE::SerializationInterface*, uint32_t) = 0;

		static inline std::vector<SerializerBase*>& GetSerializerList() { return g_SerializerList; };

		static void Save(SKSE::SerializationInterface* intfc) {
			for (auto data_block : g_SerializerList) {
				//Console_Print("[HDT-SMP] Saving data, type: %s version: %08X", UInt32toStr(data_block->StorageName()).c_str(), data_block->FormatVersion());
				data_block->SaveData(intfc);
			}
		};

		static void Load(SKSE::SerializationInterface* intfc) {
			uint32_t type, version, length;
			//auto load_begin = clock();
			while (intfc->GetNextRecordInfo(type, version, length)) {
				auto record = std::find_if(g_SerializerList.begin(), g_SerializerList.end(), [type, version](SerializerBase* a_srlzr) {
					return type == a_srlzr->StorageName() && version == a_srlzr->FormatVersion();
					}
				);
				if (record == g_SerializerList.end())continue;
				//_MESSAGE("[HDT-SMP] Reading data, type: %s version: %08X length: %d", UInt32toStr(type).c_str(), version, length);
				(*record)->ReadData(intfc, length);
			}
			//Less than a microsecond
			//Console_Print("[HDT-SMP] Serializer loading cost: %.3f sec.", (clock() - load_begin) / 1000.0f);
		};
	};

	template<class Storage_t = void, class Stream_t = std::stringstream>
	class Serializer :public SerializerBase {
	public:
		Serializer() {
			g_SerializerList.push_back(this);
		};

		~Serializer() {};

		virtual Stream_t Serialize() = 0;
		virtual Storage_t Deserialize(Stream_t&) = 0;

		void SaveData(SKSE::SerializationInterface*) override;
		void ReadData(SKSE::SerializationInterface*, uint32_t) override;

	protected:
		static inline std::string _toString(Stream_t& _stream) {
			return _stream.rdbuf()->str();
		};
	};
	
	template<class Storage_t, class Stream_t>
	inline void Serializer<Storage_t, Stream_t>::SaveData(SKSE::SerializationInterface* intfc)
	{
		Stream_t s_data_block = this->Serialize();
		intfc->OpenRecord(this->StorageName(), this->FormatVersion());
		auto success = intfc->WriteRecordData(_toString(s_data_block).c_str(), _toString(s_data_block).length());
		//Console_Print("Writing Data: \"%s\" \nStatus: %s", _toString(s_data_block).c_str(), success?"Succeeded":"Failed");
	}

	template<class Storage_t, class Stream_t>
	inline void Serializer<Storage_t, Stream_t>::ReadData(SKSE::SerializationInterface* intfc, uint32_t length)
	{
		char* data_block = new char[length];
		intfc->ReadRecordData(data_block, length);
		std::string s_data(data_block, length);
		//_MESSAGE("Reading Data: %s", s_data.c_str());
		Stream_t _stream; _stream << s_data;
		this->Deserialize(_stream);
	}
}