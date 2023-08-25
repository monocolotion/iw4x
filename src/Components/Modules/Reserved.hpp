#pragma once

namespace Components
{
	class Reserved : public Component
	{
	public:
		Reserved();

		static bool IsReservedUser(std::uint64_t id);
		static void RemoveReservedClient(const char* input);

		static std::string GetReason();
	};
}
