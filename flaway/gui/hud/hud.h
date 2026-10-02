#pragma once

struct ImDrawList;
struct ImVec2;

namespace flaway {
namespace hud {

// Element ids — order defines draw z-order (later = on top).
enum ElementId {
    E_WATERMARK = 0,
    E_KEYBINDS,
    E_TARGET,
    E_COORDS,
    E_PICKUPS,
    E_POISON,
    E_ARRAYLIST,
    E_COUNT
};

// Render the whole HUD onto the foreground draw list. Called once per frame
// while the menu is closed (GUI::render). Handles edit mode itself.
void draw();

// True while chat is open: HUD edit mode (16px grid, drag, context menu).
// Cheap (throttled JNI poll) and safe to call several times per frame.
bool edit_active();

// True when any element is enabled or edit mode is active — drives
// GUI::needs_overlay so input keeps flowing while editing an empty HUD.
bool wants_overlay();

// Release GL textures / cached state (GUI::shutdown).
void shutdown();

// ---- menu API (HUD settings page) ----
const char* element_name(int id);   // "Watermark", "Array List", ...
bool*  element_enabled(int id);     // -> globals::hud_*_enabled
float* element_scale(int id);       // -> globals::hud_*_scale (per element)
float* element_pos(int id);         // -> globals::hud_*_pos[2] (pixels)
void   reset_element(int id);       // position -> 12,12 ; scale -> 1
void   reset_all();                 // reset every element position + scale
void   save();                      // config::save_auto()

// Static mock composition drawn inside the HUD menu page (live-ish preview).
void draw_preview(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1);

} // namespace hud
} // namespace flaway
