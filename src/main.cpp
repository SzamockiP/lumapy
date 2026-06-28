#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <print>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstdlib>

#include "Logger.hpp"

extern "C" void sigint_handler(int signum)
{
    std::print("\n[Engine] SIGINT captured. Shutting down immediately...\n");
    std::exit(130);
}

namespace py = pybind11;


class Engine {
public:
    Engine() = default;

    ~Engine()
    {
        stop();
    }

    py::function on_error(py::function callback)
    {
        _logger.register_callback(callback);
        return callback;
    }

    void init() 
    {

    }

    void run(py::function function)
    {

        std::signal(SIGINT, sigint_handler);

        _is_running.store(true, std::memory_order_relaxed);


        _logic_thread = std::jthread([this, function]()
            {
                py::gil_scoped_acquire acquire;

                try
                {
                    function();
                }
                catch (const std::exception& e)
                {
                    _logger.log(e.what());
                    stop();
                }

                stop();
            });

        py::gil_scoped_release release;

        while (_is_running.load(std::memory_order_relaxed))
        {
            _logger.log("[Logger]");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (_logic_thread.joinable())
        {
            _logic_thread.request_stop();
            _logic_thread.join();
        }

        std::signal(SIGINT, SIG_DFL);
    }

    bool running() const
    {
        return _is_running.load(std::memory_order_relaxed);
    }

    void stop()
    {
        _is_running.store(false, std::memory_order_relaxed);
    }

    void log(const std::string& msg)
    {
        _logger.log(msg);
    }

private:
    std::atomic<bool> _is_running{false};
    Logger _logger;
    std::jthread _logic_thread;
};

PYBIND11_MODULE(lumapy, m){
    m.doc() = "LumaPy module";

    py::class_<Engine>(m, "Engine")
        .def(py::init<>())
        .def("init", &Engine::init)
        .def("run", &Engine::run)
        .def("running", &Engine::running)
        .def("stop", &Engine::stop)
        .def("onError", &Engine::on_error)
        .def("log", &Engine::log);
}