#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <print>
#include <atomic>
#include <expected>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

#include "Error.hpp"
#include "Logger.hpp"
#include "window.hpp"
#include "Context.hpp"
#include "Renderer.hpp"
#include "Buffer.hpp"
#include "ShaderCompiler.hpp"
#include "Pipeline.hpp"
#include "CommandBuffer.hpp"
#include "Texture.hpp"
#include "DescriptorSet.hpp"

namespace py = pybind11;

// ── Error boundary ────────────────────────────────────────────────────────────
//
// Every ErrorCode gets its own Python class so that recoverability is expressible
// as `except bz.ShaderError`. Previously every failure — a shader typo and a lost
// device alike — arrived as a bare RuntimeError, so callers had no way to keep
// running after the recoverable ones.

namespace {

py::handle exc_bazalt;
py::handle exc_initialization;
py::handle exc_device_lost;
py::handle exc_out_of_memory;
py::handle exc_shader;
py::handle exc_window;
py::handle exc_resource;

py::handle make_exception(py::module_& m, const char* name, py::handle base)
{
    std::string qualified = std::string("bazalt._core.") + name;
    py::object exc = py::reinterpret_steal<py::object>(
        PyErr_NewException(qualified.c_str(), base.ptr(), nullptr));
    m.add_object(name, exc);
    return exc.release();
}

void register_exceptions(py::module_& m)
{
    exc_bazalt         = make_exception(m, "BazaltError", PyExc_Exception);
    exc_initialization = make_exception(m, "InitializationError", exc_bazalt);
    exc_device_lost    = make_exception(m, "DeviceLostError", exc_bazalt);
    exc_out_of_memory  = make_exception(m, "OutOfMemoryError", exc_bazalt);
    exc_shader         = make_exception(m, "ShaderError", exc_bazalt);
    exc_window         = make_exception(m, "WindowError", exc_bazalt);
    exc_resource       = make_exception(m, "ResourceError", exc_bazalt);
}

py::handle exception_for(ErrorCode code)
{
    switch (code)
    {
    case ErrorCode::Initialization: return exc_initialization;
    case ErrorCode::DeviceLost:     return exc_device_lost;
    case ErrorCode::OutOfMemory:    return exc_out_of_memory;
    case ErrorCode::Shader:         return exc_shader;
    case ErrorCode::Window:         return exc_window;
    case ErrorCode::Resource:       return exc_resource;
    }
    return exc_bazalt;
}

[[noreturn]] void raise_error(const Error& error)
{
    py::handle type = exception_for(error.code);
    py::object instance = py::reinterpret_steal<py::object>(
        PyObject_CallFunction(type.ptr(), "s", error.message.c_str()));

    // Diagnostics travel as attributes rather than being smashed into the text,
    // so tooling can branch on them.
    if (error.result != VK_SUCCESS)
    {
        instance.attr("vk_result") = std::string(vk_result_name(error.result));
    }
    if (error.code == ErrorCode::Shader)
    {
        instance.attr("path") = error.path;
        instance.attr("line") = error.line;
    }

    PyErr_SetObject(type.ptr(), instance.ptr());
    throw py::error_already_set();
}

// Collapses the log-then-throw block that was copy-pasted at every call site.
template <typename T>
T unwrap(std::expected<T, Error>&& result, Logger* logger)
{
    if (result)
    {
        return std::move(result.value());
    }
    if (logger)
    {
        logger->log(result.error());
    }
    raise_error(result.error());
}

// True when the buffer's bytes are packed in C order.
//
// Dimensions of extent 1 are skipped: their stride is unconstrained and numpy
// leaves arbitrary values there, so comparing them yields false negatives.
bool is_c_contiguous(const py::buffer_info& info)
{
    py::ssize_t expected = info.itemsize;
    for (py::ssize_t i = info.ndim - 1; i >= 0; --i)
    {
        if (info.shape[i] == 1)
        {
            continue;
        }
        if (info.strides[i] != expected)
        {
            return false;
        }
        expected *= info.shape[i];
    }
    return true;
}

// Refuses strided input rather than silently uploading garbage.
//
// Copying instead would be friendlier, but a hidden allocation on every upload
// is exactly the kind of invisible cost this library exists to avoid — so the
// copy stays the caller's explicit decision.
size_t contiguous_nbytes(const py::buffer_info& info, const char* what)
{
    if (!is_c_contiguous(info))
    {
        raise_error(err_resource(
            std::string(what) + " requires a C-contiguous array, got a strided view "
            "(e.g. arr.T or arr[::2]). Pass numpy.ascontiguousarray(arr) instead."));
    }
    return static_cast<size_t>(info.size) * static_cast<size_t>(info.itemsize);
}

const char* severity_name(Severity severity)
{
    switch (severity)
    {
    case Severity::Info:    return "INFO";
    case Severity::Warning: return "WARNING";
    case Severity::Error:   return "ERROR";
    }
    return "INFO";
}

ValidationMode parse_validation(const std::string& value)
{
    if (value == "auto") return ValidationMode::Auto;
    if (value == "on")   return ValidationMode::On;
    if (value == "off")  return ValidationMode::Off;
    throw std::invalid_argument(
        "validation must be one of 'auto', 'on', 'off' (got '" + value + "')");
}

// A Context built without a logger used to render with validation off and say
// nothing about its own failures. Default to reporting warnings on stderr.
std::shared_ptr<Logger> make_default_logger()
{
    auto logger = std::make_shared<Logger>(Severity::Warning);
    logger->register_callback(py::cpp_function([](const LogMessage& msg) {
        py::object stderr_stream = py::module_::import("sys").attr("stderr");
        stderr_stream.attr("write")(
            std::string("[bazalt] ") + severity_name(msg.severity) + ": " + msg.text + "\n");
    }));
    return logger;
}

}  // namespace
void SwapchainRenderer::submit(std::shared_ptr<CommandBuffer> cmd) {
    VkCommandBuffer vkCmd = cmd->get();
    vkResetCommandBuffer(vkCmd, 0);

    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr
    };
    
    if (auto e = check(vkBeginCommandBuffer(vkCmd, &beginInfo), "begin recording command buffer")) {
        raise_error(*e);
    }

    cmd->execute(vkCmd, *this);

    if (auto e = check(vkEndCommandBuffer(vkCmd), "record command buffer")) {
        raise_error(*e);
    }

    end_frame(vkCmd);
}


PYBIND11_MODULE(_core, m) {
    m.doc() = "Bazalt native core module";

    register_exceptions(m);

    // py::arithmetic() so `msg.severity >= bz.Severity.WARNING` works — filtering
    // by level is the whole point of carrying one.
    py::enum_<Severity>(m, "Severity", py::arithmetic())
        .value("INFO", Severity::Info)
        .value("WARNING", Severity::Warning)
        .value("ERROR", Severity::Error)
        .export_values();

    py::enum_<Source>(m, "Source")
        .value("GENERAL", Source::General)
        .value("VALIDATION", Source::Validation)
        .value("WINDOW", Source::Window)
        .value("SHADER", Source::Shader)
        .value("UPLOAD", Source::Upload)
        .value("DEVICE", Source::Device)
        .export_values();

    py::class_<LogMessage>(m, "LogMessage")
        .def_readonly("severity", &LogMessage::severity)
        .def_readonly("source", &LogMessage::source)
        .def_readonly("text", &LogMessage::text)
        .def("__str__", [](const LogMessage& msg) {
            return std::string(severity_name(msg.severity)) + ": " + msg.text;
        })
        .def("__repr__", [](const LogMessage& msg) {
            return std::string("<LogMessage ") + severity_name(msg.severity) + " '" + msg.text + "'>";
        });

    py::enum_<BufferType>(m, "BufferType")
        .value("VERTEX", BufferType::VERTEX)
        .value("INDEX", BufferType::INDEX)
        .value("UNIFORM", BufferType::UNIFORM)
        .value("STORAGE", BufferType::STORAGE)
        .export_values();

    py::enum_<DataType>(m, "DataType")
        .value("FLOAT", DataType::FLOAT)
        .value("UINT32", DataType::UINT32)
        .value("UINT16", DataType::UINT16)
        .value("INT32", DataType::INT32)
        .export_values();

    py::enum_<ShaderStage>(m, "ShaderStage")
        .value("VERTEX", ShaderStage::VERTEX)
        .value("FRAGMENT", ShaderStage::FRAGMENT)
        .export_values();

    py::enum_<Format>(m, "Format")
        .value("FLOAT2", Format::FLOAT2)
        .value("FLOAT3", Format::FLOAT3)
        .value("FLOAT4", Format::FLOAT4)
        .export_values();

    py::enum_<CullMode>(m, "CullMode")
        .value("NONE", CullMode::NONE)
        .value("BACK", CullMode::BACK)
        .value("FRONT", CullMode::FRONT)
        .value("FRONT_AND_BACK", CullMode::FRONT_AND_BACK)
        .export_values();

    py::enum_<FrontFace>(m, "FrontFace")
        .value("CLOCKWISE", FrontFace::CLOCKWISE)
        .value("COUNTER_CLOCKWISE", FrontFace::COUNTER_CLOCKWISE)
        .export_values();

    py::enum_<MemoryUsage>(m, "MemoryUsage")
        .value("STATIC", MemoryUsage::STATIC)
        .value("DYNAMIC", MemoryUsage::DYNAMIC)
        .export_values();

    py::class_<MouseState>(m, "MouseState")
        .def_readonly("dx", &MouseState::dx)
        .def_readonly("dy", &MouseState::dy);

    py::class_<Buffer, std::shared_ptr<Buffer>>(m, "Buffer")
        .def("update", [](Buffer& buffer, std::string_view data) {
            buffer.update(data.data(), data.size());
        })
        .def("update", [](Buffer& buffer, py::buffer b) {
            py::buffer_info info = b.request();
            buffer.update(info.ptr, contiguous_nbytes(info, "Buffer.update"));
        })
        .def("update", [](Buffer& buffer, py::list list, std::optional<DataType> dataType = std::nullopt) {
            if (list.empty()) return;
            
            DataType actualType = DataType::FLOAT;
            if (dataType.has_value()) {
                actualType = dataType.value();
            } else {
                if (py::isinstance<py::float_>(list[0])) {
                    actualType = DataType::FLOAT;
                } else if (py::isinstance<py::int_>(list[0])) {
                    actualType = DataType::INT32;
                } else {
                    raise_error(err_resource("Could not infer data type from list elements"));
                }
            }

            size_t count = list.size();
            if (actualType == DataType::FLOAT) {
                std::vector<float> data(count);
                for(size_t i=0; i<count; ++i) data[i] = list[i].cast<float>();
                buffer.update(data.data(), count * sizeof(float));
            } else if (actualType == DataType::UINT32) {
                std::vector<uint32_t> data(count);
                for(size_t i=0; i<count; ++i) data[i] = list[i].cast<uint32_t>();
                buffer.update(data.data(), count * sizeof(uint32_t));
            } else if (actualType == DataType::UINT16) {
                // create_buffer accepted UINT16 while update rejected it, so a
                // UINT16 buffer could be created and then never written to.
                std::vector<uint16_t> data(count);
                for(size_t i=0; i<count; ++i) data[i] = list[i].cast<uint16_t>();
                buffer.update(data.data(), count * sizeof(uint16_t));
            } else if (actualType == DataType::INT32) {
                std::vector<int32_t> data(count);
                for(size_t i=0; i<count; ++i) data[i] = list[i].cast<int32_t>();
                buffer.update(data.data(), count * sizeof(int32_t));
            }
        }, py::arg("list"), py::arg("data_type") = py::none());
    
    py::class_<ShaderModule, std::shared_ptr<ShaderModule>>(m, "ShaderModule");

    py::class_<Texture, std::shared_ptr<Texture>>(m, "Texture")
        .def_property_readonly("width", &Texture::width)
        .def_property_readonly("height", &Texture::height);

    py::class_<Pipeline, std::shared_ptr<Pipeline>>(m, "Pipeline");

    py::class_<PipelineBuilder, std::shared_ptr<PipelineBuilder>>(m, "PipelineBuilder")
        .def("vertex_shader", &PipelineBuilder::vertexShader)
        .def("fragment_shader", &PipelineBuilder::fragmentShader)
        .def("vertex_format", &PipelineBuilder::vertexFormat)
        .def("depth_test", &PipelineBuilder::depthTest)
        .def("cull_mode", &PipelineBuilder::cullMode)
        .def("blend", &PipelineBuilder::blend)
        .def("push_constant", &PipelineBuilder::pushConstant)
        .def("uniform_buffer", &PipelineBuilder::uniformBuffer,
             py::arg("binding"), py::arg("stage"), py::arg("set"))
        .def("storage_buffer", &PipelineBuilder::storageBuffer,
             py::arg("binding"), py::arg("stage"), py::arg("set"))
        .def("texture", &PipelineBuilder::texture,
             py::arg("binding"), py::arg("stage"), py::arg("set"))
        .def("build", [](PipelineBuilder& builder, SwapchainRenderer& renderer) -> py::object {
            auto res = builder.build(renderer.swapchain_format(), renderer.depth_format());
            return py::cast(unwrap(std::move(res), renderer.context()->logger().get()));
        }, py::arg("renderer"));

    py::class_<DescriptorSet, std::shared_ptr<DescriptorSet>>(m, "DescriptorSet")
        .def("set_texture", &DescriptorSet::setTexture,
             py::arg("binding"), py::arg("texture"))
        .def("set_buffer", &DescriptorSet::setBuffer,
             py::arg("binding"), py::arg("buffer"));

    py::class_<DescriptorPool, std::shared_ptr<DescriptorPool>>(m, "DescriptorPool")
        .def("allocate_set", [](DescriptorPool& pool, std::shared_ptr<Pipeline> pipeline, uint32_t setIndex) -> py::object {
            return py::cast(unwrap(pool.allocateDescriptorSet(pipeline, setIndex), pool.logger().get()));
        }, py::arg("pipeline"), py::arg("set"))
        .def("allocate_frame_set", [](DescriptorPool& pool, std::shared_ptr<Pipeline> pipeline, uint32_t setIndex) -> py::object {
            return py::cast(unwrap(pool.allocateFrameDescriptorSet(pipeline, setIndex), pool.logger().get()));
        }, py::arg("pipeline"), py::arg("set"));

    py::class_<CommandBuffer, std::shared_ptr<CommandBuffer>>(m, "CommandBuffer")
        .def("begin", &CommandBuffer::begin)
        .def("begin_rendering", &CommandBuffer::beginRendering, py::arg("clear_color"))
        .def("end_rendering", &CommandBuffer::endRendering)
        .def("set_viewport", &CommandBuffer::setViewport)
        .def("set_scissor", &CommandBuffer::setScissor)
        .def("bind_pipeline", &CommandBuffer::bindPipeline, py::arg("pipeline"))
        .def("bind_vertex_buffer", &CommandBuffer::bindVertexBuffer, py::arg("buffer"))
        .def("bind_index_buffer", &CommandBuffer::bindIndexBuffer, py::arg("buffer"))
        .def("draw", &CommandBuffer::draw, py::arg("vertex_count"))
        .def("draw_indexed", &CommandBuffer::drawIndexed,
             py::arg("index_count"), py::arg("first_index") = 0, py::arg("vertex_offset") = 0)
        .def("draw_indexed_instanced", &CommandBuffer::drawIndexedInstanced,
             py::arg("index_count"), py::arg("instance_count"),
             py::arg("first_index") = 0, py::arg("vertex_offset") = 0)
        .def("push_constants", [](CommandBuffer& cmd, std::shared_ptr<Pipeline> pipeline, ShaderStage stage, uint32_t offset, std::string_view data) {
            cmd.pushConstants(pipeline, stage, offset, static_cast<uint32_t>(data.size()), data.data());
        }, py::arg("pipeline"), py::arg("stage"), py::arg("offset"), py::arg("data"))
        .def("bind_descriptor_set", &CommandBuffer::bindDescriptorSet,
             py::arg("descriptor_set"), py::arg("pipeline"), py::arg("set"));

    // ── Window (GLFW) ──
    py::class_<Window>(m, "Window")
        .def(py::init([](int width, int height, const std::string& title,
                         std::shared_ptr<Logger> logger) {
            // Window used to have no way to reach a Logger at all, so GLFW's own
            // diagnostics went nowhere.
            return unwrap(Window::create(width, height, title, logger), logger.get());
        }), py::arg("width"), py::arg("height"), py::arg("title"),
            py::arg("logger") = py::none())
        .def("is_open", &Window::is_open)
        .def("should_close", &Window::should_close)
        .def("poll_events", &Window::poll_events)
        .def("is_key_pressed", &Window::is_key_pressed, py::arg("key"))
        .def("is_mouse_button_pressed", &Window::is_mouse_button_pressed, py::arg("button"))
        .def("set_cursor_mode", &Window::set_cursor_mode, py::arg("mode"))
        .def("get_mouse_state", &Window::get_mouse_state)
        .def("set_title", &Window::set_title, py::arg("title"))
        .def_property_readonly("width", &Window::get_width)
        .def_property_readonly("height", &Window::get_height);

    // ── Logger ──
    py::class_<Logger, std::shared_ptr<Logger>>(m, "Logger")
        .def(py::init<Severity>(), py::arg("min_severity") = Severity::Warning)
        // One callback receiving a structured LogMessage, not on_error/on_warning/
        // on_info. Three callbacks would be three ways to do one thing, and the old
        // on_error was a lie anyway — it received INFO and WARNING alike.
        .def("on_message", [](Logger& self, py::function callback) {
            self.register_callback(callback);
            return callback;  // returned so it works as a decorator
        }, py::arg("callback"))
        .def("log", [](Logger& self, const std::string& text, Severity severity, Source source) {
            self.log(severity, source, text);
        }, py::arg("text"), py::arg("severity") = Severity::Info,
           py::arg("source") = Source::General)
        .def_property("min_severity", &Logger::min_severity, &Logger::set_min_severity);

    // ── Context ──
    py::class_<Context, std::shared_ptr<Context>>(m, "Context")
        .def(py::init([](std::shared_ptr<Logger> logger, const std::string& validation) {
            ValidationMode mode = parse_validation(validation);
            if (!logger) {
                logger = make_default_logger();
            }
            auto res = Context::create(logger, mode);
            if (!res) {
                logger->log(res.error());
                raise_error(res.error());
            }
            return std::move(res.value());
        }), py::arg("logger") = py::none(), py::arg("validation") = "auto")
        .def_property_readonly("logger", &Context::logger)
        .def("create_buffer", [](Context& self, py::list list, BufferType type, MemoryUsage usage, std::optional<DataType> dataType) -> py::object {
            if (list.empty()) {
                raise_error(err_resource("Cannot create buffer from empty list"));
            }

            size_t count = list.size();
            DataType actualType = DataType::FLOAT;

            if (dataType.has_value()) {
                actualType = dataType.value();
            } else {
                if (py::isinstance<py::float_>(list[0])) {
                    actualType = DataType::FLOAT;
                } else if (py::isinstance<py::int_>(list[0])) {
                    actualType = (type == BufferType::INDEX) ? DataType::UINT32 : DataType::INT32;
                } else {
                    raise_error(err_resource("Could not infer data type from list elements"));
                }
            }

            std::expected<std::shared_ptr<Buffer>, Error> res = std::unexpected(err_resource("Unknown data type"));

            if (actualType == DataType::FLOAT) {
                std::vector<float> data(count);
                for(size_t i=0; i<count; ++i) data[i] = list[i].cast<float>();
                res = Buffer::create(self, data.data(), count * sizeof(float), type, usage);
            } else if (actualType == DataType::UINT32) {
                std::vector<uint32_t> data(count);
                for(size_t i=0; i<count; ++i) data[i] = list[i].cast<uint32_t>();
                res = Buffer::create(self, data.data(), count * sizeof(uint32_t), type, usage);
            } else if (actualType == DataType::UINT16) {
                std::vector<uint16_t> data(count);
                for(size_t i=0; i<count; ++i) data[i] = list[i].cast<uint16_t>();
                res = Buffer::create(self, data.data(), count * sizeof(uint16_t), type, usage);
            } else if (actualType == DataType::INT32) {
                std::vector<int32_t> data(count);
                for(size_t i=0; i<count; ++i) data[i] = list[i].cast<int32_t>();
                res = Buffer::create(self, data.data(), count * sizeof(int32_t), type, usage);
            }

            return py::cast(unwrap(std::move(res), self.logger().get()));
        }, py::arg("list"), py::arg("type"), py::arg("usage"), py::arg("data_type") = py::none())
        .def("create_buffer", [](Context& self, py::buffer b, BufferType type, MemoryUsage usage) -> py::object {
            py::buffer_info info = b.request();
            auto res = Buffer::create(self, info.ptr,
                                      contiguous_nbytes(info, "create_buffer"), type, usage);
            return py::cast(unwrap(std::move(res), self.logger().get()));
        }, py::arg("array"), py::arg("type"), py::arg("usage"))
        .def("create_buffer", [](Context& self, size_t size_in_bytes, BufferType type, MemoryUsage usage) -> py::object {
            auto res = Buffer::create(self, nullptr, size_in_bytes, type, usage);
            return py::cast(unwrap(std::move(res), self.logger().get()));
        }, py::arg("size_in_bytes"), py::arg("type"), py::arg("usage"))
        .def("pipeline_builder", [](Context& self) -> std::shared_ptr<PipelineBuilder> {
            return std::make_shared<PipelineBuilder>(self);
        })
        .def("compile_shader", [](Context& self, const std::string& path, ShaderStage stage) -> py::object {
            return py::cast(unwrap(ShaderCompiler::compile(self, path, stage), self.logger().get()));
        }, py::arg("path"), py::arg("stage"))
        .def("load_texture", [](Context& self, const std::string& path) -> py::object {
            return py::cast(unwrap(Texture::create(self, path), self.logger().get()));
        }, py::arg("path"))
        .def("create_descriptor_pool", [](Context& self, uint32_t maxSets, uint32_t samplers, uint32_t uniformBuffers, uint32_t storageBuffers) -> py::object {
            return py::cast(unwrap(
                DescriptorPool::create(self, maxSets, samplers, uniformBuffers, storageBuffers),
                self.logger().get()));
        }, py::arg("max_sets"), py::arg("samplers") = 0, py::arg("uniform_buffers") = 0, py::arg("storage_buffers") = 0);

    // ── SwapchainRenderer ──
    py::class_<SwapchainRenderer, std::shared_ptr<SwapchainRenderer>>(m, "SwapchainRenderer")
        .def(py::init([](Window& window, std::shared_ptr<Context> context) {
            auto sp = window.get_surface_provider();
            return std::shared_ptr<SwapchainRenderer>(
                unwrap(SwapchainRenderer::create(context, std::move(sp)), context->logger().get()));
        }), py::arg("window"), py::arg("context"))
        .def(py::init([](uint64_t hwnd, std::shared_ptr<Context> context) -> std::shared_ptr<SwapchainRenderer> {
#ifdef _WIN32
            SurfaceProvider sp;
            sp.required_instance_extensions = { "VK_KHR_surface", "VK_KHR_win32_surface" };
            
            sp.create_surface = [hwnd](VkInstance instance) -> VkSurfaceKHR {
                auto pfnCreateWin32Surface = (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");
                if (!pfnCreateWin32Surface) {
                    return VK_NULL_HANDLE;
                }
                VkWin32SurfaceCreateInfoKHR createInfo{
                    .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                    .pNext = nullptr,
                    .flags = 0,
                    .hinstance = GetModuleHandle(nullptr),
                    .hwnd = (HWND)hwnd
                };
                VkSurfaceKHR surface = VK_NULL_HANDLE;
                if (pfnCreateWin32Surface(instance, &createInfo, nullptr, &surface) != VK_SUCCESS) {
                    return VK_NULL_HANDLE;
                }
                return surface;
            };
            
            sp.get_framebuffer_size = [hwnd]() -> std::pair<int, int> {
                RECT rect;
                if (GetClientRect((HWND)hwnd, &rect)) {
                    return { rect.right - rect.left, rect.bottom - rect.top };
                }
                return { 0, 0 };
            };
            
            auto last_width = std::make_shared<int>(0);
            auto last_height = std::make_shared<int>(0);
            
            sp.consume_resize_flag = [hwnd, last_width, last_height]() -> bool {
                RECT rect;
                if (GetClientRect((HWND)hwnd, &rect)) {
                    int w = rect.right - rect.left;
                    int h = rect.bottom - rect.top;
                    if (w != *last_width || h != *last_height) {
                        *last_width = w;
                        *last_height = h;
                        return true;
                    }
                }
                return false;
            };
            
            return std::shared_ptr<SwapchainRenderer>(
                unwrap(SwapchainRenderer::create(context, std::move(sp)), context->logger().get()));
#else
            raise_error(err_window("win32_hwnd constructor is only supported on Windows"));
#endif
        }), py::arg("win32_hwnd"), py::arg("context"))
        .def("begin_frame", &SwapchainRenderer::begin_frame)
        .def("submit", &SwapchainRenderer::submit, py::arg("cmd"))
        .def("create_command_buffer", [](SwapchainRenderer& self) -> py::object {
            return py::cast(unwrap(CommandBuffer::create(self), self.context()->logger().get()));
        });

    // ── Key Constants ──
    m.attr("KEY_SPACE") = GLFW_KEY_SPACE;
    m.attr("KEY_APOSTROPHE") = GLFW_KEY_APOSTROPHE;
    m.attr("KEY_COMMA") = GLFW_KEY_COMMA;
    m.attr("KEY_MINUS") = GLFW_KEY_MINUS;
    m.attr("KEY_PERIOD") = GLFW_KEY_PERIOD;
    m.attr("KEY_SLASH") = GLFW_KEY_SLASH;
    m.attr("KEY_0") = GLFW_KEY_0;
    m.attr("KEY_1") = GLFW_KEY_1;
    m.attr("KEY_2") = GLFW_KEY_2;
    m.attr("KEY_3") = GLFW_KEY_3;
    m.attr("KEY_4") = GLFW_KEY_4;
    m.attr("KEY_5") = GLFW_KEY_5;
    m.attr("KEY_6") = GLFW_KEY_6;
    m.attr("KEY_7") = GLFW_KEY_7;
    m.attr("KEY_8") = GLFW_KEY_8;
    m.attr("KEY_9") = GLFW_KEY_9;
    m.attr("KEY_SEMICOLON") = GLFW_KEY_SEMICOLON;
    m.attr("KEY_EQUAL") = GLFW_KEY_EQUAL;
    m.attr("KEY_A") = GLFW_KEY_A;
    m.attr("KEY_B") = GLFW_KEY_B;
    m.attr("KEY_C") = GLFW_KEY_C;
    m.attr("KEY_D") = GLFW_KEY_D;
    m.attr("KEY_E") = GLFW_KEY_E;
    m.attr("KEY_F") = GLFW_KEY_F;
    m.attr("KEY_G") = GLFW_KEY_G;
    m.attr("KEY_H") = GLFW_KEY_H;
    m.attr("KEY_I") = GLFW_KEY_I;
    m.attr("KEY_J") = GLFW_KEY_J;
    m.attr("KEY_K") = GLFW_KEY_K;
    m.attr("KEY_L") = GLFW_KEY_L;
    m.attr("KEY_M") = GLFW_KEY_M;
    m.attr("KEY_N") = GLFW_KEY_N;
    m.attr("KEY_O") = GLFW_KEY_O;
    m.attr("KEY_P") = GLFW_KEY_P;
    m.attr("KEY_Q") = GLFW_KEY_Q;
    m.attr("KEY_R") = GLFW_KEY_R;
    m.attr("KEY_S") = GLFW_KEY_S;
    m.attr("KEY_T") = GLFW_KEY_T;
    m.attr("KEY_U") = GLFW_KEY_U;
    m.attr("KEY_V") = GLFW_KEY_V;
    m.attr("KEY_W") = GLFW_KEY_W;
    m.attr("KEY_X") = GLFW_KEY_X;
    m.attr("KEY_Y") = GLFW_KEY_Y;
    m.attr("KEY_Z") = GLFW_KEY_Z;
    m.attr("KEY_LEFT_BRACKET") = GLFW_KEY_LEFT_BRACKET;
    m.attr("KEY_BACKSLASH") = GLFW_KEY_BACKSLASH;
    m.attr("KEY_RIGHT_BRACKET") = GLFW_KEY_RIGHT_BRACKET;
    m.attr("KEY_GRAVE_ACCENT") = GLFW_KEY_GRAVE_ACCENT;
    m.attr("KEY_WORLD_1") = GLFW_KEY_WORLD_1;
    m.attr("KEY_WORLD_2") = GLFW_KEY_WORLD_2;
    m.attr("KEY_ESCAPE") = GLFW_KEY_ESCAPE;
    m.attr("KEY_ENTER") = GLFW_KEY_ENTER;
    m.attr("KEY_TAB") = GLFW_KEY_TAB;
    m.attr("KEY_BACKSPACE") = GLFW_KEY_BACKSPACE;
    m.attr("KEY_INSERT") = GLFW_KEY_INSERT;
    m.attr("KEY_DELETE") = GLFW_KEY_DELETE;
    m.attr("KEY_RIGHT") = GLFW_KEY_RIGHT;
    m.attr("KEY_LEFT") = GLFW_KEY_LEFT;
    m.attr("KEY_DOWN") = GLFW_KEY_DOWN;
    m.attr("KEY_UP") = GLFW_KEY_UP;
    m.attr("KEY_PAGE_UP") = GLFW_KEY_PAGE_UP;
    m.attr("KEY_PAGE_DOWN") = GLFW_KEY_PAGE_DOWN;
    m.attr("KEY_HOME") = GLFW_KEY_HOME;
    m.attr("KEY_END") = GLFW_KEY_END;
    m.attr("KEY_CAPS_LOCK") = GLFW_KEY_CAPS_LOCK;
    m.attr("KEY_SCROLL_LOCK") = GLFW_KEY_SCROLL_LOCK;
    m.attr("KEY_NUM_LOCK") = GLFW_KEY_NUM_LOCK;
    m.attr("KEY_PRINT_SCREEN") = GLFW_KEY_PRINT_SCREEN;
    m.attr("KEY_PAUSE") = GLFW_KEY_PAUSE;
    m.attr("KEY_F1") = GLFW_KEY_F1;
    m.attr("KEY_F2") = GLFW_KEY_F2;
    m.attr("KEY_F3") = GLFW_KEY_F3;
    m.attr("KEY_F4") = GLFW_KEY_F4;
    m.attr("KEY_F5") = GLFW_KEY_F5;
    m.attr("KEY_F6") = GLFW_KEY_F6;
    m.attr("KEY_F7") = GLFW_KEY_F7;
    m.attr("KEY_F8") = GLFW_KEY_F8;
    m.attr("KEY_F9") = GLFW_KEY_F9;
    m.attr("KEY_F10") = GLFW_KEY_F10;
    m.attr("KEY_F11") = GLFW_KEY_F11;
    m.attr("KEY_F12") = GLFW_KEY_F12;
    m.attr("KEY_F13") = GLFW_KEY_F13;
    m.attr("KEY_F14") = GLFW_KEY_F14;
    m.attr("KEY_F15") = GLFW_KEY_F15;
    m.attr("KEY_F16") = GLFW_KEY_F16;
    m.attr("KEY_F17") = GLFW_KEY_F17;
    m.attr("KEY_F18") = GLFW_KEY_F18;
    m.attr("KEY_F19") = GLFW_KEY_F19;
    m.attr("KEY_F20") = GLFW_KEY_F20;
    m.attr("KEY_F21") = GLFW_KEY_F21;
    m.attr("KEY_F22") = GLFW_KEY_F22;
    m.attr("KEY_F23") = GLFW_KEY_F23;
    m.attr("KEY_F24") = GLFW_KEY_F24;
    m.attr("KEY_F25") = GLFW_KEY_F25;
    m.attr("KEY_KP_0") = GLFW_KEY_KP_0;
    m.attr("KEY_KP_1") = GLFW_KEY_KP_1;
    m.attr("KEY_KP_2") = GLFW_KEY_KP_2;
    m.attr("KEY_KP_3") = GLFW_KEY_KP_3;
    m.attr("KEY_KP_4") = GLFW_KEY_KP_4;
    m.attr("KEY_KP_5") = GLFW_KEY_KP_5;
    m.attr("KEY_KP_6") = GLFW_KEY_KP_6;
    m.attr("KEY_KP_7") = GLFW_KEY_KP_7;
    m.attr("KEY_KP_8") = GLFW_KEY_KP_8;
    m.attr("KEY_KP_9") = GLFW_KEY_KP_9;
    m.attr("KEY_KP_DECIMAL") = GLFW_KEY_KP_DECIMAL;
    m.attr("KEY_KP_DIVIDE") = GLFW_KEY_KP_DIVIDE;
    m.attr("KEY_KP_MULTIPLY") = GLFW_KEY_KP_MULTIPLY;
    m.attr("KEY_KP_SUBTRACT") = GLFW_KEY_KP_SUBTRACT;
    m.attr("KEY_KP_ADD") = GLFW_KEY_KP_ADD;
    m.attr("KEY_KP_ENTER") = GLFW_KEY_KP_ENTER;
    m.attr("KEY_KP_EQUAL") = GLFW_KEY_KP_EQUAL;
    m.attr("KEY_LEFT_SHIFT") = GLFW_KEY_LEFT_SHIFT;
    m.attr("KEY_LEFT_CONTROL") = GLFW_KEY_LEFT_CONTROL;
    m.attr("KEY_LEFT_ALT") = GLFW_KEY_LEFT_ALT;
    m.attr("KEY_LEFT_SUPER") = GLFW_KEY_LEFT_SUPER;
    m.attr("KEY_RIGHT_SHIFT") = GLFW_KEY_RIGHT_SHIFT;
    m.attr("KEY_RIGHT_CONTROL") = GLFW_KEY_RIGHT_CONTROL;
    m.attr("KEY_RIGHT_ALT") = GLFW_KEY_RIGHT_ALT;
    m.attr("KEY_RIGHT_SUPER") = GLFW_KEY_RIGHT_SUPER;
    m.attr("KEY_MENU") = GLFW_KEY_MENU;
    m.attr("KEY_LAST") = GLFW_KEY_LAST;
 
    m.attr("MOUSE_BUTTON_LEFT") = GLFW_MOUSE_BUTTON_LEFT;
    m.attr("MOUSE_BUTTON_RIGHT") = GLFW_MOUSE_BUTTON_RIGHT;
    m.attr("MOUSE_BUTTON_MIDDLE") = GLFW_MOUSE_BUTTON_MIDDLE;

    m.attr("CURSOR_NORMAL") = GLFW_CURSOR_NORMAL;
    m.attr("CURSOR_DISABLED") = GLFW_CURSOR_DISABLED;
    m.attr("CURSOR_HIDDEN") = GLFW_CURSOR_HIDDEN;
}