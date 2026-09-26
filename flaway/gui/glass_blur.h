#pragma once

namespace glass_blur {

// Initialize the blur pipeline (FBO, shaders, quad). Call once with a current GL context.
bool init();

// Capture the current framebuffer into the blur source texture.
// Call BEFORE ImGui renders its overlay.
void capture_framebuffer(int width, int height);

// Apply multi-pass Gaussian blur to the captured framebuffer.
// Returns the blurred texture ID (GLuint).
unsigned int blur(int passes = 4);

// Get the blurred texture ID (0 if not ready).
unsigned int get_blurred_texture();

// Draw blurred texture as fullscreen quad (for glass background).
// Call BEFORE ImGui::Render to paint the blur behind transparent UI.
void draw_blur_background();

// Release GL resources.
void shutdown();

// Resize FBOs if window size changes.
void resize(int width, int height);

}
