#pragma once
#include <fstream>
#include <iostream>
#include <format>
#include <sstream>

// NOTE: (César) There are other (cleaner) ways to do this
#define KPM_SET_LOG_PREFIX(prefix) std::string_view _kpm_log_prefix = #prefix
extern std::string_view _kpm_log_prefix;

struct KpmLogPolicy { static std::string_view GetGlobalFmt() { return _kpm_log_prefix; } };
struct KpmLogPolicyTrace   : KpmLogPolicy { static constexpr std::string_view Prefix() { return "[TRACE]"; } };
struct KpmLogPolicyDebug   : KpmLogPolicy { static constexpr std::string_view Prefix() { return "[DEBUG]"; } };
struct KpmLogPolicyInfo    : KpmLogPolicy { static constexpr std::string_view Prefix() { return "[INFO]"; } };
struct KpmLogPolicyWarning : KpmLogPolicy { static constexpr std::string_view Prefix() { return "[WARNING]"; } };
struct KpmLogPolicyError   : KpmLogPolicy { static constexpr std::string_view Prefix() { return "[ERROR]"; } };

template<typename T>
concept KpmLogPolicyType = std::is_base_of_v<KpmLogPolicy, T>;

template<KpmLogPolicyType Policy, typename ...Args>
inline void KpmLog(const std::string& fmt, Args&&... args)
{
#ifndef LTRACE
	// If LTRACE is off do not print/log traces
	if constexpr(std::is_same_v<Policy, KpmLogPolicyTrace>) return;
	constexpr bool KPM_LOG_FILE = true;
#else
	constexpr bool KPM_LOG_FILE = false;
#endif

	std::stringstream ss;
	ss << "[" << Policy::GetGlobalFmt() << "]" << Policy::Prefix() << " ";
	ss << std::vformat(fmt, std::make_format_args(std::forward<Args>(args)...));
	std::cout << ss.str() << std::endl;

	if constexpr (KPM_LOG_FILE)
	{
		static std::ofstream file("kpm_latest.log");
		if(file.is_open())
		{
			file << ss.str();
		}
	}
}

#define DECLARE_LOGGER(type) \
template<typename ...Args> \
void KpmLog##type(const std::string& fmt, Args&&... args) { KpmLog<KpmLogPolicy##type>(fmt, args...); } \

DECLARE_LOGGER(Trace);
DECLARE_LOGGER(Debug);
DECLARE_LOGGER(Info);
DECLARE_LOGGER(Warning);
DECLARE_LOGGER(Error);

#undef DECLARE_LOGGER

