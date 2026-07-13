import sys
import os
from PyQt6.QtWidgets import QApplication, QWidget
from PyQt6.QtCore import QTimer
import bazalt as bz

class VulkanWidget(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Bazalt PyQt6 Integration Demo")
        self.resize(800, 600)
        
        # 1. Initialize Bazalt Renderer (without GLFW Window!)
        self.renderer = bz.Renderer()
        
        # 2. Connect directly to native Win32 window handle
        self.renderer.connect_win32(int(self.winId()))
        
        # Handle error callbacks
        @self.renderer.on_error
        def on_err(msg):
            print(f"[Vulkan Error]: {msg}")

        self.setup_vulkan()
        
        # 3. Timer to tick frames asynchronously (integrates with Qt's event loop)
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.tick)
        self.timer.start(16)  # ~60 FPS
        
    def setup_vulkan(self):
        # Paths to shaders
        script_dir = os.path.dirname(os.path.abspath(__file__))
        vert_path = os.path.join(script_dir, "triangle.vert")
        frag_path = os.path.join(script_dir, "triangle.frag")
        
        # Compile GLSL shaders
        vert_spv = self.renderer.compile_shader(vert_path, bz.ShaderStage.VERTEX)
        frag_spv = self.renderer.compile_shader(frag_path, bz.ShaderStage.FRAGMENT)
        
        # Create pipeline: Position + Color
        self.pipeline = (self.renderer.create_pipeline()
            .vertex_shader(vert_spv)
            .fragment_shader(frag_spv)
            .vertex_format([bz.Format.FLOAT3, bz.Format.FLOAT3])
            .build())
            
        # Triangle geometry
        vertices = [
             0.0, -0.5, 0.0,   1.0, 0.0, 0.0, # Top / Red
            -0.5,  0.5, 0.0,   0.0, 1.0, 0.0, # Bottom-Left / Green
             0.5,  0.5, 0.0,   0.0, 0.0, 1.0, # Bottom-Right / Blue
        ]
        self.vbuf = self.renderer.create_buffer(vertices, bz.BufferType.VERTEX, bz.DataType.FLOAT)
        
        self.ibuf = self.renderer.create_buffer([0, 1, 2], bz.BufferType.INDEX, bz.DataType.UINT32)
        
        # Record commands
        self.cmd = self.renderer.create_command_buffer()
        self.cmd.begin()
        self.cmd.begin_rendering(clear_color=[0.15, 0.15, 0.2, 1.0])
        self.cmd.set_viewport()
        self.cmd.set_scissor()
        self.cmd.bind_pipeline(self.pipeline)
        self.cmd.bind_vertex_buffer(self.vbuf)
        self.cmd.bind_index_buffer(self.ibuf)
        self.cmd.draw_indexed(3)
        self.cmd.end_rendering()
        
    def tick(self):
        # begin_frame acquires swapchain image and handles automatic resize recreation
        if self.renderer.begin_frame():
            self.renderer.submit(self.cmd)

if __name__ == "__main__":
    app = QApplication(sys.argv)
    widget = VulkanWidget()
    widget.show()
    sys.exit(app.exec())
