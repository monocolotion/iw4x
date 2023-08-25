#include <STDInclude.hpp>
#include <Utils/InfoString.hpp>

#include "GSC/Script.hpp"

#include "Download.hpp"
#include "Events.hpp"
#include "MapRotation.hpp"
#include "Node.hpp"
#include "Party.hpp"
#include "ServerInfo.hpp"

#include <mongoose.h>

#define MG_OVERRIDE_LOG_FN

namespace Components
{
	static mg_mgr Mgr;

	Dvar::Var Download::SV_wwwDownload;
	Dvar::Var Download::SV_wwwBaseUrl;

	Dvar::Var Download::UIDlTimeLeft;
	Dvar::Var Download::UIDlProgress;
	Dvar::Var Download::UIDlTransRate;

	Download::ClientDownload Download::CLDownload;

	std::thread Download::ServerThread;
	volatile bool Download::Terminate;
	bool Download::ServerRunning;

	std::vector<std::shared_ptr<Download::ScriptDownload>> Download::ScriptDownloads;

	std::string Download::MongooseLogBuffer;

#pragma region Client

	void Download::InitiateMapDownload(const std::string& map, bool needPassword)
	{
		InitiateClientDownload(map, needPassword, true);
	}

	void Download::InitiateClientDownload(const std::string& mod, bool needPassword, bool map)
	{
		if (CLDownload.running_) return;

		Scheduler::Once([]
		{
			UIDlTimeLeft.set(Utils::String::FormatTimeSpan(0));
			UIDlProgress.set("(0/0) %");
			UIDlTransRate.set("0.0 MB/s");
		}, Scheduler::Pipeline::MAIN);

		Command::Execute("openmenu mod_download_popmenu", false);

		if (needPassword)
		{
			const auto password = Dvar::Var("password").get<std::string>();
			if (password.empty())
			{
				// shouldn't ever happen but this is safe
				Party::ConnectError("A password is required to connect to this server!");
				return;
			}

			CLDownload.hashedPassword_ = Utils::String::DumpHex(Utils::Cryptography::SHA256::Compute(password), "");
		}

		CLDownload.running_ = true;
		CLDownload.isMap_ = map;
		CLDownload.mod_ = mod;
		CLDownload.terminateThread_ = false;
		CLDownload.totalBytes_ = 0;
		CLDownload.lastTimeStamp_ = 0;
		CLDownload.downBytes_ = 0;
		CLDownload.timeStampBytes_ = 0;
		CLDownload.isPrivate_ = needPassword;
		CLDownload.target_ = Party::Target();
		CLDownload.thread_ = std::thread(ModDownloader, &CLDownload);
	}

	bool Download::ParseModList(ClientDownload* download, const std::string& list)
	{
		if (!download) return false;
		download->files_.clear();

		nlohmann::json listData;
		try
		{
			listData = nlohmann::json::parse(list);
		}
		catch (const nlohmann::json::parse_error& ex)
		{
			Logger::PrintError(Game::CON_CHANNEL_ERROR, "JSON Parse Error: {}\n", ex.what());
			return false;
		}

		if (!listData.is_array())
		{
			return false;
		}

		download->totalBytes_ = 0;
		const nlohmann::json::array_t listDataArray = listData;

		for (auto& file : listDataArray)
		{
			if (!file.is_object()) return false;

			try
			{
				const auto hash = file.at("hash").get<std::string>();
				const auto name = file.at("name").get<std::string>();
				const auto size = file.at("size").get<std::size_t>();

				ClientDownload::File fileEntry;
				fileEntry.name = name;
				fileEntry.hash = hash;
				fileEntry.size = size;

				if (!fileEntry.name.empty())
				{
					download->files_.push_back(fileEntry);
					download->totalBytes_ += fileEntry.size;
				}
			}
			catch (const nlohmann::json::exception& ex)
			{
				Logger::PrintError(Game::CON_CHANNEL_ERROR, "JSON Error: {}\n", ex.what());
				return false;
			}
		}

		return true;
	}

	bool Download::DownloadFile(ClientDownload* download, unsigned int index)
	{
		if (!download || download->files_.size() <= index) return false;

		auto file = download->files_[index];

		auto path = download->mod_ + "/" + file.name;
		if (download->isMap_)
		{
			path = "usermaps/" + path;
		}

		if (Utils::IO::FileExists(path))
		{
			auto data = Utils::IO::ReadFile(path);
			if (data.size() == file.size && Utils::String::DumpHex(Utils::Cryptography::SHA256::Compute(data), "") == file.hash)
			{
				download->totalBytes_ += file.size;
				return true;
			}
		}

		auto host = "http://" + download->target_.getString();
		auto fastHost = SV_wwwBaseUrl.get<std::string>();
		if (Utils::String::StartsWith(fastHost, "https://"))
		{
			download->thread_.detach();
			download->clear();

			Scheduler::Once([]
			{
				Command::Execute("closemenu mod_download_popmenu");
				Party::ConnectError("HTTPS not supported for downloading!");
			}, Scheduler::Pipeline::CLIENT);

			return false;
		}

		if (!Utils::String::StartsWith(fastHost, "http://"))
		{
			fastHost = "http://" + fastHost;
		}

		std::string url;

		// file directory for fasthost looks like this
		// /-usermaps
		//  /-mp_test
		//    -mp_test.ff
		//    -mp_test.iwd
		//   /-mp_whatever
		//	  /-mp_whatever.ff
		// /-mods
		//  /-mod1
		//	  -mod1.iwd
		//    -mod.ff
		//  /-mod2
		//     ...
		if (SV_wwwDownload.get<bool>())
		{
			if (!Utils::String::EndsWith(fastHost, "/")) fastHost.append("/");
			url = fastHost + path;
		}
		else
		{
			url = host + "/file/" + (download->isMap_ ? "map/" : "") + file.name
				+ (download->isPrivate_ ? ("?password=" + download->hashedPassword_) : "");
		}

		Logger::Print("Downloading from url {}\n", url);

		FileDownload fDownload;
		fDownload.file = file;
		fDownload.index = index;
		fDownload.download = download;
		fDownload.downloading = true;
		fDownload.receivedBytes = 0;

		Utils::String::Replace(url, " ", "%20");

		download->valid_ = true;

		fDownload.downloading = true;

		Utils::WebIO webIO;
		webIO.setProgressCallback([&fDownload, &webIO](std::size_t bytes, std::size_t)
		{
			if(!fDownload.downloading || fDownload.download->terminateThread_)
			{
				webIO.cancelDownload();
				return;
			}

			DownloadProgress(&fDownload, bytes - fDownload.receivedBytes);
		});

		auto result = false;
		fDownload.buffer = webIO.get(url, &result);
		if (!result) fDownload.buffer.clear();

		fDownload.downloading = false;

		download->valid_ = false;

		if (fDownload.buffer.size() != file.size || Utils::Cryptography::SHA256::Compute(fDownload.buffer, true) != file.hash)
		{
			return false;
		}

		if (download->isMap_) Utils::IO::CreateDir("usermaps/" + download->mod_);
		Utils::IO::WriteFile(path, fDownload.buffer);

		return true;
	}

	void Download::ModDownloader(ClientDownload* download)
	{
		if (!download) download = &CLDownload;

		const auto host = "http://" + download->target_.getString();

		const auto listUrl = host + (download->isMap_ ? "/map" : "/list") + (download->isPrivate_ ? ("?password=" + download->hashedPassword_) : "");

		const auto list = Utils::WebIO("IW4x", listUrl).setTimeout(5000)->get();
		if (list.empty())
		{
			if (download->terminateThread_) return;

			download->thread_.detach();
			download->clear();

			Scheduler::Once([]
			{
				Command::Execute("closemenu mod_download_popmenu");
				Party::ConnectError("Failed to download the modlist!");
			}, Scheduler::Pipeline::CLIENT);

			return;
		}

		if (download->terminateThread_) return;

		if (!ParseModList(download, list))
		{
			if (download->terminateThread_) return;

			download->thread_.detach();
			download->clear();

			Scheduler::Once([]
			{
				Command::Execute("closemenu mod_download_popmenu");
				Party::ConnectError("Failed to parse the modlist!");
			}, Scheduler::Pipeline::CLIENT);

			return;
		}

		if (download->terminateThread_) return;

		static std::string mod;
		mod = download->mod_;

		for (std::size_t i = 0; i < download->files_.size(); ++i)
		{
			if (download->terminateThread_) return;

			if (!DownloadFile(download, i))
			{
				if (download->terminateThread_) return;

				mod = std::format("Failed to download file: {}!", download->files_[i].name);
				download->thread_.detach();
				download->clear();

				Scheduler::Once([]
				{
					Dvar::Var("partyend_reason").set(mod);
					mod.clear();

					Command::Execute("closemenu mod_download_popmenu");
					Command::Execute("openmenu menu_xboxlive_partyended");
				}, Scheduler::Pipeline::CLIENT);

				return;
			}
		}

		if (download->terminateThread_) return;

		download->thread_.detach();
		download->clear();

		if (download->isMap_)
		{
			Scheduler::Once([]
			{
				Command::Execute("reconnect", false);
			}, Scheduler::Pipeline::CLIENT);
		}
		else
		{
			// Run this on the main thread
			Scheduler::Once([]
			{
				Game::Dvar_SetString(*Game::fs_gameDirVar, mod.data());
				const_cast<Game::dvar_t*>((*Game::fs_gameDirVar))->modified = true;

				mod.clear();

				Command::Execute("closemenu mod_download_popmenu", false);

				if (Dvar::Var("cl_modVidRestart").get<bool>())
				{
					Command::Execute("vid_restart", false);
				}

				Command::Execute("reconnect", false);
			}, Scheduler::Pipeline::MAIN);
		}
	}

	void Download::DownloadProgress(FileDownload* fDownload, std::size_t bytes)
	{
		fDownload->receivedBytes += bytes;
		fDownload->download->downBytes_ += bytes;
		fDownload->download->timeStampBytes_ += bytes;

		static volatile bool framePushed = false;

		if (!framePushed)
		{
			double progress = 0;
			if (fDownload->download->totalBytes_)
			{
				progress = (100.0 / fDownload->download->totalBytes_) * fDownload->download->downBytes_;
			}

			static std::uint32_t dlIndex, dlSize, dlProgress;
			dlIndex = fDownload->index + 1;
			dlSize = fDownload->download->files_.size();
			dlProgress = static_cast<std::uint32_t>(progress);

			framePushed = true;
			Scheduler::Once([]
			{
				framePushed = false;
				UIDlProgress.set(std::format("({}/{}) {}%", dlIndex, dlSize, dlProgress));
			}, Scheduler::Pipeline::MAIN);
		}

		auto delta = Game::Sys_Milliseconds() - fDownload->download->lastTimeStamp_;
		if (delta > 300)
		{
			const auto doFormat = fDownload->download->lastTimeStamp_ != 0;
			fDownload->download->lastTimeStamp_ = Game::Sys_Milliseconds();

			const auto dataLeft = fDownload->download->totalBytes_ - fDownload->download->downBytes_;

			int timeLeft = 0;
			if (fDownload->download->timeStampBytes_)
			{
				const double timeLeftD = ((1.0 * dataLeft) / fDownload->download->timeStampBytes_) * delta;
				timeLeft = static_cast<int>(timeLeftD);
			}

			if (doFormat)
			{
				static std::size_t dlTsBytes;
				static int dlDelta, dlTimeLeft;
				dlTimeLeft = timeLeft;
				dlDelta = delta;
				dlTsBytes = fDownload->download->timeStampBytes_;

				Scheduler::Once([]
				{
					UIDlTimeLeft.set(Utils::String::FormatTimeSpan(dlTimeLeft));
					UIDlTransRate.set(Utils::String::FormatBandwidth(dlTsBytes, dlDelta));
				}, Scheduler::Pipeline::MAIN);
			}

			fDownload->download->timeStampBytes_ = 0;
		}
	}

#pragma endregion

#pragma region Server

	void Download::LogFn(char c, [[maybe_unused]] void* param)
	{
		// Truncate & print if buffer is 1024 characters in length or otherwise only print when we reached a 'new line'
		if (!std::isprint(static_cast<unsigned char>(c)) || MongooseLogBuffer.size() == 1024)
		{
			Logger::Print(Game::CON_CHANNEL_NETWORK, "{}\n", MongooseLogBuffer);
			MongooseLogBuffer.clear();
			return;
		}

		MongooseLogBuffer.push_back(c);
	}

	static std::optional<std::string> InfoHandler([[maybe_unused]] mg_connection* c, [[maybe_unused]] const mg_http_message* hm)
	{
		const auto status = ServerInfo::GetInfo();
		const auto host = ServerInfo::GetHostInfo();

		std::unordered_map<std::string, nlohmann::json> info;
		info["status"] = status.to_json();
		info["host"] = host.to_json();
		info["map_rotation"] = MapRotation::to_json();
		info["dedicated"] = Dedicated::com_dedicated->current.integer;

		std::vector<nlohmann::json> players;

		// Build player list
		for (auto i = 0; i < Game::MAX_CLIENTS; ++i)
		{
			std::unordered_map<std::string, nlohmann::json> playerInfo;
			// Insert default values
			playerInfo["score"] = 0;
			playerInfo["ping"] = 0;
			playerInfo["name"] = "Unknown Soldier";
			playerInfo["test_client"] = 0;

			if (Dedicated::IsRunning())
			{
				if (Game::svs_clients[i].header.state < Game::CS_ACTIVE) continue;
				if (!Game::svs_clients[i].gentity || !Game::svs_clients[i].gentity->client) continue;

				playerInfo["score"] = Game::SV_GameClientNum_Score(i);
				playerInfo["ping"] = Game::svs_clients[i].ping;
				playerInfo["name"] = Game::svs_clients[i].name;
				playerInfo["test_client"] = Game::svs_clients[i].bIsTestClient;
			}
			else
			{
				// Score and ping are irrelevant
				const auto* name = Game::PartyHost_GetMemberName(Game::g_lobbyData, i);
				if (!name || !*name) continue;

				playerInfo["name"] = name;
			}

			players.emplace_back(playerInfo);
		}

		info["players"] = players;
		std::string out = nlohmann::json(info).dump();

		return { out };
	}

	static std::optional<std::string> ListHandler([[maybe_unused]] mg_connection* c, [[maybe_unused]] const mg_http_message* hm)
	{
		static nlohmann::json jsonList;
		static std::filesystem::path fsGamePre;

		const std::filesystem::path fsGame = (*Game::fs_gameDirVar)->current.string;

		if (!fsGame.empty() && (fsGamePre != fsGame))
		{
			fsGamePre = fsGame;

			std::vector<nlohmann::json> fileList;

			const auto path = (*Game::fs_basepath)->current.string / fsGame;
			auto list = FileSystem::GetSysFileList(path.generic_string(), "iwd", false);
			list.emplace_back("mod.ff");

			for (const auto& file : list)
			{
				auto filename = path / file;

				if (file.find("_svr_") != std::string::npos) // Files that are 'server only' are skipped
				{
					continue;
				}

				auto fileBuffer = Utils::IO::ReadFile(filename.generic_string());
				if (fileBuffer.empty())
				{
					continue;
				}

				std::unordered_map<std::string, nlohmann::json> jsonFileList;
				jsonFileList["name"] = file;
				jsonFileList["size"] = fileBuffer.size();
				jsonFileList["hash"] = Utils::Cryptography::SHA256::Compute(fileBuffer, true);

				fileList.emplace_back(jsonFileList);
			}

			jsonList = fileList;
		}

		std::string out = jsonList.dump();

		return { out };
	}

	static std::optional<std::string> MapHandler([[maybe_unused]] mg_connection* c, [[maybe_unused]] const mg_http_message* hm)
	{
		static std::string mapNamePre;
		static nlohmann::json jsonList;

		const std::string mapName = Party::IsInUserMapLobby() ? (*Game::ui_mapname)->current.string : Maps::GetUserMap()->getName();
		if (!Maps::GetUserMap()->isValid() && !Party::IsInUserMapLobby())
		{
			mapNamePre.clear();
			jsonList = {};
		}
		else if (!mapName.empty() && mapName != mapNamePre)
		{
			std::vector<nlohmann::json> fileList;

			mapNamePre = mapName;

			const std::filesystem::path basePath = (*Game::fs_basepath)->current.string;
			const auto path = basePath / "usermaps" / mapName;

			for (std::size_t i = 0; i < ARRAYSIZE(Maps::UserMapFiles); ++i)
			{
				const auto filename = std::format("{}\\{}{}", path.generic_string(), mapName, Maps::UserMapFiles[i]);

				std::unordered_map<std::string, nlohmann::json> file;
				auto fileBuffer = Utils::IO::ReadFile(filename);
				if (fileBuffer.empty())
				{
					continue;
				}

				file["name"] = mapName + Maps::UserMapFiles[i];
				file["size"] = fileBuffer.size();
				file["hash"] = Utils::Cryptography::SHA256::Compute(fileBuffer, true);

				fileList.emplace_back(file);
			}

			jsonList = fileList;
		}

		std::string out = jsonList.dump();

		return { out };
	}

	static std::optional<std::string> FileHandler(mg_connection* c, const mg_http_message* hm)
	{
		std::string url(hm->uri.ptr, hm->uri.len);

		Utils::String::Replace(url, "\\", "/");

		url = url.substr(6); // Strip /file
		Utils::String::Replace(url, "%20", " ");

		auto isMap = false;
		if (url.starts_with("map/"))
		{
			isMap = true;
			url = url.substr(4); // Strip map/

			std::string mapName = (Party::IsInUserMapLobby() ? (*Game::ui_mapname)->current.string : Maps::GetUserMap()->getName());
			auto isValidFile = false;
			for (std::size_t i = 0; i < ARRAYSIZE(Maps::UserMapFiles); ++i)
			{
				if (url == (mapName + Maps::UserMapFiles[i]))
				{
					isValidFile = true;
					break;
				}
			}

			if ((!Maps::GetUserMap()->isValid() && !Party::IsInUserMapLobby()) || !isValidFile)
			{
				mg_http_reply(c, 403, "Content-Type: text/html\r\n", "%s", "403 - Forbidden");
				return {};
			}

			url = std::format("usermaps\\{}\\{}", mapName, url);
		}
		else
		{
			if ((!url.ends_with(".iwd") && url != "mod.ff") || url.find("_svr_") != std::string::npos)
			{
				mg_http_reply(c, 403, "Content-Type: text/html\r\n", "%s", "403 - Forbidden");
				return {};
			}
		}

		const std::string fsGame = (*Game::fs_gameDirVar)->current.string;
		const auto path = std::format("{}\\{}{}", (*Game::fs_basepath)->current.string, isMap ? ""s : (fsGame + "\\"s), url);

		std::string file;
		if ((!isMap && fsGame.empty()) || !Utils::IO::ReadFile(path, &file))
		{
			mg_http_reply(c, 404, "Content-Type: text/html\r\n", "404 - Not Found %s", path.data());
		}
		else
		{
			mg_printf(c, "%s", "HTTP/1.1 200 OK\r\n");
			mg_printf(c, "%s", "Content-Type: application/octet-stream\r\n");
			mg_printf(c, "Content-Length: %d\r\n", static_cast<int>(file.size()));
			mg_printf(c, "%s", "Connection: close\r\n");
			mg_printf(c, "%s", "\r\n");
			mg_send(c, file.data(), file.size());
		}

		return {};
	}

	static std::optional<std::string> ServerListHandler([[maybe_unused]] mg_connection* c, [[maybe_unused]] const mg_http_message* hm)
	{
		std::vector<std::string> servers;

		const auto nodes = Node::GetNodes();
		for (const auto& node : nodes)
		{
			const auto address = node.address.getString();
			servers.emplace_back(address);
		}

		nlohmann::json jsonList = servers;
		std::string out = jsonList.dump();

		return { out };
	}

	static void EventHandler(mg_connection* c, const int ev, void* ev_data, [[maybe_unused]] void* fn_data)
	{
		using callback = std::function<std::optional<std::string>(mg_connection*, const mg_http_message*)>;

		static const auto handlers = []() -> std::unordered_map<std::string, callback>
		{
			std::unordered_map<std::string, callback> f;

			f["/file"] = FileHandler;
			f["/info"] = InfoHandler;
			f["/list"] = ListHandler;
			f["/map"] = MapHandler;
			f["/serverlist"] = ServerListHandler;

			return f;
		}();

		if (ev != MG_EV_HTTP_MSG)
		{
			return;
		}

		auto* hm = static_cast<mg_http_message*>(ev_data);
		const std::string url(hm->uri.ptr, hm->uri.len);

		auto handled = false;
		for (auto i = handlers.begin(); i != handlers.end();)
		{
			if (url.starts_with(i->first))
			{
				if (const auto reply = i->second(c, hm))
				{
					mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", reply.value().data());
				}

				handled = true;
				break;
			}

			++i;
		}

		if (!handled)
		{
			mg_http_serve_opts opts = { .root_dir = "iw4x/html" }; // Serve local dir
			mg_http_serve_dir(c, hm, &opts);
		}

		c->is_resp = FALSE; // This is important, the lack of this line of code will make the server die (in-game)
		c->is_draining = TRUE;
	}

#pragma endregion

	Download::ScriptDownload::ScriptDownload(std::string url, unsigned int object)
		: url_(std::move(url)), object_(object), webIO_(nullptr), done_(false)
		, success_(false), notifyRequired_(false), totalSize_(0), currentSize_(0)
	{
		Game::AddRefToObject(this->getObject());
	}

	Download::ScriptDownload::~ScriptDownload()
	{
		if (this->getObject())
		{
			Game::RemoveRefToObject(this->getObject());
			this->object_ = 0;
		}

		if (this->workerThread_.joinable())
		{
			this->workerThread_.join();
		}

		this->destroyWebIO();
	}

	void Download::ScriptDownload::startWorking()
	{
		if (!this->isWorking())
		{
			this->workerThread_ = std::thread(std::bind(&ScriptDownload::handler, this));
		}
	}

	bool Download::ScriptDownload::isWorking() const
	{
		return this->workerThread_.joinable();
	}

	void Download::ScriptDownload::notifyProgress()
	{
		if (this->notifyRequired_)
		{
			this->notifyRequired_ = false;

			if (Game::Scr_IsSystemActive())
			{
				Game::Scr_AddInt(static_cast<int>(this->totalSize_));
				Game::Scr_AddInt(static_cast<int>(this->currentSize_));
				Game::Scr_NotifyId(this->getObject(), static_cast<std::uint16_t>(Game::SL_GetString("progress", 0)), 2);
			}
		}
	}

	void Download::ScriptDownload::updateProgress(std::size_t currentSize, std::size_t totalSize)
	{
		this->currentSize_ = currentSize;
		this->totalSize_ = totalSize;
		this->notifyRequired_ = true;
	}

	void Download::ScriptDownload::notifyDone() const
	{
		if (!this->isDone()) return;

		if (Game::Scr_IsSystemActive())
		{
			Game::Scr_AddString(this->result_.data()); // No binary data supported yet
			Game::Scr_AddInt(this->success_);
			Game::Scr_NotifyId(this->getObject(), static_cast<std::uint16_t>(Game::SL_GetString("done", 0)), 2);
		}
	}

	bool Download::ScriptDownload::isDone() const
	{
		return this->done_;
	}

	std::string Download::ScriptDownload::getUrl() const
	{
		return this->url_;
	}

	unsigned int Download::ScriptDownload::getObject() const
	{
		return this->object_;
	}

	void Download::ScriptDownload::cancel() const
	{
		if (this->webIO_)
		{
			this->webIO_->cancelDownload();
		}
	}

	void Download::ScriptDownload::handler()
	{
		this->destroyWebIO();

		this->webIO_ = new Utils::WebIO("IW4x");
		this->webIO_->setProgressCallback(std::bind(&ScriptDownload::updateProgress, this, std::placeholders::_1, std::placeholders::_2));

		this->result_ = this->webIO_->get(this->url_, &this->success_);

		this->destroyWebIO();
		this->done_ = true;
	}

	void Download::ScriptDownload::destroyWebIO()
	{
		if (this->webIO_)
		{
			delete this->webIO_;
			this->webIO_ = nullptr;
		}
	}

	Download::Download()
	{
		AssertSize(Game::va_info_t, 0x804);
		AssertSize(jmp_buf, 0x40);
		AssertSize(Game::TraceThreadInfo, 0x8);

		if (Dedicated::IsEnabled())
		{
			if (!Flags::HasFlag("disable-mongoose"))
			{
#ifdef _DEBUG
				mg_log_set(MG_LL_INFO);
#else
				mg_log_set(MG_LL_ERROR);
#endif

#ifdef MG_OVERRIDE_LOG_FN
				mg_log_set_fn(LogFn, nullptr);
#endif

				mg_mgr_init(&Mgr);

				Events::OnNetworkInit([]() -> void
				{
					const auto* nc = mg_http_listen(&Mgr, Utils::String::VA(":%hu", Network::GetPort()), &EventHandler, &Mgr);
					if (!nc)
					{
						Logger::PrintError(Game::CON_CHANNEL_ERROR, "Failed to bind TCP socket, mod download won't work!\n");
						Terminate = true;
					}
				});

				ServerRunning = true;
				Terminate = false;
				ServerThread = Utils::Thread::CreateNamedThread("Mongoose", []() -> void
				{
					Com_InitThreadData();

					while (!Terminate)
					{
						mg_mgr_poll(&Mgr, 1000);
					}
				});
			}
		}
		else
		{
			Events::OnDvarInit([]() -> void
			{
				UIDlTimeLeft = Dvar::Register<const char*>("ui_dl_timeLeft", "", Game::DVAR_NONE, "");
				UIDlProgress = Dvar::Register<const char*>("ui_dl_progress", "", Game::DVAR_NONE, "");
				UIDlTransRate = Dvar::Register<const char*>("ui_dl_transRate", "", Game::DVAR_NONE, "");
			});

			UIScript::Add("mod_download_cancel", []([[maybe_unused]] const UIScript::Token& token, [[maybe_unused]] const Game::uiInfo_s* info)
			{
				CLDownload.clear();
			});
		}

		Events::OnDvarInit([]
		{
			SV_wwwDownload = Dvar::Register<bool>("sv_wwwDownload", false, Game::DVAR_NONE, "Set to true to enable downloading maps/mods from an external server.");
			SV_wwwBaseUrl = Dvar::Register<const char*>("sv_wwwBaseUrl", "", Game::DVAR_NONE, "Set to the base url for the external map download.");
		});

		Scheduler::Loop([]
		{
			auto workingCount = 0;

			for (auto i = ScriptDownloads.begin(); i != ScriptDownloads.end();)
			{
				const auto& download = *i;
				if (download->isDone())
				{
					download->notifyDone();
					i = ScriptDownloads.erase(i);
					continue;
				}

				if (download->isWorking())
				{
					download->notifyProgress();
					++workingCount;
				}

				++i;
			}

			for (const auto& download : ScriptDownloads)
			{
				if (workingCount > 5) break;
				if (!download->isWorking())
				{
					download->startWorking();
					++workingCount;
				}
			}

		}, Scheduler::Pipeline::SERVER);

		GSC::Script::AddFunction("HttpGet", []
		{
			const auto* url = Game::Scr_GetString(0);
			if (!url)
			{
				Game::Scr_ParamError(0, "^1HttpGet: Illegal parameter!\n");
				return;
			}

			const auto object = Game::AllocObject();

			Game::Scr_AddObject(object);

			ScriptDownloads.push_back(std::make_shared<ScriptDownload>(url, object));
			Game::RemoveRefToObject(object);
		});

		GSC::Script::AddFunction("HttpCancel", []
		{
			const auto object = Game::Scr_GetObject(0);
			for (const auto& download : ScriptDownloads)
			{
				if (object == download->getObject())
				{
					download->cancel();
					break;
				}
			}
		});
	}

	Download::~Download()
	{
		if (ServerRunning)
		{
			mg_mgr_free(&Mgr);
		}
	}

	void Download::preDestroy()
	{
		Terminate = true;
		if (ServerThread.joinable())
		{
			ServerThread.join();
		}

		if (!Dedicated::IsEnabled())
		{
			CLDownload.clear();
		}

		ScriptDownloads.clear();
	}
}
