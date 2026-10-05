#include "GUI.h"
#include "../hooks/Hook.h"
#include "../globals/globals.h"
#include "../config/config.h"
#include "../utils/logger.h"
#include "../utils/rlog.h"
#include "../utils/discord_rpc.h"
#include "../../utils/imgui/imgui.h"
#include "../../utils/imgui/imgui_internal.h"
#include "../../utils/imgui/imgui_impl_opengl3.h"
#include "../../platform/linux/x11_helper.h"
#include "../flaway.h"
#include "../modules/esp/esp.h"
#include "../modules/storage_esp/storage_esp.h"
#include "../modules/base_finder/base_finder.h"
#include "../modules/backtrack/backtrack.h"
#include "../modules/aimassist/aimassist.h"
#include "../modules/friend_manager/friend_manager.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/entity/entity.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <initializer_list>
#include <chrono>
#include <unordered_map>
#include <dlfcn.h>
#include "flaway/utils/no_log.h"
#include "data/fonts.h"
#include "data/monocraft_regular.h"
#include "data/monocraft_bold.h"
#include "hud/hud.h"
#include <fcntl.h>
#include <unistd.h>
static int s_gui_diag_fd = -1;
static void gui_diag(const char* msg) {
    // Rate limit: some paths run every frame and would flood the file (two write()s per
    // frame = ~10MB/hour). Every distinct line is logged at most 5 times.
    static struct { char m[160]; int n; } seen[16];
    static int next_slot = 0;
    int slot = -1;
    for (int i = 0; i < 16; i++) if (seen[i].n > 0 && strcmp(seen[i].m, msg) == 0) { slot = i; break; }
    if (slot < 0) {
        for (int i = 0; i < 16; i++) if (seen[i].n == 0) { slot = i; break; }
        if (slot < 0) { slot = next_slot; next_slot = (next_slot + 1) % 16; }
        snprintf(seen[slot].m, sizeof(seen[slot].m), "%s", msg);
        seen[slot].n = 0;
    }
    if (++seen[slot].n > 5) return;
    if (s_gui_diag_fd < 0) {
        const char* home = getenv("HOME");
        if (!home) return;
        char path[512];
        snprintf(path, sizeof(path), "%s/.minecraft/flaway_diag.txt", home);
        s_gui_diag_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    }
    if (s_gui_diag_fd >= 0) { write(s_gui_diag_fd, msg, strlen(msg)); write(s_gui_diag_fd, "\n", 1); }
}
static bool g_is_init = false; static bool s_overlay_disabled = false; static const char* k_version = "1.21.10"; static ImFont* s_font_main = nullptr; static ImFont* s_font_small = nullptr; static ImFont* s_font_hud = nullptr; static ImFont* s_font_hud_bold = nullptr; static ImFont* s_font_bold = nullptr; static ImFont* s_font_tiny = nullptr; static ImFont* s_font_icons = nullptr; static ImFont* hud_font() { if (s_font_hud) return s_font_hud; if (s_font_main) return s_font_main; if (ImGui::GetCurrentContext()) return ImGui::GetIO().FontDefault; return nullptr; } static ImU32 u32_rgb(int r, int g,
int b) { return IM_COL32(r, g, b, 255); } static ImU32 u32_rgba(int r, int g, int b, int a) { if (a < 0) a = 0; if (a > 255) a = 255; return IM_COL32(r, g, b, a); } static ImVec4 v4_rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); } static ImU32 with_alpha(ImU32 c, int a) { if (a < 0) a = 0; if (a > 255) a = 255; return (c & 0x00FFFFFFu) | ((unsigned)a << 24); } static ImVec4 v4_u32(ImU32 c, float a = 1.0f) { return ImVec4((c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f, a * ((c >> 24) & 255) / 255.0f); } static ImU32
lerp_u32(ImU32 a, ImU32 b, float t) { if (t <= 0.0f) return a; if (t >= 1.0f) return b; int ar = (int)(a & 0xFF), ag = (int)((a >> 8) & 0xFF), ab = (int)((a >> 16) & 0xFF), aa = (int)((a >> 24) & 0xFF); int br = (int)(b & 0xFF), bg = (int)((b >> 8) & 0xFF), bb = (int)((b >> 16) & 0xFF), ba = (int)((b >> 24) & 0xFF); return IM_COL32( ar + (int)((br - ar) * t), ag + (int)((bg - ag) * t), ab + (int)((bb - ab) * t), aa + (int)((ba - aa) * t)); } static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); } static float ease_out_cubic(float t) { t = clamp01(t); return 1.0f - powf(1.0f
- t, 3.0f); } static long long now_us() { return std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now().time_since_epoch()).count(); } static ImU32 hsl_to_rgb(float h, float s, float l) { h = h - floorf(h); float r = l, g = l, b = l; if (s > 0.0f) { float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s; float p = 2.0f * l - q; auto hue2rgb = [](float pp, float qq, float t) -> float { if (t < 0.0f) t += 1.0f; if (t > 1.0f) t -= 1.0f; if (t < 1.0f / 6.0f) return pp + (qq - pp) * 6.0f * t; if (t < 0.5f) return qq; if (t < 2.0f / 3.0f) return pp + (qq - pp) * (2.0f
/ 3.0f - t) * 6.0f; return pp; }; r = hue2rgb(p, q, h + 1.0f / 3.0f); g = hue2rgb(p, q, h); b = hue2rgb(p, q, h - 1.0f / 3.0f); } return IM_COL32((int)(r * 255.0f), (int)(g * 255.0f), (int)(b * 255.0f), 255); } static bool s_dark_mode = true; struct MacPalette { ImU32 windowTop, windowBottom; ImU32 titleBg; ImU32 menuBg; ImU32 sidebarBg; ImU32 canvasBg; ImU32 cardBg; ImU32 cardHover; ImU32 inputBg; ImU32 hoverBg; ImU32 pressedBg; ImU32 separator; ImU32 text, textDim, textFaint; ImU32 accent, accentHover, accentActive; ImU32 chevron; ImU32 toggleOn, toggleOff, toggleKnob; ImU32 sliderTrack;
ImU32 scrollGrab; };
static auto frost = [](int a) { return IM_COL32(255, 255, 255, a); };
static const MacPalette k_dark = { IM_COL32(18, 19, 24, 255), IM_COL32(12, 13, 17, 255), IM_COL32(16, 17, 22, 255), IM_COL32(16, 17, 22, 255), IM_COL32(14, 15, 20, 255), IM_COL32(12, 13, 17, 255), IM_COL32(26, 28, 36, 180), IM_COL32(28, 30, 38, 255), IM_COL32(13, 14, 18, 255), frost(20), frost(35), frost(28), IM_COL32(232, 234, 240, 255), IM_COL32(145, 148, 162, 255), IM_COL32(90, 93, 108, 255), IM_COL32(80, 130, 255, 255), IM_COL32(110, 160, 255, 255), IM_COL32(60, 100, 210, 255), frost(145),
IM_COL32(48, 210, 88, 255), frost(35), IM_COL32(255, 255, 255, 255), frost(16), frost(44), }; static const MacPalette k_light = { IM_COL32(246, 246, 249, 255), IM_COL32(226, 226, 231, 255), IM_COL32(246, 246, 249, 255), IM_COL32(246, 246, 249, 255), IM_COL32(233, 233, 238, 255), IM_COL32(226, 226, 231, 255), IM_COL32(255, 255, 255, 255), IM_COL32(240, 240, 244, 255), IM_COL32(238, 238, 242, 255), IM_COL32(0, 0, 0, 16), IM_COL32(0, 0, 0, 30), IM_COL32(205, 205, 212, 255), IM_COL32(40, 40, 44, 255), IM_COL32(110, 110, 115, 255), IM_COL32(142, 142, 147, 255
), IM_COL32(0, 122, 255, 255), IM_COL32(26, 142, 255, 255), IM_COL32(0, 102, 224, 255), IM_COL32(60, 60, 67, 200), IM_COL32(48, 209, 88, 255), IM_COL32(199, 199, 204, 255), IM_COL32(255, 255, 255, 255), IM_COL32(216, 216, 222, 255), IM_COL32(165, 165, 172, 255), }; // --- theme swatches (Appearance page) --------------------------------------
// The accent swatch and the gradient preset drive the whole client: the menu
// palette accent, hstyle::grad_a/grad_b (HUD cards) and therefore the ESP
// boxes, tracers, name plates and arrows, which all derive from accent_a().
static const ImU32 k_accents[6] = {
    IM_COL32(80, 130, 255, 255), IM_COL32(120, 80, 255, 255),
    IM_COL32(255, 100, 130, 255), IM_COL32(48, 210, 88, 255),
    IM_COL32(255, 180, 40, 255), IM_COL32(0, 200, 220, 255) };
static const ImU32 k_grad_a[4] = { IM_COL32(80,130,255,255), IM_COL32(180,60,255,255),
                                   IM_COL32(255,80,120,255), IM_COL32(40,200,180,255) };
static const ImU32 k_grad_b[4] = { IM_COL32(130,80,255,255), IM_COL32(255,60,180,255),
                                   IM_COL32(255,160,40,255), IM_COL32(48,210,88,255) };
static const char* k_grad_names[4] = { "Ocean", "Sunset", "Fire", "Forest" };
static const int k_n_accents = 6;
static const int k_n_grads = 4;

// Accent shown by the theme: an active gradient preset wins (so the menu
// matches the HUD/ESP gradient), otherwise the accent swatch.
static ImU32 theme_accent_u32() {
    int g = globals::theme_grad;
    if (g >= 0 && g < k_n_grads) return k_grad_a[g];
    int a = globals::theme_accent;
    if (a < 0 || a >= k_n_accents) a = 0;
    return k_accents[a];
}

static MacPalette s_pal;
static const MacPalette& pal() {
    s_pal = s_dark_mode ? k_dark : k_light;
    ImU32 acc = theme_accent_u32();
    if (acc != s_pal.accent) {
        s_pal.accent = acc;
        s_pal.accentHover = lerp_u32(acc, IM_COL32(255, 255, 255, 255), 0.16f);
        s_pal.accentActive = lerp_u32(acc, IM_COL32(18, 19, 24, 255), 0.22f);
    }
    return s_pal;
}

bool GUI::gradient_pair(unsigned int* a, unsigned int* b) {
    int g = globals::theme_grad;
    if (g < 0 || g >= k_n_grads) return false;
    if (a) *a = (unsigned)k_grad_a[g];
    if (b) *b = (unsigned)k_grad_b[g];
    return true;
} static bool is_dark() { return s_dark_mode; } static ImU32 fallback_or(ImU32 themed, ImU32 fallback) { if (g_is_init) return themed; return fallback; } unsigned int GUI::accent_a() { return fallback_or(pal().accent, IM_COL32(88, 140, 255, 255)); } unsigned
int GUI::accent_b() { return fallback_or(pal().accent, IM_COL32(168, 96, 255, 255)); } unsigned int GUI::sidebar_a() { return fallback_or(pal().accent, IM_COL32(0, 122, 255, 255)); } unsigned int GUI::sidebar_b() { return fallback_or(pal().accent, IM_COL32(0, 122, 255, 255)); } unsigned int GUI::text_primary() { return fallback_or(pal().text, IM_COL32(232, 236, 242, 255)); } unsigned int GUI::text_dim() { return fallback_or(pal().textDim, IM_COL32(150, 155, 170, 255)); } unsigned int GUI::text_faint() { return fallback_or(pal().textFaint, IM_COL32(118, 126, 146, 255)); } unsigned int GUI::
border_color() { return fallback_or(pal().separator, IM_COL32(90, 100, 125, 120)); } unsigned int GUI::card_color() { return fallback_or(pal().cardBg, IM_COL32(24, 27, 35, 215)); } ImFont* GUI::font_hud() { if (s_font_hud) return s_font_hud; if (ImGui::GetCurrentContext()) return ImGui::GetIO().FontDefault; return nullptr; } ImFont* GUI::font_hud_bold() { if (s_font_hud_bold) return s_font_hud_bold; return GUI::font_hud(); } static ImVector<ImWchar> s_glyph_ranges; static ImVector<ImWchar> s_icon_ranges; static void load_fonts() { ImGuiIO& io = ImGui::GetIO(); ImFontAtlas* atlas = io.Fonts; gui_diag("[G] fonts: building ranges"); rlog::logf("fonts: building glyph ranges (cyrillic + default + special icons)"); rlog::logf("fonts: embedded data latin400=%zu cyr400=%zu latin700=%zu cyr700=%zu bytes", font_inter_latin_400_size, font_inter_cyrillic_400_size, font_inter_latin_700_size, font_inter_cyrillic_700_size); ImFontGlyphRangesBuilder bld; bld.AddRanges(atlas->GetGlyphRangesCyrillic()); bld.AddRanges(atlas->GetGlyphRangesDefault()); bld.AddChar(0x2013); bld.AddChar(0x2014); bld.AddChar(0x2022); bld.AddChar(0x2192); bld.AddChar(0x2318); bld.AddChar(0x25BC); bld.AddChar(0x25B6); bld.AddChar(0x25C6); bld.AddChar(0x2605); bld.AddChar(0x2694); bld.AddChar(0x269E); bld.AddChar(0x26A1); bld.AddChar(0x26C3); bld.AddChar(0x2713); bld.AddChar(0x2726); bld.AddChar(0x2716); bld.AddChar(0x279C); bld.BuildRanges(&s_glyph_ranges); { char buf[64]; snprintf(buf, sizeof(buf), "[G] fonts: text=%d ranges", s_glyph_ranges.Size); gui_diag(buf); } ImFontConfig latin_cfg; latin_cfg.FontDataOwnedByAtlas = false; ImFont* latin_font = atlas->AddFontFromMemoryTTF((void*)font_inter_latin_400, (int)font_inter_latin_400_size, 15.0f, &latin_cfg, s_glyph_ranges.Data); ImFontConfig cyrillic_cfg; cyrillic_cfg.FontDataOwnedByAtlas = false; cyrillic_cfg.MergeMode = true; atlas->AddFontFromMemoryTTF((void*)font_inter_cyrillic_400, (int)font_inter_cyrillic_400_size, 15.0f, &cyrillic_cfg, s_glyph_ranges.Data); ImFont* main_font = latin_font ? latin_font : atlas->AddFontDefault(); gui_diag("[G] fonts: main loaded from embedded Inter"); rlog::logf("fonts: base latin400 15px -> %p (%s)", (void*)latin_font, latin_font ? "embedded Inter" : "AddFontDefault FALLBACK"); ImFont* bold_font = nullptr; { ImFontConfig bold_latin; bold_latin.FontDataOwnedByAtlas = false; bold_font = atlas->AddFontFromMemoryTTF((void*)font_inter_latin_700, (int)font_inter_latin_700_size, 15.0f, &bold_latin, s_glyph_ranges.Data); ImFontConfig bold_cyrillic; bold_cyrillic.FontDataOwnedByAtlas = false; bold_cyrillic.MergeMode = true; atlas->AddFontFromMemoryTTF((void*)font_inter_cyrillic_700, (int)font_inter_cyrillic_700_size, 15.0f, &bold_cyrillic, s_glyph_ranges.Data); } if (!bold_font) bold_font = main_font; ImFont* small_font = nullptr; { ImFontConfig sm_latin; sm_latin.FontDataOwnedByAtlas = false; small_font = atlas->AddFontFromMemoryTTF((void*)font_inter_latin_400, (int)font_inter_latin_400_size, 12.0f, &sm_latin, s_glyph_ranges.Data); ImFontConfig sm_cyrillic; sm_cyrillic.FontDataOwnedByAtlas = false; sm_cyrillic.MergeMode = true; atlas->AddFontFromMemoryTTF((void*)font_inter_cyrillic_400, (int)font_inter_cyrillic_400_size, 12.0f, &sm_cyrillic, s_glyph_ranges.Data); } if (!small_font) small_font = main_font; ImFont* hud_font_ptr = nullptr; ImFont* hud_bold_ptr = nullptr; { ImFontConfig mc_reg; mc_reg.FontDataOwnedByAtlas = false; hud_font_ptr = atlas->AddFontFromMemoryTTF((void*)monocraft_regular_ttf, (int)monocraft_regular_ttf_size, 15.0f, &mc_reg, s_glyph_ranges.Data); ImFontConfig mc_m1; mc_m1.FontDataOwnedByAtlas = false; mc_m1.MergeMode = true; atlas->AddFontFromMemoryTTF((void*)font_inter_latin_400, (int)font_inter_latin_400_size, 15.0f, &mc_m1, s_glyph_ranges.Data); ImFontConfig mc_m2; mc_m2.FontDataOwnedByAtlas = false; mc_m2.MergeMode = true; atlas->AddFontFromMemoryTTF((void*)font_inter_cyrillic_400, (int)font_inter_cyrillic_400_size, 15.0f, &mc_m2, s_glyph_ranges.Data); } { ImFontConfig mc_bold; mc_bold.FontDataOwnedByAtlas = false; hud_bold_ptr = atlas->AddFontFromMemoryTTF((void*)monocraft_bold_ttf, (int)monocraft_bold_ttf_size, 15.0f, &mc_bold, s_glyph_ranges.Data); ImFontConfig mc_b1; mc_b1.FontDataOwnedByAtlas = false; mc_b1.MergeMode = true; atlas->AddFontFromMemoryTTF((void*)font_inter_latin_700, (int)font_inter_latin_700_size, 15.0f, &mc_b1, s_glyph_ranges.Data); ImFontConfig mc_b2; mc_b2.FontDataOwnedByAtlas = false; mc_b2.MergeMode = true; atlas->AddFontFromMemoryTTF((void*)font_inter_cyrillic_700, (int)font_inter_cyrillic_700_size, 15.0f, &mc_b2, s_glyph_ranges.Data); } if (!hud_font_ptr) hud_font_ptr = main_font; if (!hud_bold_ptr) hud_bold_ptr = hud_font_ptr; ImFont* tiny_font = nullptr; { ImFontConfig tn_latin; tn_latin.FontDataOwnedByAtlas = false; tiny_font = atlas->AddFontFromMemoryTTF((void*)font_inter_latin_400, (int)font_inter_latin_400_size, 10.0f, &tn_latin, s_glyph_ranges.Data); ImFontConfig tn_cyrillic; tn_cyrillic.FontDataOwnedByAtlas = false; tn_cyrillic.MergeMode = true; atlas->AddFontFromMemoryTTF((void*)font_inter_cyrillic_400, (int)font_inter_cyrillic_400_size, 10.0f, &tn_cyrillic, s_glyph_ranges.Data); } if (!tiny_font) tiny_font = small_font; s_font_icons = nullptr; s_font_main = main_font; s_font_bold = bold_font; s_font_small = small_font; s_font_hud = hud_font_ptr; s_font_hud_bold = hud_bold_ptr; s_font_tiny = tiny_font; rlog::logf("fonts: load_fonts done: atlas->Fonts=%d glyph_ranges=%d icon_ranges=%d", atlas->Fonts.Size, s_glyph_ranges.Size, s_icon_ranges.Size); rlog::logf("fonts: ptrs main=%p bold=%p small=%p hud=%p tiny=%p icons=%p", (void*)s_font_main, (void*)s_font_bold, (void*)s_font_small, (void*)s_font_hud, (void*)s_font_tiny, (void*)s_font_icons); if (!s_font_icons) rlog::logf("fonts: s_font_icons is NULL -> icon glyphs drawn with the main font"); if (!s_font_main) rlog::logf("fonts: CRITICAL s_font_main is NULL"); gui_diag("[G] fonts: loaded OK (embedded Inter)"); } static void setup_style() { ImGuiStyle& s = ImGui::GetStyle(); s.WindowRounding = 16.0f; s.ChildRounding = 13.0f; s.PopupRounding = 13.0f; s.FrameRounding = 10.0f
; s.GrabRounding = 10.0f; s.TabRounding = 10.0f; s.ScrollbarRounding = 12.0f; s.WindowBorderSize = 0.0f; s.FrameBorderSize = 0.0f; s.PopupBorderSize = 0.0f; s.WindowPadding = ImVec2(16, 16); s.FramePadding = ImVec2(10, 5); s.ItemSpacing = ImVec2(10, 8); s.ItemInnerSpacing = ImVec2(6, 5); s.WindowTitleAlign = ImVec2(0.5f, 0.5f); s.ScrollbarSize = 10.0f; s.GrabMinSize = 12.0f; s.AntiAliasedLines = true; s.AntiAliasedFill = true; const MacPalette& p = pal(); s.Colors[ImGuiCol_Text] = v4_u32(p.text); s.Colors[ImGuiCol_TextDisabled] = v4_u32(p.textFaint); s.Colors[ImGuiCol_WindowBg] = v4_rgb(0, 0, 0, 0)
; s.Colors[ImGuiCol_ChildBg] = v4_rgb(0, 0, 0, 0); s.Colors[ImGuiCol_PopupBg] = v4_u32(p.cardBg, 0.98f); s.Colors[ImGuiCol_Border] = v4_u32(p.separator); s.Colors[ImGuiCol_BorderShadow] = v4_rgb(0, 0, 0, 0); s.Colors[ImGuiCol_FrameBg] = v4_u32(p.inputBg); s.Colors[ImGuiCol_FrameBgHovered] = v4_u32(p.cardHover); s.Colors[ImGuiCol_FrameBgActive] = v4_u32(p.pressedBg); s.Colors[ImGuiCol_TitleBg] = v4_u32(p.titleBg); s.Colors[ImGuiCol_TitleBgActive] = v4_u32(p.titleBg); s.Colors[ImGuiCol_TitleBgCollapsed] = v4_u32(p.titleBg); s.Colors[ImGuiCol_MenuBarBg] = v4_u32(p.menuBg); s.Colors[
ImGuiCol_ScrollbarBg] = v4_rgb(0, 0, 0, 0); s.Colors[ImGuiCol_ScrollbarGrab] = v4_u32(p.scrollGrab); s.Colors[ImGuiCol_ScrollbarGrabHovered] = v4_u32(p.scrollGrab); s.Colors[ImGuiCol_ScrollbarGrabActive] = v4_u32(p.accent); s.Colors[ImGuiCol_CheckMark] = v4_rgb(255, 255, 255); s.Colors[ImGuiCol_SliderGrab] = v4_u32(p.accent); s.Colors[ImGuiCol_SliderGrabActive] = v4_u32(p.accentHover); s.Colors[ImGuiCol_Button] = v4_u32(p.cardBg); s.Colors[ImGuiCol_ButtonHovered] = v4_u32(p.cardHover); s.Colors[ImGuiCol_ButtonActive] = v4_u32(p.pressedBg); s.Colors[ImGuiCol_Header] = v4_rgb(0, 0, 0, 0); s.
Colors[ImGuiCol_HeaderHovered] = v4_u32(p.hoverBg); s.Colors[ImGuiCol_HeaderActive] = v4_u32(p.pressedBg); s.Colors[ImGuiCol_Separator] = v4_u32(p.separator); s.Colors[ImGuiCol_SeparatorHovered] = v4_u32(p.accent); s.Colors[ImGuiCol_SeparatorActive] = v4_u32(p.accentActive); s.Colors[ImGuiCol_ResizeGrip] = v4_u32(p.separator); s.Colors[ImGuiCol_ResizeGripHovered] = v4_u32(p.accent); s.Colors[ImGuiCol_ResizeGripActive] = v4_u32(p.accentActive); s.Colors[ImGuiCol_Tab] = v4_u32(p.cardBg); s.Colors[ImGuiCol_TabHovered] = v4_u32(p.cardHover); s.Colors[ImGuiCol_TabActive] = v4_u32(p.accent); s.
Colors[ImGuiCol_TabUnfocused] = v4_u32(p.cardBg); s.Colors[ImGuiCol_TabUnfocusedActive] = v4_u32(p.cardHover); s.Colors[ImGuiCol_TextSelectedBg] = v4_u32(p.accent, 0.28f); s.Colors[ImGuiCol_PlotHistogram] = v4_u32(p.accent); s.Colors[ImGuiCol_PlotHistogramHovered] = v4_u32(p.accentHover); s.Colors[ImGuiCol_ModalWindowDimBg] = v4_rgb(0, 0, 0, is_dark() ? 0.45f : 0.18f); } bool GUI::init() { if (g_is_init) return true; gui_diag("[G] init: starting"); rlog::logf("init: starting, version=%s", k_version); if (ImGui::GetCurrentContext()) { rlog::logf("init: destroying pre-existing ImGui context (re-init)"); ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext(); } IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr; io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; gui_diag("[G] init: loading fonts..."); load_fonts(); gui_diag("[G] init: fonts loaded, setting up style..."); setup_style(); gui_diag("[G] init: style OK, calling ImGui_ImplOpenGL3_Init..."); rlog::logf("init: fonts loaded + style set, calling ImGui_ImplOpenGL3_Init(\"#version 150\")");
    // hook runs on Minecraft's render thread with the context active).
    if (!ImGui_ImplOpenGL3_Init("#version 150")) { gui_diag("[G] init: ImGui_ImplOpenGL3_Init FAILED"); rlog::logf("init: ImGui_ImplOpenGL3_Init FAILED (glsl '#version 150')"); logger::log("GUI::init FAILED: ImGui_ImplOpenGL3_Init"); return false; } gui_diag("[G] init: ImGui_ImplOpenGL3_Init OK"); rlog::logf("init: ImGui_ImplOpenGL3_Init OK (glsl '#version 150'), font atlas texID=%p built=%d", (void*)ImGui::GetIO().Fonts->TexID, (int)ImGui::GetIO().Fonts->IsBuilt()); g_is_init = true; logger::log("GUI::init OK (OpenGL3 backend)");
return true; } bool GUI::get_is_init() { return g_is_init; } bool GUI::needs_overlay() { if (s_overlay_disabled) return false; if (!g_is_init) return false; if (globals::show_gui) return true; if (flaway::hud::wants_overlay()) return true; if (globals::box_enabled || globals::esp_health_bar || globals::esp_name_enabled || globals::esp_item_enabled || globals::esp_tracers || globals::esp_arrows) return true; if (globals::storage_esp_enabled) return true; if (globals::base_finder_enabled) return true; if (globals::
backtrack_visualization_enabled) return true; if (globals::aimassist_enabled && globals::esp_show_fov) return true; return false; } void GUI::shutdown() { if (g_is_init) { rlog::logf("shutdown: GUI::shutdown called (frame=%llu)", rlog::frame_no()); flaway::hud::shutdown(); ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext(); g_is_init = false; s_font_main = nullptr; s_font_small = nullptr; s_font_hud = nullptr; s_font_hud_bold = nullptr; s_font_bold = nullptr; s_font_tiny = nullptr; } } static bool key_waiting = false; static int* waiting_key = nullptr; bool GUI::is_keybind_waiting() { if (!g_is_init) return false; if (!globals::show_gui) return false;
return key_waiting; } void GUI::cancel_keybind_capture() { key_waiting = false; waiting_key = nullptr; } std::string GUI::key_name(int key) { if (key <= 0) return "\xE2\x80\x94"; if (key >= VK_F1 && key <= VK_F12) { static const char* f[] = {"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"}; return f[key - VK_F1]; } if (key >= 'A' && key <= 'Z') return std::string(1, (char)key); if (key >= '0' && key <= '9') return std::string(1, (char)key); switch (key) { case VK_RSHIFT: return "RShift"; case VK_LSHIFT: return "LShift"; case VK_LCONTROL: return "LCtrl"; case VK_RCONTROL:
return "RCtrl"; case VK_LMENU: return "LAlt"; case VK_RMENU: return "RAlt"; case VK_RETURN: return "Enter"; case VK_ESCAPE: return "Esc"; case VK_SPACE: return "Space"; case VK_TAB: return "Tab"; case VK_BACK: return "Backspace"; default: return "Key"; } } static std::string key_name(int key) { return GUI::key_name(key); } static void update_keybind_waiting() { if (!key_waiting || !waiting_key) return; int new_key = x11_helper::poll_new_key_press();
if (new_key > 0) { if (new_key == VK_ESCAPE) { key_waiting = false; waiting_key = nullptr; return; } *waiting_key = new_key; key_waiting = false; waiting_key = nullptr; return; } for (auto& evt : x11_helper::consume_mouse_events()) { if (evt.down && (evt.button == 0 || evt.button == 1)) {
key_waiting = false; waiting_key = nullptr; return; } } } static ImGuiKey vk_to_imgui_key(int vk) { if (vk >= 'A' && vk <= 'Z') return (ImGuiKey)(ImGuiKey_A + (vk - 'A')); if (vk >= '0' && vk <= '9') return (ImGuiKey)(ImGuiKey_0 + (vk - '0')); if (vk >= VK_F1 && vk <= VK_F12) return (ImGuiKey)(ImGuiKey_F1 + (vk - VK_F1)); if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return (ImGuiKey)(ImGuiKey_Keypad0 + (vk - VK_NUMPAD0)); switch (vk) { case VK_LSHIFT: return ImGuiKey_LeftShift; case VK_RSHIFT: return ImGuiKey_RightShift; case VK_LCONTROL: return ImGuiKey_LeftCtrl; case VK_RCONTROL: return
ImGuiKey_RightCtrl; case VK_LMENU: return ImGuiKey_LeftAlt; case VK_RMENU: return ImGuiKey_RightAlt; case VK_RETURN: return ImGuiKey_Enter; case VK_ESCAPE: return ImGuiKey_Escape; case VK_TAB: return ImGuiKey_Tab; case VK_BACK: return ImGuiKey_Backspace; case VK_SPACE: return ImGuiKey_Space; case VK_LEFT: return ImGuiKey_LeftArrow; case VK_RIGHT: return ImGuiKey_RightArrow; case VK_UP: return ImGuiKey_UpArrow; case VK_DOWN: return ImGuiKey_DownArrow; case VK_HOME: return ImGuiKey_Home; case VK_END: return ImGuiKey_End; case VK_INSERT: return ImGuiKey_Insert; case VK_DELETE: return
ImGuiKey_Delete; case VK_PRIOR: return ImGuiKey_PageUp; case VK_NEXT: return ImGuiKey_PageDown; default: return ImGuiKey_None; } } static void feed_keys_to_imgui() { static int s_prev_keys[128]; ImGuiIO& io = ImGui::GetIO(); std::vector<int> held = x11_helper::held_keys_snapshot(); for (int i = 0; i < 128; i++) { int vk = i < 26 ? ('A' + i) : i < 36 ? ('0' + (i - 26)) : i < 48 ? (VK_F1 + (i - 36)) : i < 58 ? (VK_NUMPAD0 + (i - 48)) : -1; ImGuiKey key = vk >= 0 ? vk_to_imgui_key(vk) : ImGuiKey_None; bool down = (key != ImGuiKey_None) && x11_helper::is_key_held_fast(vk, held); if (down != (
s_prev_keys[i] != 0)) { io.AddKeyEvent(key, down); s_prev_keys[i] = down ? 1 : 0; } } } static bool s_reset_input = false; static void feed_input_to_imgui(bool gui_shown) { ImGuiIO& io = ImGui::GetIO(); if (s_reset_input) { io.AddMouseButtonEvent(0, false); io.AddMouseButtonEvent(1, false); io.AddMouseButtonEvent(2, false); io.AddMouseWheelEvent(0.0f, 0.0f); s_reset_input = false; } x11_helper::InputData input = x11_helper::poll_input(); if (gui_shown) { int cx, cy; if (x11_helper::poll_cursor_window(&cx, &cy)) io.AddMousePosEvent((float)cx, (float)cy); else io.AddMousePosEvent(input.mouse_x,
input.mouse_y); } else { io.AddMousePosEvent(input.mouse_x, input.mouse_y); } io.AddMouseWheelEvent(0.0f, input.wheel); for (int i = 0; i < 3; i++) { io.AddMouseButtonEvent(i, input.mouse_down[i] != 0); } for (auto& evt : x11_helper::consume_mouse_events()) { io.AddMouseButtonEvent(evt.button, evt.down); } if (input.key_char > 0) { io.AddInputCharacter(input.key_char); } feed_keys_to_imgui(); x11_helper::drain_key_presses(); } static void draw_switch(ImDrawList* dl, bool on, float anim, const ImVec2& tmin, float track_w, float track_h) { ImVec2 tmax(tmin.x + track_w, tmin.y + track_h); ImU32
track = lerp_u32(pal().toggleOff, pal().toggleOn, anim); dl->AddRectFilled(tmin, tmax, track, track_h * 0.5f); float knob_d = track_h - 4.0f; float kx = tmin.x + 2.0f + (track_w - knob_d - 4.0f) * anim; float ky = tmin.y + (track_h - knob_d) * 0.5f; ImVec2 kc(kx + knob_d * 0.5f, ky + knob_d * 0.5f); dl->AddCircleFilled(ImVec2(kc.x, kc.y + 1.0f), knob_d * 0.5f + 0.5f, IM_COL32(0, 0, 0, 60), 24); dl->AddCircleFilled(kc, knob_d * 0.5f, pal().toggleKnob, 24); dl->AddCircle(kc, knob_d * 0.5f, frost(200), 24, 0.8f)
; } // Rows share one label column so sliders, combos and keybind buttons all
// start at the same x. A label that does not fit wraps onto extra lines (the
// row grows with it) instead of being clipped at the card edge.
static const float k_label_col_w = 130.0f;
static const float k_combo_max_w = 170.0f;
struct LabelBox {
	float col;      // label column width: the control starts at pos.x + col
	float wrap_w;   // max line width inside the column
	float label_h;  // wrapped label height
	float row_h;    // row height (grows when the label wraps)
};
static LabelBox label_box(const char* label, float avail_w, float min_row_h)
{
	LabelBox lb;
	float max_col = avail_w * 0.45f;
	if (max_col < 70.0f) max_col = 70.0f;
	lb.col = (k_label_col_w < max_col) ? k_label_col_w : max_col;
	lb.wrap_w = lb.col - 6.0f;
	ImVec2 ts = ImGui::CalcTextSize(label, nullptr, false, lb.wrap_w);
	lb.label_h = ts.y;
	lb.row_h = (min_row_h > ts.y + 6.0f) ? min_row_h : ts.y + 6.0f;
	return lb;
}
static void draw_row_label(const char* label, const LabelBox& lb, const ImVec2& pos, ImU32 col)
{
	if (!label || !*label) return;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 lp(pos.x, pos.y + (lb.row_h - lb.label_h) * 0.5f);
	dl->PushClipRect(ImVec2(pos.x - 2.0f, pos.y), ImVec2(pos.x + lb.col - 4.0f, pos.y + lb.row_h), true);
	dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), lp, col, label, nullptr, lb.wrap_w);
	dl->PopClipRect();
}
// TextColored that wraps: long lines break inside the card instead of being
// cut off by the card's clip rect.
static void TextWrapColored(const ImVec4& col, const char* fmt, ...) {
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	ImGui::PushStyleColor(ImGuiCol_Text, col);
	ImGui::TextWrapped("%s", buf);
	ImGui::PopStyleColor();
}
static bool ToggleSwitch(const char* label, bool* v) { ImGui::PushID(label); ImDrawList* dl = ImGui::GetWindowDrawList(); const float track_w = 40.0f, track_h = 24.0f; float avail_w = ImGui::GetContentRegionAvail().x; float text_w = avail_w - track_w - 10.0f; if (text_w < 40.0f) text_w = 40.0f; ImVec2 ts = ImGui::CalcTextSize(label, nullptr, false, text_w); const float row_h = std::max(ImGui::GetFrameHeight() + 4.0f, ts.y + 6.0f); ImVec2 pos = ImGui::GetCursorScreenPos(); const float row_w = track_w + 10.0f + ts.x; ImGui::InvisibleButton("##tr", ImVec2(row_w, row_h)); bool hovered = ImGui::IsItemHovered(); bool clicked = ImGui::IsItemClicked(0); if (clicked) *v = !*v; ImGuiID anim_id = ImGui::GetID("##tan"); float a = ImGui::GetStateStorage()->
GetFloat(anim_id, *v ? 1.0f : 0.0f); float target = *v ? 1.0f : 0.0f; a += (target - a) * std::min(1.0f, ImGui::GetIO().DeltaTime * 16.0f); if (fabsf(target - a) < 0.001f) a = target; ImGui::GetStateStorage()->SetFloat(anim_id, a); ImVec2 tm(pos.x, pos.y + (row_h - track_h) * 0.5f); draw_switch(dl, *v, a, tm, track_w, track_h); dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + track_w + 10.0f, pos.y + (row_h - ts.y) * 0.5f), with_alpha(pal().text, 250), label, nullptr, text_w); ImGui::PopID(); return clicked; } static void MiniToggle(bool* v) { ImDrawList* dl = ImGui::GetWindowDrawList(); const float track_w = 30.0f
, track_h = 17.0f; ImVec2 pos = ImGui::GetCursorScreenPos(); ImGuiID anim_id = ImGui::GetID("##mt"); float a = ImGui::GetStateStorage()->GetFloat(anim_id, *v ? 1.0f : 0.0f); float target = *v ? 1.0f : 0.0f; a += (target - a) * std::min(1.0f, ImGui::GetIO().DeltaTime * 16.0f); if (fabsf(target - a) < 0.001f) a = target; ImGui::GetStateStorage()->SetFloat(anim_id, a); draw_switch(dl, *v, a, pos, track_w, track_h); ImGui::Dummy(ImVec2(track_w, track_h)); } static bool SubRow(const char* label, bool* v) { ImGui::PushID(label); ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 p0 = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x; float text_w = w - 46.0f; if (text_w < 40.0f) text_w = 40.0f; ImVec2 sub_ts = ImGui::CalcTextSize(label, nullptr, false, text_w); float row_h = std::max(30.0f, sub_ts.y + 8.0f); ImVec2 p1(p0.x + w, p0.y + row_h); ImGui::InvisibleButton("##sr", ImVec2(w, row_h)); bool hovered = ImGui::IsItemHovered(); bool clicked = ImGui::IsItemClicked(0); if (clicked && hovered) *v = !*v; if (hovered) dl->AddRectFilled(p0, p1, frost(is_dark() ? 18 : 30), 6.0f); if (*v) { dl->AddRectFilled(ImVec2(p0.x, p0.y + 6), ImVec2(p0.x + 2.5f, p1.y - 6), with_alpha(pal().accent, 120), 1.5f); } dl->PushClipRect(ImVec2(p0.x, p0.y), ImVec2(p1.x - 40.0f, p1.y), true); dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p0.x + 6, p0.y + (row_h - sub_ts.y) * 0.5f), with_alpha(pal().text, 245), label, nullptr, text_w); dl->PopClipRect(); ImGui::SetCursorScreenPos(ImVec2(p1.x - 34, p0.y + (row_h - 17.0f) * 0.5f)); MiniToggle(v); ImGui::
SetCursorScreenPos(p0); ImGui::Dummy(ImVec2(w, row_h)); ImGui::PopID(); return clicked; } static bool CustomCheckbox(const char* label, bool* v) { ImGui::PushID(label); ImDrawList* dl = ImGui::GetWindowDrawList(); const float size = 17.0f; float cb_avail = ImGui::GetContentRegionAvail().x; float cb_text_w = cb_avail - size - 8.0f; if (cb_text_w < 40.0f) cb_text_w = 40.0f; ImVec2 cb_ts = ImGui::CalcTextSize(label, nullptr, false, cb_text_w); const float row_h = std::max(std::max(ImGui::GetFontSize(), size) + 6.0f, cb_ts.y + 6.0f); const float row_w = size + 8.0f + cb_ts.x; ImVec2 pos = ImGui::GetCursorScreenPos(); ImGui::InvisibleButton("##cb", ImVec2(row_w, row_h)); bool hovered = ImGui::IsItemHovered(); bool clicked = ImGui::IsItemClicked(0); if (clicked) *v = !*v;
ImVec2 c0(pos.x, pos.y + (row_h - size) * 0.5f); ImVec2 c1(c0.x + size, c0.y + size); const float r = 4.5f; if (*v) { dl->AddRectFilled(c0, c1, pal().accent, r); dl->AddLine(ImVec2(c0.x + 3.6f, c0.y + size * 0.53f), ImVec2(c0.x + 7.0f, c0.y + size * 0.80f), IM_COL32(255, 255, 255, 255), 2.2f); dl->AddLine(ImVec2(c0.x + 7.0f, c0.y + size * 0.80f), ImVec2(c1.x - 3.4f, c0.y + 3.2f), IM_COL32(255, 255, 255, 255), 2.2f); } else { dl->AddRectFilled(c0, c1, pal().inputBg, r); dl->AddRect(c0, c1, with_alpha(pal().separator, 200), r, 0, 1.0f); } if (hovered && !(*v)) dl->AddRect(c0, c1, with_alpha(pal
().accent, 120), r, 0, 1.2f); dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(c1.x + 8.0f, pos.y + (row_h - cb_ts.y) * 0.5f), with_alpha(pal().text, 250), label, nullptr, cb_text_w); ImGui::PopID(); return clicked; } static bool CustomSliderFloat(const char* label, float* v, float v_min, float v_max, const char* fmt = "%.1f", bool as_int = false) { ImGui::PushID(label); ImDrawList* dl = ImGui::GetWindowDrawList(); float avail_w = ImGui::GetContentRegionAvail().x; const LabelBox lb = label_box(label ? label : "", avail_w, 26.0f); const float row_h = lb.row_h; ImVec2 pos = ImGui::GetCursorScreenPos(); ImGui::InvisibleButton("##sl", ImVec2(avail_w, row_h)); bool hovered = ImGui::IsItemHovered(); bool active = ImGui::
IsItemActive(); const float label_w = (label && *label) ? lb.col : 0.0f; char buf[32]; if (as_int) snprintf(buf, sizeof(buf), fmt, (int)lrintf(*v)); else snprintf(buf, sizeof(buf), fmt, (double)*v); const float value_w = ImGui::CalcTextSize(buf).x + 16.0f; float track_x = pos.x + label_w; float track_w = avail_w - label_w - value_w - 6.0f; if (track_w < 40.0f) track_w = 40.0f; if (track_x + track_w > pos.x + avail_w) track_w = std::max(0.0f, pos.x + avail_w - track_x); const float track_h = 6.0f; const float track_y = pos.y + (row_h - track_h) * 0.5f; float t = (v_max != v_min) ? (*v - v_min) / (v_max - v_min) : 0.0f; t = clamp01(t); if (active) { float mx = (ImGui::GetIO().MousePos.x - track_x) / track_w; t = clamp01(mx); *v = v_min + t * (v_max - v_min
); } if (hovered && !active && ImGui::GetIO().MouseWheel != 0.0f) { *v += ImGui::GetIO().MouseWheel * (v_max - v_min) * 0.02f; if (*v < v_min) *v = v_min; if (*v > v_max) *v = v_max; t = (v_max != v_min) ? (*v - v_min) / (v_max - v_min) : 0.0f; } draw_row_label(label, lb, pos, with_alpha(pal().text, 235)); ImVec2 t0(track_x, track_y); ImVec2 t1(t0.x + track_w, t0.y + track_h); dl->AddRectFilled(t0, t1, pal().sliderTrack, track_h * 0.5f); if (t > 0.004f) { ImVec2 f1(t0.x + track_w * t, t1.y); dl->AddRectFilled(t0, f1, pal()
.accent, track_h * 0.5f); } const float knob_r = (hovered || active) ? 8.5f : 7.5f; float gx = t0.x + track_w * t; float gy = (t0.y + t1.y) * 0.5f; dl->AddCircleFilled(ImVec2(gx, gy + 1.0f), knob_r, IM_COL32(0, 0, 0, 80), 24); dl->AddCircleFilled(ImVec2(gx, gy), knob_r, pal().toggleKnob, 24); dl->AddCircle(ImVec2(gx, gy), knob_r, frost(100), 24, 0.8f); if (as_int) snprintf(buf, sizeof(buf), fmt, (int)lrintf(*v)); else snprintf(buf, sizeof(buf), fmt, (double)*v); ImVec2 vs = ImGui::CalcTextSize(buf); float value_x = t0.x + track_w + 8.0f; if (value_x + vs.x > pos.x + avail_w) value_x = pos.x + avail_w - vs.x - 2.0f; dl->AddText(ImVec2(value_x, pos.y + (row_h - ImGui::GetFontSize()) * 0.5f), with_alpha(pal().textDim, 215), buf); if (
hovered || active) { float bw = vs.x + 14.0f, bh = 19.0f; ImVec2 bp0(gx - bw * 0.5f, track_y - bh - 8.0f); ImVec2 bp1(bp0.x + bw, bp0.y + bh); dl->AddRectFilled(ImVec2(bp0.x + 1, bp0.y + 2), ImVec2(bp1.x + 1, bp1.y + 2), IM_COL32(0, 0, 0, 70), 5.0f); dl->AddRectFilled(bp0, bp1, pal().accent, 5.0f); dl->AddText(ImVec2(bp0.x + (bw - vs.x) * 0.5f, bp0.y + (bh - vs.y) * 0.5f), IM_COL32(255, 255, 255, 255), buf); } ImGui::PopID(); return active || hovered; } static bool CustomSliderInt(const char* label, int* v, int v_min, int v_max, const char* fmt = "%d") { float f = (float)*v; bool r =
CustomSliderFloat(label, &f, (float)v_min, (float)v_max, fmt, true); int nv = (int)roundf(f); if (nv < v_min) nv = v_min; if (nv > v_max) nv = v_max; if (nv != *v) { *v = nv; r = true; } return r; } static void SliderDouble(const char* label, double* v, double v_min, double v_max, const char* fmt = "%.2f") { float f = (float)*v; CustomSliderFloat(label, &f, (float)v_min, (float)v_max, fmt); double nv = (double)f; if (nv < v_min) nv = v_min; if (nv > v_max) nv = v_max; *v = nv; } static void CustomKeybind(const char* label, int* key) { ImGui::PushID(label); ImDrawList* dl = ImGui::GetWindowDrawList(
); float avail_w = ImGui::GetContentRegionAvail().x; float w = 118.0f; if (w > avail_w) w = std::max(40.0f, avail_w); const LabelBox lb = label_box(label ? label : "", avail_w, 26.0f); const float h = lb.row_h; ImVec2 pos = ImGui::GetCursorScreenPos(); ImVec2 b0(pos.x + std::max(0.0f, avail_w - w), pos.y + (h - 26.0f) * 0.5f); ImVec2 b1(b0.x + w, b0.y + 26.0f); if (label && *label) { ImVec2 lp(pos.x, pos.y + (h - lb.label_h) * 0.5f); float clip_x = pos.x + lb.col - 4.0f; if (clip_x > b0.x - 6.0f) clip_x = b0.x - 6.0f; if (clip_x < pos.x - 2.0f) clip_x = pos.x - 2.0f; dl->PushClipRect(ImVec2(pos.x - 2.0f, pos.y), ImVec2(clip_x, pos.y + h), true); dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), lp, with_alpha(pal().text, 235), label, nullptr, lb.wrap_w); dl->PopClipRect(); } ImGui::SetCursorScreenPos(b0); ImGui::InvisibleButton("##kb", ImVec2(w, 26.0f)); bool hovered = ImGui::IsItemHovered(); if (ImGui::IsItemClicked(0)) { key_waiting = true; waiting_key = key; } bool waiting = key_waiting &&
waiting_key == key; ImGuiID kanim = ImGui::GetID("##kba"); float ka = ImGui::GetStateStorage()->GetFloat(kanim, 0.0f); float ktarget = waiting ? 1.0f : 0.0f; ka += (ktarget - ka) * std::min(1.0f, ImGui::GetIO().DeltaTime * 12.0f); if (fabsf(ktarget - ka) < 0.001f) ka = ktarget; ImGui::GetStateStorage()->SetFloat(kanim, ka); ImU32 bgc = ImColor(lerp_u32(waiting ? pal().accent : (hovered ? pal().cardHover : pal().inputBg), pal().accent, ka)); dl->AddRectFilled(b0, b1, bgc, 6.0f); dl->AddRect(b0, b1, with_alpha(waiting ? IM_COL32(255,255,255,255) : pal().separator, 170), 6.0f, 0, 1.0f); std::string ktxt = waiting ? "Press..." : key_name(*key); ImVec2 vs = ImGui::CalcTextSize(ktxt.c_str()); dl->AddText(ImVec2(b0.x + (w - vs.x) * 0.5f, b0.y + (26.0f - vs.y) * 0.5f), waiting ? IM_COL32(255,255,255,255) : (hovered ? with_alpha(pal().text,245) : with_alpha(pal().textDim,210)), ktxt.c_str()); ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h)); ImGui::Dummy(ImVec2(0, 0)); ImGui::PopID(); } static std::vector<std::string>
s_combo_items; static int* s_combo_ptr = nullptr; static ImVec2 s_combo_anchor(-1, -1); static float s_combo_width = 0.0f; static bool s_combo_open = false; static bool s_combo_just_opened = false; static int s_combo_sel = 0; static ImVec2 s_content_rmin(0.0f, 0.0f); static ImVec2 s_content_rmax(0.0f, 0.0f); static void draw_combo_overlay(float alpha) {
	if (!s_combo_open || !s_combo_ptr || s_combo_items.empty()) {
		s_combo_just_opened = false;
		return;
	}
	// Scrolling shifts the row the popup points at - close it instead of
	// leaving it dangling over nothing.
	if (ImGui::GetIO().MouseWheel != 0.0f && !s_combo_just_opened) {
		s_combo_open = false;
		s_combo_just_opened = false;
		return;
	}
	float total_h = (float)s_combo_items.size() * 26.0f;
	float w = s_combo_width;
	if (w < 40.0f) w = 40.0f;
	const float k_gap = 4.0f;
	const float box_h = 26.0f;

	// Open upward while there is room, downward otherwise, then clamp inside
	// the content area so the popup never spills out of the menu.
	ImVec2 rmin = s_content_rmin;
	ImVec2 rmax = s_content_rmax;
	float top_space = s_combo_anchor.y - k_gap - rmin.y;
	float bot_space = rmax.y - (s_combo_anchor.y + box_h + k_gap);
	float o0y = (top_space >= total_h && top_space >= bot_space)
		? s_combo_anchor.y - total_h - k_gap
		: s_combo_anchor.y + box_h + k_gap;
	float o0x = s_combo_anchor.x + 4.0f;
	if (o0x + w > rmax.x - 2.0f) o0x = rmax.x - 2.0f - w;
	if (o0x < rmin.x + 2.0f) o0x = rmin.x + 2.0f;
	if (o0y < rmin.y + 2.0f) o0y = rmin.y + 2.0f;
	if (o0y + total_h > rmax.y - 2.0f) o0y = rmax.y - 2.0f - total_h;
	if (o0y < rmin.y + 2.0f) o0y = rmin.y + 2.0f;

	ImVec2 mouse = ImGui::GetMousePos();
	bool inside = mouse.x >= o0x && mouse.x <= o0x + w && mouse.y >= o0y && mouse.y <= o0y + total_h;
	const bool just_opened = s_combo_just_opened;
	s_combo_just_opened = false;
	if (!just_opened && !inside && ImGui::IsMouseClicked(0)) {
		s_combo_open = false;
		return;
	}

	// Shadow: only the strips outside the popup box, on the foreground list so
	// it lies over the menu but never over the popup itself.
	ImDrawList* fdl = ImGui::GetForegroundDrawList();
	ImU32 sh = IM_COL32(0, 0, 0, (int)(110 * alpha));
	fdl->AddRectFilled(ImVec2(o0x + 4.0f, o0y + total_h), ImVec2(o0x + w + 4.0f, o0y + total_h + 6.0f), sh, 6.0f);
	fdl->AddRectFilled(ImVec2(o0x + w, o0y + 6.0f), ImVec2(o0x + w + 4.0f, o0y + total_h + 6.0f), sh, 6.0f);

	// Its own top-level window: drawn above the menu and it owns the mouse
	// while open, so widgets underneath cannot steal the click.
	ImGui::SetNextWindowPos(ImVec2(o0x, o0y), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(w, total_h), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3.0f, 3.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(20, 22, 28, 245));
	ImGui::Begin("##combo_popup", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	for (size_t i = 0; i < s_combo_items.size(); i++) {
		ImVec2 r0(o0x + 3.0f, o0y + (float)i * 26.0f + 3.0f);
		ImVec2 r1(o0x + w - 3.0f, r0.y + 20.0f);
		ImGui::SetCursorScreenPos(r0);
		ImGui::InvisibleButton((std::string("##ci") + std::to_string(i)).c_str(), ImVec2(r1.x - r0.x, r1.y - r0.y));
		bool hv = ImGui::IsItemHovered();
		bool sel = (int)i == s_combo_sel;
		if (ImGui::IsItemClicked(0)) {
			*s_combo_ptr = (int)i;
			s_combo_open = false;
		}
		if (sel) dl->AddRectFilled(r0, r1, pal().accent, 5.0f);
		else if (hv) dl->AddRectFilled(r0, r1, pal().hoverBg, 5.0f);
		ImU32 tcol = sel ? IM_COL32(255, 255, 255, 255) : with_alpha(pal().text, 240);
		ImVec2 chk = ImGui::CalcTextSize("\xE2\x9C\x93");
		float text_max = r1.x - 9.0f - (sel ? chk.x + 8.0f : 0.0f);
		dl->PushClipRect(ImVec2(r0.x + 4.0f, r0.y), ImVec2(text_max, r1.y), true);
		dl->AddText(ImVec2(r0.x + 9.0f, r0.y + (20.0f - ImGui::GetFontSize()) * 0.5f), tcol, s_combo_items[i].c_str());
		dl->PopClipRect();
		if (sel) dl->AddText(ImVec2(r1.x - chk.x - 8.0f, r0.y + (20.0f - chk.y) * 0.5f), IM_COL32(255, 255, 255, 255), "\xE2\x9C\x93");
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);
}
static bool ComboMode(const char* label, int* v, const char* items) { std::vector<std::string> parts; const char* p = items; while (*p) { parts.push_back(p); p += strlen(p) + 1; } if (parts.size() < 2) return false; ImGui::PushID(label); ImDrawList* dl = ImGui::GetWindowDrawList(); const float avail_w = ImGui::GetContentRegionAvail().x; const LabelBox lb = label_box(label ? label : "", avail_w, 26.0f); const float row_h = lb.row_h; ImVec2
pos = ImGui::GetCursorScreenPos(); float bx = pos.x + lb.col; float bw = avail_w - lb.col; if (bw > k_combo_max_w) bw = k_combo_max_w; if (bw < 40.0f) bw = 40.0f; if (bx + bw > pos.x + avail_w) bw = std::max(0.0f, pos.x + avail_w - bx); ImVec2 b0(bx, pos.y + (row_h - 26.0f) * 0.5f); ImVec2 b1(b0.x + bw, b0.y + 26.0f); ImGui::SetCursorScreenPos(b0); bool clicked = ImGui::InvisibleButton("##combo", ImVec2(bw, 26.0f)); bool hovered = ImGui::IsItemHovered(); int idx = (*v >= 0 && *v < (int)parts.size()) ? *v : 0; const char* cur = parts[idx].c_str(); draw_row_label(label, lb, pos, with_alpha(pal().text,
235)); ImU32 bg = hovered ? pal().cardHover : pal().inputBg; dl->AddRectFilled(b0, b1, bg, 6.0f); dl->AddRect(b0, b1, with_alpha(pal().separator, 170), 6.0f, 0, 1.0f); ImVec2 car = ImGui::CalcTextSize("\xE2\x96\xBC"); dl->PushClipRect(ImVec2(b0.x + 6.0f, b0.y), ImVec2(b1.x - car.x - 10.0f, b1.y), true); dl->AddText(ImVec2(b0.x + 10.0f, b0.y + (26.0f - ImGui::GetFontSize()) * 0.5f), with_alpha(pal().text, 245), cur); dl->PopClipRect(); dl->AddText(ImVec2(b1.x - car.x - 8.0f, b0.y + (26.0f - car.y) * 0.5f), with_alpha(pal().textDim, 200), "\xE2\x96\xBC"); if (ImGui::IsItemClicked(0) || ImGui::IsItemClicked(1)) { const bool is_current = s_combo_open && s_combo_ptr == v; if (is_current) { s_combo_open = false; s_combo_just_opened = false; } else { s_combo_items = parts; s_combo_ptr = v; s_combo_anchor = b0; s_combo_width = bw; s_combo_sel = idx; s_combo_open = true; s_combo_just_opened = true; } }
ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + row_h)); ImGui::Dummy(ImVec2(0, 0)); ImGui::PopID(); return false; } static bool GradientButton(const char* label, const ImVec2& size = ImVec2(0, 0)) { ImGui::PushID(label); ImDrawList* dl = ImGui::GetWindowDrawList(); const float w = size.x > 0 ? size.x : ImGui::CalcTextSize(label).x + 30.0f; const float h = size.y > 0 ? size.y : 30.0f; ImGui::InvisibleButton("##gb", ImVec2(w, h)); bool hovered = ImGui::IsItemHovered(); bool active = ImGui::IsItemActive(); bool clicked = ImGui::IsItemClicked(0); ImVec2 p0 = ImGui::GetItemRectMin(); ImVec2 p1 = ImGui::GetItemRectMax(); ImU32 col = active ? pal().accentActive : hovered ? pal().
accentHover : pal().accent; dl->AddRectFilled(ImVec2(p0.x + 1.5f, p0.y + 2.0f), ImVec2(p1.x + 1.5f, p1.y + 2.0f), IM_COL32(0, 0, 0, 80), 6.0f); dl->AddRectFilled(p0, p1, col, 6.0f); if (hovered) dl->AddRectFilled(p0, p1, IM_COL32(255,255,255,26), 6.0f); else dl->AddRectFilled(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p1.x - 1, p0.y + 3), IM_COL32(255,255,255,36), 5.0f); ImVec2 ts = ImGui::CalcTextSize(label); dl->AddText(ImVec2(p0.x + (w - ts.x) * 0.5f, p0.y + (h - ts.y) * 0.5f), IM_COL32(255, 255, 255, 240), label); ImGui::PopID(); return clicked; } static bool MacButton(const char* label, const
ImVec2& size = ImVec2(0, 0)) { ImGui::PushID(label); ImDrawList* dl = ImGui::GetWindowDrawList(); const float w = size.x > 0 ? size.x : ImGui::CalcTextSize(label).x + 30.0f; const float h = size.y > 0 ? size.y : 30.0f; ImGui::InvisibleButton("MacButton", ImVec2(w, h)); bool hovered = ImGui::IsItemHovered(); bool clicked = ImGui::IsItemClicked(0); ImVec2 p0 = ImGui::GetItemRectMin(); ImVec2 p1 = ImGui::GetItemRectMax(); ImU32 bg = hovered ? pal().cardHover : pal().inputBg; dl->AddRectFilled(ImVec2(p0.x + 1, p0.y + 2), ImVec2(p1.x + 1, p1.y + 2), IM_COL32(0, 0, 0, 45), 6.0f); dl->AddRectFilled(
p0, p1, bg, 6.0f); dl->AddRect(p0, p1, with_alpha(pal().separator, 180), 6.0f); dl->AddRectFilled(p0, p1, with_alpha(pal().hoverBg, hovered ? 20 : 0), 6.0f); ImVec2 ts = ImGui::CalcTextSize(label); dl->AddText(ImVec2(p0.x + (w - ts.x) * 0.5f, p0.y + (h - ts.y) * 0.5f), pal().text, label); ImGui::PopID(); return clicked; } static bool CustomColorEdit(const char* label, float col[4]) { ImGui::PushStyleColor(ImGuiCol_FrameBg, v4_u32(pal().inputBg)); ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, v4_u32(pal().cardHover)); ImGui::PushStyleColor(ImGuiCol_FrameBgActive, v4_u32(pal().pressedBg));
ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f); ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); bool ret = ImGui::ColorEdit4(label, col, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs); ImGui::PopStyleVar(); ImGui::PopStyleColor(3); return ret; } struct BlockInfo { const char* title; bool* enabled; int* keybind; }; struct BlockState { bool open = false; float height = 0.0f; }; static std::unordered_map<std::string, BlockState> s_block_states; static int s_block_seq = 0; struct BlockCtx { BlockState* st = nullptr; const BlockInfo* info = nullptr; ImVec2 b0, b1;
bool open = false; float body_top = 0.0f; float card_h = 0.0f; bool second_col = false; }; static const float k_block_header_h = 30.0f; static ImVec2 s_row_left(0.0f, 0.0f); static float s_row_left_w = 0.0f; static float s_row_bottom = 0.0f; static BlockCtx block_begin(const BlockInfo& info, float ) { BlockCtx ctx; ctx.info = &info; ctx.st = &s_block_states[info.title]; BlockState& st = *ctx.st; ImDrawList* dl = ImGui::GetWindowDrawList(); float full_w = ImGui::GetContentRegionAvail().x; const bool second_col = (full_w > 400.0f) && ((s_block_seq % 2) == 1); float w = (full_w > 400.0f) ? (full_w - 10.0f) * 0.5f : full_w; ImVec2 b0 = ImGui::GetCursorScreenPos(); if (second_col) { b0.x = s_row_left.x + s_row_left_w + 10.0f; b0.y = s_row_left.y; ImGui::SetCursorScreenPos(b0); } else { s_row_left = b0; s_row_left_w = w; s_row_bottom = b0.y; } ctx.b0 = b0; ctx.second_col = second_col; const float est_h = st.height > 1.0f ? st.height : 300.0f; const float body_h = st.open ? est_h : 0.0f; const float card_h = k_block_header_h + body_h + 12.0f; ctx.card_h = card_h; ImVec2 b1(b0.x + w, b0
.y + card_h); const float r = std::min(std::min(14.0f, (b1.x - b0.x) * 0.5f), (b1.y - b0.y) * 0.5f); dl->AddRectFilled(ImVec2(b0.x + 1, b0.y + 1), ImVec2(b1.x - 1, b1.y - 1), IM_COL32(26, 28, 36, is_dark() ? 180 : 245), r); dl->AddRect(b0, b1, frost(is_dark() ? 22 : 130), r, 0, 1.0f); dl->AddRectFilled(ImVec2(b0.x + 3, b0.y + 1), ImVec2(b1.x - 3, b0.y + 3), frost(is_dark() ? (st.open ? 14 : 7) : 120), 3.0f); dl->AddRectFilled(ImVec2(b0.x + 3, b1.y - 2), ImVec2(b1.x - 3, b1.y - 1), frost(8), 2.0f); ImVec2 hb0(b0.x + 6.0f, b0.y + 2.0f); ImGui::
SetCursorScreenPos(hb0); ImGui::InvisibleButton((std::string("##hdr") + info.title).c_str(), ImVec2(w - 12.0f, k_block_header_h)); bool h_hover = ImGui::IsItemHovered(); if (ImGui::IsItemClicked(1)) st.open = !st.open; if (h_hover) dl->AddRectFilled(ImVec2(b0.x + 4, b0.y + 4), ImVec2(b1.x - 4, b0.y + k_block_header_h), frost(is_dark() ? 12 : 30), 8.0f); bool en = info.enabled ? *info.enabled : false; float cent_y = b0.y + k_block_header_h * 0.5f; if (info.enabled) { if (info.enabled) { dl->AddRectFilled(ImVec2(b0.x, b0.y + 4), ImVec2(b0.x + 3, b0.y + k_block_header_h - 4), with_alpha(IM_COL32(48, 210, 88, 255), en ? 180 : 60), 2.0f); } dl->AddCircleFilled(ImVec2(b0.x + 20, cent_y + 0.6f), 3.5f, IM_COL32(0, 0, 0, 50), 20); ImU32 dot = en ? IM_COL32(
48, 209, 88, 255) : with_alpha(pal().textFaint, 200); dl->AddCircleFilled(ImVec2(b0.x + 20, cent_y), 3.5f, dot, 20); dl->AddCircle(ImVec2(b0.x + 20, cent_y), 5.5f, en ? with_alpha(IM_COL32(48,210,88,255), 50) : with_alpha(pal().textFaint, 35), 20, 1.0f); } ImFont* bf = s_font_bold ? s_font_bold : s_font_main; if (!bf) bf = ImGui::GetFont(); ImVec2 title_clip_max(b1.x - 36.0f, b0.y + k_block_header_h); ImGui::PushClipRect(ImVec2(b0.x + 32, b0.y), title_clip_max, true); ImVec2 title_sz = bf->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, info.title); dl->AddText(bf, 13.0f, ImVec2(b0.x + 32, cent_y - title_sz.y * 0.5f), with_alpha(pal().text, 245), info.title); ImGui::PopClipRect(); ImVec2 chev0(b1.x - 30.0f, cent_y + 6.0f); ImGui::SetCursorScreenPos(ImVec2(chev0.x, cent_y - 6.0f)); ImGui::InvisibleButton((std::string("##chev") + info.title).
c_str(), ImVec2(24, 16)); if (ImGui::IsItemClicked(1)) st.open = !st.open; bool ch_hover = ImGui::IsItemHovered(); const char* glyph = st.open ? "\xE2\x96\xBC" : "\xE2\x96\xB6"; ImVec2 gsz = ImGui::CalcTextSize(glyph); dl->AddText(ImVec2(chev0.x + (24.0f - gsz.x) * 0.5f, chev0.y + (16.0f - gsz.y) * 0.5f), with_alpha(pal().chevron, ch_hover ? 255 : 170), glyph); ctx.open = st.open; if (ctx.open) { ctx.body_top = b0.y + k_block_header_h + 8.0f; ImVec2 clip_min(b0.x + 14.0f, ctx.body_top); ImVec2 clip_max(b1.x - 14.0f, clip_min.y + body_h + 6.0f); char body_id[128]; snprintf(body_id, sizeof(body_id), "##body%d_%s", s_block_seq, info.title); ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f); ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f)); ImGui::SetCursorScreenPos(clip_min); ImGui::BeginChild(body_id, ImVec2(clip_max.x - clip_min.x, clip_max.y - clip_min.y), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground); ImGui::PushID(info.title); } return ctx; } static void block_end(BlockCtx& ctx, float ) { if (ctx.open) { ImGui::PopID(); float nat_h = ImGui::GetCursorScreenPos().y - ctx.body_top + 8.0f; if (nat_h > 20.0f) ctx.st->height = nat_h; else rlog::logf("layout: block '%s' measured nat_h=%.1f (<=20: keep previous height %.1f)", ctx.info ? ctx.info->title : "?", (double)nat_h, (double)ctx.st->height); ImGui::EndChild(); ImGui::PopStyleVar(2); } const float bottom = ctx.b0.y + ctx.card_h; if (!ctx.second_col) s_row_bottom = bottom; else if (bottom > s_row_bottom) s_row_bottom = bottom; ImGui::SetCursorScreenPos(ImVec2(s_row_left.x, s_row_bottom)); ImGui::Dummy(ImVec2(0.0f, 0.0f)); s_block_seq++; } static void draw_combat_page(float t) { { static const BlockInfo info = { "Aim Assist", &globals::aimassist_enabled, &globals::aimassist_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { ToggleSwitch("Enabled", &globals::aimassist_enabled); CustomKeybind("Keybind",
&globals::aimassist_keybind); CustomSliderFloat("FOV", &globals::aimassist_fov, 1.0f, 90.0f, "%.0f"); SliderDouble("Max Distance", &globals::aimassist_max_distance, 1.0, 10.0, "%.1f"); CustomSliderFloat("Speed", &globals::aimassist_speed, 0.2f, 4.0f, "%.2f"); CustomSliderFloat("Smoothing", &globals::aimassist_smoothing, 0.0f, 95.0f, "%.0f%%"); CustomSliderFloat("Prediction", &globals::aimassist_prediction, 0.0f, 1.0f, "%.2f"); CustomCheckbox("Horizontal (Yaw)", &globals::aimassist_horizontal); CustomCheckbox("Vertical (Pitch)", &globals::aimassist_vertical); CustomCheckbox("Randomize", &
globals::aimassist_randomize); if (globals::aimassist_randomize) CustomSliderFloat("Random Strength", &globals::aimassist_random_strength, 0.0f, 3.0f, "%.2f"); } block_end(b, t); } { static const BlockInfo info = { "Triggerbot", &globals::triggerbot_enabled, &globals::triggerbot_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::triggerbot_enabled); CustomKeybind("Keybind", &globals::triggerbot_keybind); CustomSliderFloat("Distance", &globals::triggerbot_distance, 0.5f, 6.0f, "%.1f"); ComboMode("Sprint Mode", &globals::triggerbot_sprint_mode,
"HvH\0Normal\0Legit\0Off\0"); CustomCheckbox("Weapon Only", &globals::triggerbot_weapon_only); CustomCheckbox("Crit (падение)", &globals::triggerbot_jump_only); } block_end(b, t); } { static const BlockInfo info = { "Reach", &globals::reach_enabled, &globals::reach_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::reach_enabled); CustomKeybind("Keybind", &
globals::reach_keybind); ComboMode("Mode", &globals::reach_mode, "Normal\0Extra\0"); SliderDouble("Distance", &globals::reach_distance, 3.0, 6.0, "%.2f"); } block_end(b, t); } { static const BlockInfo info = { "Hitbox", &globals::hitbox_enabled, &globals::hitbox_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::hitbox_enabled); CustomKeybind("Keybind", &globals::hitbox_keybind); ComboMode("Mode", &globals::hitbox_mode, "Expand\0Override\0"); SliderDouble("Width", &globals::hitbox_expand_width, 0.0, 3.0, "%.2f"); SliderDouble("Height", &globals::
hitbox_expand_height, 0.0, 3.0, "%.2f"); } block_end(b, t); } { static const BlockInfo info = { "Shield Breaker", &globals::shield_breaker_enabled, &globals::shield_breaker_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::shield_breaker_enabled); CustomKeybind("Keybind", &globals::shield_breaker_keybind); ComboMode("Keybind Mode", &globals::shield_breaker_keybind_mode, "Hold\0Toggle\0"); CustomCheckbox("Aim", &globals::shield_breaker_aim); CustomCheckbox("Switch Back", &globals::shield_breaker_switch_back); CustomSliderInt("Delay", &globals::
shield_breaker_delay_ms, 0, 500); } block_end(b, t); } } static void draw_player_page(float t) { { static const BlockInfo info = { "AutoCrystal", &globals::autocrystal_enabled, &globals::autocrystal_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::autocrystal_enabled); CustomKeybind("Keybind", &globals::autocrystal_keybind); ComboMode("Mode", &globals::autocrystal_mode, "Normal\0Aura\0"); CustomSliderInt("Delay", &globals::autocrystal_delay_ms, 0, 1000); CustomCheckbox("Debug", &globals::autocrystal_debug_enabled); } block_end(b, t); } { static
const BlockInfo info = { "AutoTotem", &globals::autototem_enabled, &globals::autototem_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::autototem_enabled); CustomKeybind("Keybind", &globals::autototem_keybind); ComboMode("Mode", &globals::autototem_mode, "Always\0Switch\0"); CustomCheckbox("Rage Mode", &globals::autototem_rage_mode); } block_end(b, t); } { static const BlockInfo info = { "Mace", &globals::mace_enabled, &globals::mace_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::mace_enabled);
CustomKeybind("Keybind", &globals::mace_keybind); ComboMode("Keybind Mode", &globals::mace_keybind_mode, "Hold\0Toggle\0"); CustomCheckbox("Look", &globals::mace_look); CustomCheckbox("Switch Back", &globals::mace_switch_back); CustomCheckbox("Remove Elytra", &globals::mace_remove_elytra); SliderDouble("Min Fall Distance", &globals::mace_min_fall_distance, 0.0, 50.0, "%.1f"); SliderDouble("Height Above Target", &globals::mace_height_above_target, 0.0, 10.0, "%.1f"); SliderDouble("Fall Hitbox Width", &globals::mace_fall_hitbox_width, 0.0, 5.0, "%.1f"); SliderDouble("Fall Hitbox Height", &
globals::mace_fall_hitbox_height, 0.0, 5.0, "%.1f"); } block_end(b, t); } { static const BlockInfo info = { "Pearl Catch", &globals::pearl_catch_enabled, &globals::pearl_catch_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::pearl_catch_enabled); CustomKeybind("Keybind", &globals::pearl_catch_keybind); ComboMode("Mode", &globals::pearl_catch_mode, "Hold\0Toggle\0Always\0"); ComboMode("Aim Mode", &globals::pearl_catch_aim_mode, "Silent\0Normal\0"); } block_end(b, t); } { static const BlockInfo info = { "Anchor Macro", &globals::
anchor_macro_enabled, &globals::anchor_macro_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::anchor_macro_enabled); CustomKeybind("Keybind", &globals::anchor_macro_keybind); ComboMode("Mode", &globals::anchor_macro_mode, "Hold\0Toggle\0"); CustomCheckbox("Break Anchor", &globals::anchor_macro_break_anchor); CustomSliderInt("Swap Delay", &globals::anchor_macro_swap_delay_ms, 0, 1000); CustomSliderInt("Charge Delay", &globals::anchor_macro_charge_delay_ms, 0, 1000); CustomSliderInt("Break Delay", &globals::anchor_macro_break_delay_ms, 0, 1000); }
block_end(b, t); } { static const BlockInfo info = { "Backtrack", &globals::backtrack_enabled, &globals::backtrack_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::backtrack_enabled); CustomKeybind("Keybind", &globals::backtrack_keybind); ComboMode("Mode", &globals::backtrack_mode, "Normal\0Advanced\0"); CustomSliderInt("Max Delay", &globals::backtrack_max_delay_ms, 0, 500); SliderDouble("Min Distance", &globals::backtrack_min_distance, 0.0, 10.0, "%.1f"); SliderDouble("Max Distance", &globals::backtrack_max_distance, 0.0, 10.0, "%.1f");
CustomSliderInt("Max Hurt Time", &globals::backtrack_max_hurt_time_ms, 0, 1000); CustomCheckbox("Disable on Hit", &globals::backtrack_disable_on_hit); CustomSliderFloat("Cooldown", &globals::backtrack_cooldown_seconds, 0.0f, 5.0f, "%.1fs"); CustomCheckbox("Visualization", &globals::backtrack_visualization_enabled); CustomColorEdit("Color", globals::backtrack_visualization_color); CustomSliderFloat("Line Width", &globals::backtrack_visualization_line_width, 0.5f, 5.0f, "%.1f"); CustomCheckbox("Filled", &globals::backtrack_visualization_filled); } block_end(b, t); } { static const BlockInfo
info = { "Stun Slam", &globals::stun_slam_enabled, nullptr }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::stun_slam_enabled); CustomSliderFloat("Chance", &globals::stun_slam_chance, 0.0f, 100.0f, "%.0f%%"); CustomSliderInt("Swap Delay", &globals::stun_slam_swap_delay_ms, 0, 500); CustomSliderInt("Axe Delay", &globals::stun_slam_axe_delay_ms, 0, 500); CustomSliderInt("Mace Delay", &globals::stun_slam_mace_delay_ms, 0, 500); SliderDouble("Min Fall", &globals::stun_slam_min_fall, 0.0, 50.0, "%.1f"); } block_end(b, t); } { static const BlockInfo info = {
"Server Rotation", &globals::server_rotation_enabled, nullptr }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::server_rotation_enabled); } block_end(b, t); } { static const BlockInfo info = { "Debug", &globals::debug_logging_enabled, nullptr }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Timing Logs", &globals::debug_logging_enabled); } block_end(b, t); } { static const BlockInfo info = { "Friends", nullptr, nullptr }; BlockCtx b = block_begin(info, t); if (b.open) { static char s_friend_buf[64] = ""; TextWrapColored(v4_u32(pal().textDim), "Add friend by nick:"); ImGui::SetNextItemWidth(180); ImGui::InputText("##friend_input", s_friend_buf, sizeof(s_friend_buf)); ImGui::SameLine(); if (GradientButton("Add", ImVec2(60, 26))) { if (s_friend_buf[0] != '\0') { flaway::modules::friend_manager::add(s_friend_buf); s_friend_buf[0] = '\0'; } } ImGui::Dummy(ImVec2(0, 4.0f)); const auto& friends = flaway::modules::friend_manager::get_list(); if (friends.empty()) { TextWrapColored(v4_u32(pal().textFaint), "No friends added."); } else { for (int i = 0; i < (int)friends.size(); i++) { ImGui::PushID(i); TextWrapColored(v4_rgb(48, 210, 88), "%s", friends[i].c_str()); ImGui::SameLine(); if (MacButton("Remove", ImVec2(70, 22))) { flaway::modules::friend_manager::remove(friends[i]); ImGui::PopID(); break; } ImGui::PopID(); } } ImGui::Dummy(ImVec2(0, 2.0f)); TextWrapColored(v4_u32(pal().textFaint), "Friends: ESP green, aimbot skip"); } block_end(b, t); } } static void draw_movement_page(float t) { { static const BlockInfo info = { "W-Tap", &globals::wtap_enabled, nullptr }; BlockCtx b = block_begin(info, t); if (b.open) {
CustomCheckbox("Enabled", &globals::wtap_enabled); CustomSliderInt("Duration", &globals::wtap_duration_ms, 50, 500); } block_end(b, t); } { static const BlockInfo info = { "S-Tap", &globals::stap_enabled, nullptr }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::stap_enabled); CustomSliderInt("Duration", &globals::stap_duration_ms, 50, 500); } block_end(b, t); } { static const BlockInfo info = { "Auto Jump Reset", &globals::autojumpreset_enabled, &globals::autojumpreset_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled",
&globals::autojumpreset_enabled); CustomKeybind("Keybind", &globals::autojumpreset_keybind); ComboMode("Mode", &globals::autojumpreset_mode, "Always\0Fall\0"); CustomSliderInt("Cooldown", &globals::autojumpreset_cooldown_ms, 0, 1000); } block_end(b, t); } { static const BlockInfo info = { "AutoSprint", &globals::sprint_enabled, nullptr }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::sprint_enabled); CustomCheckbox("Keep Swimming", &globals::autosprint_keep_swimming); } block_end(b, t); } { static const BlockInfo info = { "Flight", &globals::
flight_enabled, nullptr }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::flight_enabled); } block_end(b, t); } } static void draw_visuals_page(float t) { { static const BlockInfo info = { "ESP", &globals::box_enabled, &globals::esp_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::box_enabled); CustomKeybind("Keybind", &globals::esp_keybind); ComboMode("Mode", &globals::esp_mode, "Glow\0Outline\0Box\0Box 3D\0"); SubRow("Box", &globals::box_enabled); SubRow("Health Bar", &globals::esp_health_bar); SubRow(
"Names", &globals::esp_name_enabled); SubRow("Hide Vanilla Names", &globals::esp_hide_vanilla_names); SubRow("Item ESP", &globals::esp_item_enabled); SubRow("Tracers", &globals::esp_tracers); SubRow("Arrows", &globals::esp_arrows); SubRow("Mob Stats", &globals::mobstats_enabled); CustomSliderFloat("Vertical Offset", &globals::esp_vertical_offset, -5.0f, 5.0f, "%.1f"); } block_end(b, t); } { static const BlockInfo info = { "Fullbright", &globals::fullbright_enabled, &globals::fullbright_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::fullbright_enabled); CustomKeybind("Keybind", &globals::fullbright_keybind); SliderDouble(
"Gamma (Brightness)", &globals::fullbright_gamma, 0.0, 1000.0, "%.0f"); } block_end(b, t); } { static const BlockInfo info = { "BaseFinder", &globals::base_finder_enabled, &globals::base_finder_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::base_finder_enabled); CustomKeybind("Keybind", &globals::base_finder_keybind); ComboMode("Mode", &globals::base_finder_mode, "Cave\0Bypass\0Both\0"); SubRow("Click", &globals::base_finder_click); SubRow("HolyWorld", &globals::base_finder_holy_world); SliderDouble("Range", &globals::base_finder_range, 1.0, 128.0, "%.0f"); CustomSliderInt("Min Size", &globals::base_finder_min_size, 1, 100); CustomSliderInt("Max Size", &globals::base_finder_max_size, 1, 500); CustomSliderInt("Min Length", &globals::base_finder_min_length, 1, 100); CustomSliderInt("Min Width", &globals::base_finder_min_width, 1, 100); CustomColorEdit("Cave Color", globals::base_finder_cave_color); CustomColorEdit("Bypass Color", globals::base_finder_bypass_color); } block_end(b, t); } } static void draw_hud_page(float t) { { static const BlockInfo info = { "HUD", nullptr, nullptr }; BlockCtx b = block_begin(info, t); if (b.open) { ImVec2 pv0 = ImGui::GetCursorScreenPos(); float pvw = ImGui::GetContentRegionAvail().x; float pvh = 176.0f; ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddRectFilled(pv0, ImVec2(pv0.x + pvw, pv0.y + pvh), IM_COL32(9, 10, 14, 255), 10.0f); dl->AddRect(pv0, ImVec2(pv0.x + pvw, pv0.y + pvh), with_alpha(pal().separator, 90), 10.0f, 0, 1.0f); flaway::hud::draw_preview(dl, ImVec2(pv0.x + 8.0f, pv0.y + 8.0f), ImVec2(pv0.x + pvw - 8.0f, pv0.y + pvh - 8.0f)); ImGui::Dummy(ImVec2(pvw, pvh)); ImGui::Dummy(ImVec2(0.0f, 6.0f)); TextWrapColored(v4_u32(pal().textDim), "Edit mode: open chat (T). Drag with LMB; RMB gives Hide / Reset / Size."); CustomSliderFloat("HUD Scale", &globals::hud_scale, 0.6f, 2.0f, "%.2f"); for (int id = 0; id < flaway::hud::E_COUNT; id++) { bool* en = flaway::hud::element_enabled(id); float* sc = flaway::hud::element_scale(id); const char* nm = flaway::hud::element_name(id); if (!en || !sc || !nm || !*nm) continue; SubRow(nm, en); std::string sl = std::string(nm) + " size"; CustomSliderFloat(sl.c_str(), sc, 0.6f, 1.8f, "%.2f"); } if (GradientButton("Reset Layout", ImVec2(180, 30))) { flaway::hud::reset_all(); flaway::hud::save(); } TextWrapColored(v4_u32(pal().textDim), "HUD renders only while the menu is closed; F1 hides it."); } block_end(b, t); } } static void draw_storage_page(float t) { { static const BlockInfo info = { "Storage ESP", &globals::storage_esp_enabled, nullptr }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::storage_esp_enabled); SubRow("Chest", &globals::storage_esp_chest); SubRow("Ender Chest", &globals::storage_esp_ender_chest); SubRow("Shulker", &globals::storage_esp_shulker); ComboMode("Render", &globals::storage_esp_mode, "2D Box\03D Wireframe\0")
; } block_end(b, t); } { static const BlockInfo info = { "Chest Stealer", &globals::chest_stealer_enabled, &globals::chest_stealer_keybind }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Enabled", &globals::chest_stealer_enabled); CustomKeybind("Keybind", &globals::chest_stealer_keybind); ComboMode("Mode", &globals::chest_stealer_mode, "FunTime\0HolyWorld\0ReallyWorld\0Custom\0"); if (globals::chest_stealer_mode == 3) { CustomSliderInt("Start Delay", &globals::chest_stealer_start_delay, 0, 60); CustomSliderInt("Min Delay", &globals::chest_stealer_min_delay, 0, 60);
CustomSliderInt("Max Delay", &globals::chest_stealer_max_delay, 0, 60); if (globals::chest_stealer_close_screen) CustomSliderInt("Close Delay", &globals::chest_stealer_close_delay, 0, 60); } CustomCheckbox("Close Screen", &globals::chest_stealer_close_screen); } block_end(b, t); } } static void draw_theme_page(float t) { { static const BlockInfo info = { "Appearance", nullptr, nullptr }; BlockCtx b = block_begin(info, t); if (b.open) { if (MacButton("Light", ImVec2(96, 30))) { globals::theme_dark = false; } ImGui::SameLine(0, 8.0f); if (MacButton("Dark", ImVec2(96, 30))) { globals::theme_dark = true; } ImGui::Dummy(ImVec2(0, 6.0f)); TextWrapColored(v4_u32(pal().textDim), "Accent color:"); for (int i = 0; i < k_n_accents; i++) { if (i > 0) ImGui::SameLine(0, 6.0f); ImVec2 cp = ImGui::GetCursorScreenPos(); ImGui::InvisibleButton((std::string("##ac") + std::to_string(i)).c_str(), ImVec2(28, 28)); bool hov = ImGui::IsItemHovered(); if (ImGui::IsItemClicked(0)) { globals::theme_accent = i; globals::theme_grad = -1; setup_style(); } ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddRectFilled(cp, ImVec2(cp.x + 28, cp.y + 28), k_accents[i], 6.0f); if (k_accents[i] == theme_accent_u32()) dl->AddRect(cp, ImVec2(cp.x + 28, cp.y + 28), IM_COL32(255, 255, 255, 220), 6.0f, 0, 2.0f); if (hov) dl->AddRect(cp, ImVec2(cp.x + 28, cp.y + 28), IM_COL32(255, 255, 255, 120), 6.0f, 0, 1.0f); } ImGui::Dummy(ImVec2(0, 8.0f)); TextWrapColored(v4_u32(pal().textDim), "Gradient presets (HUD cards + ESP):"); for (int i = 0; i < k_n_grads; i++) { ImVec2 gp = ImGui::GetCursorScreenPos(); float bw = ImGui::GetContentRegionAvail().x; ImGui::InvisibleButton((std::string("##gr") + std::to_string(i)).c_str(), ImVec2(bw, 28)); bool hov = ImGui::IsItemHovered(); if (ImGui::IsItemClicked(0)) { globals::theme_grad = i; setup_style(); } ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddRectFilled(gp, ImVec2(gp.x + bw, gp.y + 28), k_grad_a[i], 6.0f); dl->AddRectFilledMultiColor(ImVec2(gp.x + bw * 0.5f, gp.y), ImVec2(gp.x + bw, gp.y + 28), IM_COL32(0,0,0,0), k_grad_a[i], k_grad_b[i], IM_COL32(0,0,0,0)); if (globals::theme_grad == i) dl->AddRect(gp, ImVec2(gp.x + bw, gp.y + 28), IM_COL32(255,255,255,200), 6.0f, 0, 2.0f); if (hov) dl->AddRect(gp, ImVec2(gp.x + bw, gp.y + 28), IM_COL32(255,255,255,110), 6.0f, 0, 1.0f); ImVec2 ts = ImGui::CalcTextSize(k_grad_names[i]); dl->AddText(ImVec2(gp.x + (bw - ts.x) * 0.5f, gp.y + (28 - ts.y) * 0.5f), IM_COL32(255,255,255,240), k_grad_names[i]); } TextWrapColored(v4_u32(pal().textDim), "No preset selected: the gradient follows the accent swatch."); } block_end(b, t); } { static const BlockInfo info = { "Config", nullptr, nullptr }; BlockCtx b = block_begin(info, t); if (b.open) { TextWrapColored(v4_u32(pal().textDim), "Profiles stored in ~/.minecraft/flaway/"); TextWrapColored(v4_u32(pal().textDim), "auto.flaway loads on inject, saves on exit."); if (GradientButton("Save (auto.flaway)", ImVec2(230, 30))) { flaway::config::save_auto(); } if (GradientButton("Load (auto.flaway)", ImVec2(230, 30))) { flaway::config::load_auto(); } TextWrapColored(v4_u32(pal().textDim), "Custom profile name:"); static char s_cfg_name[64] = "flaway"; ImGui::PushItemWidth(180); ImGui::InputText("##cfgname", s_cfg_name, sizeof(s_cfg_name)); ImGui::PopItemWidth(); std::string filename = std::string(s_cfg_name) + ".flaway"; if (GradientButton("Save Named", ImVec2(230, 30))) { flaway::config::save(filename); } if (GradientButton("Load Named", ImVec2(230, 30))) { flaway::config::load(filename); } } block_end(b, t); } { static const BlockInfo info = { "Discord RPC", &globals::discord_rpc_enabled, nullptr }; BlockCtx b = block_begin(info, t); if (b.open) { CustomCheckbox("Add status", &globals::discord_rpc_enabled); TextWrapColored(v4_u32(pal().textDim), "Shows your joined server IP (or in menu) in the Discord status."); ImGui::Dummy(ImVec2(0, 4.0f)); TextWrapColored(discord_rpc::connected() ? v4_rgb(48, 210, 88) : v4_u32(pal().textDim), "Status: %s", discord_rpc::status()); } block_end(b, t); } { static const BlockInfo info = { "Unhook (Unload)", &globals::unhook_all_enabled, &globals::unhook_all_keybind }; BlockCtx b =
block_begin(info, t); if (b.open) { TextWrapColored(v4_rgb(255, 120, 90), "Fully unloads the .so and removes all hooks from the game."); CustomKeybind("Keybind", &globals::unhook_all_keybind); if (GradientButton("Unhook Now", ImVec2(220, 30))) { globals::unhook_all_enabled = true; } } block_end(b, t); } } struct CatDef { const char* name; const char* icon; }; static const CatDef s_cats[7] = { { "Combat", "\xE2\x9A\x94" }, { "Player", "\xE2\x97\x86" }, { "Movement", "\xE2\x86\x92" }, { "Visuals", "\xE2\x98\x85" }, { "HUD", "\xE2\x9A\xA1" }, { "Storage", "\xE2\x9C\xA6" }, { "Theme",
"\xE2\x9E\x9C" }, }; static int s_active_category = 0; static double s_menu_open_time = -1e9; static bool s_show_about = false; static void draw_traffic_lights(float alpha, const ImVec2& wmin) { ImDrawList* dl = ImGui::GetWindowDrawList(); const ImU32 lights[3] = { IM_COL32(255, 95, 85, 255), IM_COL32(255, 188, 46, 255), IM_COL32(39, 200, 63, 255), }; const char* glyphs[3] = { "\xC3\x97", "\xE2\x80\x93", "+" }; for (int i = 0; i < 3; i++) { ImVec2 c(wmin.x + 18 + i * 19, wmin.y + 17); ImGui::SetCursorScreenPos(ImVec2(c.x - 7, c.y - 7)); ImGui::InvisibleButton((std::string("##light") + std::
to_string(i)).c_str(), ImVec2(14, 14)); bool hovered = ImGui::IsItemHovered(); bool active = ImGui::IsItemActive(); bool clicked = ImGui::IsItemClicked(0); ImU32 col = active ? lerp_u32(lights[i], IM_COL32(30, 30, 34, 255), 0.22f) : lights[i]; dl->AddCircleFilled(c, 6.0f, with_alpha(col, (int)((hovered ? 255 : 205) * alpha)), 24); if (hovered) { ImVec2 gs = ImGui::CalcTextSize(glyphs[i]); dl->AddText(ImVec2(c.x - gs.x * 0.5f, c.y - gs.y * 0.5f), IM_COL32(60, 40, 34, 255), glyphs[i]); } if (clicked && i == 0) { globals::show_gui = false; } } } static void draw_title_bar(const MacPalette& p,
float alpha, const ImVec2& wmin, const ImVec2& wmax, float title_h) { ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddRectFilled(ImVec2(wmin.x, wmin.y), ImVec2(wmax.x, wmin.y + title_h), IM_COL32(16, 17, 22, 230)); dl->AddRectFilled(ImVec2(wmin.x, wmin.y + title_h - 1), ImVec2(wmax.x, wmin.y + title_h), frost(15)); draw_traffic_lights(alpha, wmin); const char* app = "flaway"; ImFont* f = s_font_bold ? s_font_bold : s_font_main; ImVec2 ts = f->CalcTextSizeA(13.5f, FLT_MAX, 0, app); float cx = wmin.x + (wmax.x - wmin.x) * 0.5f - ts.x * 0.5f; dl->AddText(f, 13.5f, ImVec2(
cx, wmin.y + (title_h - ts.y) * 0.5f - 1.0f), with_alpha(p.text, 235), app); } enum MenuAction { M_NONE = 0, M_CLOSE, M_SAVE, M_LOAD, M_ABOUT, M_THEME_LIGHT, M_THEME_DARK, }; static int s_open_menu = -1; static ImVec2 s_menu_anchor(-1, -1); static void run_menu_action(int a) { switch (a) { case M_CLOSE: globals::show_gui = false; break; case M_SAVE: flaway::config::save_auto(); break; case M_LOAD: flaway::config::load_auto(); break; case M_ABOUT: s_show_about = true; break; case M_THEME_LIGHT: globals::theme_dark = false; s_dark_mode = false; setup_style(); break; case M_THEME_DARK: globals::theme_dark = true; s_dark_mode = true; setup_style();
break; } } static void draw_menu_bar(const MacPalette& p, float alpha, const ImVec2& wmin, float ww, float menu_top) { static const char* names[5] = { "\xD0\xA4\xD0\xB0\xD0\xB9\xD0\xBB", "\xD0\x9F\xD1\x80\xD0\xB0\xD0\xB2\xD0\xBA\xD0\xB0", "\xD0\x92\xD0\xB8\xD0\xB4", "\xD0\x9E\xD0\xBA\xD0\xBD\xD0\xBE", "\xD0\xA1\xD0\xBF\xD1\x80\xD0\xB0\xD0\xB2\xD0\xBA\xD0\xB0" }; ImDrawList* dl = ImGui::GetWindowDrawList(); ImFont* f = s_font_main ? s_font_main : hud_font(); dl->AddRectFilled(ImVec2(wmin.x, menu_top), ImVec2(wmin.x + ww, menu_top + 28.0f), IM_COL32(16, 17, 22, 230)); dl->AddRectFilled(ImVec2(
wmin.x, menu_top + 27.0f), ImVec2(wmin.x + ww, menu_top + 28.0f), with_alpha(p.separator, 90)); float x = wmin.x + 10.0f; for (int i = 0; i < 5; i++) { ImVec2 ss = f->CalcTextSizeA(12.0f, FLT_MAX, 0, names[i]); float w = ss.x + 20.0f; ImVec2 p0(x, menu_top); ImVec2 p1(x + w, menu_top + 28.0f); ImGui::SetCursorScreenPos(p0); ImGui::InvisibleButton((std::string("##mnu") + std::to_string(i)).c_str(), ImVec2(w, 28.0f)); bool hovered = ImGui::IsItemHovered(); bool clicked = ImGui::IsItemClicked(0); if (hovered || s_open_menu == i) dl->AddRectFilled(p0, p1, with_alpha(p.hoverBg, is_dark() ? 45 : 36
), 5.0f); dl->AddText(f, 12.0f, ImVec2(p0.x + 10, p0.y + (28.0f - 12.0f) * 0.5f), with_alpha(p.text, 245), names[i]); if (clicked) { if (s_open_menu == i) s_open_menu = -1; else { s_open_menu = i; s_menu_anchor = p0; } } x += w; } } struct MenuEntry { const char* label; const char* shortcut; bool checked; int action; bool separator; }; static void draw_menu_entries(const MacPalette& p, const std::vector<MenuEntry>& entries, float alpha, float w, float* out_bottom = nullptr) { ImDrawList* dl = ImGui::GetWindowDrawList(); float x0 = s_menu_anchor.x; float y = s_menu_anchor.y + 34.0f; for (const
MenuEntry& e : entries) { if (e.separator) { ImVec2 sp0(x0 + 10, y + 7.0f); dl->AddRectFilled(ImVec2(sp0.x, sp0.y), ImVec2(x0 + w - 10, sp0.y + 1), with_alpha(p.separator, 160)); y += 16.0f; continue; } ImVec2 r0(x0 + 3, y); ImVec2 r1(x0 + w - 3, y + 22.0f); ImGui::SetCursorScreenPos(r0); const char* gname = e.label ? e.label : "sep"; ImGui::InvisibleButton((std::string("##mi") + gname).c_str(), ImVec2(r1.x - r0.x, r1.y - r0.y)); bool hv = ImGui::IsItemHovered(); bool sel = ImGui::IsItemClicked(0); ImVec2 mr0 = ImGui::GetItemRectMin(); ImVec2 mr1 = ImGui::GetItemRectMax(); if (hv || sel) dl->
AddRectFilled(ImVec2(mr0.x + 2, mr0.y + 1), ImVec2(mr1.x - 2, mr1.y - 1), pal().accent, 5.0f); ImU32 tcol = (hv || sel) ? IM_COL32(255,255,255,255) : with_alpha(p.text, 238); float lead = 14.0f; if (e.checked) { lead = 22.0f; ImVec2 chk = ImGui::CalcTextSize("\xE2\x9C\x93"); dl->AddText(ImVec2(mr0.x + 4, mr0.y + (22.0f - chk.y) * 0.5f), tcol, "\xE2\x9C\x93"); } dl->AddText(ImVec2(mr0.x + lead, mr0.y + (22.0f - ImGui::GetFontSize()) * 0.5f), tcol, e.label); if (e.shortcut) { ImFont* tf = s_font_small ? s_font_small : s_font_main; ImVec2 ssz = tf->CalcTextSizeA(11.0f, FLT_MAX, 0, e.shortcut);
dl->AddText(tf, 11.0f, ImVec2(mr1.x - ssz.x - 12.0f, mr0.y + (22.0f - ssz.y) * 0.5f), (hv || sel) ? IM_COL32(255,255,255,240) : with_alpha(p.textDim, 200), e.shortcut); } if (sel) { s_open_menu = -1; run_menu_action(e.action); } y += 22.0f; } if (out_bottom) *out_bottom = y; } static void draw_menu_overlay(float alpha) { if (s_open_menu < 0) return; const MacPalette& p = pal(); ImVec2 mouse = ImGui::GetMousePos(); std::vector<MenuEntry> entries; float w = 250.0f; switch (s_open_menu) { case 0: entries = { { "Скрыть окно", "⌘W", false, M_CLOSE }, { "Сохранить конфиг", "⌘S", false, M_SAVE }, {
"Загрузить конфиг", "⌘O", false, M_LOAD }, { nullptr, nullptr, false, 0, true }, { "Выйти", "⌘Q", false, M_CLOSE } }; w = 240.0f; break; case 1: entries = { { "Копировать", "⌘C", false, M_NONE }, { "Вставить", "⌘V", false, M_NONE }, { "Выделить всё", "⌘A", false, M_NONE } }; break; case 2: entries = { { "Светлая тема", "\xE2\x8C\xA5""1", !is_dark(), M_THEME_LIGHT }, { "Тёмная тема", "\xE2\x8C\xA5""2", is_dark(), M_THEME_DARK }, { nullptr, nullptr, false, 0, true }, { "\xD0\xA1\xD0\xBA\xD1\x80\xD1\x8B\xD1\x82\xD1\x8C", "\xE2\x8C\x98" "M", false, M_CLOSE } }; break; case 3: entries = { {
"Минимизировать", "⌘M", false, M_NONE }, { "Закрыть окно", "⌘W", false, M_CLOSE } }; break; case 4: entries = { { "О flaway", "", false, M_ABOUT } }; break; } float total_h = 0.0f; for (auto& e : entries) total_h += e.separator ? 16.0f : 22.0f; total_h += 8.0f; float x0 = s_menu_anchor.x; float y0 = s_menu_anchor.y + 32.0f; ImVec2 o0(x0, y0); ImVec2 o1(x0 + w, y0 + total_h); ImVec2 m(mouse.x, mouse.y); bool inside = m.x >= o0.x && m.x <= o1.x && m.y >= o0.y && m.y <= o1.y; if (!inside && ImGui::IsMouseClicked(0)) { s_open_menu = -1; return; } ImDrawList* dl = ImGui::GetWindowDrawList(); dl->
AddRectFilled(ImVec2(o0.x + 4, o0.y + 6), ImVec2(o1.x + 4, o1.y + 6), IM_COL32(0, 0, 0, 80), 8.0f); dl->AddRectFilled(o0, o1, p.cardBg, 8.0f); dl->AddRect(o0, o1, with_alpha(p.separator, 160), 8.0f, 0, 1.0f); draw_menu_entries(p, entries, alpha, w); } static bool sidebar_button(int idx, bool active, const MacPalette& p) { ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 p0 = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x; const float h = 34.0f; ImVec2 p1(p0.x + w, p0.y + h); ImGui::InvisibleButton((std::string("##sb") + s_cats[idx].name).c_str(), ImVec2(w, h));
bool clicked = ImGui::IsItemClicked(0); bool hovered = ImGui::IsItemHovered(); ImGuiID anim_id = ImGui::GetID("##sb_hv"); float ha = ImGui::GetStateStorage()->GetFloat(anim_id, 0.0f); float htarget = hovered ? 1.0f : 0.0f; ha += (htarget - ha) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f); if (fabsf(htarget - ha) < 0.001f) ha = htarget; ImGui::GetStateStorage()->SetFloat(anim_id, ha); if (active) { dl->AddRectFilled(ImVec2(p0.x + 4, p0.y + 2), ImVec2(p1.x - 4, p1.y - 2), with_alpha(p.accent, 35), 10.0f); dl->AddRectFilled(ImVec2(p0.x, p0.y + 6), ImVec2(p0.x + 3, p1.y - 6), p.accent, 2.0f); } else if (ha > 0.01f) { dl->AddRectFilled(ImVec2(p0.x + 6, p0.y + 2), ImVec2(p1.x - 6, p1.y - 2), with_alpha(frost(15), (int)(255 * ha)), 10.0f); } ImFont* f = s_font_main ? s_font_main : hud_font(); ImFont* ico_font = s_font_icons ? s_font_icons : f; ImU32 ico = active ? IM_COL32(255, 255, 255, 255) : hovered ? with_alpha(p.text, 235) : with_alpha(p.textDim, 210); ImVec2 sb_ico_sz = ico_font->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, s_cats[idx].icon);
dl->AddText(ico_font, 14.0f, ImVec2(p0.x + 20, p0.y + (h - sb_ico_sz.y) * 0.5f)
, ico, s_cats[idx].icon); ImVec2 sb_ts = f->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, s_cats[idx].name); dl->AddText(f, 12.0f, ImVec2(p0.x + 46, p0.y + (h - sb_ts.y) * 0.5f), ico, s_cats[idx].name); return clicked; } static void draw_sidebar(const MacPalette& p, const ImVec2& wmin, const ImVec2& s0, const ImVec2& s1) { ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddRectFilledMultiColor(s0, s1, IM_COL32(16, 17, 22, 245), IM_COL32(16, 17, 22, 245), IM_COL32(12, 13, 17, 245), IM_COL32(12, 13, 17, 245)); ImGui::SetCursorScreenPos(ImVec2(s0.x + 8.0f, s0.y + 8.0f)); ImGui::BeginChild("##sidebar", ImVec2(s1.x - s0.x - 16.0f, s1.y - s0.y - 16.0f), false,
ImGuiWindowFlags_NoScrollbar); { ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2)); for (int i = 0; i < 7; i++) { if (sidebar_button(i, s_active_category == i, p)) s_active_category = i; } ImGui::PopStyleVar
(); } ImGui::EndChild(); } static void draw_status_bar(const MacPalette& p, float alpha, const ImVec2& wmin, float ww, float wmax_y, float sb_h) { ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 sb0(wmin.x, wmax_y - sb_h); ImVec2 sb1(wmin.x + ww, wmax_y); dl->AddRectFilled(sb0, sb1, IM_COL32(16, 17, 22, 210), 0.0f); dl->AddRectFilled(ImVec2(sb0.x, sb0.y), ImVec2(sb1.x, sb0.y + 1), frost(12)); ImFont* f = s_font_small ? s_font_small : hud_font(); if (!f) return; bool active = g_is_init; ImVec2 dot(sb0.x + 14, sb0.y + sb_h * 0.5f); dl->AddCircleFilled(ImVec2(dot.x, dot.y + 0.5f), 4.0f,
IM_COL32(0, 0, 0, 50), 16); dl->AddCircleFilled(dot, 4.0f, active ? IM_COL32(48, 209, 88, (int)(240 * alpha)) : with_alpha(p.textDim, 240), 16); const char* st = active ? "\xD0\x98\xD0\xBD\xD0\xB6\xD0\xB5\xD0\xBA\xD1\x82\xD0\xB8\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD" : "\xD0\x9D\xD0\xB5 \xD0\xB8\xD0\xBD\xD0\xB6\xD0\xB5\xD0\xBA\xD1\x82\xD0\xB8\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD"; dl->AddText(f, 11.0f, ImVec2(dot.x + 10.0f, sb0.y + (sb_h - f->FontSize) * 0.5f + 0.5f), with_alpha(active ? p.text : p.textDim, (int)(240 * alpha)), st); char right[128]; ImGuiIO& io = ImGui::GetIO(); snprintf
(right, sizeof(right), "v%s   %.0f FPS", k_version, (Hook::get_game_fps() > 0.0f ? Hook::get_game_fps() : io.Framerate)); ImVec2 rs = f->CalcTextSizeA(11.0f, FLT_MAX, 0, right); dl->AddText(f, 11.0f, ImVec2(sb1.x - rs.x - 14.0f, sb0.y + (sb_h - f->FontSize) * 0.5f + 0.5f), with_alpha(p.textDim, (int)(240 * alpha)), right); } static void draw_about_window() { static bool s_prev_about = false; if (s_show_about && !s_prev_about) ImGui::OpenPopup("\xD0\x9E flaway"); s_prev_about = s_show_about; if (!s_show_about) return; if (!ImGui::BeginPopupModal("\xD0\x9E flaway", &s_show_about)) return; const
MacPalette& p = pal(); TextWrapColored(v4_u32(p.text), "flaway"); TextWrapColored(v4_u32(p.textDim), "v%s", k_version); TextWrapColored(v4_u32(p.textDim), "Utility client overlay for the game."); ImGui::Dummy(ImVec2(0, 6.0f)); if (GradientButton("\xD0\x97\xD0\xB0\xD0\xBA\xD1\x80\xD1\x8B\xD1\x82\xD1\x8C", ImVec2(140, 30))) s_show_about = false; ImGui::EndPopup(); } static void draw_menu_window() { ImGuiIO& io = ImGui::GetIO(); ImVec2 disp = io.DisplaySize; float mw = disp.x - 40.0f; if (mw > 960.0f) mw = 960.0f; if (mw < 600.0f) mw = 600.0f; float mh = disp.y - 40.0f; if (mh > 540.0f)
mh = 540.0f; if (mh < 420.0f) mh = 420.0f; float t = (float)(ImGui::GetTime() - s_menu_open_time); float p = t / 0.18f; float alpha = ease_out_cubic(clamp01(p)); ImVec2 size(mw, mh); ImVec2 ctr(disp.x * 0.5f, disp.y * 0.5f); { ImDrawList* bgdl = ImGui::GetBackgroundDrawList(); ImVec2 sp0(ctr.x - size.x * 0.5f, ctr.y - size.y * 0.5f); ImVec2 sp1(sp0.x + size.x, sp0.y + size.y); const float offs[4] = { 16.0f, 10.0f, 6.0f, 3.0f }; for (int i = 0; i < 4; i++) { float a = i == 0 ? 12.0f : (i == 1 ? 20.0f : (i == 2 ? 30.0f : 46.0f)); bgdl->AddRectFilled( ImVec2(sp0.x - offs[i], sp0.y - offs[i] * 0.5f), ImVec2(sp1.
x + offs[i], sp1.y + offs[i] * 1.5f), IM_COL32(0, 0, 0, (int)(a * alpha)), 26.0f + offs[i]); } } ImGui::SetNextWindowPos(ImVec2(ctr.x - size.x * 0.5f, ctr.y - size.y * 0.5f)); { static ImVec2 s_last_sz(-1.0f, -1.0f); static float s_last_al = -1.0f; static ImVec2 s_last_dp(-1.0f, -1.0f); if (size.x != s_last_sz.x || size.y != s_last_sz.y || alpha != s_last_al || disp.x != s_last_dp.x || disp.y != s_last_dp.y) { s_last_sz = size; s_last_al = alpha; s_last_dp = disp; rlog::logf("menu: want pos=(%.0f,%.0f) size=%.0fx%.0f alpha=%.3f disp=%.0fx%.0f", (double)(ctr.x - size.x * 0.5f), (double)(ctr.y - size.y * 0.5f), (double)size.x, (double)size.y, (double)alpha, (double)disp.x, (double)disp.y); } } ImGui::SetNextWindowSize(size); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha); ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f); ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0)); bool open_menu = ImGui::Begin("Flaway", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus |
ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings); { static int s_last_open = -1; int o = open_menu ? 1 : 0; if (o != s_last_open) { s_last_open = o; rlog::logf("menu: Begin(\"Flaway\") returned %d (%s)", o, open_menu ? "window submitted" : "not submitted -> NOTHING DRAWN this frame"); } } if (open_menu) { const MacPalette& p = pal(); ImDrawList* wdl = ImGui::GetWindowDrawList(); ImVec2 wmin = ImGui::GetWindowPos(); float ww = ImGui::GetWindowWidth(); float wh = ImGui::GetWindowHeight(); ImVec2 wmax(wmin.x + ww, wmin.y + wh); { static float s_lx = -1.0f, s_ly = -1.0f, s_lw2 = -1.0f, s_lh2 = -1.0f; if (wmin.x != s_lx || wmin.y != s_ly || ww != s_lw2 || wh != s_lh2) { s_lx = wmin.x; s_ly = wmin.y; s_lw2 = ww; s_lh2 = wh; rlog::logf("menu: actual window rect pos=(%.0f,%.0f) size=%.0fx%.0f", (double)wmin.x, (double)wmin.y, (double)ww, (double)wh); } } const float chrome = 0.0f; { const float r = std::min(std::min(12.0f, (wmax.x - wmin.x) * 0.5f), (wmax.y - wmin.y) * 0.5f); wdl->
AddRectFilled(ImVec2(wmin.x + 1, wmin.y + 1), ImVec2(wmax.x - 1, wmax.y - 1), p.windowBottom, r); wdl->AddRectFilled(ImVec2(wmin.x + 1, wmin.y + 1), ImVec2(wmax.x - 1, wmax.y - 1), IM_COL32(16, 17, 22, 210), r); { float sheen_h = (wmax.y - wmin.y) / 3.0f; ImU32 sheen = IM_COL32(255, 255, 255, is_dark() ? 14 : 40); wdl->AddRectFilledMultiColor( ImVec2(wmin.x + 1, wmin.y + 1), ImVec2(wmax.x - 1, wmin.y + 1 + sheen_h), sheen, sheen, IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0)); } wdl->AddRectFilled(ImVec2(wmin.x + 2, wmin.y + 1), ImVec2(wmax.x - 2, wmin.y + 3), IM_COL32(255, 255, 255, is_dark() ?
25 : 90), 1.0f); wdl->AddRect(wmin, wmax, frost(24), r, 0, 1.0f); wdl->AddRect(ImVec2(wmin.x + 1, wmin.y + 1), ImVec2(wmax.x - 1, wmax.y - 1), frost(is_dark() ? 8 : 40), std::max(r - 1.0f, 0.0f), 0, 1.0f); } ImVec2 s0(wmin.x, wmin.y + chrome); ImVec2 s1(wmin.x + 190.0f, wmax.y); draw_sidebar(p, wmin, s0, s1); ImVec2 c0(wmin.x + 190.0f, wmin.y + chrome); ImVec2 c1(wmax.x, wmax.y); wdl->AddRectFilled(c0, c1, IM_COL32(12, 13, 17, 255), 0.0f); const float gap = 16.0f; s_content_rmin = ImVec2(c0.x + gap, c0.y + gap); s_content_rmax = ImVec2(c1.x - gap, c1.y - gap); ImGui::SetCursorScreenPos(s_content_rmin); ImGui::BeginChild("##content", ImVec2(c1.x - c0.x - gap * 2.0f, c1.y - c0.y - gap * 2.0f), false); { static float s_lcw = -1.0f, s_lch = -1.0f; float cw = c1.x - c0.x - gap * 2.0f, ch = c1.y - c0.y - gap * 2.0f; if (cw != s_lcw || ch != s_lch) { s_lcw = cw; s_lch = ch; rlog::logf("menu: content child size=%.0fx%.0f%s", (double)cw, (double)ch, (cw <= 0.0f || ch <= 0.0f) ? " <- ZERO/NEGATIVE: page renders blank" : ""); } } { static int s_last_category = -1; if (s_last_category != s_active_category) { s_last_category = s_active_category; rlog::logf("menu: category -> %d", s_active_category); ImGui::SetScrollY(0.0f); } ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 12)); s_block_seq = 0; switch (s_active_category) { case 0: draw_combat_page(alpha); break; case 1: draw_player_page(alpha); break; case 2: draw_movement_page(alpha); break; case 3: draw_visuals_page(alpha); break; case 4: draw_hud_page(alpha); break; case 5: draw_storage_page(alpha); break; case 6: draw_theme_page(alpha); break; default: break; } ImGui::PopStyleVar(); } ImGui::EndChild(); } ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(3); draw_combo_overlay(alpha); if (s_show_about) draw_about_window(); } 

// Render diagnostics. Everything that can make the overlay look broken (fonts,
// icon glyphs, skipped windows, GL state) is logged to ~/.minecraft/
// flaway_render.txt through rlog() — see flaway/utils/rlog.h.
// ---------------------------------------------------------------------------

// Code points the UI actually draws with AddText() — if a font is missing one
// of these, the glyph falls back to '?' / a box and the icon "breaks".
static const ImWchar k_icon_probe[] = {
    0x26A1, // ⚡ hud / keybinds
    0x2726, // ✦ sidebar marker
    0x26C3, 0x279C, 0x2192,             // ⛃ ➜ →
    0x25BC, 0x25B6, 0x25C6,             // ▼ ▶ ◆
    0x2605, 0x2694, 0x269E,             // ★ ⚔ ⚞
    0x2713, 0x2716, 0x2318,             // ✓ ✖ ⌘
    0x2013, 0x2014, 0x2022,             // – — •
    0x042F, 0x044F, 0x0418, 0x0438,     // Я я И и (cyrillic coverage)
    0
};

static void audit_font_glyphs(const char* name, ImFont* font) {
    if (!font) { rlog::logf("glyph: font '%s' is NULL", name); return; }
    char missing[240];
    int off = 0;
    for (int i = 0; k_icon_probe[i]; i++) {
        ImWchar c = k_icon_probe[i];
        if (font->FindGlyphNoFallback(c)) continue;
        int n = snprintf(missing + off, sizeof(missing) - (size_t)off, "U+%04X ", (unsigned)c);
        if (n <= 0 || off + n >= (int)sizeof(missing)) break;
        off += n;
    }
    if (off == 0) snprintf(missing, sizeof(missing), "none");
    rlog::logf("glyph: font='%s' size=%.0f glyphs=%d atlas=%p missing=%s",
               name, (double)font->FontSize, font->Glyphs.Size,
               (void*)font->ContainerAtlas, missing);
}

// Runs right after ImGui::NewFrame(), i.e. after the GL backend has built the
// atlas: reports which icon glyphs every font is actually missing.
static void audit_glyphs(unsigned long long frame) {
    if (frame > 3 && (frame % 600) != 0) return;
    if (!ImGui::GetCurrentContext()) { rlog::logf("glyph: no ImGui context"); return; }
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (!atlas) { rlog::logf("glyph: io.Fonts is NULL"); return; }
    if (!atlas->IsBuilt()) {
        rlog::logf("glyph: atlas NOT built yet (fonts=%d texID=%p)", atlas->Fonts.Size, (void*)atlas->TexID);
        return;
    }
    rlog::logf("glyph: atlas built tex=%dx%d texID=%p fonts=%d ranges=%d",
               atlas->TexWidth, atlas->TexHeight, (void*)atlas->TexID,
               atlas->Fonts.Size, s_glyph_ranges.Size);
    audit_font_glyphs("main", s_font_main);
    audit_font_glyphs("bold", s_font_bold);
    audit_font_glyphs("small", s_font_small);
    audit_font_glyphs("hud", s_font_hud);
    audit_font_glyphs("tiny", s_font_tiny);
    audit_font_glyphs("icons", s_font_icons);
    ImFont* cur = ImGui::GetFont();
    rlog::logf("glyph: GetFont()=%p (%s) FontDefault=%p",
               (void*)cur, cur ? cur->GetDebugName() : "-", (void*)ImGui::GetIO().FontDefault);
}

// A window whose SkipItems/Hidden flags are set draws nothing even though
// Begin() ran — this is the usual cause of "the menu is empty".
static void audit_windows(const char* tag) {
    if (!GImGui) { rlog::logf("win: %s: GImGui is NULL", tag); return; }
    ImGuiContext& g = *GImGui;
    int total = 0, drawn = 0, skipped = 0, hidden = 0;
    char detail[600];
    int off = 0;
    for (int i = 0; i < g.Windows.Size; i++) {
        ImGuiWindow* w = g.Windows[i];
        if (!w || !w->Name) continue;
        total++;
        if (w->Hidden) hidden++;
        if (w->SkipItems) skipped++;
        if (!w->SkipItems && !w->Hidden) drawn++;
        bool interesting = w->SkipItems || w->Hidden || w->HiddenFramesCanSkipItems ||
                           w->HiddenFramesCannotSkipItems || w->HiddenFramesForRenderOnly;
        if (interesting && off < (int)sizeof(detail) - 110) {
            int n = snprintf(detail + off, sizeof(detail) - (size_t)off,
                             " %s[s=%d h=%d hf=%d/%d/%d vtx=%d]",
                             w->Name, (int)w->SkipItems, (int)w->Hidden,
                             (int)w->HiddenFramesCanSkipItems,
                             (int)w->HiddenFramesCannotSkipItems,
                             (int)w->HiddenFramesForRenderOnly,
                             w->DrawList ? w->DrawList->VtxBuffer.Size : -1);
            if (n > 0) off += n;
        }
    }
    if (off == 0) snprintf(detail, sizeof(detail), " (none skipped/hidden)");
    rlog::logf("win: %s: total=%d drawn=%d skipped=%d hidden=%d detail:%s",
               tag, total, drawn, skipped, hidden, detail);
}

// Logs every change of render-visible state: window size, display size,
// framebuffer scale (aliasing/blur), style alpha, current font, atlas texture.
static void track_render_state(int window_width, int window_height) {
    static int s_lw = -1, s_lh = -1;
    static ImVec2 s_ldisp(-1.0f, -1.0f), s_lfb(-1.0f, -1.0f);
    static float s_lalpha = -1.0f;
    static float s_lhi = -1.0f;
    static void* s_ltex = (void*)-1;
    static int s_lfonts = -1;
    static bool s_lbuilt = false;
    static void* s_lfont = (void*)-1;
    static int s_lmenu = -1;

    if (window_width != s_lw || window_height != s_lh) {
        s_lw = window_width; s_lh = window_height;
        rlog::logf("win: hook window=%dx%d", window_width, window_height);
    }
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x != s_ldisp.x || io.DisplaySize.y != s_ldisp.y ||
        io.DisplayFramebufferScale.x != s_lfb.x || io.DisplayFramebufferScale.y != s_lfb.y) {
        s_ldisp = io.DisplaySize; s_lfb = io.DisplayFramebufferScale;
        rlog::logf("io: DisplaySize=%.0fx%.0f fb_scale=%.2fx%.2f (text sharpness/aliasing depends on this)",
                   (double)io.DisplaySize.x, (double)io.DisplaySize.y,
                   (double)io.DisplayFramebufferScale.x, (double)io.DisplayFramebufferScale.y);
    }
    int menu = globals::show_gui ? 1 : 0;
    if (menu != s_lmenu) { s_lmenu = menu; rlog::logf("io: menu %s", menu ? "OPEN" : "closed"); }
    float alpha = ImGui::GetStyle().Alpha;
    if (alpha != s_lalpha) {
        s_lalpha = alpha;
        rlog::logf("style: Alpha=%.3f%s", (double)alpha, alpha <= 0.001f ? " <- EVERYTHING INVISIBLE" : "");
    }
    ImFontAtlas* atlas = io.Fonts;
    void* tex = atlas ? (void*)atlas->TexID : nullptr;
    int fonts = atlas ? atlas->Fonts.Size : -1;
    bool built = atlas && atlas->IsBuilt();
    if (tex != s_ltex || fonts != s_lfonts || built != s_lbuilt) {
        s_ltex = tex; s_lfonts = fonts; s_lbuilt = built;
        rlog::logf("atlas: texID=%p built=%d fonts=%d size=%dx%d", tex, (int)built, fonts,
                   atlas ? atlas->TexWidth : 0, atlas ? atlas->TexHeight : 0);
    }
    // g.Font is only assigned inside ImGui::NewFrame(), so on the very first
    // frame it is still NULL — don't report that as an error.
    if (rlog::frame_no() > 1) {
        ImFont* cur = ImGui::GetCurrentContext() ? ImGui::GetFont() : nullptr;
        if ((void*)cur != s_lfont) {
            s_lfont = (void*)cur;
            rlog::logf("font: GetFont()=%p %s size=%.0f", (void*)cur,
                       cur ? cur->GetDebugName() : "(NULL!)", cur ? (double)cur->FontSize : 0.0);
            if (!cur) rlog::logf("font: CURRENT FONT IS NULL -> text vanishes or crashes");
        }
    }
    float dt = io.DeltaTime;
    float hi = dt > 0.05f ? 1.0f : 0.0f;
    if (s_lhi < 0.0f || hi != s_lhi) {
        if (s_lhi >= 0.0f && hi > 0.0f) rlog::logf("io: DeltaTime spike %.1fms", (double)(dt * 1000.0f));
        s_lhi = hi;
    }
    // Frame-time window so FPS regressions are measurable from the log:
    // one "perf:" line every ~5s with the average and the worst frame.
    {
        static double s_sum = 0.0, s_worst = 0.0;
        static int s_n = 0;
        static unsigned long long s_last = 0;
        if (dt > 0.0f && dt < 1.0f) {
            s_sum += (double)dt;
            if ((double)dt > s_worst) s_worst = (double)dt;
            s_n++;
        }
        if (s_n > 0 && rlog::frame_no() - s_last >= 300) {
            rlog::logf("perf: %d frames avg=%.2fms worst=%.2fms (~%.0f fps)",
                       s_n, s_sum / (double)s_n * 1000.0, s_worst * 1000.0,
                       (double)s_n / s_sum);
            s_last = rlog::frame_no();
            s_sum = 0.0; s_worst = 0.0; s_n = 0;
        }
    }
}

bool GUI::render(int window_width, int window_height, const uint8_t* /*captured_frame*/) {
    rlog::frame();
    const unsigned long long frame = rlog::frame_no();
    rlog::gl_drain("render.entry");
    if (frame <= 3) rlog::gl_viewport("render.entry");
    gui_diag("[G] render: entered");
    if (!g_is_init) { rlog::logf("render: early-out (!g_is_init)"); gui_diag("[G] render: !g_is_init"); return false; }
    if (!needs_overlay()) { rlog::logf("render: early-out (needs_overlay=false: menu closed + no overlay module)"); gui_diag("[G] render: !needs_overlay"); return false; }
    if (s_overlay_disabled) { rlog::logf("render: early-out (s_overlay_disabled)"); gui_diag("[G] render: s_overlay_disabled"); return false; }
    ImGuiIO& io = ImGui::GetIO();
    if (window_width > 0 && window_height > 0) {
        io.DisplaySize = ImVec2((float)window_width, (float)window_height);
    }
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) { rlog::logf("render: early-out (bad DisplaySize %.1fx%.1f, win=%dx%d)", (double)io.DisplaySize.x, (double)io.DisplaySize.y, window_width, window_height); gui_diag("[G] render: bad DisplaySize"); return false; }
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    track_render_state(window_width, window_height);
    // Theme application point: the Appearance buttons and a config load only
    // write globals::theme_*; pick them up at the top of the frame so the
    // whole frame renders with one palette and the ImGui style colors (slider
    // grab, separators, ...) follow the chosen accent/dark mode.
    {
        static int s_last_accent = 0, s_last_grad = -1;
        bool dark_changed = globals::theme_dark != s_dark_mode;
        if (dark_changed) s_dark_mode = globals::theme_dark;
        if (dark_changed || globals::theme_accent != s_last_accent ||
            globals::theme_grad != s_last_grad) {
            s_last_accent = globals::theme_accent;
            s_last_grad = globals::theme_grad;
            setup_style();
        }
    }

    static bool s_was_shown = false;
    static int s_render_count = 0;
    bool shown = globals::show_gui;
    if (shown && !s_was_shown) { s_reset_input = true; s_menu_open_time = ImGui::GetTime(); rlog::logf("render: menu OPEN transition"); }
    if (!shown && s_was_shown) rlog::logf("render: menu CLOSE transition");
    s_was_shown = shown;
    if (shown) update_keybind_waiting();
    feed_input_to_imgui(shown || flaway::hud::edit_active());
    if (s_render_count < 3) gui_diag("[G] render: before NewFrame");
    // Snapshot the game's GL state before we draw anything: compared against a
    // second snapshot right before returning, any difference is state our
    // overlay leaked into the game's context (Minecraft/Sodium may cache it).
    static char s_gl_before[512];
    s_gl_before[0] = 0;
    rlog::gl_state_snapshot(s_gl_before, sizeof(s_gl_before));
    ImGui_ImplOpenGL3_NewFrame();
    if (s_render_count < 3) gui_diag("[G] render: before ImGui::NewFrame");
    ImGui::NewFrame();
    if (s_render_count < 3) { char buf[128]; snprintf(buf, sizeof(buf), "[G] render#%d: shown=%d size=(%.0f,%.0f)", s_render_count, (int)shown, io.DisplaySize.x, io.DisplaySize.y); gui_diag(buf); }
    s_render_count++;
    audit_glyphs(frame);
    rlog::gl_drain("render.after_NewFrame");
    flaway::modules::esp::draw_boxes();
    flaway::modules::storage_esp::draw_boxes();
    flaway::modules::base_finder::draw_boxes();
    flaway::modules::backtrack::draw_indicators();
    if (globals::aimassist_enabled && !key_waiting) flaway::modules::aimassist::draw_fov();
    if (shown) { draw_menu_window(); } else { flaway::hud::draw(); }
    static int s_last_menu_flag = -1;
    int menu_flag = shown ? 1 : 0;
    if (frame <= 3 || menu_flag != s_last_menu_flag || (frame % 300) == 0) {
        audit_windows(shown ? "menu-open" : "hud");
        s_last_menu_flag = menu_flag;
    }
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data && draw_data->CmdListsCount > 0 && draw_data->TotalVtxCount > 0 && draw_data->TotalIdxCount > 0) {
        static int s_lv = -1, s_li = -1, s_lc = -1;
        static ImVec2 s_ld(-1.0f, -1.0f), s_ls(-1.0f, -1.0f);
        if (draw_data->TotalVtxCount != s_lv || draw_data->TotalIdxCount != s_li ||
            draw_data->CmdListsCount != s_lc || draw_data->DisplaySize.x != s_ld.x ||
            draw_data->DisplaySize.y != s_ld.y || draw_data->FramebufferScale.x != s_ls.x ||
            draw_data->FramebufferScale.y != s_ls.y) {
            s_lv = draw_data->TotalVtxCount; s_li = draw_data->TotalIdxCount; s_lc = draw_data->CmdListsCount;
            s_ld = draw_data->DisplaySize; s_ls = draw_data->FramebufferScale;
            rlog::logf("draw: lists=%d vtx=%d idx=%d disp=%.0fx%.0f fb_scale=%.2f,%.2f",
                       draw_data->CmdListsCount, draw_data->TotalVtxCount, draw_data->TotalIdxCount,
                       (double)draw_data->DisplaySize.x, (double)draw_data->DisplaySize.y,
                       (double)draw_data->FramebufferScale.x, (double)draw_data->FramebufferScale.y);
        }
        if (frame % 300 == 0)
            rlog::logf("hb: frame=%llu shown=%d lists=%d vtx=%d idx=%d alpha=%.2f texID=%p",
                       frame, (int)shown, draw_data->CmdListsCount, draw_data->TotalVtxCount,
                       draw_data->TotalIdxCount, (double)ImGui::GetStyle().Alpha,
                       (void*)io.Fonts->TexID);
        if (s_render_count <= 4) { char buf[128]; snprintf(buf, sizeof(buf), "[G] render#%d: draw_data=%p cmds=%d vtx=%d idx=%d", s_render_count-1, draw_data, draw_data->CmdListsCount, draw_data->TotalVtxCount, draw_data->TotalIdxCount); gui_diag(buf); }
        ImGui_ImplOpenGL3_RenderDrawData(draw_data);
        rlog::gl_drain("render.after_RenderDrawData");
        if (frame <= 3 || (frame % 300) == 0) rlog::gl_viewport("render.after_RenderDrawData");
        if (s_render_count <= 4) gui_diag("[G] render: RenderDrawData done");
    } else {
        rlog::logf("draw: EMPTY draw_data=%p lists=%d vtx=%d idx=%d shown=%d menu=%d frame=%llu",
                   (void*)draw_data,
                   draw_data ? draw_data->CmdListsCount : -1,
                   draw_data ? draw_data->TotalVtxCount : -1,
                   draw_data ? draw_data->TotalIdxCount : -1,
                   (int)shown, globals::show_gui ? 1 : 0, frame);
        if (s_render_count <= 4) gui_diag("[G] render#?: draw_data=NULL/empty");
    }

    {
        char st_after[512];
        if (s_gl_before[0] && rlog::gl_state_snapshot(st_after, sizeof(st_after)) &&
            strcmp(st_after, s_gl_before) != 0) {
            rlog::logf("gl: overlay CHANGED GL state | before: %s", s_gl_before);
            rlog::logf("gl: overlay CHANGED GL state | after : %s", st_after);
        }
    }

    return true;
}
