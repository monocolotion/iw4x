#include <STDInclude.hpp>

#include "Events.hpp"
#include "Reserved.hpp"

#include "GSC/Script.hpp"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

namespace Components
{
	namespace
	{
		const char* ReservedListFile = "userraw/reserved-users.json";

		std::string Reason;

		using reservedList = std::unordered_set<std::uint64_t>;
		Utils::Concurrency::Container<reservedList> ReservedList;

		// Have only one instance of IW4x read/write the file
		std::unique_lock<Utils::NamedMutex> Lock()
		{
			static Utils::NamedMutex mutex{ "iw4x-reserved-list-lock" };
			std::unique_lock lock{ mutex };
			return lock;
		}

		rapidjson::Document ParseReservedList()
		{
			rapidjson::Document defaultDoc{};
			defaultDoc.SetObject();

			const auto _ = Lock();

			auto data = Utils::IO::ReadFile(ReservedListFile);
			if (data.empty())
			{
				Logger::Print("reserved-users.json does not exist\n");
				return defaultDoc;
			}

			rapidjson::Document doc{};
			const rapidjson::ParseResult result = doc.Parse(data);

			if (!result || !doc.IsObject())
			{
				return defaultDoc;
			}

			return doc;
		}

		void LoadReservedList()
		{
			const auto listDocument = ParseReservedList();
			if (!listDocument.HasMember("Reason"))
			{
				Logger::Print("reserved-users.json does not contain JSOn member \"Reason\". Defaulting to {}\n", Reason);
			}
			else if (listDocument["Reason"].IsString())
			{
				Reason = listDocument["Reason"].GetString();
			}

			if (!listDocument.HasMember("GUID"))
			{
				Logger::Print("reserved-users.json does not contain JSON member \"GUID\". No user will be considered privileged\n");
				return;
			}

			const rapidjson::Value& memberList = listDocument["GUID"];
			if (!memberList.IsArray() || memberList.Empty())
			{
				return;
			}

			ReservedList.access([&memberList](reservedList& list)
			{
				for (const auto& entry : memberList.GetArray())
				{
					if (entry.IsUint64())
					{
						list.insert(entry.GetUint64());
					}
				}
			});
		}

		void SaveReservedList()
		{
			const auto _ = Lock();

			rapidjson::Document listDocument;
			listDocument.SetObject();

			listDocument.AddMember("Reason", Reason, listDocument.GetAllocator());

			rapidjson::Value guidEntries(rapidjson::kArrayType);
			auto copyList = ReservedList.access<reservedList>([](reservedList& list) -> reservedList
			{
				return list;
			});

			assert(!copyList.empty());

			for (const auto& entry : copyList)
			{
				rapidjson::Value guid;
				guid.SetUint64(entry);

				guidEntries.PushBack(guid, listDocument.GetAllocator());
			}

			listDocument.AddMember("GUID", guidEntries, listDocument.GetAllocator());

			rapidjson::StringBuffer buffer{};
			rapidjson::Writer<rapidjson::StringBuffer, rapidjson::Document::EncodingType, rapidjson::ASCII<>>
				writer(buffer);
			listDocument.Accept(writer);

			Utils::IO::WriteFile(ReservedListFile, std::string{ buffer.GetString(), buffer.GetLength() });

			Logger::Print("Saved {} users IDs to reserved-users.json\n", copyList.size());
		}

		void ReserveClient(const Game::client_s* client)
		{
			const auto xuid = client->steamID;
			ReservedList.access([&xuid](reservedList& list)
			{
				list.insert(xuid);
			});

			Scheduler::Once(SaveReservedList, Scheduler::Pipeline::ASYNC);

			Logger::Print("{} was given VIP\n", client->name);
			Game::SV_GameSendServerCommand(client - Game::svs_clients, Game::SV_CMD_CAN_IGNORE,
			                               Utils::String::VA("%c \"You were given VIP\"", 0x65));
		}

		void ReserveClient(const std::uint64_t id)
		{
			ReservedList.access([&id](reservedList& list)
			{
				list.insert(id);
			});

			Logger::Print("{:#X} was given VIP\n", id);
			Scheduler::Once(SaveReservedList, Scheduler::Pipeline::ASYNC);
		}

		void AddReservedCommands()
		{
			Command::AddSV("reserveClient", [](const Command::Params* params)
			{
				if (!Dedicated::IsRunning())
				{
					Logger::Print("Server is not running.\n");
					return;
				}

				const auto* cmd = params->get(0);
				if (params->size() < 2)
				{
					Logger::Print("Usage: {} <client number> : reserve this client\n", cmd);
					return;
				}

				if (Utils::String::IsNumber(params->get(1)))
				{
					const auto* client = Game::SV_GetPlayerByNum();
					if (client && !client->bIsTestClient)
					{
						ReserveClient(client);
					}

					return;
				}

				const auto id = std::strtoull(params->get(1), nullptr, 16);
				ReserveClient(id);
			});

			Command::AddSV("removeReservedClient", [](const Command::Params* params)
			{
				if (!Dedicated::IsRunning())
				{
					Logger::Print("Server is not running.\n");
					return;
				}

				const auto* cmd = params->get(0);
				if (params->size() < 2)
				{
					Logger::Print("Usage: {} <GUID> : revoke VIP status from the specified client\n", cmd);
					return;
				}

				Reserved::RemoveReservedClient(params->get(1));
			});
		}
	}

	bool Reserved::IsReservedUser(const std::uint64_t id)
	{
		return ReservedList.access<bool>([&id](const reservedList& list)
		{
			return list.contains(id);
		});
	}

	void Reserved::RemoveReservedClient(const char* input)
	{
		const auto xuid = std::strtoull(input, nullptr, 16);
		ReservedList.access([&xuid](reservedList& list)
		{
			list.erase(xuid);
		});

		Logger::Print("{} is no longer a VIP\n", input);
		SaveReservedList();
	}

	std::string Reserved::GetReason()
	{
		return Reason;
	}

	Reserved::Reserved()
	{
		Reason = "EXE_DISCONNECTED"s;

		Scheduler::OnGameInitialized(LoadReservedList, Scheduler::Pipeline::MAIN);
		Events::OnSVInit(AddReservedCommands);

		GSC::Script::AddMethod("IsReserved", [](const Game::scr_entref_t entref)
		{
			const auto* ent = GSC::Script::Scr_GetPlayerEntity(entref);
			const auto* client = GSC::Script::GetClient(ent);
			const auto xuid = client->steamID;

			Game::Scr_AddBool(IsReservedUser(xuid));
		});
	}
}
