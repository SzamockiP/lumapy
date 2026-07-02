#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <print>
#include <atomic>
#include <expected>

#include "Logger.hpp"
#include "window.hpp"
#include "Renderer.hpp"

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
        logger_.register_callback(callback);
        return callback;
    }

    void init(int width, int height, const std::string& title)
    {
        renderer_.reset();
        window_.reset();

        auto res = Window::create(width, height, title)
            .and_then([&](std::unique_ptr<Window> win) {
                window_ = std::move(win);
                return Renderer::create(logger_, *window_);
            })
            .and_then([&](std::unique_ptr<Renderer> ren) {
                renderer_ = std::move(ren);
                return std::expected<void, std::string>{};
            })
            .or_else([&](const std::string& err) -> std::expected<void, std::string> {
                logger_.log(err);
                throw std::runtime_error(err);
            });
    }

    void run()
    {
        is_running_.store(true, std::memory_order_relaxed);

        try
        {
            py::gil_scoped_release release;

            while (is_running_.load(std::memory_order_relaxed))
            {
                window_->poll_events();
                if (window_->should_close())
                {
                    stop();
                }

                {
                    py::gil_scoped_acquire acquire;
                    if (PyErr_CheckSignals() != 0)
                    {
                        throw py::error_already_set();
                    }
                    frame_function_();
                }
            }
        }
        catch (...)
        {
            logger_.shutdown();
            throw;
        }
        
        logger_.shutdown();
    }

    bool running() const
    {
        return is_running_.load(std::memory_order_relaxed);
    }

    void stop()
    {
        is_running_.store(false, std::memory_order_relaxed);
    }

    void log(const std::string& msg)
    {
        logger_.log(msg);
    }

    py::function on_frame(py::function fun)
    {
        frame_function_ = fun;
        return fun;
    }

    MouseState get_mouse_state() const
    {
        return window_->get_mouse_state();
    }

    bool is_key_pressed(int key) const
    {
        return window_->is_key_pressed(key);
    }

private:
    std::atomic<bool> is_running_{false};
    Logger logger_;
    std::jthread logic_thread_;

    std::unique_ptr<Window> window_;
    std::unique_ptr<Renderer> renderer_;

    py::function frame_function_;
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
        .def("onFrame", &Engine::on_frame)
        .def("log", &Engine::log)
        .def("getMouseState", &Engine::get_mouse_state)
        .def("isKeyPressed", &Engine::is_key_pressed);
}