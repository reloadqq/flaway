#pragma once

// Custom Minecraft menu background + falling stars overlay + loading gif.
// Drawn from JNI render hooks on concrete classes (TitleScreen.renderBackground,
// ProgressScreen.render / SplashOverlay.render) because Screen itself cannot be
// copied by jnihook (its blanket rewrite breaks foreign descriptors and the
// copy fails verification with a VerifyError).

// All draw functions use the current GL viewport for their size (MC's
// Screen.width/height is a *scaled* gui size, useless for glViewport) and are
// expected to be called on the GL thread inside the screen's render.

namespace flaway {
namespace modules {
namespace better_minecraft {
namespace menu_bg {

// Called once from init_gui_hooks. Loads shaders, creates VAO/VBO.
bool init();

// Returns true if the given screen is a TitleScreen (class_442).
bool is_custom_bg_screen(void* env, void* screen);

// Returns true if the given screen is a ProgressScreen (class_435,
// shown during resource-pack reload / server join).
bool is_loading_screen(void* env, void* screen);

// Draw menu2.png as fullscreen aspect-fill background.
void draw_background();

// Draw stars.png as scrolling additive overlay (falling stars with bloom).
void draw_stars();

// Draw texture.gif (animated, aspect-fit) over a dark fullscreen backdrop.
void draw_loading();

// Draw the vanilla title logo + edition banner as black silhouettes at the
// vanilla positions (gui_w/gui_h are the Screen's scaled size).
void draw_logo_black(int gui_w, int gui_h);

// Advance star scroll animation.
void tick(float dt_seconds);

}}}} // namespace flaway::modules::better_minecraft::menu_bg
