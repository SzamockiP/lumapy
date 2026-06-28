#include <pybind11/pybind11.h>
#include <pybind11/functional.h>

#include <semaphore>
#include <thread>

#include "MpscQueue.hpp"

namespace py = pybind11;

class Logger
{
public:
	Logger()
	{
		_log_thread = std::jthread([this](std::stop_token stop_token)
			{
				while (!stop_token.stop_requested())
				{
					_error_semaphore.acquire();

					py::gil_scoped_acquire gil;
					std::lock_guard<std::mutex> lock(_callbacks_mutex);
					while (std::optional<std::string> msg = _error_messages.pop())
					{

						for (const auto& callback : _error_callbacks)
						{
							callback(*msg);
						}
					}
				}
			});
	}

	~Logger()
	{
		_log_thread.request_stop();
		_error_semaphore.release();
		if (_log_thread.joinable())
		{
			py::gil_scoped_release release;
			_log_thread.join();
		}
	}

	void register_callback(py::function callback)
	{
		std::lock_guard<std::mutex> lock(_callbacks_mutex);
		_error_callbacks.push_back(callback);
	}
	
	void log(const std::string& message)
	{
		_error_messages.push(message);
		_error_semaphore.release();
	}
private:
	MpscQueue<std::string> _error_messages{};
	std::counting_semaphore<> _error_semaphore{ 0 };

	std::mutex _callbacks_mutex;
	std::vector<py::function> _error_callbacks{};

	std::jthread _log_thread;
};
