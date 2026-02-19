#define DSTASK_IMPLEMENTATION
#include "../dstask.h"
#include "../kpm.h"

KpmTuiMessageAutoComplete KpmSetupTuiScopeMessage(const std::string& name, const std::string& message)
{
	static auto& ctx = dst::Context::Instance();
	return ctx.createStatusLine(name, message, dst::Status::Running);
}

KpmTuiMessageAutoComplete::KpmTuiMessageAutoComplete(dst::StatusLineContext&& ctx) : _ctx(ctx), _ok(true)
{
	// Nothing else to do
}

KpmTuiMessageAutoComplete::~KpmTuiMessageAutoComplete()
{
	if(_ok)
	{
		_ctx.ok().message("Done!");
	}
}

void KpmTuiMessageAutoComplete::error(const std::string& message)
{
	_ok = false;
	_ctx.error().message(message);
}

void KpmTuiMessageAutoComplete::message(const std::string& message)
{
	_ctx.message(message);
}
