#include <pybind11/pybind11.h>
#include <print>

namespace py = pybind11;


class Engine {
    public:
    Engine() {
        std::print("Engine created");
    }

    void init() {
        std::print("Engine initiated");
    }
};

PYBIND11_MODULE(lumapy, m){
    m.doc() = "LumaPy module";

    py::class_<Engine>(m, "Engine")
        .def(py::init<>())
        .def("init", &Engine::init);
}