#pragma once

#define LOGGER_TRACE

namespace Components
{
	class Logger : public Component
	{
	public:
		Logger();
		~Logger();
		
		static bool IsConsoleReady();

		static void Print_Stub(int channel, const char* message, ...);

		static void PipeOutput(void(*callback)(const std::string&));

		static void PrintInternal(Game::conChannel_t channel, const std::string_view& fmt, std::format_args&& args);
		static void ErrorInternal(Game::errorParm_t error, const std::string_view& fmt, std::format_args&& args);
		static void PrintErrorInternal(Game::conChannel_t channel, const std::string_view& fmt, std::format_args&& args);
		static void WarningInternal(Game::conChannel_t channel, const std::string_view& fmt, std::format_args&& args);
#ifdef LOGGER_TRACE
		static void DebugInternal(const std::string_view& fmt, std::format_args&& args, const std::source_location& loc);
#else
		static void DebugInternal(const std::string_view& fmt, std::format_args&& args);
#endif

		static void Print(const std::string_view& fmt)
		{
			PrintInternal(Game::CON_CHANNEL_DONT_FILTER, fmt, std::make_format_args(0));
		}

		static void Print(Game::conChannel_t channel, const std::string_view& fmt)
		{
			PrintInternal(channel, fmt, std::make_format_args(0));
		}

		template <typename... Args>
		static void Print(const std::string_view& fmt, Args&&... args)
		{
			(Utils::String::SanitizeFormatArgs(args), ...);
			PrintInternal(Game::CON_CHANNEL_DONT_FILTER, fmt, std::make_format_args(args...));
		}

		template <typename... Args>
		static void Print(Game::conChannel_t channel, const std::string_view& fmt, Args&&... args)
		{
			(Utils::String::SanitizeFormatArgs(args), ...);
			PrintInternal(channel, fmt, std::make_format_args(args...));
		}

		static void Error(Game::errorParm_t error, const std::string_view& fmt)
		{
			ErrorInternal(error, fmt, std::make_format_args(0));
		}

		template <typename... Args>
		static void Error(Game::errorParm_t error, const std::string_view& fmt, Args&&... args)
		{
			(Utils::String::SanitizeFormatArgs(args), ...);
			ErrorInternal(error, fmt, std::make_format_args(args...));
		}

		static void Warning(Game::conChannel_t channel, const std::string_view& fmt)
		{
			WarningInternal(channel, fmt, std::make_format_args(0));
		}

		template <typename... Args>
		static void Warning(Game::conChannel_t channel, const std::string_view& fmt, Args&&... args)
		{
			(Utils::String::SanitizeFormatArgs(args), ...);
			WarningInternal(channel, fmt, std::make_format_args(args...));
		}

		static void PrintError(Game::conChannel_t channel, const std::string_view& fmt)
		{
			PrintErrorInternal(channel, fmt, std::make_format_args(0));
		}

		template <typename... Args>
		static void PrintError(Game::conChannel_t channel, const std::string_view& fmt, Args&&... args)
		{
			(Utils::String::SanitizeFormatArgs(args), ...);
			PrintErrorInternal(channel, fmt, std::make_format_args(args...));
		}

		struct FormatWithLocation
		{
			std::string_view format;
#ifdef LOGGER_TRACE
			std::source_location location;
#endif

#ifdef LOGGER_TRACE
			FormatWithLocation(const std::string_view& fmt, std::source_location loc = std::source_location::current()) : format(fmt), location(std::move(loc))
#else
			FormatWithLocation(const std::string_view& fmt) : format(fmt)
#endif
			{
			}

#ifdef LOGGER_TRACE
			FormatWithLocation(const char* fmt, std::source_location loc = std::source_location::current()) : format(fmt), location(std::move(loc))
#else
			FormatWithLocation(const char* fmt) : format(fmt)
#endif
			{
			}
		};

		template <typename... Args>
		static void Debug([[maybe_unused]] const FormatWithLocation& f, [[maybe_unused]] const Args&... args)
		{
#ifdef _DEBUG
			(Utils::String::SanitizeFormatArgs(args), ...);
#ifdef LOGGER_TRACE
			DebugInternal(f.format, std::make_format_args(args...), f.location);
#else
			DebugInternal(f.format, std::make_format_args(args...));
#endif
#endif
		}

	private:
		static std::mutex MessageMutex;
		static std::vector<std::string> MessageQueue;

		static std::recursive_mutex LoggingMutex;
		static std::vector<Network::Address> LoggingAddresses[2];

		static void(*PipeCallback)(const std::string&);

		static void MessagePrint(int channel, const std::string& msg);

		static void Frame();

		static void G_LogPrintf_Hk(const char* fmt, ...);
		static void PrintMessage_Stub();
		static void PrintMessagePipe(const char* data);
		static void EnqueueMessage(const std::string& message);

		static void NetworkLog(const char* data, bool gLog);

		static void LSP_LogString_Stub(int localControllerIndex, const char* string);
		static void LSP_LogStringAboutUser_Stub(int localControllerIndex, std::uint64_t xuid, const char* string);

		static void AddServerCommands();
	};
}
