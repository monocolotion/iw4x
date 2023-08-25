#pragma once
#include <Utils/WebIO.hpp> // Just end me, please

namespace Components
{
	class Download : public Component
	{
	public:
		Download();
		~Download();

		void preDestroy() override;

		static void InitiateClientDownload(const std::string& mod, bool needPassword, bool map = false);
		static void InitiateMapDownload(const std::string& map, bool needPassword);

		static Dvar::Var SV_wwwDownload;
		static Dvar::Var SV_wwwBaseUrl;

		static Dvar::Var UIDlTimeLeft;
		static Dvar::Var UIDlProgress;
		static Dvar::Var UIDlTransRate;

	private:
		class ClientDownload
		{
		public:
			ClientDownload(bool isMap = false) : running_(false), valid_(false), terminateThread_(false), isMap_(isMap), totalBytes_(0), downBytes_(0), lastTimeStamp_(0), timeStampBytes_(0) {}
			~ClientDownload() { this->clear(); }

			bool running_;
			bool valid_;
			bool terminateThread_;
			bool isMap_;
			bool isPrivate_;
			Network::Address target_;
			std::string hashedPassword_;
			std::string mod_;
			std::thread thread_;

			std::size_t totalBytes_;
			std::size_t downBytes_;

			int lastTimeStamp_;
			std::size_t timeStampBytes_;

			class File
			{
			public:
				std::string name;
				std::string hash;
				std::size_t size;
			};

			std::vector<File> files_;

			void clear()
			{
				this->terminateThread_ = true;

				if (this->thread_.joinable())
				{
					this->thread_.join();
				}

				this->running_ = false;
				this->mod_.clear();
				this->files_.clear();

				if (this->valid_)
				{
					this->valid_ = false;
				}
			}
		};

		class FileDownload
		{
		public:
			ClientDownload* download;
			ClientDownload::File file;

			int timestamp;
			bool downloading;
			unsigned int index;
			std::string buffer;
			std::size_t receivedBytes;
		};

		class ScriptDownload
		{
		public:
			ScriptDownload(std::string url, unsigned int object);
			ScriptDownload(ScriptDownload&& other) noexcept = delete;
			ScriptDownload& operator=(ScriptDownload&& other) noexcept = delete;

			~ScriptDownload();

			void startWorking();

			[[nodiscard]] bool isWorking() const;

			void notifyProgress();

			void updateProgress(std::size_t currentSize, std::size_t totalSize);

			void notifyDone() const;

			[[nodiscard]] bool isDone() const;

			[[nodiscard]] std::string getUrl() const;
			[[nodiscard]] unsigned int getObject() const;

			void cancel() const;

		private:
			std::string url_;
			std::string result_;
			unsigned int object_;
			std::thread workerThread_;
			Utils::WebIO* webIO_;

			bool done_;
			bool success_;
			bool notifyRequired_;
			std::size_t totalSize_;
			std::size_t currentSize_;

			void handler();

			void destroyWebIO();
		};

		static ClientDownload CLDownload;
		static std::thread ServerThread;
		static volatile bool Terminate;
		static bool ServerRunning;

		static std::vector<std::shared_ptr<ScriptDownload>> ScriptDownloads;

		static std::string MongooseLogBuffer;

		static void DownloadProgress(FileDownload* fDownload, std::size_t bytes);

		static void ModDownloader(ClientDownload* download);
		static bool ParseModList(ClientDownload* download, const std::string& list);
		static bool DownloadFile(ClientDownload* download, unsigned int index);

		static void LogFn(char c, void* param);
	};
}
