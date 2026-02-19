#pragma once
#include <string>
#include <optional>
#include "dstask.h"

class KpmTuiMessageAutoComplete
{
	public:
		KpmTuiMessageAutoComplete(dst::StatusLineContext&& ctx);
		~KpmTuiMessageAutoComplete();

		void error(const std::string& message);
		void message(const std::string& message);

	private:
		dst::StatusLineContext _ctx;
		bool _ok;
};

bool KpmInstall(const std::string& package, const std::string& path);
bool KpmRemove(const std::string& package);
void KpmList();

std::string KpmGetCachePath();

KpmTuiMessageAutoComplete KpmSetupTuiScopeMessage(const std::string& name, const std::string& message);
