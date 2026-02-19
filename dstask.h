#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>
#include <string>
#include <iostream>
#include <thread>
#include <vector>
#include <shared_mutex>

// Declaration
namespace
{
	// Spinner
	static constexpr std::string_view SPINNER_FRAMES[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
	static constexpr std::int32_t SPINNER_N = sizeof(SPINNER_FRAMES) / sizeof(SPINNER_FRAMES[0]);

	// Colors
	static constexpr const char* CRESET   = "\x1b[0m";
	static constexpr const char* CBOLD    = "\x1b[1m";
	static constexpr const char* CDIM     = "\x1b[2m";
	static constexpr const char* CSUCCESS = "\x1b[38;5;46m";
	static constexpr const char* CRUNNING = "\x1b[38;5;214m";
	static constexpr const char* CERROR   = "\x1b[38;5;196m";
	static constexpr const char* CINFO    = "\x1b[38;5;39m";
	static constexpr const char* CMUTED   = "\x1b[38;5;244m";
	static constexpr const char* CACCENT  = "\x1b[38;5;141m";

	// Other
	static constexpr const char* CHIDE  = "\x1b[?25l";
	static constexpr const char* CSHOW  = "\x1b[?25h";
	static constexpr const char* CCLEAR = "\x1b[2J";
	static constexpr const char* CHOME  = "\x1b[H";
}

namespace dst
{
	enum class Status
	{
		Stalled,
		Running,
		Ok,
		Error
	};

	struct LineState
	{
		std::string name;
		std::string message;
		Status status = Status::Stalled;
		std::int32_t spinner_frame = 0;
		std::uint32_t id;
	};

	class StatusLineContext
	{
		public:
			explicit StatusLineContext(std::uint32_t id) : _id(id) {}

		public:
			StatusLineContext& message(const std::string& message);
			StatusLineContext& append(const std::string& message);
			StatusLineContext& stalled();
			StatusLineContext& running();
			StatusLineContext& ok();
			StatusLineContext& error();
			void erase();
		private:
			std::uint32_t _id;
	};

	class Context
	{
	public:
		~Context();
		static inline Context& Instance()
		{
			static Context context;
			return context;
		}

		StatusLineContext createStatusLine(const std::string& name, const std::string& message, const Status& status);

		inline std::shared_mutex& getMutex()
		{
			return _mtx;
		}

		inline std::optional<std::reference_wrapper<LineState>> getLineStateById(std::uint32_t id)
		{
			auto ls_candidate = std::find_if(_lines.begin(), _lines.end(), [id](const LineState& line) { return id == line.id; });
			if(ls_candidate == _lines.end()) return std::nullopt;
			return *ls_candidate;
		}

		inline void deleteLineById(std::uint32_t id)
		{
			std::unique_lock lock(_mtx);
			auto ls_candidate = std::find_if(_lines.begin(), _lines.end(), [id](const LineState& line) { return id == line.id; });
			if(ls_candidate != _lines.end()) _lines.erase(ls_candidate);
		}

	private:
		Context();
		Context(const Context& other) = delete;
		Context(Context&& other) = delete;

	private:
		std::vector<LineState> _lines;
		std::shared_mutex _mtx;
		std::atomic<bool> _stop;
		std::unique_ptr<std::thread> _renderer;
	};

	constexpr std::string_view StatusTextFromTag(const Status& status);
	void StatusSetColorFromTag(const Status& status);
	Context CreateDstContext();
}
// End Declaration

// Implementation
#ifdef DSTASK_IMPLEMENTATION
namespace dst
{
	Context::Context()
	{
		_renderer = std::make_unique<std::thread>([this](){
			std::cout << CHIDE << CCLEAR;

			while(!_stop.load())
			{
				// Take snapshot and update spinners
				std::vector<LineState> snapshot;
				{
					std::unique_lock lock(_mtx);
					snapshot = _lines;
					for(auto& ls : _lines)
					{
						if(ls.status == Status::Running) ls.spinner_frame = (ls.spinner_frame + 1) % SPINNER_N;
					}
				}

				// Render snapshot
				std::cout << CHOME;
				std::cout << CBOLD;
				std::cout << "KPM ";
				std::cout << CRESET << CDIM;
				std::cout << "(v0.3)\n";
				std::cout << CRESET;

				for(auto& ls : snapshot)
				{
					std::string_view spin = (ls.status == Status::Running) ? SPINNER_FRAMES[ls.spinner_frame] : (ls.status == Status::Ok ? "✔" : (ls.status == Status::Stalled) ? "⏳" : "✖");
					StatusSetColorFromTag(ls.status);
					std::cout << spin << " " << CRESET << CBOLD << ls.name << CRESET << " " << CDIM << "[" << StatusTextFromTag(ls.status) << "]" << CRESET;
					std::cout << " " << ls.message << "\x1b[K\n";
				}

				std::cout << "\x1b[J";
				std::cout.flush();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}

			std::cout << CSHOW << CRESET;
		});
	}

	Context::~Context()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		_stop.store(true);
		_renderer->join();
	}

	constexpr std::string_view StatusTextFromTag(const Status& status)
	{
		switch(status)
		{
			case Status::Stalled: return "STALLED";
			case Status::Running: return "RUNNING";
			case Status::Ok: return "OK";
			case Status::Error: return "ERROR";
		}
		return "";
	}

	void StatusSetColorFromTag(const Status& status)
	{
		switch(status)
		{
			case Status::Stalled: std::cout << CINFO; break;
			case Status::Running: std::cout << CRUNNING; break;
			case Status::Ok: std::cout << CSUCCESS; break;
			case Status::Error: std::cout << CERROR; break;
		}
	}

	StatusLineContext Context::createStatusLine(const std::string& name, const std::string& message, const Status& status)
	{
		std::unique_lock lock(_mtx);
		static std::uint32_t idgen = 0;
		_lines.push_back({
			.name = name,
			.message = message,
			.status = status,
			.spinner_frame = 0,
			.id = idgen
		});

		return StatusLineContext(idgen++);
	}

	StatusLineContext& StatusLineContext::message(const std::string& message)
	{
		static auto& ctx = Context::Instance();
		std::shared_lock lock(ctx.getMutex());
		auto ls = ctx.getLineStateById(_id);

		if(ls.has_value())
		{
			ls->get().message = message;
		}
		return *this;
	}

	StatusLineContext& StatusLineContext::append(const std::string& message)
	{
		static auto& ctx = Context::Instance();
		std::shared_lock lock(ctx.getMutex());
		auto ls = ctx.getLineStateById(_id);

		if(ls.has_value())
		{
			ls->get().message += message;
		}
		return *this;
	}

	static inline void SetStatusInternal(Status&& status, const std::uint32_t id)
	{
		static auto& ctx = Context::Instance();
		std::shared_lock lock(ctx.getMutex());
		auto ls = ctx.getLineStateById(id);

		if(ls.has_value())
		{
			ls->get().status = status;
		}
	}

	StatusLineContext& StatusLineContext::stalled()
	{
		SetStatusInternal(Status::Stalled, _id);
		return *this;
	}

	StatusLineContext& StatusLineContext::running()
	{
		SetStatusInternal(Status::Running, _id);
		return *this;
	}

	StatusLineContext& StatusLineContext::ok()
	{
		SetStatusInternal(Status::Ok, _id);
		return *this;
	}

	StatusLineContext& StatusLineContext::error()
	{
		SetStatusInternal(Status::Error, _id);
		return *this;
	}

	void StatusLineContext::erase()
	{
		static auto& ctx = Context::Instance();
		ctx.deleteLineById(_id);
	}
}
#endif
// End Implementation
