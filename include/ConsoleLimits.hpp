#pragma once
#include <cstddef>
#include <cctype>
#include <string_view>

namespace console_limits
{

inline constexpr std::size_t commandBytes = 4096;
inline constexpr double commandRate = 5;
inline constexpr double commandBurst = 5;
inline constexpr std::size_t pendingCommands = 64 * 1024;
inline constexpr std::size_t historyBytes = 64 * 1024;
inline constexpr std::size_t outputChunk = 8192;
inline constexpr std::size_t clientQueueBytes = 256 * 1024;
inline constexpr std::size_t readBudget = 64 * 1024;
inline constexpr double	acknowledgementSeconds = 10;

inline bool	validCommand(std::string_view command)
{
	bool	nonSpace;

	if (command.empty() || command.size() > commandBytes)
		return (false);
	nonSpace = false;
	for (unsigned char ch : command)
	{
		if (!std::isprint(ch))
			return (false);
        if (ch != ' ')
            nonSpace = true;
	}
	return (nonSpace);
}
} // namespace console_limits
