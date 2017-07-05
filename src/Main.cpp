#include "STDInclude.hpp"

namespace Main
{
	void Initialize()
	{
		Utils::SetEnvironment();
		Utils::Cryptography::Initialize();
		Components::Loader::Initialize();

#if defined(DEBUG) || defined(FORCE_UNIT_TESTS)
		if (Components::Loader::IsPerformingUnitTests())
		{
			DWORD result = (Components::Loader::PerformUnitTests() ? 0 : -1);
			Components::Loader::Uninitialize();
			ExitProcess(result);
		}
#else
		if (Components::Flags::HasFlag("tests"))
		{
			Components::Logger::Print("Unit tests are disabled outside the debug environment!\n");
		}
#endif
	}

	void Uninitialize()
	{
		Components::Loader::Uninitialize();
		Utils::Cache::Uninitialize();
		google::protobuf::ShutdownProtobufLibrary();
	}

	__declspec(naked) void EntryPoint()
	{
		__asm
		{
			pushad
			call Main::Initialize
			popad

			push 6BAA2Fh // Continue init routine
			push 6CA062h // ___security_init_cookie
			retn
		}
	}
}

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD  ul_reason_for_call, LPVOID /*lpReserved*/)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		// Not sure if it conflicts with our TLS variables
		//DisableThreadLibraryCalls(hModule);

		Steam::Proxy::RunMod();

		// Ensure we're working with our desired binary
		if (Utils::Hook::Get<DWORD>(0x4C0FFF) != 0x6824748B) return FALSE;

#ifndef DISABLE_ANTICHEAT
		[]()
		{
			if (!Components::Dedicated::IsEnabled())
			{
				Components::AntiCheat::ProtectProcess();
				Components::AntiCheat::PatchThreadCreation();
			}
		}();
#endif

		DWORD oldProtect;
		std::uint8_t* module = reinterpret_cast<std::uint8_t*>(GetModuleHandle(nullptr));
		VirtualProtect(module + 0x1000, 0x2D6000, PAGE_EXECUTE_READ, &oldProtect); // Protect the .text segment

		// Install entry point hook
		Utils::Hook(0x6BAC0F, Main::EntryPoint, HOOK_JUMP).install()->quick();
	}
	else if (ul_reason_for_call == DLL_PROCESS_DETACH)
	{
		Main::Uninitialize();
	}
	else if (ul_reason_for_call == DLL_THREAD_ATTACH)
	{
#ifndef DISABLE_ANTICHEAT
		[]()
		{
			if (!Components::Dedicated::IsEnabled())
			{
				Components::AntiCheat::VerifyThreadIntegrity();
			}
		}();
#endif
	}

	return TRUE;
}
