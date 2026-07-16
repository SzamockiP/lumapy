#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <semaphore>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Error.hpp"
#include "MpscQueue.hpp"

namespace py = pybind11;

// Severity is data, not a prefix baked into the message string.
//
// NOTE: the enumerators are deliberately NOT spelled ERROR/WARNING/INFO —
// <windows.h> (pulled in by volk via VK_USE_PLATFORM_WIN32_KHR) defines ERROR
// as a macro. They are exported to Python under the uppercase names in main.cpp.
enum class Severity
{
	Info = 0,
	Warning = 1,
	Error = 2,
};

// Where a message came from. Lets a callback route by subsystem instead of
// pattern-matching on the text.
enum class Source
{
	General,
	Validation,
	Window,
	Shader,
	Upload,
	Device,
};

struct LogMessage
{
	Severity severity = Severity::Info;
	Source source = Source::General;
	std::string text;
};

inline Source source_from_error_code(ErrorCode code)
{
	switch (code)
	{
	case ErrorCode::Shader:      return Source::Shader;
	case ErrorCode::Window:      return Source::Window;
	case ErrorCode::DeviceLost:  return Source::Device;
	default:                     return Source::General;
	}
}

class Logger
{
public:
	explicit Logger(Severity min_severity = Severity::Warning)
		: min_severity_(min_severity)
	{
		log_thread_ = std::jthread([this](std::stop_token stop_token)
			{
				while (true)
				{
					message_semaphore_.acquire();

					if (stop_token.stop_requested())
						break;

					drain_messages();
				}

				drain_messages();
			});
	}

	~Logger()
	{
		shutdown();
	}

	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;

	void shutdown()
	{
		if (!log_thread_.joinable())
			return;

		log_thread_.request_stop();
		message_semaphore_.release();

		py::gil_scoped_release release;
		log_thread_.join();
	}

	void register_callback(py::function callback)
	{
		std::lock_guard<std::mutex> lock(callbacks_mutex_);
		callbacks_.push_back(std::move(callback));
	}

	Severity min_severity() const { return min_severity_; }
	void set_min_severity(Severity severity) { min_severity_ = severity; }

	void log(Severity severity, Source source, std::string text)
	{
		// Filter before queueing — a suppressed message costs nothing.
		if (severity < min_severity_.load())
			return;

		messages_.push(LogMessage{ severity, source, std::move(text) });
		message_semaphore_.release();
	}

	// An Error is just a message that also happens to be returned. Logging it
	// keeps the severity/source mapping in one place.
	void log(const Error& error)
	{
		log(Severity::Error, source_from_error_code(error.code), error.message);
	}

private:
	void drain_messages()
	{
		py::gil_scoped_acquire gil;
		std::lock_guard<std::mutex> lock(callbacks_mutex_);
		while (std::optional<LogMessage> msg = messages_.pop())
		{
			for (const auto& callback : callbacks_)
			{
				callback(*msg);
			}
		}
	}

	MpscQueue<LogMessage> messages_{};
	std::counting_semaphore<> message_semaphore_{ 0 };

	std::atomic<Severity> min_severity_;

	std::mutex callbacks_mutex_;
	std::vector<py::function> callbacks_{};

	std::jthread log_thread_;
};
