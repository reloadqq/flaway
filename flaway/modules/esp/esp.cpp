#include "esp.h"
#include "../../flaway.h"
#include "../../globals/globals.h"
#include "../../gui/GUI.h"
#include "../../gui/hud/hud_internal.h"
#include "../../gui/data/pointer_png.h"
#include "../../utils/logger.h"
#include "../nametag_hook/nametag_hook.h"
#include "../aimassist/aimassist.h"
#include "../friend_manager/friend_manager.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/world/world.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/minecraft/util/box.h>
#include <sdk/classloader.h>
#include <cmath>
#include <cfloat>
#include <vector>
#include <deque>
#include <mutex>
#include <string>
#include <cstring>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <chrono>
#include <sdk/projection.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static std::unordered_map<int, esp_render_entry> g_players; static std::unordered_map<int, esp_render_entry> g_items; static esp_camera_data esp_cam; static std::mutex esp_mutex; static std::deque<esp_pickup_entry> g_pickups; static jobject g_last_world = nullptr; static std::map<int, std::string> g_name_cache; static std::map<int, std::string> g_item_name_cache; static int g_frame_count = 0; struct entity_scan_cache { long long ts = 0; double bxmin = 0, bymin = 0, bzmin = 0; double bxmax = 0, bymax = 0, bzmax = 0; float health = 20.0f, max_health = 20.0f; int hurt_time = 0; }; static std::unordered_map<int,
entity_scan_cache> g_scan_cache; static constexpr long long k_scan_interval_us = 200000; static constexpr long long k_fade_in_us = 120000; static constexpr long long k_grace_us = 400000; static constexpr long long k_fade_out_us = 500000; // --- colors ----------------------------------------------------------------
// Every ESP color is derived from the client (menu) accent: the same
// hstyle::grad_a / grad_b pair the HUD glass cards use, refreshed once per
// draw pass so boxes, bars, plates, tracers and arrows all match the theme.
static ImU32 s_c0 = IM_COL32(88, 140, 255, 255);
static ImU32 s_c1 = IM_COL32(150, 175, 255, 255);
static const ImU32 plate_bg_b = IM_COL32(15, 17, 22, 238);
static const ImU32 esp_friend = IM_COL32(48, 210, 88, 255);
static const ImU32 esp_friend_hi = IM_COL32(170, 255, 200, 255);

static void refresh_accent() {
    s_c0 = hstyle::grad_a(0.0f);
    s_c1 = hstyle::grad_b(0.30f);
}

static inline int col_r(ImU32 c) { return (int)((c >> IM_COL32_R_SHIFT) & 0xFF); }
static inline int col_g(ImU32 c) { return (int)((c >> IM_COL32_G_SHIFT) & 0xFF); }
static inline int col_b(ImU32 c) { return (int)((c >> IM_COL32_B_SHIFT) & 0xFF); }
static inline int a_mul(int alpha, float k) {
    int v = (int)((float)alpha * k);
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static void plate_shadow(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float rounding, int alpha) {
    for (int i = 3; i >= 1; i--) {
        int a = alpha * (4 - i) / 4;
        if (a <= 0) continue;
        float k = (float)i;
        dl->AddRectFilled(ImVec2(p0.x - k * 1.4f, p0.y - k * 0.6f + k * 1.5f),
                          ImVec2(p1.x + k * 1.4f, p1.y + k * 0.6f + k * 1.5f),
                          IM_COL32(0, 0, 0, a), rounding + k);
    }
}

// Shared rounded glass body for every plate: shadow, body, accent gloss,
// GPU sweep shader (hud_fx) and the accent border.
static void plate_body(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float rounding,
                       int alpha, ImU32 accent, ImU32 accent_hi, float phase) {
    plate_shadow(dl, p0, p1, rounding, a_mul(alpha, 0.85f));
    dl->AddRectFilled(p0, p1, hstyle::with_a(plate_bg_b, alpha), rounding);
    dl->PushClipRect(ImVec2(p0.x + rounding * 0.6f, p0.y), ImVec2(p1.x - rounding * 0.6f, p0.y + 9.0f), true);
    dl->AddRectFilledMultiColor(ImVec2(p0.x, p0.y), ImVec2(p1.x, p0.y + 9.0f),
                                hstyle::with_a(accent_hi, a_mul(alpha, 0.30f)),
                                hstyle::with_a(accent_hi, a_mul(alpha, 0.30f)),
                                hstyle::with_a(accent_hi, 0),
                                hstyle::with_a(accent_hi, 0));
    dl->PopClipRect();
    hud_fx::fx(dl, p0, p1, rounding, 0.34f * ((float)alpha / 255.0f), phase, hud_fx::FX_SWEEP);
    dl->AddRect(ImVec2(p0.x - 3.5f, p0.y - 3.5f), ImVec2(p1.x + 3.5f, p1.y + 3.5f),
                hstyle::with_a(accent, a_mul(alpha, 0.20f)), rounding + 3.5f, 0, 1.5f);
    dl->AddRect(p0, p1, hstyle::with_a(accent, a_mul(alpha, 0.95f)), rounding, 0, 1.5f);
}

static void plate_pointer(ImDrawList* dl, const ImVec2& p1, float cx, int alpha, ImU32 accent) {
    ImVec2 a(cx - 5.5f, p1.y), b(cx + 5.5f, p1.y), c(cx, p1.y + 6.0f);
    dl->AddTriangleFilled(a, b, c, hstyle::with_a(plate_bg_b, alpha));
    dl->AddTriangle(a, b, c, hstyle::with_a(accent, a_mul(alpha, 0.95f)), 1.5f);
}

// Name tag: bold Monocraft text, dark glass, accent border, shader sweep.
// `bottom_y` is where the plate ends — the little pointer hangs below it, so
// the caller only has to place it just above the box. Returns the height.
static float draw_name_plate(ImDrawList* dl, float cx, float bottom_y, const char* text,
                             int alpha, ImU32 accent, ImU32 accent_hi, float phase) {
    if (!dl || !text || !text[0] || alpha <= 0) return 0.0f;
    ImFont* f = GUI::font_hud_bold();
    if (!f) f = ImGui::GetFont();
    const float fs = 14.0f;
    const float pad_x = 11.0f, pad_y = 6.5f, rounding = 9.0f;
    ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    float w = ts.x + pad_x * 2.0f;
    float h = ts.y + pad_y * 2.0f;
    ImVec2 p0(cx - w * 0.5f, bottom_y - h);
    ImVec2 p1(cx + w * 0.5f, bottom_y);
    plate_body(dl, p0, p1, rounding, alpha, accent, accent_hi, phase);
    ImVec2 tp(cx - ts.x * 0.5f, p0.y + pad_y);
    dl->AddText(f, fs, ImVec2(tp.x, tp.y + 1.5f), IM_COL32(0, 0, 0, a_mul(alpha, 0.70f)), text);
    hstyle::gradient_text(dl, tp, text, f, fs, accent_hi, IM_COL32(255, 255, 255, 255), alpha);
    plate_pointer(dl, p1, cx, alpha, accent);
    return h;
}

// Dropped-item plate: same glass, but with the vanilla rarity icon box inside.
static float draw_item_plate(ImDrawList* dl, float cx, float bottom_y, const char* text,
                             unsigned char ir, unsigned char ig, unsigned char ib,
                             int alpha, ImU32 accent, ImU32 accent_hi, float phase) {
    if (!dl || !text || !text[0] || alpha <= 0) return 0.0f;
    ImFont* f = GUI::font_hud_bold();
    if (!f) f = ImGui::GetFont();
    const float fs = 14.0f, icon_size = 20.0f;
    const float pad_x = 9.0f, pad_y = 5.0f, gap = 8.0f, rounding = 9.0f;
    ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    float h = icon_size + pad_y * 2.0f;
    float w = pad_x + icon_size + gap + ts.x + pad_x;
    if (w < h) w = h;
    ImVec2 p0(cx - w * 0.5f, bottom_y - h);
    ImVec2 p1(cx + w * 0.5f, bottom_y);
    plate_body(dl, p0, p1, rounding, alpha, accent, accent_hi, phase);
    dl->AddRectFilled(p0, p1, IM_COL32(ir, ig, ib, a_mul(alpha, 0.10f)), rounding);
    ImVec2 ip0(p0.x + pad_x, p0.y + pad_y);
    ImVec2 ip1(ip0.x + icon_size, ip0.y + icon_size);
    dl->AddRectFilled(ip0, ip1,
                      IM_COL32((int)(ir * 0.16f), (int)(ig * 0.16f), (int)(ib * 0.16f), a_mul(alpha, 0.96f)), 5.0f);
    dl->AddRectFilled(ImVec2(ip0.x + 1.0f, ip0.y + 1.0f), ImVec2(ip1.x - 1.0f, ip0.y + icon_size * 0.45f),
                      IM_COL32(255, 255, 255, a_mul(alpha, 0.14f)), 4.0f);
    dl->AddRect(ip0, ip1, IM_COL32(ir, ig, ib, alpha), 5.0f, 0, 1.4f);
    char glyph[2] = { text[0], 0 };
    if (glyph[0] >= 'a' && glyph[0] <= 'z') glyph[0] = (char)(glyph[0] - 'a' + 'A');
    if (glyph[0] == 0) glyph[0] = '?';
    ImVec2 gs = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, glyph);
    float gx = ip0.x + (icon_size - gs.x) * 0.5f;
    float gy = ip0.y + (icon_size - gs.y) * 0.5f;
    dl->AddText(f, fs, ImVec2(gx, gy + 1.5f), IM_COL32(0, 0, 0, a_mul(alpha, 0.70f)), glyph);
    dl->AddText(f, fs, ImVec2(gx, gy), IM_COL32(245, 248, 255, a_mul(alpha, 0.97f)), glyph);
    ImVec2 tp(ip1.x + gap, p0.y + (h - ts.y) * 0.5f);
    dl->AddText(f, fs, ImVec2(tp.x, tp.y + 1.5f), IM_COL32(0, 0, 0, a_mul(alpha, 0.70f)), text);
    hstyle::gradient_text(dl, tp, text, f, fs, accent_hi, IM_COL32(255, 255, 255, 255), alpha);
    plate_pointer(dl, p1, cx, alpha, accent);
    return h;
}

static void draw_item_tile(ImDrawList* dl, float x, float y, const esp_item_slot& slot,
                           int alpha, ImU32 accent, ImU32 accent_hi) {
    if (!dl || alpha <= 0) return;
    ImFont* f = GUI::font_hud_bold();
    if (!f) f = ImGui::GetFont();
    const float s = 19.0f;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + s, y + s),
                      hstyle::with_a(IM_COL32(11, 12, 16, 245), alpha), 5.0f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + s, y + s),
                      IM_COL32(slot.icon_r, slot.icon_g, slot.icon_b, a_mul(alpha, 0.24f)), 5.0f);
    dl->AddRectFilled(ImVec2(x + 1.0f, y + 1.0f), ImVec2(x + s - 1.0f, y + s * 0.5f),
                      IM_COL32(255, 255, 255, a_mul(alpha, 0.10f)), 4.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + s, y + s),
                hstyle::with_a(accent, a_mul(alpha, 0.90f)), 5.0f, 0, 1.4f);
    char glyph[2] = { slot.name.empty() ? '?' : slot.name[0], 0 };
    if (glyph[0] >= 'a' && glyph[0] <= 'z') glyph[0] = (char)(glyph[0] - 'a' + 'A');
    if (glyph[0] == 0) glyph[0] = '?';
    const float fs = 12.0f;
    ImVec2 gs = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, glyph);
    float gx = x + (s - gs.x) * 0.5f;
    float gy = y + (s - gs.y) * 0.5f - (slot.count > 1 ? 1.5f : 0.0f);
    dl->AddText(f, fs, ImVec2(gx, gy + 1.5f), IM_COL32(0, 0, 0, a_mul(alpha, 0.70f)), glyph);
    dl->AddText(f, fs, ImVec2(gx, gy), IM_COL32(245, 248, 255, a_mul(alpha, 0.97f)), glyph);
    if (slot.count > 1) {
        std::string badge = "x" + std::to_string(slot.count);
        const float bfs = 10.0f;
        ImVec2 cs = f->CalcTextSizeA(bfs, FLT_MAX, 0.0f, badge.c_str());
        ImVec2 b0(x + s - cs.x - 5.0f, y + s - cs.y - 4.0f);
        ImVec2 b1(b0.x + cs.x + 6.0f, b0.y + cs.y + 4.0f);
        float br = (b1.y - b0.y) * 0.5f;
        dl->AddRectFilled(b0, b1, hstyle::with_a(IM_COL32(8, 9, 12, 245), alpha), br);
        dl->AddRect(b0, b1, hstyle::with_a(accent, a_mul(alpha, 0.85f)), br, 0, 1.0f);
        dl->AddText(f, bfs, ImVec2(b0.x + 3.0f, b0.y + 2.0f), hstyle::with_a(accent_hi, alpha), badge.c_str());
    }
}

static void draw_entity_item_tiles(ImDrawList* dl, float cx, float top_y,
                                   const std::vector<esp_item_slot>& items,
                                   int alpha, ImU32 accent, ImU32 accent_hi) {
    const float s = 19.0f, gap = 3.0f;
    size_t n = items.size();
    if (n > 6) n = 6;
    if (n == 0) return;
    float total = (float)n * (s + gap) - gap;
    float x0 = cx - total * 0.5f;
    for (size_t i = 0; i < n; i++)
        draw_item_tile(dl, x0 + (float)i * (s + gap), top_y, items[i], alpha, accent, accent_hi);
}

// Rounded outline with a vertical accent ramp. AddRect() keeps the corners
// rounded (AddRectFilledMultiColor has no rounding in this ImGui version), so
// the ramp is built from horizontal bands clipped onto the outline.
static void draw_grad_rect(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1,
                           ImU32 c0, ImU32 c1, int alpha, float th, float rounding) {
    dl->AddRect(ImVec2(p0.x - 1.5f, p0.y - 1.5f), ImVec2(p1.x + 1.5f, p1.y + 1.5f),
                IM_COL32(0, 0, 0, a_mul(alpha, 0.60f)), rounding + 1.5f, 0, th + 3.0f);
    // 6 clipped bands instead of 12: every band forces ImGui to split the draw
    // command (one glScissor + one draw call each), and on a box outline the
    // half-step difference is not visible.
    const int bands = 6;
    float y0 = p0.y - th * 0.5f;
    float bh = (p1.y + th * 0.5f - y0) / (float)bands;
    if (bh <= 0.0f) return;
    for (int i = 0; i < bands; i++) {
        ImU32 c = hstyle::with_a(hstyle::lerp(c0, c1, ((float)i + 0.5f) / (float)bands), alpha);
        dl->PushClipRect(ImVec2(-100000.0f, y0 + (float)i * bh),
                         ImVec2(100000.0f, y0 + (float)(i + 1) * bh), true);
        dl->AddRect(p0, p1, c, rounding, 0, th);
        dl->PopClipRect();
    }
}

// Extra soft rings for the glow box mode.
static void draw_box_glow(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float rounding,
                          int alpha, ImU32 c0, ImU32 c1) {
    dl->AddRect(ImVec2(p0.x - 5.0f, p0.y - 5.0f), ImVec2(p1.x + 5.0f, p1.y + 5.0f),
                hstyle::with_a(c0, a_mul(alpha, 0.34f)), rounding + 5.0f, 0, 1.7f);
    dl->AddRect(ImVec2(p0.x - 10.0f, p0.y - 10.0f), ImVec2(p1.x + 10.0f, p1.y + 10.0f),
                hstyle::with_a(c1, a_mul(alpha, 0.16f)), rounding + 10.0f, 0, 1.5f);
}

// Tracer: dark backing + accent ramp along the line.
static void draw_grad_line(ImDrawList* dl, const ImVec2& a, const ImVec2& b, int alpha, float th) {
    dl->AddLine(a, b, IM_COL32(0, 0, 0, a_mul(alpha, 0.55f)), th + 2.0f);
    const int seg = 4;
    for (int i = 0; i < seg; i++) {
        float t0 = (float)i / (float)seg;
        float t1 = (float)(i + 1) / (float)seg;
        ImVec2 p0(a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0);
        ImVec2 p1(a.x + (b.x - a.x) * (t1 + 0.03f), a.y + (b.y - a.y) * (t1 + 0.03f));
        dl->AddLine(p0, p1, hstyle::with_a(hstyle::lerp(s_c0, s_c1, (t0 + t1) * 0.5f), alpha), th);
    }
}

// Direction arrow (assets/pointer.png, embedded): rotated, tinted quad with a
// thin dark rim so it stays readable on snow/sky. No backing disc — the
// pointer silhouette alone keeps it small and clean.
static void draw_arrow(ImDrawList* dl, float cx, float cy, float size, float angle,
                       ImU32 tint, int alpha) {
    if (!dl || alpha <= 0) return;
    unsigned tex = hud_icons::png(pointer_png, pointer_png_size);
    if (!tex) return;
    float s = sinf(angle), c = cosf(angle);
    auto rot = [&](float x, float y) { return ImVec2(cx + x * c - y * s, cy + x * s + y * c); };
    float hw = size * 0.5f, hh = size * 0.52f;
    const ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 0.0f), uv2(1.0f, 1.0f), uv3(0.0f, 1.0f);
    const float k = 1.18f;   // rim: ~1.5px dark outline around a 17px pointer
    dl->PushTextureID((ImTextureID)(intptr_t)tex);
    dl->PrimReserve(6, 4);
    dl->PrimQuadUV(rot(-hw * k, -hh * k), rot(hw * k, -hh * k), rot(hw * k, hh * k), rot(-hw * k, hh * k),
                   uv0, uv1, uv2, uv3, IM_COL32(0, 0, 0, a_mul(alpha, 0.75f)));
    dl->PrimReserve(6, 4);
    dl->PrimQuadUV(rot(-hw, -hh), rot(hw, -hh), rot(hw, hh), rot(-hw, hh),
                   uv0, uv1, uv2, uv3, hstyle::with_a(tint, alpha));
    dl->PopTextureID();
}
static jmethodID g_entity_get_name_mid = nullptr; static
jmethodID g_text_get_string_mid = nullptr; static jmethodID g_player_get_profile_mid = nullptr; static jmethodID g_profile_get_name_mid = nullptr; static jmethodID cache_method(JNIEnv* env, jclass cls, const char* name, const char* sig, jmethodID& slot) { if (slot) return slot; slot = env->GetMethodID(cls, name, sig); if (env->ExceptionCheck()) { env->ExceptionClear(); slot = nullptr; } return slot; } static std::string get_entity_name(JNIEnv* env, jobject entity) { std::string result; jclass ec = sdk::classloader::find_class(env, sdk::mappings::entity_class_sig); if (!ec) return result;
jmethodID mid = cache_method(env, ec, sdk::mappings::entity_get_name_name, sdk::mappings::entity_get_name_sig, g_entity_get_name_mid); env->DeleteLocalRef(ec); if (!mid) return result; jobject name_text = env->CallObjectMethod(entity, mid); if (env->ExceptionCheck()) { env->ExceptionClear(); return result; } if (!name_text) return result; jclass tc = sdk::classloader::find_class(env, sdk::mappings::text_class_sig); if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(name_text); return result; } if (!tc) { env->DeleteLocalRef(name_text); return result; } jmethodID str_mid =
cache_method(env, tc, sdk::mappings::text_get_string_name, sdk::mappings::text_get_string_sig, g_text_get_string_mid); if (str_mid) { jstring s = (jstring)env->CallObjectMethod(name_text, str_mid, 0x7FFFFFFF); if (env->ExceptionCheck()) { env->ExceptionClear(); } else if (s) { const char* utf = env->GetStringUTFChars(s, nullptr); if (utf) { result = utf; env->ReleaseStringUTFChars(s, utf); } env->DeleteLocalRef(s); } } env->DeleteLocalRef(tc); env->DeleteLocalRef(name_text); return result; } static std::string get_player_nick(JNIEnv* env, jobject entity) { std::string result; jclass pe = sdk
::classloader::find_class(env, sdk::mappings::player_entity_class_sig); if (!pe) return result; jmethodID mid = cache_method(env, pe, sdk::mappings::player_get_game_profile_name, sdk::mappings::player_get_game_profile_sig, g_player_get_profile_mid); env->DeleteLocalRef(pe); if (!mid) return result; jobject profile = env->CallObjectMethod(entity, mid); if (env->ExceptionCheck()) { env->ExceptionClear(); return result; } if (!profile) return result; jclass gc = env->GetObjectClass(profile); if (!gc) { env->DeleteLocalRef(profile); return result; } jmethodID gn_mid = cache_method(env, gc, sdk::
mappings::game_profile_get_name_name, sdk::mappings::game_profile_get_name_sig, g_profile_get_name_mid); if (gn_mid) { jstring name = (jstring)env->CallObjectMethod(profile, gn_mid); if (env->ExceptionCheck()) env->ExceptionClear(); if (name) { const char* utf = env->GetStringUTFChars(name, nullptr); if (utf) { result = utf; env->ReleaseStringUTFChars(name, utf); } env->DeleteLocalRef(name); } } env->DeleteLocalRef(gc); env->DeleteLocalRef(profile); return result; } static void get_item_rarity_color(JNIEnv* env, jobject stack, unsigned char& r, unsigned char& g, unsigned char& b) { r = 88; g
= 140; b = 255; jclass sc = env->GetObjectClass(stack); if (!sc) return; jmethodID get_rarity_mid = env->GetMethodID(sc, sdk::mappings::itemstack_get_rarity_name, sdk::mappings::itemstack_get_rarity_sig); if (env->ExceptionCheck()) env->ExceptionClear(); if (!get_rarity_mid) { env->DeleteLocalRef(sc); return; } jobject rarity = env->CallObjectMethod(stack, get_rarity_mid); if (env->ExceptionCheck()) env->ExceptionClear(); env->DeleteLocalRef(sc); if (!rarity) return; jclass rc = sdk::classloader::find_class(env, sdk::mappings::rarity_class_sig); if (!rc) { env->DeleteLocalRef(rarity); return;
} jfieldID idx_fid = env->GetFieldID(rc, sdk::mappings::rarity_index_name, sdk::mappings::rarity_index_sig); if (env->ExceptionCheck()) env->ExceptionClear(); if (idx_fid) { int idx = env->GetIntField(rarity, idx_fid); if (env->ExceptionCheck()) env->ExceptionClear(); switch (idx) { case 1: r = 255; g = 214; b = 84; break; case 2: r = 94; g = 196; b = 255; break; case 3: r = 190; g = 124; b = 255; break; default: r = 168; g = 176; b = 188; break; } } env->DeleteLocalRef(rc); env->DeleteLocalRef(rarity); } static std::string clean_name(const std::string& raw); static void rarity_slot_color(int
idx, unsigned char& r, unsigned char& g, unsigned char& b) { switch (idx) { case 0: r = 150; g = 150; b = 150; break; case 1: r = 85; g = 255; b = 85; break; case 2: r = 85; g = 170; b = 255; break; case 3: r = 170; g = 85; b = 255; break; case 4: r = 255; g = 170; b = 0; break; default: r = 88; g = 140; b = 255; break; } } static const char* k_item_stack_class_sig = "net/minecraft/class_1799"; static std::string stack_tex_suffix(JNIEnv* env, jobject stack) { std::string suffix; if (!env || !stack) return suffix; static jmethodID s_tex_item_mid = nullptr; static jmethodID s_tex_key_mid = nullptr; if (!s_tex_item_mid) { jclass sc = sdk::classloader::find_class(env, k_item_stack_class_sig); if (!sc) return suffix; s_tex_item_mid = env->GetMethodID(sc, sdk::mappings::itemstack_get_item_name, sdk::mappings::itemstack_get_item_sig); if (env->ExceptionCheck()) { env->ExceptionClear(); s_tex_item_mid = nullptr; } env->DeleteLocalRef(sc); if (!s_tex_item_mid) return suffix; } jobject item = env->CallObjectMethod(stack, s_tex_item_mid); if (env->ExceptionCheck()) { env->ExceptionClear(); item = nullptr; } if (!item) return suffix; jclass ic = env->GetObjectClass(item); if (ic && !s_tex_key_mid) { s_tex_key_mid = env->GetMethodID(ic, sdk::mappings::item_get_translation_key_name, sdk::mappings::item_get_translation_key_sig); if (env->ExceptionCheck()) { env->ExceptionClear(); s_tex_key_mid = nullptr; } } if (s_tex_key_mid) { jstring key = (jstring)env->CallObjectMethod(item, s_tex_key_mid); if (env->ExceptionCheck()) { env->ExceptionClear(); key = nullptr; } if (key) { const char* ckey = env->GetStringUTFChars(key, nullptr); if (ckey) { static const char* k_prefixes[] = { "item.minecraft.", "block.minecraft." }; for (const char* p : k_prefixes) { size_t pl = strlen(p); if (strncmp(ckey, p, pl) == 0) { suffix = ckey + pl; break; } } env->ReleaseStringUTFChars(key, ckey); } env->DeleteLocalRef(key); } } if (ic) env->DeleteLocalRef(ic); env->DeleteLocalRef(item); return suffix; } static jfieldID g_equip_inv_fid = nullptr; static jfieldID g_equip_selected_fid = nullptr; static jfieldID g_equip_offhand_fid = nullptr; static jmethodID g_equip_get_stack_mid = nullptr; static
jmethodID g_stack_is_empty_mid = nullptr; static jmethodID g_stack_get_count_mid = nullptr; static jmethodID g_stack_get_name_mid = nullptr; static jmethodID g_stack_get_rarity_mid = nullptr; static bool fill_player_equipment(JNIEnv* env, jobject player, std::vector<esp_item_slot>& out) { out.clear(); if (!env || !player) return false; if (!g_equip_inv_fid) { jclass pc = sdk::classloader::find_class(env, sdk::mappings::player_entity_class_sig); if (!pc) return false; g_equip_inv_fid = env->GetFieldID(pc, sdk::mappings::player_inventory_name, sdk::mappings::player_inventory_sig); if (env->
ExceptionCheck()) env->ExceptionClear(); env->DeleteLocalRef(pc); if (!g_equip_inv_fid) return false; } jobject inv = env->GetObjectField(player, g_equip_inv_fid); if (env->ExceptionCheck()) { env->ExceptionClear(); return false; } if (!inv) return false; jclass ic = env->GetObjectClass(inv); if (!ic) { env->DeleteLocalRef(inv); return false; } if (!g_equip_get_stack_mid) { g_equip_get_stack_mid = env->GetMethodID(ic, sdk::mappings::inventory_get_stack_name, sdk::mappings::inventory_get_stack_sig); if (env->ExceptionCheck()) env->ExceptionClear(); } if (!g_equip_selected_fid) {
g_equip_selected_fid = env->GetFieldID(ic, sdk::mappings::inventory_selected_slot_name, sdk::mappings::inventory_selected_slot_sig); if (env->ExceptionCheck()) env->ExceptionClear(); } if (!g_equip_offhand_fid) { g_equip_offhand_fid = env->GetFieldID(ic, sdk::mappings::inventory_offhand_name, sdk::mappings::inventory_offhand_sig); if (env->ExceptionCheck()) env->ExceptionClear(); } jint sel = 0; jint off = 40; if (g_equip_selected_fid) { sel = env->GetIntField(inv, g_equip_selected_fid); if (env->ExceptionCheck()) env->ExceptionClear(); } if (g_equip_offhand_fid) { off = env->GetIntField(inv,
g_equip_offhand_fid); if (env->ExceptionCheck()) env->ExceptionClear(); } if (!g_stack_is_empty_mid || !g_stack_get_count_mid || !g_stack_get_name_mid || !g_stack_get_rarity_mid) { jclass sc = sdk::classloader::find_class(env, k_item_stack_class_sig); if (!sc) { env->DeleteLocalRef(ic); env->DeleteLocalRef(inv); return false; } g_stack_is_empty_mid = cache_method(env, sc, sdk::mappings::itemstack_is_empty_name, sdk::mappings::itemstack_is_empty_sig, g_stack_is_empty_mid); g_stack_get_count_mid = cache_method(env, sc, "method_7947", "()I", g_stack_get_count_mid); g_stack_get_name_mid =
cache_method(env, sc, sdk::mappings::itemstack_get_name_name, sdk::mappings::itemstack_get_name_sig, g_stack_get_name_mid); g_stack_get_rarity_mid = cache_method(env, sc, sdk::mappings::itemstack_get_rarity_name, sdk::mappings::itemstack_get_rarity_sig, g_stack_get_rarity_mid); env->DeleteLocalRef(sc); } const int slot_ids[6] = { sel, off, 39, 38, 37, 36 }; for (int i = 0; i < 6; i++) { if (!g_equip_get_stack_mid || !g_stack_is_empty_mid) break; jobject stack = env->CallObjectMethod(inv, g_equip_get_stack_mid, slot_ids[i]); if (env->ExceptionCheck()) { env->ExceptionClear(); continue; } if (!
stack) continue; if (env->CallBooleanMethod(stack, g_stack_is_empty_mid) == JNI_TRUE) { if (env->ExceptionCheck()) env->ExceptionClear(); env->DeleteLocalRef(stack); continue; } if (env->ExceptionCheck()) env->ExceptionClear(); std::string iname; if (g_stack_get_name_mid) { jobject name_text = env->CallObjectMethod(stack, g_stack_get_name_mid); if (env->ExceptionCheck()) env->ExceptionClear(); if (name_text) { jclass tc = sdk::classloader::find_class(env, sdk::mappings::text_class_sig); if (tc) { jmethodID str_mid = cache_method(env, tc, sdk::mappings::text_get_string_name, sdk::mappings::
text_get_string_sig, g_text_get_string_mid); if (str_mid) { jstring s = (jstring)env->CallObjectMethod(name_text, str_mid, 0x7FFFFFFF); if (env->ExceptionCheck()) env->ExceptionClear(); if (s) { const char* utf = env->GetStringUTFChars(s, nullptr); if (utf) { iname = utf; env->ReleaseStringUTFChars(s, utf); } env->DeleteLocalRef(s); } } env->DeleteLocalRef(tc); } env->DeleteLocalRef(name_text); } } if (iname.empty() && g_stack_get_name_mid) { jclass sc2 = env->GetObjectClass(stack); if (sc2) { jmethodID get_item_mid = env->GetMethodID(sc2, sdk::mappings::itemstack_get_item_name, sdk::mappings
::itemstack_get_item_sig); if (env->ExceptionCheck()) env->ExceptionClear(); if (get_item_mid) { jobject item = env->CallObjectMethod(stack, get_item_mid); if (env->ExceptionCheck()) env->ExceptionClear(); if (item) { jclass ic2 = env->GetObjectClass(item); if (ic2) { jmethodID key_mid = env->GetMethodID(ic2, sdk::mappings::item_get_translation_key_name, sdk::mappings::item_get_translation_key_sig); if (env->ExceptionCheck()) env->ExceptionClear(); if (key_mid) { jstring key = (jstring)env->CallObjectMethod(item, key_mid); if (env->ExceptionCheck()) env->ExceptionClear(); if (key) { const
char* ckey = env->GetStringUTFChars(key, nullptr); if (ckey) { iname = ckey; env->ReleaseStringUTFChars(key, ckey); } env->DeleteLocalRef(key); } } env->DeleteLocalRef(ic2); } env->DeleteLocalRef(item); } } env->DeleteLocalRef(sc2); } } if (iname.empty()) { env->DeleteLocalRef(stack); continue; } std::string tex = stack_tex_suffix(env, stack); unsigned char ir = 88, ig = 140, ib = 255; if (g_stack_get_rarity_mid) { jobject rarity = env->CallObjectMethod(stack, g_stack_get_rarity_mid); if (env->ExceptionCheck()) env->ExceptionClear(); if (rarity) { jclass rc = sdk::classloader::find_class(env, sdk::mappings::rarity_class_sig); if (rc) {
jfieldID idx_fid = env->GetFieldID(rc, sdk::mappings::rarity_index_name, sdk::mappings::rarity_index_sig); if (env->ExceptionCheck()) env->ExceptionClear(); if (idx_fid) { int idx = env->GetIntField(rarity, idx_fid); if (env->ExceptionCheck()) env->ExceptionClear(); rarity_slot_color(idx, ir, ig, ib); } env->DeleteLocalRef(rc); } env->DeleteLocalRef(rarity); } } int count = 1; if (g_stack_get_count_mid) { count = env->CallIntMethod(stack, g_stack_get_count_mid); if (env->ExceptionCheck()) { env->ExceptionClear(); count = 1; } } out.push_back({ clean_name(iname), ir, ig, ib, count, tex }); env->
DeleteLocalRef(stack); } env->DeleteLocalRef(ic); env->DeleteLocalRef(inv); return !out.empty(); } static std::string clean_name(const std::string& raw) { if (raw.empty()) return raw; std::string s = raw; size_t dot = s.find_last_of('.'); if (dot != std::string::npos && dot + 1 < s.size()) s = s.substr(dot + 1); for (char& c : s) if (c == '_') c = ' '; if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = (char)(s[0] - 'a' + 'A'); return s; } static inline long long now_us() { return std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now().time_since_epoch()).
count(); } static void update_entity_render(esp_render_entry& entry, double x, double y, double z, double min_x, double min_y, double min_z, double max_x, double max_y, double max_z, long long ) { entry.smooth.target[0] = x; entry.smooth.target[1] = y; entry.smooth.target[2] = z; entry.smooth.target[3] = min_x; entry.smooth.target[4] = min_y; entry.smooth.target[5] = min_z; entry.smooth.target[6] = max_x; entry.smooth.target[7] = max_y; entry.smooth.target[8] = max_z; if (!entry.smooth.valid) { for (int i = 0; i < 9; i++) entry.smooth.current[i] = entry.smooth.target[i]; entry.smooth.valid =
true; } } static void update_item_render(esp_render_entry& entry, double x, double y, double z, long long ) { entry.smooth.target[0] = x; entry.smooth.target[1] = y; entry.smooth.target[2] = z; for (int i = 3; i < 9; i++) entry.smooth.target[i] = 0.0; if (!entry.smooth.valid) { for (int i = 0; i < 9; i++) entry.smooth.current[i] = entry.smooth.target[i]; entry.smooth.valid = true; } } static void step_smooth(esp_smooth_state& st, double factor = 0.45) {
    if (!st.valid) return;
    // FPS-independent smoothing: converge the same amount per second no matter
    // the overlay present rate, so the ESP never trails more at 30fps than at
    // 120fps and doesn't overshoot when a frame hiccups.
    static long long s_last_us = 0;
    long long now = now_us();
    double dt_frames = 1.0;
    if (s_last_us != 0) {
        double dt = (double)(now - s_last_us) / 1000000.0;
        if (dt > 0.001 && dt < 0.2) dt_frames = dt * 60.0;
    }
    s_last_us = now;
    if (dt_frames <= 0.0) dt_frames = 1.0;
    double f = 1.0 - pow(1.0 - factor, dt_frames);
    for (int i = 0; i < 9; i++) st.current[i] += (st.target[i] - st.current[i]) * f;
} static int compute_alpha(const
esp_render_entry& entry, long long now) { long long age = now - entry.created_us; long long since_seen = now - entry.last_seen_us; if (age < k_fade_in_us) return (int)(255 * (double)age / (double)k_fade_in_us); if (since_seen < k_grace_us) return 255; long long fade_elapsed = since_seen - k_grace_us; if (fade_elapsed >= k_fade_out_us) return 0; return (int)(255 * (1.0 - (double)fade_elapsed / (double)k_fade_out_us)); } void flaway::modules::esp::run() { { std::lock_guard<std::mutex> lock(esp_mutex); long long stale_threshold = k_grace_us + k_fade_out_us; long long ts = now_us(); for (auto it = g_players.begin(); it != g_players.end(); ) { if (it->second.last_seen_us <= 0 || ts - it->second.last_seen_us > stale_threshold) it = g_players.erase(it); else ++it; } for (auto it = g_items.begin(); it != g_items.end(); ) { if (it->second.last_seen_us <= 0 || ts - it->second.last_seen_us > stale_threshold) it = g_items.erase(it); else ++it; } } bool esp_any = globals::box_enabled || globals::esp_health_bar || globals::esp_name_enabled || globals::esp_item_enabled || globals::esp_tracers || globals::esp_arrows || globals::hud_target_enabled || globals::hud_pickups_enabled; bool want_hide =
esp_any && globals::esp_hide_vanilla_names; nametag_hook::set_enabled(want_hide); if (want_hide && !nametag_hook::is_initialized()) nametag_hook::init(); if (!esp_any) { std::lock_guard<std::mutex> lock(esp_mutex); g_players.clear(); g_items.clear(); g_name_cache.clear(); g_item_name_cache.clear(); return; } auto env = flaway::instance->get_env(); if (!env || !sdk::instance) { return; } if (sdk::instance->is_screen_open()) { std::lock_guard<std::mutex> lock(esp_mutex); g_players.clear(); g_items.clear(); return; } { jobject player = sdk::instance->get_player(); if (player) { sdk::
entity_client local(player); esp_camera_data cam; cam.cam_x = local.get_x(); cam.cam_y = local.get_y() + 1.62; cam.cam_z = local.get_z(); cam.yaw = local.get_yaw(); cam.pitch = local.get_pitch(); cam.fov = 70.0f; { std::lock_guard<std::mutex> lock(esp_mutex); if (esp_cam.fov > 1.0f) cam.fov = esp_cam.fov; esp_cam = cam; } env->DeleteLocalRef(player); } } static auto g_last_scan = std::chrono::steady_clock::now();
    auto g_now = std::chrono::steady_clock::now();
    // 100 ms (10 Hz) instead of 300 ms: player positions are refreshed much
    // more often, so between-scans the smoothing only has to cover a third of
    // the distance -> no more lagging/jerky ESP boxes behind fast players.
    // The expensive parts (names, bboxes, health, equipment) are still cached
    // on longer intervals further down.
    constexpr auto k_scan_interval = std::chrono::milliseconds(100);
    if (g_now - g_last_scan < k_scan_interval) return; g_last_scan = g_now
; jobject world = sdk::instance->get_world(); if (!world) { if (g_last_world) { env->DeleteGlobalRef(g_last_world); g_last_world = nullptr; } std::lock_guard<std::mutex> lock(esp_mutex); g_players.clear(); g_items.clear(); return; } if (g_last_world) { if (!env->IsSameObject(g_last_world, world)) { env->DeleteGlobalRef(g_last_world); g_last_world = env->NewGlobalRef(world); g_name_cache.clear(); g_item_name_cache.clear(); std::lock_guard<std::mutex> lock(esp_mutex); g_players.clear(); g_items.clear(); } } else g_last_world = env->NewGlobalRef(world); jobject player = sdk::instance->get_player
(); if (!player) { env->DeleteLocalRef(world); return; } sdk::camera_data cam = sdk::instance->get_camera(); double eye_x, eye_y, eye_z; float cam_yaw, cam_pitch, cam_fov = 70.0f; if (cam.valid) { eye_x = cam.x; eye_y = cam.y; eye_z = cam.z; cam_yaw = cam.yaw; cam_pitch = cam.pitch; cam_fov = cam.fov; } else { sdk::entity_client player_entity(player); eye_x = player_entity.get_x(); eye_y = player_entity.get_y() + 1.62; eye_z = player_entity.get_z(); cam_yaw = player_entity.get_yaw(); cam_pitch = player_entity.get_pitch(); } sdk::world_client wc(world); std::vector<jobject> wplayers = wc.
get_players(); jclass living_cls = sdk::classloader::find_class(env, sdk::mappings::living_entity_class_sig); jmethodID get_health_mid = nullptr; jmethodID get_max_health_mid = nullptr; static jmethodID s_health_mid = nullptr; static jmethodID s_max_health_mid = nullptr; if (living_cls) { if (!s_health_mid) { s_health_mid = env->GetMethodID(living_cls, sdk::mappings::living_entity_get_health_name, sdk::mappings::living_entity_get_health_sig); if (env->ExceptionCheck()) env->ExceptionClear(); } if (!s_max_health_mid) { s_max_health_mid = env->GetMethodID(living_cls, sdk::mappings::
living_entity_get_max_health_name, sdk::mappings::living_entity_get_max_health_sig); if (env->ExceptionCheck()) env->ExceptionClear(); } get_health_mid = s_health_mid; get_max_health_mid = s_max_health_mid; } static jfieldID s_hurt_time_fid = nullptr; if (living_cls && !s_hurt_time_fid) { s_hurt_time_fid = env->GetFieldID(living_cls, sdk::mappings::living_entity_hurt_time_name, sdk::mappings::living_entity_hurt_time_sig); if (env->ExceptionCheck()) env->ExceptionClear(); } static long long last_item_fill_us = 0; bool fill_items_pass = (globals::esp_item_enabled || globals::hud_target_enabled) && (last_item_fill_us == 0 || now_us() - last_item_fill_us >= 2000000); if (fill_items_pass) last_item_fill_us = now_us(); for (jobject p : wplayers) { if (!p) continue; sdk::entity_client ec(p); if (ec.is_same_object(player)) continue; int eid = ec.get_entity_id(); if (eid <= 0) continue; double ex
= ec.get_x(); double ey = ec.get_y(); double ez = ec.get_z(); double dxp = ex - eye_x; double dyp = ey - eye_y; double dzp = ez - eye_z; if (dxp * dxp + dyp * dyp + dzp * dzp > 4096.0) continue; double bxmin = ex - 0.3, bymin = ey, bzmin = ez - 0.3; double bxmax = ex + 0.3, bymax = ey + 1.8, bzmax = ez + 0.3; float health = 20.0f, max_health = 20.0f; int hurt_time = 0; long long ts_now = now_us(); auto scc = g_scan_cache.find(eid); if (scc != g_scan_cache.end() && ts_now - scc->second.ts < k_scan_interval_us) { bxmin = scc->second.bxmin; bymin = scc->second.bymin; bzmin = scc->second.bzmin; bxmax = scc->second.bxmax; bymax = scc->second.bymax; bzmax = scc->second.bzmax; health = scc->second.health; max_health = scc->second.max_health; hurt_time = scc->second.hurt_time; } else { jobject bb = ec.get_bounding_box(); if (bb) { sdk::box_client bx(bb); bxmin = bx.get_min_x(); bymin = bx.get_min_y(); bzmin = bx.get_min_z(); bxmax = bx.get_max_x(); bymax = bx.get_max_y(); bzmax = bx.get_max_z(); env->DeleteLocalRef(bb); } if (get_health_mid && get_max_health_mid) { health = env->CallFloatMethod(p, get_health_mid); if (env->ExceptionCheck()) { env->ExceptionClear(); health = 20.0f; } max_health = env->CallFloatMethod(p, get_max_health_mid);
if (env->ExceptionCheck()) { env->ExceptionClear(); max_health = 20.0f; } } entity_scan_cache& sc = g_scan_cache[eid]; sc.ts = ts_now; sc.bxmin = bxmin; sc.bymin = bymin; sc.bzmin = bzmin; sc.bxmax = bxmax; sc.bymax = bymax; sc.bzmax = bzmax; sc.health = health; sc.max_health = max_health; if (s_hurt_time_fid) { sc.hurt_time = env->GetIntField(p, s_hurt_time_fid); if (env->ExceptionCheck()) env->ExceptionClear(); } } std::string name; auto ncit = g_name_cache.find(eid); if (ncit != g_name_cache.end()) name = ncit->second; else { name = get_player_nick(env, p); if (name.empty()) name = get_entity_name(env, p); if (!name.empty()) g_name_cache[eid] = clean_name(name); } std::vector<esp_item_slot> items; if (
fill_items_pass) fill_player_equipment(env, p, items); long long ts = now_us(); std::lock_guard<std::mutex> lock(esp_mutex); esp_render_entry& entry = g_players[eid]; entry.name = name; entry.health = health; entry.max_health = max_health; entry.hurt_time = hurt_time; entry.is_friend = flaway::modules::friend_manager::is_friend(name); entry.last_seen_us = ts; if (entry.created_us == 0) entry.created_us = ts; if (fill_items_pass) entry.items = std::move(items); update_entity_render(entry, ex, ey, ez, bxmin, bymin, bzmin, bxmax, bymax, bzmax, ts); } if (globals::mobstats_enabled && living_cls) { jclass player_cls = sdk::classloader::find_class(env, sdk::mappings::player_entity_class_sig);
std::vector<jobject> all_entities = wc.get_entities(); for (jobject e : all_entities) { if (!e) continue; if (player_cls && env->IsInstanceOf(e, player_cls)) { env->DeleteLocalRef(e); continue; } if (!env->IsInstanceOf(e, living_cls)) { env->DeleteLocalRef(e); continue; } sdk::entity_client ec(e); int eid = ec.get_entity_id(); if (eid <= 0) { env->DeleteLocalRef(e); continue; } double ex = ec.get_x(); double ey = ec.get_y(); double ez = ec.get_z(); double dxp = ex - eye_x; double dyp = ey - eye_y; double dzp = ez - eye_z; if (dxp * dxp + dyp * dyp + dzp * dzp > 4096.0) { env->DeleteLocalRef(e
); continue; } double bxmin = ex - 0.3, bymin = ey, bzmin = ez - 0.3; double bxmax = ex + 0.3, bymax = ey + 1.8, bzmax = ez + 0.3; float health = 20.0f, max_health = 20.0f; int hurt_time = 0; long long ts_now = now_us(); auto scc = g_scan_cache.find(eid); if (scc != g_scan_cache.end() && ts_now - scc->second.ts < k_scan_interval_us) { bxmin = scc->second.bxmin; bymin = scc->second.bymin; bzmin = scc->second.bzmin; bxmax = scc->second.bxmax; bymax = scc->second.bymax; bzmax = scc->second.bzmax; health = scc->second.health; max_health = scc->second.max_health; hurt_time = scc->second.hurt_time; } else { jobject bb = ec.get_bounding_box(); if (bb)
{ sdk::box_client bx(bb); bxmin = bx.get_min_x(); bymin = bx.get_min_y(); bzmin = bx.get_min_z(); bxmax = bx.get_max_x(); bymax = bx.get_max_y(); bzmax = bx.get_max_z(); env->DeleteLocalRef(bb); } if (get_health_mid && get_max_health_mid) { health = env->CallFloatMethod(e, get_health_mid); if (env->ExceptionCheck()) { env->ExceptionClear(); health = 20.0f; } max_health = env->CallFloatMethod(e, get_max_health_mid); if (env->ExceptionCheck()) { env->ExceptionClear(); max_health = 20.0f; } } entity_scan_cache& sc = g_scan_cache[eid]; sc.ts = ts_now; sc.bxmin = bxmin; sc.bymin = bymin; sc.bzmin
= bzmin; sc.bxmax = bxmax; sc.bymax = bymax; sc.bzmax = bzmax; sc.health = health; sc.max_health = max_health; if (s_hurt_time_fid) { sc.hurt_time = env->GetIntField(e, s_hurt_time_fid); if (env->ExceptionCheck()) env->ExceptionClear(); } } std::string name; auto ncit = g_name_cache.find(eid); if (ncit != g_name_cache.end()) name = ncit->second; else { name = get_entity_name(env, e); if (!name.empty()) g_name_cache[eid] = clean_name(name); } long long ts = now_us(); std::lock_guard<std::mutex> lock(esp_mutex); esp_render_entry& entry = g_players[eid]; entry.name = name; entry.health = health; entry.max_health = max_health; entry.hurt_time = hurt_time; entry.is_friend = flaway::modules::friend_manager::is_friend(name); entry.last_seen_us = ts; if (entry.created_us == 0) entry.created_us = ts; update_entity_render(
entry, ex, ey, ez, bxmin, bymin, bzmin, bxmax, bymax, bzmax, ts); env->DeleteLocalRef(e); } if (player_cls) env->DeleteLocalRef(player_cls); } if (living_cls) env->DeleteLocalRef(living_cls); if (globals::esp_item_enabled || globals::hud_pickups_enabled) { jclass item_entity_cls = sdk::classloader::find_class(env, sdk::mappings::item_entity_class_sig); if (item_entity_cls) { std::vector<jobject> items = wc.get_entities_by_class(item_entity_cls); static jmethodID s_get_stack_mid = nullptr; if (!s_get_stack_mid) { s_get_stack_mid = env->GetMethodID(item_entity_cls, sdk::mappings::item_entity_get_stack_name, sdk::mappings::
item_entity_get_stack_sig); if (env->ExceptionCheck()) env->ExceptionClear(); if (!s_get_stack_mid) { s_get_stack_mid = env->GetMethodID(item_entity_cls, "getStack", "()Lnet/minecraft/class_1799;"); if (env->ExceptionCheck()) env->ExceptionClear(); } } for (jobject e : items) { if (!e) continue; sdk::entity_client ec(e); int eid = ec.get_entity_id(); if (eid <= 0) continue; double ix = ec.get_x(); double iy = ec.get_y(); double iz = ec.get_z(); double dxp = ix - eye_x; double dyp = iy - eye_y; double dzp = iz - eye_z; if (dxp * dxp + dyp * dyp + dzp * dzp > 4096.0) continue; std::string iname
; unsigned char icon_r = 88, icon_g = 140, icon_b = 255; std::string tex; auto icit = g_item_name_cache.find(eid); if (icit != g_item_name_cache.end() && !icit->second.empty()) { iname = icit->second; } else if (s_get_stack_mid) { jobject stack = env->CallObjectMethod(e, s_get_stack_mid); if (env->ExceptionCheck()) env->ExceptionClear(); if (stack) { tex = stack_tex_suffix(env, stack); get_item_rarity_color(env, stack, icon_r, icon_g, icon_b); jclass sc = env->GetObjectClass(stack); if (sc) { static jmethodID s_get_name_mid = nullptr; jmethodID get_name_mid = cache_method(env, sc, sdk::mappings::itemstack_get_name_name, sdk::mappings::
itemstack_get_name_sig, s_get_name_mid); if (get_name_mid) { jobject name_text = env->CallObjectMethod(stack, get_name_mid); if (env->ExceptionCheck()) env->ExceptionClear(); if (name_text) { jclass tc = sdk::classloader::find_class(env, sdk::mappings::text_class_sig); if (tc) { static jmethodID s_text_str_mid = nullptr; jmethodID str_mid = cache_method(env, tc, sdk::mappings::text_get_string_name, sdk::mappings::text_get_string_sig, s_text_str_mid); if (str_mid) { jstring s = (jstring)env->CallObjectMethod(name_text, str_mid, 0x7FFFFFFF); if (env->ExceptionCheck()) env->ExceptionClear(); if
(s) { const char* utf = env->GetStringUTFChars(s, nullptr); if (utf) { iname = utf; env->ReleaseStringUTFChars(s, utf); } env->DeleteLocalRef(s); } } env->DeleteLocalRef(tc); } env->DeleteLocalRef(name_text); } } env->DeleteLocalRef(sc); } if (iname.empty()) { jclass sc2 = env->GetObjectClass(stack); if (sc2) { static jmethodID s_get_item_mid = nullptr; jmethodID get_item_mid = cache_method(env, sc2, sdk::mappings::itemstack_get_item_name, sdk::mappings::itemstack_get_item_sig, s_get_item_mid); if (get_item_mid) { jobject item = env->CallObjectMethod(stack, get_item_mid); if (env->
ExceptionCheck()) env->ExceptionClear(); if (item) { jclass ic = env->GetObjectClass(item); if (ic) { static jmethodID s_get_key_mid = nullptr; jmethodID get_key_mid = cache_method(env, ic, sdk::mappings::item_get_translation_key_name, sdk::mappings::item_get_translation_key_sig, s_get_key_mid); if (get_key_mid) { jstring key = (jstring)env->CallObjectMethod(item, get_key_mid); if (env->ExceptionCheck()) env->ExceptionClear(); if (key) { const char* ckey = env->GetStringUTFChars(key, nullptr); if (ckey) { iname = ckey; env->ReleaseStringUTFChars(key, ckey); } env->DeleteLocalRef(key); } } env
->DeleteLocalRef(ic); } env->DeleteLocalRef(item); } } env->DeleteLocalRef(sc2); } env->DeleteLocalRef(stack); } else { env->DeleteLocalRef(stack); } } } if (iname.empty()) iname = get_entity_name(env, e); if (!iname.empty()) g_item_name_cache[eid] = clean_name(iname); long long ts = now_us(); std::lock_guard<std::mutex> lock(esp_mutex); esp_render_entry& entry = g_items[eid]; bool fresh = (entry.created_us == 0); entry.name = iname; entry.icon_r = icon_r; entry.icon_g = icon_g; entry.icon_b = icon_b; entry.last_seen_us = ts; if (entry.created_us == 0) entry.created_us = ts;
update_item_render(entry, ix, iy, iz, ts); if (fresh && !iname.empty() && dxp * dxp + dyp * dyp + dzp * dzp <= 16.0) { g_pickups.push_back({iname, icon_r, icon_g, icon_b, ts, tex}); if (g_pickups.size() > 8) g_pickups.pop_front(); } } for (jobject e : items) if (e) env->DeleteLocalRef(e); env->DeleteLocalRef(item_entity_cls); } } { std::lock_guard<std::mutex> lock(esp_mutex); esp_cam.cam_x = eye_x; esp_cam.cam_y = eye_y; esp_cam.cam_z = eye_z; esp_cam.yaw = cam_yaw; esp_cam.pitch = cam_pitch; esp_cam.fov = cam_fov; if (esp_cam.fov < 1.0f) esp_cam.fov = 70.0f; } for (jobject p : wplayers) if (p) env->
DeleteLocalRef(p); env->DeleteLocalRef(world); env->DeleteLocalRef(player); } void flaway::modules::esp::draw_boxes() {
    const bool want_box = globals::box_enabled;
    const bool want_hp = globals::esp_health_bar;
    const bool want_name = globals::esp_name_enabled;
    const bool want_item = globals::esp_item_enabled;
    const bool want_tracer = globals::esp_tracers;
    const bool want_arrows = globals::esp_arrows;
    if (!want_box && !want_hp && !want_name && !want_item && !want_tracer && !want_arrows) return;
    if (!GUI::get_is_init()) return;
    if (sdk::instance && sdk::instance->is_screen_open()) return;
    ImGuiIO& io = ImGui::GetIO();
    int sw = (int)io.DisplaySize.x;
    int sh = (int)io.DisplaySize.y;
    if (sw <= 0 || sh <= 0) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;
    // The ESP queues GPU sweeps too: reset (frame-guarded) and pick up the
    // current client accent before anything is drawn.
    hud_fx::begin_frame();
    refresh_accent();
    std::unordered_map<int, esp_render_entry> pc;
    std::unordered_map<int, esp_render_entry> ic;
    esp_camera_data cam;
    {
        std::lock_guard<std::mutex> lock(esp_mutex);
        pc.swap(g_players);
        ic.swap(g_items);
        cam = esp_cam;
    }
    if (pc.empty() && ic.empty()) return;
    for (auto& kv : pc) step_smooth(kv.second.smooth);
    for (auto& kv : ic) step_smooth(kv.second.smooth);
    projection::set_view((float)cam.cam_x, (float)cam.cam_y, (float)cam.cam_z, cam.yaw, cam.pitch, cam.fov, sw, sh);
    g_frame_count++;
    long long ts = now_us();
    float offset = globals::esp_vertical_offset;
    int locked_id = flaway::modules::aimassist::get_locked_id();
    for (auto& kv : pc) {
        esp_render_entry& entry = kv.second;
        int alpha = compute_alpha(entry, ts);
        if (alpha <= 0) continue;
        bool is_locked = ((int)kv.first == locked_id);
        float dist_plates = 0.0f;
        double* cv = entry.smooth.current;
        float bmin[3] = { (float)cv[3], (float)cv[4], (float)cv[5] };
        float bmax[3] = { (float)cv[6], (float)cv[7], (float)cv[8] };
        {
            const flaway::projection::view_state& vs = flaway::projection::state();
            if (vs.valid) {
                float cx = (bmin[0] + bmax[0]) * 0.5f;
                float cy = (bmin[1] + bmax[1]) * 0.5f;
                float cz = (bmin[2] + bmax[2]) * 0.5f;
                float dx = cx - vs.cam_pos[0];
                float dy = cy - vs.cam_pos[1];
                float dz = cz - vs.cam_pos[2];
                float dot = dx * vs.forward[0] + dy * vs.forward[1] + dz * vs.forward[2];
                if (dot < -1.5f) continue;
            }
        }
        float corners[8][3] = {
            {bmin[0], bmin[1] + offset, bmin[2]}, {bmax[0], bmin[1] + offset, bmin[2]},
            {bmax[0], bmin[1] + offset, bmax[2]}, {bmin[0], bmin[1] + offset, bmax[2]},
            {bmin[0], bmax[1] + offset, bmin[2]}, {bmax[0], bmax[1] + offset, bmin[2]},
            {bmax[0], bmax[1] + offset, bmax[2]}, {bmin[0], bmax[1] + offset, bmax[2]} };
        float sx[8], sy[8];
        bool corner_visible[8] = {};
        int valid = 0;
        float min_sx = FLT_MAX, max_sx = -FLT_MAX;
        float min_sy = FLT_MAX, max_sy = -FLT_MAX;
        for (int i = 0; i < 8; i++) {
            if (projection::world_to_screen(corners[i][0], corners[i][1], corners[i][2], sx[i], sy[i])) {
                corner_visible[i] = true;
                if (sx[i] < min_sx) min_sx = sx[i];
                if (sx[i] > max_sx) max_sx = sx[i];
                if (sy[i] < min_sy) min_sy = sy[i];
                if (sy[i] > max_sy) max_sy = sy[i];
                valid++;
            }
        }
        if (valid < 4) continue;
        if (max_sx < -24.0f || min_sx > (float)sw + 24.0f || max_sy < -24.0f || min_sy > (float)sh + 24.0f) continue;
        {
            const flaway::projection::view_state& vs = flaway::projection::state();
            float dx = (bmin[0] + bmax[0]) * 0.5f - vs.cam_pos[0];
            float dy = (bmin[1] + bmax[1]) * 0.5f - vs.cam_pos[1];
            float dz = (bmin[2] + bmax[2]) * 0.5f - vs.cam_pos[2];
            dist_plates = sqrtf(dx * dx + dy * dy + dz * dz);
        }
        ImU32 c0 = entry.is_friend ? esp_friend : s_c0;
        ImU32 c1 = entry.is_friend ? esp_friend_hi : s_c1;
        const float box_rounding = 5.0f;
        if (want_box) {
            if (globals::esp_mode == 3) {
                static const int edges[12][2] = { {0,1},{1,2},{2,3},{3,0},
                    {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7} };
                float ytop = min_sy, ybot = max_sy;
                if (ybot - ytop < 1.0f) ybot = ytop + 1.0f;
                for (int e = 0; e < 12; e++) {
                    int ia = edges[e][0], ib = edges[e][1];
                    if (!corner_visible[ia] || !corner_visible[ib]) continue;
                    dl->AddLine(ImVec2(sx[ia], sy[ia]), ImVec2(sx[ib], sy[ib]),
                                IM_COL32(0, 0, 0, a_mul(alpha, 0.55f)), 4.4f);
                }
                for (int e = 0; e < 12; e++) {
                    int ia = edges[e][0], ib = edges[e][1];
                    if (!corner_visible[ia] || !corner_visible[ib]) continue;
                    float t = (((sy[ia] - ytop) + (sy[ib] - ytop)) * 0.5f) / (ybot - ytop);
                    dl->AddLine(ImVec2(sx[ia], sy[ia]), ImVec2(sx[ib], sy[ib]),
                                hstyle::with_a(hstyle::lerp(c0, c1, t), alpha), 2.3f);
                }
            } else {
                ImVec2 bp0(min_sx, min_sy), bp1(max_sx, max_sy);
                if (globals::esp_mode == 0) draw_box_glow(dl, bp0, bp1, box_rounding, alpha, c0, c1);
                if (globals::esp_mode == 2)  // "Box": faint tinted interior
                    dl->AddRectFilled(bp0, bp1, hstyle::with_a(c0, a_mul(alpha, 0.08f)), box_rounding);
                draw_grad_rect(dl, bp0, bp1, c0, c1, alpha, is_locked ? 3.2f : 2.5f, box_rounding);
                if (is_locked)
                    dl->AddRect(bp0, bp1, IM_COL32(255, 255, 255, a_mul(alpha, 0.85f)),
                                box_rounding, 0, 1.0f);
            }
        }
        if (want_tracer) {
            draw_grad_line(dl, ImVec2((float)sw * 0.5f, (float)sh),
                           ImVec2((min_sx + max_sx) * 0.5f, (min_sy + max_sy) * 0.5f),
                           a_mul(alpha, 0.92f), 2.1f);
        }
        if (want_hp && entry.max_health > 0) {
            float hp = entry.health / entry.max_health;
            if (hp > 1.0f) hp = 1.0f;
            if (hp < 0.0f) hp = 0.0f;
            const float bw = 5.0f;
            float bx = min_sx - bw - 5.0f;
            float bh = max_sy - min_sy;
            float fh = bh * hp;
            dl->AddRectFilled(ImVec2(bx - 1.5f, min_sy - 1.5f), ImVec2(bx + bw + 1.5f, max_sy + 1.5f),
                              IM_COL32(0, 0, 0, a_mul(alpha, 0.65f)), 3.5f);
            dl->AddRectFilled(ImVec2(bx, min_sy), ImVec2(bx + bw, max_sy),
                              hstyle::with_a(IM_COL32(34, 36, 44, 255), a_mul(alpha, 0.90f)), 2.5f);
            if (fh > 0.5f) {
                ImU32 fill = hstyle::lerp(IM_COL32(235, 74, 74, 255), c0, hp);
                dl->AddRectFilled(ImVec2(bx, max_sy - fh), ImVec2(bx + bw, max_sy),
                                  hstyle::with_a(fill, alpha), 2.5f);
                dl->PushClipRect(ImVec2(bx, max_sy - fh), ImVec2(bx + bw, max_sy), true);
                dl->AddRectFilled(ImVec2(bx + 0.5f, max_sy - fh),
                                  ImVec2(bx + bw - 0.5f, max_sy - fh + 3.0f),
                                  IM_COL32(255, 255, 255, a_mul(alpha, 0.55f)), 2.0f);
                dl->PopClipRect();
            }
        }
        float plate_x = (min_sx + max_sx) * 0.5f;
        float plate_bottom = min_sy - 7.0f;
        float plate_h = 0.0f;
        if (want_name && !entry.name.empty() && dist_plates <= 96.0f) {
            float phase = (float)((int)kv.first % 97) / 97.0f;
            plate_h = draw_name_plate(dl, plate_x, plate_bottom, entry.name.c_str(), alpha, c0, c1, phase);
        }
        if (want_item && !entry.items.empty() && dist_plates <= 96.0f) {
            // equipment row stacks above the name tag so nothing covers the box
            float tiles_top = plate_h > 0.0f
                                  ? plate_bottom - plate_h - 7.0f
                                  : min_sy - 6.0f - 19.0f;
            draw_entity_item_tiles(dl, plate_x, tiles_top, entry.items, alpha, c0, c1);
        }
    }
    if (want_arrows) {
        const flaway::projection::view_state& vs = flaway::projection::state();
        if (vs.valid && hud_icons::png(pointer_png, pointer_png_size)) {
            const float cx0 = (float)sw * 0.5f, cy0 = (float)sh * 0.5f;
            float R = (float)(sw < sh ? sw : sh) * 0.30f;
            if (R < 70.0f) R = 70.0f;
            if (R > 150.0f) R = 150.0f;
            const float yr = vs.yaw * (float)M_PI / 180.0;
            const float fwx = -sinf(yr), fwz = cosf(yr);
            const float rgx = -fwz, rgz = fwx;
            const float m = 26.0f;
            for (auto& kv : pc) {
                esp_render_entry& entry = kv.second;
                int alpha = compute_alpha(entry, ts);
                if (alpha <= 0) continue;
                double* cv = entry.smooth.current;
                float dx = (float)(cv[0] - vs.cam_pos[0]);
                float dz = (float)(cv[2] - vs.cam_pos[2]);
                if (dx * dx + dz * dz < 4.0f) continue;
                float px, py;
                // On screen (with a margin) -> the box already tells you where
                // it is; only the missing/off-screen targets get an arrow.
                if (projection::world_to_screen(cv[0], cv[1] + 1.0, cv[2], px, py) &&
                    px >= 16.0f && px <= (float)sw - 16.0f &&
                    py >= 16.0f && py <= (float)sh - 16.0f)
                    continue;
                float bearing = atan2f(dx * rgx + dz * rgz, dx * fwx + dz * fwz);
                float ax = cx0 + sinf(bearing) * R;
                float ay = cy0 - cosf(bearing) * R;
                if (ax < m) ax = m;
                if (ax > (float)sw - m) ax = (float)sw - m;
                if (ay < m) ay = m;
                if (ay > (float)sh - m) ay = (float)sh - m;
                draw_arrow(dl, ax, ay, 17.0f, bearing,
                           entry.is_friend ? esp_friend : s_c0, alpha);
            }
        }
    }
    if (want_item) {
        // Item plates are the most expensive ESP element: 3-layer shadow, gloss
        // clip, 2 borders, two text passes and one GPU sweep each. Scan range is
        // 64 blocks, so a loot pile can queue hundreds of them and eat the whole
        // frame — only the nearest N get a plate (and everything is skipped
        // outright when the feature is off).
        constexpr int k_max_item_plates = 24;
        struct item_cand { float d2; float sx, sy; int alpha; int id; const esp_render_entry* e; };
        std::vector<item_cand> cs;
        cs.reserve(ic.size());
        const flaway::projection::view_state& vs = flaway::projection::state();
        for (auto& kv : ic) {
            const esp_render_entry& entry = kv.second;
            int alpha = compute_alpha(entry, ts);
            if (alpha <= 0 || entry.name.empty()) continue;
            const double* cv = entry.smooth.current;
            float ix = (float)cv[0], iy = (float)cv[1], iz = (float)cv[2];
            if (vs.valid) {
                float dx = ix - vs.cam_pos[0];
                float dy = iy - vs.cam_pos[1];
                float dz = iz - vs.cam_pos[2];
                if (dx * vs.forward[0] + dy * vs.forward[1] + dz * vs.forward[2] < -0.5f) continue;
            }
            float sx, sy;
            if (!projection::world_to_screen(ix, iy + 0.3f + offset, iz, sx, sy)) continue;
            if (sx < -24.0f || sx > (float)sw + 24.0f || sy < -24.0f || sy > (float)sh + 24.0f) continue;
            float d2 = 0.0f;
            if (vs.valid) {
                float dx = ix - vs.cam_pos[0], dy = iy - vs.cam_pos[1], dz = iz - vs.cam_pos[2];
                d2 = dx * dx + dy * dy + dz * dz;
            }
            cs.push_back({d2, sx, sy, alpha, (int)kv.first, &entry});
        }
        if ((int)cs.size() > k_max_item_plates) {
            std::nth_element(cs.begin(), cs.begin() + k_max_item_plates, cs.end(),
                             [](const item_cand& a, const item_cand& b) { return a.d2 < b.d2; });
            cs.resize(k_max_item_plates);
        }
        for (const item_cand& c : cs) {
            float phase = (float)(c.id % 89) / 89.0f;
            draw_item_plate(dl, c.sx, c.sy - 3.0f, c.e->name.c_str(),
                            c.e->icon_r, c.e->icon_g, c.e->icon_b,
                            c.alpha, s_c0, s_c1, phase);
        }
    }
{ std::lock_guard<std::mutex> lock(esp_mutex); long long stale_threshold = k_grace_us + k_fade_out_us; for (auto it = g_players.begin(); it != g_players.end(); ) { if (it->second.last_seen_us <= 0) { it = g_players.erase(it); continue; } if (ts - it->second.last_seen_us > stale_threshold) it = g_players.erase(it); else ++it; } for (auto it = g_items.begin(); it !=
g_items.end(); ) { if (it->second.last_seen_us <= 0) { it = g_items.erase(it); continue; } if (ts - it->second.last_seen_us > stale_threshold) it = g_items.erase(it); else ++it; } for (auto& kv : pc) { auto it = g_players.find(kv.first); if (it == g_players.end()) g_players.emplace(kv.first, std::move(kv.second)); else for (int i = 0; i < 9; i++) it->second.smooth.current[i] = kv.second.smooth.current[i]; } for (auto& kv : ic) { auto it = g_items.find(kv.first); if (it == g_items.end()) g_items.emplace(kv.first, std::move(kv.second)); else for (int i = 0; i < 9; i++) it->second.smooth.current
[i] = kv.second.smooth.current[i]; } } } void flaway::modules::esp::cleanup() { g_players.clear(); g_items.clear(); g_name_cache.clear(); g_item_name_cache.clear(); g_scan_cache.clear(); { std::lock_guard<std::mutex> lock(esp_mutex); g_pickups.clear(); } if (g_last_world) { if (flaway::instance) { if (auto env = flaway::instance->get_env()) env->DeleteGlobalRef(g_last_world); } g_last_world = nullptr; } } std::unordered_map<int, esp_render_entry> flaway::modules::esp::snapshot_players() { std::lock_guard<std::mutex> lock(esp_mutex); return g_players; } esp_camera_data flaway::modules::esp::camera() { std::
lock_guard<std::mutex> lock(esp_mutex); return esp_cam; } bool flaway::modules::esp::snapshot_target(esp_render_entry& out) { std::lock_guard<std::mutex> lock(esp_mutex); for (auto& kv : g_players) { if (kv.second.is_target) { out = kv.second; return true; } } return false; } bool flaway::modules::esp::snapshot_entry(int id, esp_render_entry& out) { std::lock_guard<std::mutex> lock(esp_mutex); auto it = g_players.find(id); if (it == g_players.end()) return false; out = it->second; return true; } std::vector<esp_pickup_entry> flaway::modules::esp::pickups() { std::lock_guard<std::mutex> lock(
esp_mutex); return std::vector<esp_pickup_entry>(g_pickups.begin(), g_pickups.end()); }
