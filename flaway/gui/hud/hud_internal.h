#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "hud.h"
#include "../GUI.h"
#include "../../globals/globals.h"
#include "../../modules/esp/esp.h"
#include "../../../utils/imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Style primitives — glassmorphism cards, animated accents, helpers shared by
// every HUD element. Colors come from the menu theme so the HUD follows it.
// ---------------------------------------------------------------------------
namespace hstyle {

inline ImU32 with_a(ImU32 c, int a) {
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    return (c & 0x00FFFFFFu) | ((ImU32)a << 24);
}
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float ease_out(float t) { t = clamp01(t); return 1.0f - powf(1.0f - t, 3.0f); }
inline float damp(float cur, float target, float speed, float dt) {
    float k = 1.0f - powf(1.0f - speed, dt * 60.0f);
    if (k > 1.0f) k = 1.0f;
    return cur + (target - cur) * k;
}
inline ImU32 lerp(ImU32 a, ImU32 b, float t) {
    t = clamp01(t);
    int ar = (int)(a & 0xFF), ag = (int)((a >> 8) & 0xFF), ab = (int)((a >> 16) & 0xFF), aa = (int)((a >> 24) & 0xFF);
    int br = (int)(b & 0xFF), bg = (int)((b >> 8) & 0xFF), bb = (int)((b >> 16) & 0xFF), ba = (int)((b >> 24) & 0xFF);
    return IM_COL32(ar + (int)((br - ar) * t), ag + (int)((bg - ag) * t),
                    ab + (int)((bb - ab) * t), aa + (int)((ba - aa) * t));
}
ImU32 hsl(float h, float s, float l, int a = 255);

// Animated gradient pair derived from the theme accent (hue drifts slowly).
ImU32 accent();
ImU32 grad_a(float phase = 0.0f);
ImU32 grad_b(float phase = 0.0f);
// Color along the animated gradient: t in [0,1], phase shifts the hue.
ImU32 grad(float t, float phase = 0.0f);

ImU32 text_main();
ImU32 text_dim();
ImU32 text_faint();

ImFont* font();       // Monocraft regular (HUD)
ImFont* font_bold();  // Monocraft bold (headers / values)

// Blurred game-frame texture used by glass cards (0 = render opaque glass).
void set_blur_tex(unsigned tex);

inline float time_now() { return ImGui::GetTime() > 0.0 ? (float)ImGui::GetTime() : 0.0f; }

struct Card {
    float rounding = 10.0f;
    float alpha = 1.0f;        // master opacity (appear/fade/ghost)
    bool  blur = true;         // blurred backdrop (glass_blur texture)
    bool  accent_border = false;
    float glow = 0.0f;         // pulse glow amount 0..1 (adds accent ring)
    float hover = 0.0f;        // edit-mode hover highlight 0..1
};

// Glass body (+ optional blur backdrop, border, hover glow).
void card(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, const Card& c);

// Text with a left→right gradient (also animates with time when anim=true).
void gradient_text(ImDrawList* dl, const ImVec2& pos, const char* text,
                   ImFont* font, float size, ImU32 c0, ImU32 c1, int alpha = 255);

// Rounded pill background (chips) — gradient glass, no blur.
void pill(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float alpha = 1.0f);

// Digit-roller: draws `text` at `p`, but whenever the value under `key`
// changes the old glyphs roll up and fade while the new ones roll in from
// below (staggered left→right). State is keyed by `key`, so every call site
// keeps its own counter. `clip` bounds the rolling glyphs to the chip.
void rolling_text(ImDrawList* dl, ImFont* font, float size, const ImVec2& p,
                  ImU32 col, const char* text, const char* key,
                  const ImVec2& clip0, const ImVec2& clip1);

} // namespace hstyle

// ---------------------------------------------------------------------------
// GPU caustic sheen (hud_fx.cpp): animated shader overlay clipped to a card.
// Queue one call per card while building the draw list; it renders through an
// ImDrawList user callback inside the ImGui GL3 backend.
// ---------------------------------------------------------------------------
namespace hud_fx {

// Reset the per-frame request queue. Frame-guarded: only the first call of a
// given ImGui frame clears the queue, so ESP/HUD/menu can all call it safely.
void begin_frame();

// Effect ids for fx()/sheen().
enum { FX_CAUSTIC = 0, FX_SWEEP = 1, FX_EDGE = 2 };

// Queue an animated shader overlay inside [p0,p1] with rounded corners.
// alpha 0..1 (master opacity), phase decorrelates cards from each other,
// mode selects the effect (FX_CAUSTIC / FX_SWEEP / FX_EDGE).
void fx(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float rounding,
        float alpha, float phase, int mode);

// FX_CAUSTIC shorthand (what HUD cards used before the extra modes existed).
void sheen(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float rounding,
           float alpha, float phase);

// Release GL objects (only when a context is still current).
void shutdown();

} // namespace hud_fx

// ---------------------------------------------------------------------------
// Per-frame data providers (throttled JNI polls + cached GL icons).
// ---------------------------------------------------------------------------
namespace hud_data {

struct TargetInfo {
    bool valid = false;
    std::string name;
    float hp = 20.0f, max_hp = 20.0f;
    double dist = 0.0;
    std::vector<esp_item_slot> items;
    std::string skin_hash;   // "" = not resolved yet
    unsigned int skin_tex = 0;
};

// Called once per frame from hud::draw(): refreshes all throttled caches.
void poll();
void shutdown();

bool chat_open();            // edit mode (ChatScreen instance check)
bool hud_f1_shown();         // MinecraftClient.isHudEnabled()
bool ping(int& ms);          // false while unknown
bool server(std::string& out); // false in singleplayer
void coords(double& x, double& y, double& z, double& bps);
bool poisoned();
const TargetInfo& target();
const std::vector<esp_pickup_entry>& pickups();
long long now_us();

} // namespace hud_data

// ---------------------------------------------------------------------------
// GL textures for HUD icons: vanilla item PNGs (from the version jar) and
// cached player skins (from ~/.minecraft/assets/skins).
// ---------------------------------------------------------------------------
namespace hud_icons {

unsigned item(const std::string& suffix);      // item/<suffix>.png or block/<suffix>.png
unsigned skin(const std::string& hash);        // full 64x64 skin texture
unsigned png(const unsigned char* data, size_t len);  // embedded PNG byte array
void shutdown();

} // namespace hud_icons

// ---------------------------------------------------------------------------
// Element implementations (hud_elements.cpp): measure content, then draw it.
// ---------------------------------------------------------------------------
namespace helem {

struct Ctx {
    ImDrawList* dl = nullptr;
    float s = 1.0f;           // element scale (global * per-element * hover zoom)
    bool edit = false;        // edit mode active
    bool ghost = false;       // disabled element shown for editing
    float alpha = 1.0f;       // master opacity for this element
    ImVec2 r0, r1;            // final screen rect
    long long now_us = 0;     // frame-stable clock (steady clock)
};

using MeasureFn = ImVec2 (*)(Ctx&);
using DrawFn = void (*)(Ctx&, const ImVec2& size);

struct Ops {
    MeasureFn measure;
    DrawFn draw;
    bool blur;                // uses the blurred backdrop
};

const Ops* ops(int id);

} // namespace helem
