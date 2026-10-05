#include "hud_internal.h"
#include "../../config/config.h"
#include "../../utils/rlog.h"
#include "../glass_blur.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Style primitives: glassmorphism cards, animated accent gradients, helpers.
// Colors come from the menu theme (GUI::text_* / card_color / accent).
// ---------------------------------------------------------------------------
namespace hstyle {
namespace {

unsigned s_blur_tex = 0;

void rgb_to_hsl(ImU32 c, float& h, float& s, float& l) {
    float r = (c & 255) / 255.0f, g = ((c >> 8) & 255) / 255.0f, b = ((c >> 16) & 255) / 255.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    l = (mx + mn) * 0.5f;
    if (mx == mn) { h = 0.0f; s = 0.0f; return; }
    float d = mx - mn;
    s = l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
    if (mx == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g) h = (b - r) / d + 2.0f;
    else h = (r - g) / d + 4.0f;
    h /= 6.0f;
}

// Opaque glass body used when there is no blurred backdrop yet.
void glass_body(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float r, float a) {
    ImU32 base = GUI::card_color();
    ImU32 top = with_a(lerp(base, IM_COL32(255, 255, 255, 255), 0.05f), (int)(170 * a));
    ImU32 bot = with_a(base, (int)(180 * a));
    dl->AddRectFilledMultiColor(p0, p1, top, top, bot, bot);
}

} // namespace

void set_blur_tex(unsigned tex) { s_blur_tex = tex; }

ImU32 hsl(float h, float s, float l, int a) {
    h = h - floorf(h);
    float r = l, g = l, b = l;
    if (s > 0.0f) {
        float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
        float p = 2.0f * l - q;
        auto hue2rgb = [](float pp, float qq, float t) -> float {
            if (t < 0.0f) t += 1.0f;
            if (t > 1.0f) t -= 1.0f;
            if (t < 1.0f / 6.0f) return pp + (qq - pp) * 6.0f * t;
            if (t < 0.5f) return qq;
            if (t < 2.0f / 3.0f) return pp + (qq - pp) * (2.0f / 3.0f - t) * 6.0f;
            return pp;
        };
        r = hue2rgb(p, q, h + 1.0f / 3.0f);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1.0f / 3.0f);
    }
    return IM_COL32((int)(r * 255.0f), (int)(g * 255.0f), (int)(b * 255.0f), a);
}

ImU32 accent() { return GUI::accent_a(); }

// Gradient stops for every glass card / bar / ESP outline. With a theme
// gradient preset selected the pair comes straight from the preset (still
// slowly drifting so it stays alive); otherwise both stops are derived from
// the current accent swatch, so a theme change in the menu recolors the HUD
// and the ESP together.
ImU32 grad_a(float phase) {
    unsigned pa = 0, pb = 0;
    bool preset = GUI::gradient_pair(&pa, &pb);
    float h, s, l;
    rgb_to_hsl(preset ? pa : GUI::accent_a(), h, s, l);
    float t = time_now();
    float drift = 0.045f * sinf(t * 0.6f + phase * 6.2831853f);
    float ls = preset ? (l < 0.42f ? 0.42f : l) : (l < 0.42f ? 0.55f : l);
    float ss = preset ? s : (s < 0.55f ? 0.85f : s);
    return hsl(h + drift, ss, ls, 255);
}

ImU32 grad_b(float phase) {
    unsigned pa = 0, pb = 0;
    bool preset = GUI::gradient_pair(&pa, &pb);
    float h, s, l;
    rgb_to_hsl(preset ? pb : GUI::accent_a(), h, s, l);
    float t = time_now();
    float drift = 0.055f * sinf(t * 0.6f + phase * 6.2831853f + 1.7f);
    float ls = l + (preset ? 0.0f : 0.10f);
    if (ls > 0.86f) ls = 0.86f;
    float ss = preset ? s : (s < 0.55f ? 0.80f : s);
    return hsl(h + (preset ? 0.0f : 0.14f) + drift, ss, ls, 255);
}

ImU32 grad(float t, float phase) { return lerp(grad_a(phase), grad_b(phase), t); }

ImU32 text_main() { return GUI::text_primary(); }
ImU32 text_dim() { return GUI::text_dim(); }
ImU32 text_faint() { return GUI::text_faint(); }

ImFont* font() { return GUI::font_hud(); }
ImFont* font_bold() { return GUI::font_hud_bold(); }

void card(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, const Card& c) {
    if (!dl) return;
    float w = p1.x - p0.x, h = p1.y - p0.y;
    if (w < 1.0f || h < 1.0f) return;
    float a = clamp01(c.alpha);
    if (a <= 0.004f) return;
    float r = c.rounding;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0.0f) r = 0.0f;

    bool blurred = false;
    if (c.blur && s_blur_tex != 0) {
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        if (disp.x > 1.0f && disp.y > 1.0f) {
            ImVec2 uv0(p0.x / disp.x, 1.0f - p0.y / disp.y);
            ImVec2 uv1(p1.x / disp.x, 1.0f - p1.y / disp.y);
            dl->AddImageRounded((ImTextureID)(intptr_t)s_blur_tex, p0, p1, uv0, uv1,
                                IM_COL32(255, 255, 255, 255), r);
            dl->AddRectFilled(p0, p1, with_a(GUI::card_color(), (int)(150 * a)), r);
            blurred = true;
        }
    }
    if (!blurred) glass_body(dl, p0, p1, r, a);

    dl->AddRect(p0, p1, with_a(text_main(), (int)(22 * a)), r, 0, 1.0f);

    float ga = clamp01(c.glow);
    if (c.accent_border && ga <= 0.001f) ga = 0.55f;
    if (c.accent_border || c.glow > 0.001f) {
        ImU32 ac = accent();
        dl->AddRect(p0, p1, with_a(ac, (int)(110 * ga * a)), r, 0, 1.2f);
        if (ga > 0.05f) {
            dl->AddRect(ImVec2(p0.x - 2.5f, p0.y - 2.5f), ImVec2(p1.x + 2.5f, p1.y + 2.5f),
                        with_a(ac, (int)(46 * ga * a)), r + 2.5f, 0, 1.4f);
            dl->AddRect(ImVec2(p0.x - 5.5f, p0.y - 5.5f), ImVec2(p1.x + 5.5f, p1.y + 5.5f),
                        with_a(ac, (int)(20 * ga * a)), r + 5.5f, 0, 1.0f);
        }
    }
    if (c.hover > 0.01f) {
        float hv = clamp01(c.hover);
        ImU32 ac = accent();
        dl->AddRect(ImVec2(p0.x - 3.0f, p0.y - 3.0f), ImVec2(p1.x + 3.0f, p1.y + 3.0f),
                    with_a(ac, (int)(70 * hv)), r + 3.0f, 0, 1.6f);
        dl->AddRect(ImVec2(p0.x - 7.0f, p0.y - 7.0f), ImVec2(p1.x + 7.0f, p1.y + 7.0f),
                    with_a(ac, (int)(26 * hv)), r + 7.0f, 0, 1.2f);
    }
}

// --- digit roller ----------------------------------------------------------
// Per-slot state so counters only ever animate against their own previous
// value. A value change that lands mid-transition is parked in `pending` and
// starts on the next boundary, so a fast-changing FPS/HP read never thrashes.
struct RollSlot {
    std::string prev, cur, pending;
    float t = 1.0f;       // 0..1, 1 = settled
    float next_ok = 0.0f; // ImGui::GetTime() before another roll may start
};
std::unordered_map<std::string, RollSlot> s_roll;

float roll_prefix(ImFont* f, float fs, const std::string& s, size_t n) {
    if (!f || n == 0 || s.empty()) return 0.0f;
    if (n >= s.size()) return f->CalcTextSizeA(fs, FLT_MAX, 0.0f, s.c_str()).x;
    return f->CalcTextSizeA(fs, FLT_MAX, 0.0f, s.c_str(), s.c_str() + n).x;
}

bool roll_has_utf8(const std::string& a, const std::string& b) {
    for (unsigned char c : a) if (c >= 0x80) return true;
    for (unsigned char c : b) if (c >= 0x80) return true;
    return false;
}

void roll_glyph(ImDrawList* dl, ImFont* f, float fs, float x, float y,
                const std::string& s, size_t i, ImU32 col) {
    if (((col >> 24) & 0xFF) == 0) return;
    char buf[2] = {s[i], 0};
    dl->AddText(f, fs, ImVec2(x, y), col, buf);
}

void rolling_text(ImDrawList* dl, ImFont* font, float size, const ImVec2& p,
                  ImU32 col, const char* text, const char* key,
                  const ImVec2& clip0, const ImVec2& clip1) {
    if (!dl || !font || !text || !*text) return;
    if (!key) {
        dl->AddText(font, size, p, col, text);
        return;
    }

    RollSlot& s = s_roll[key];
    const float now = (float)ImGui::GetTime();
    // Always park the newest value; it starts rolling only once the previous
    // transition has settled AND its cooldown elapsed, so a per-frame counter
    // (FPS) rolls a couple of times a second instead of never resting.
    if (s.cur != text) s.pending = text;
    else if (s.pending == s.cur) s.pending.clear();

    if (s.t >= 1.0f && !s.pending.empty() && s.pending != s.cur && now >= s.next_ok) {
        s.prev = s.cur;
        s.cur = s.pending;
        s.pending.clear();
        s.t = 0.0f;
        s.next_ok = now + 0.45f;
    }

    if (s.t >= 1.0f) {
        dl->AddText(font, size, p, col, s.cur.c_str());
        return;
    }
    if (roll_has_utf8(s.prev, s.cur)) {  // one byte per glyph below
        s.t = 1.0f;
        dl->AddText(font, size, p, col, s.cur.c_str());
        return;
    }

    s.t += ImGui::GetIO().DeltaTime / 0.34f;
    if (s.t > 1.0f) s.t = 1.0f;

    const size_t n = s.prev.size() > s.cur.size() ? s.prev.size() : s.cur.size();
    if (n == 0) return;
    const float stag = 0.075f;
    const float units = 1.0f + (float)(n - 1) * stag;
    const float rise = size * 0.55f;
    const ImU32 rgb = col & 0x00FFFFFFu;
    const int base_a = (int)((col >> 24) & 0xFF);

    dl->PushClipRect(clip0, clip1, true);
    for (size_t i = 0; i < n; i++) {
        const bool has_old = i < s.prev.size();
        const bool has_new = i < s.cur.size();
        if (has_old && has_new && s.prev[i] == s.cur[i]) {
            roll_glyph(dl, font, size, p.x + roll_prefix(font, size, s.cur, i), p.y,
                       s.cur, i, col);
            continue;
        }
        float local = s.t * units - (float)i * stag;
        if (local < 0.0f) local = 0.0f;
        else if (local > 1.0f) local = 1.0f;
        const float e = ease_out(local);
        if (has_old) {
            int a = (int)(base_a * (1.0f - e));
            if (a > 0)
                roll_glyph(dl, font, size, p.x + roll_prefix(font, size, s.prev, i),
                           p.y - rise * e, s.prev, i, rgb | ((ImU32)a << 24));
        }
        if (has_new) {
            int a = (int)(base_a * e);
            if (a > 0)
                roll_glyph(dl, font, size, p.x + roll_prefix(font, size, s.cur, i),
                           p.y + rise * (1.0f - e), s.cur, i, rgb | ((ImU32)a << 24));
        }
    }
    dl->PopClipRect();
}

void pill(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float alpha) {
    Card c;
    float h = p1.y - p0.y;
    c.rounding = h * 0.5f;      // full capsule (card() clamps to h*0.5)
    c.alpha = alpha;
    c.blur = false;
    card(dl, p0, p1, c);
}

void gradient_text(ImDrawList* dl, const ImVec2& pos, const char* text, ImFont* font,
                   float size, ImU32 c0, ImU32 c1, int alpha) {
    if (!dl || !text || !font) return;
    float x = pos.x;
    size_t n = strlen(text);
    for (size_t i = 0; i < n; i++) {
        char ch[2] = { text[i], 0 };
        float t = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
        dl->AddText(font, size, ImVec2(x, pos.y), with_a(lerp(c0, c1, t), alpha), ch);
        x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, ch).x;
    }
}

} // namespace hstyle

// ---------------------------------------------------------------------------
// HUD core: layout, edit mode (chat open), blur refresh, menu preview.
// ---------------------------------------------------------------------------
namespace flaway {
namespace hud {
namespace {

ImVec2 s_r0[E_COUNT], s_r1[E_COUNT];
float s_s[E_COUNT];
bool s_vis[E_COUNT];
float s_hover[E_COUNT];
float s_appear = 0.0f;
int s_drag = -1;
bool s_drag_moved = false;
ImVec2 s_drag_off;
int s_ctx = -1;
ImVec2 s_ctx0, s_ctx1;
bool s_blur_init_done = false, s_blur_ok = false;

bool* const k_enabled[E_COUNT] = {
    &globals::hud_watermark_enabled, &globals::hud_keybinds_enabled,
    &globals::hud_target_enabled, &globals::hud_coords_enabled,
    &globals::hud_pickups_enabled, &globals::hud_poison_enabled,
    &globals::hud_arraylist_enabled,
};
float* const k_scale[E_COUNT] = {
    &globals::hud_watermark_scale, &globals::hud_keybinds_scale,
    &globals::hud_target_scale, &globals::hud_coords_scale,
    &globals::hud_pickups_scale, &globals::hud_poison_scale,
    &globals::hud_arraylist_scale,
};
float* const k_pos[E_COUNT] = {
    globals::hud_watermark_pos, globals::hud_keybinds_pos,
    globals::hud_target_pos, globals::hud_coords_pos,
    globals::hud_pickups_pos, globals::hud_poison_pos,
    globals::hud_arraylist_pos,
};
const char* const k_names[E_COUNT] = {
    "Watermark", "Keybinds", "Target HUD", "Coords", "Pickups", "Poison", "Array List",
};

// Right-side elements anchor their *right* edge (pos[0] = inset from the
// right border), so the array list stays pinned top-right at the 12,12 default.
bool right_anchored(int id) { return id == E_ARRAYLIST || id == E_PICKUPS; }

void norm_pos(float* p) {
    if (!(p[0] >= 0.0f) || p[0] > 100000.0f) p[0] = 12.0f; // NaN / legacy -1
    if (!(p[1] >= 0.0f) || p[1] > 100000.0f) p[1] = 12.0f;
}

float safe_scale(float* f, float lo, float hi, float def) {
    float v = *f;
    if (!(v >= lo && v <= hi)) { *f = def; v = def; }
    return v;
}

void place_rect(int id, float W) {
    ImVec2 size = s_r1[id] - s_r0[id];
    norm_pos(k_pos[id]);
    float x = right_anchored(id) ? W - k_pos[id][0] - size.x : k_pos[id][0];
    s_r0[id] = ImVec2(x, k_pos[id][1]);
    s_r1[id] = ImVec2(x + size.x, k_pos[id][1] + size.y);
}

void update_blur(bool want, int w, int h) {
    if (!want || w <= 0 || h <= 0) { hstyle::set_blur_tex(0); return; }
    if (!s_blur_init_done) {
        s_blur_init_done = true;
        s_blur_ok = glass_blur::init();
        if (!s_blur_ok) rlog::logf("hud: glass_blur init failed -> glass cards render opaque");
    }
    if (!s_blur_ok) { hstyle::set_blur_tex(0); return; }
    static int s_tick = 0;
    if ((++s_tick % 3) != 1) return; // capture at most every 3rd frame
    glass_blur::capture_framebuffer(w, h);
    unsigned tex = glass_blur::blur(2);
    hstyle::set_blur_tex(tex);
}

void draw_grid(ImDrawList* dl, float W, float H, float appear) {
    ImU32 minor = hstyle::with_a(hstyle::text_main(), (int)(30 * appear));
    ImU32 major = hstyle::with_a(hstyle::accent(), (int)(70 * appear));
    for (float x = 16.0f; x < W; x += 16.0f)
        dl->AddLine(ImVec2(x, 0.0f), ImVec2(x, H), ((int)x % 64 == 0) ? major : minor, 1.0f);
    for (float y = 16.0f; y < H; y += 16.0f)
        dl->AddLine(ImVec2(0.0f, y), ImVec2(W, y), ((int)y % 64 == 0) ? major : minor, 1.0f);
}

void hover_glow(ImDrawList* dl, const ImVec2& r0, const ImVec2& r1, float a) {
    if (a <= 0.02f) return;
    ImU32 ac = hstyle::accent();
    dl->AddRect(ImVec2(r0.x - 3.0f, r0.y - 3.0f), ImVec2(r1.x + 3.0f, r1.y + 3.0f),
                hstyle::with_a(ac, (int)(64 * a)), 14.0f, 0, 1.6f);
    dl->AddRect(ImVec2(r0.x - 7.0f, r0.y - 7.0f), ImVec2(r1.x + 7.0f, r1.y + 7.0f),
                hstyle::with_a(ac, (int)(22 * a)), 18.0f, 0, 1.2f);
}

// --- context menu (ПКМ): Скрыть / Сброс позиции / Размер S M L -------------
const float k_ctx_w = 178.0f, k_ctx_h = 98.0f;

ImVec2 ctx_menu_pos(int id, const ImVec2& el0, const ImVec2& el1) {
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImVec2 pos(el1.x + 8.0f, el0.y);
    if (pos.x + k_ctx_w > disp.x - 8.0f) pos.x = el0.x - k_ctx_w - 8.0f;
    if (pos.x < 8.0f) pos.x = 8.0f;
    if (pos.y + k_ctx_h > disp.y - 8.0f) pos.y = disp.y - k_ctx_h - 8.0f;
    if (pos.y < 8.0f) pos.y = 8.0f;
    return pos;
}

bool in_rect(const ImVec2& p, const ImVec2& a, float w, float h) {
    return p.x >= a.x && p.x <= a.x + w && p.y >= a.y && p.y <= a.y + h;
}

bool in_box(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y;
}

void draw_ctx_menu(ImDrawList* dl, int id, float appear) {
    if (s_ctx < 0 || !s_vis[s_ctx]) return;
    const ImVec2 pos = s_ctx0;
    const float x0 = pos.x, y0 = pos.y;
    const float x1 = x0 + k_ctx_w, y1 = y0 + k_ctx_h;
    float a = appear;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
                      hstyle::with_a(GUI::card_color(), (int)(242 * a)), 10.0f);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), hstyle::with_a(hstyle::text_main(), (int)(30 * a)), 10.0f, 0, 1.0f);

    ImFont* f = hstyle::font();
    if (!f) return;
    float dt = ImGui::GetIO().DeltaTime;
    static float s_hv[5];
    ImVec2 m = ImGui::GetIO().MousePos;
    // rows: 0 hide, 1 reset, 2 S, 3 M, 4 L
    ImVec2 rows[5] = {
        ImVec2(x0 + 5, y0 + 5), ImVec2(x0 + 5, y0 + 35),
        ImVec2(x0 + k_ctx_w - 101, y0 + 67), ImVec2(x0 + k_ctx_w - 65, y0 + 67),
        ImVec2(x0 + k_ctx_w - 29, y0 + 67),
    };
    const char* labels[5] = {
        "\xD0\xA1\xD0\xBA\xD1\x80\xD1\x8B\xD1\x82\xD1\x8C",                 // Скрыть
        "\xD0\xA1\xD0\xB1\xD1\x80\xD0\xBE\xD1\x81 \xD0\xBF\xD0\xBE\xD0\xB7\xD0\xB8\xD1\x86\xD0\xB8\xD0\xB8", // Сброс позиции
        "S", "M", "L",
    };
    float w[5] = { k_ctx_w - 10.0f, k_ctx_w - 10.0f, 32.0f, 32.0f, 32.0f };
    float h[5] = { 26.0f, 26.0f, 26.0f, 26.0f, 26.0f };
    float fs = 12.0f;
    for (int i = 0; i < 5; i++) {
        bool hv = in_rect(m, rows[i], w[i], h[i]);
        s_hv[i] = hstyle::damp(s_hv[i], hv ? 1.0f : 0.0f, 0.18f, dt);
        ImVec2 b0 = rows[i], b1(rows[i].x + w[i], rows[i].y + h[i]);
        if (s_hv[i] > 0.02f)
            dl->AddRectFilled(b0, b1, hstyle::with_a(hstyle::accent(), (int)(36 * s_hv[i] * a)), 6.0f);
        dl->AddRect(b0, b1, hstyle::with_a(hstyle::text_main(), (int)(26 * a)), 6.0f, 0, 1.0f);
        ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, labels[i]);
        dl->AddText(f, fs, ImVec2(b0.x + (w[i] - ts.x) * 0.5f, b0.y + (h[i] - ts.y) * 0.5f),
                    hstyle::with_a(i >= 2 ? hstyle::text_main() : hstyle::text_dim(),
                                   (int)(240 * a)),
                    labels[i]);
    }
    // "Размер" label
    const char* sz_lbl = "\xD0\xA0\xD0\xB0\xD0\xB7\xD0\xBC\xD0\xB5\xD1\x80"; // Размер
    ImVec2 ls = f->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, sz_lbl);
    dl->AddText(f, 11.0f, ImVec2(x0 + 10.0f, y0 + 67.0f + (26.0f - ls.y) * 0.5f),
                hstyle::with_a(hstyle::text_faint(), (int)(230 * a)), sz_lbl);

    // active size chip highlight
    float cur = safe_scale(k_scale[id], 0.4f, 3.0f, 1.0f);
    const float presets[3] = { 0.85f, 1.0f, 1.25f };
    for (int i = 0; i < 3; i++) {
        if (fabsf(cur - presets[i]) < 0.07f) {
            ImVec2 b0 = rows[2 + i], b1(b0.x + 32.0f, b0.y + 26.0f);
            dl->AddRect(b0, b1, hstyle::with_a(hstyle::accent(), (int)(170 * a)), 6.0f, 0, 1.4f);
        }
    }
}

// Returns true when the click was consumed by the context menu.
bool ctx_menu_click(int id, const ImVec2& mouse) {
    if (s_ctx < 0 || !s_vis[s_ctx]) return false;
    const ImVec2 pos = s_ctx0;
    if (!in_rect(mouse, pos, k_ctx_w, k_ctx_h))
        return false;
    ImVec2 rows[5] = {
        ImVec2(pos.x + 5, pos.y + 5), ImVec2(pos.x + 5, pos.y + 35),
        ImVec2(pos.x + k_ctx_w - 101, pos.y + 67), ImVec2(pos.x + k_ctx_w - 65, pos.y + 67),
        ImVec2(pos.x + k_ctx_w - 29, pos.y + 67),
    };
    float w[5] = { k_ctx_w - 10.0f, k_ctx_w - 10.0f, 32.0f, 32.0f, 32.0f };
    for (int i = 0; i < 5; i++) {
        if (!in_rect(mouse, rows[i], w[i], 26.0f))
            continue;
        if (i == 0) { *k_enabled[s_ctx] = false; config::save_auto(); s_ctx = -1; }
        else if (i == 1) { k_pos[s_ctx][0] = 12.0f; k_pos[s_ctx][1] = 12.0f; config::save_auto(); s_ctx = -1; }
        else { *k_scale[s_ctx] = (i == 2) ? 0.85f : (i == 3) ? 1.0f : 1.25f; config::save_auto(); }
        return true;
    }
    return true;
}

void measure_all(ImDrawList* dl, bool edit, float gs, float appear, long long now, float W) {
    for (int id = 0; id < E_COUNT; id++) {
        s_vis[id] = false;
        bool on = *k_enabled[id];
        if (!on && !edit) continue;
        float es = safe_scale(k_scale[id], 0.4f, 3.0f, 1.0f);
        helem::Ctx c;
        c.dl = dl;
        c.edit = edit;
        c.ghost = !on;
        // Disabled elements still have to be placeable while the chat is open,
        // but they must not read as real content: a 0.75 ghost is as bright as
        // a live card and shows up as a grey slab next to the active pills.
        c.alpha = appear * (on ? 1.0f : 0.38f);
        c.s = gs * es * (1.0f + 0.02f * hstyle::clamp01(s_hover[id]));
        c.now_us = now;
        ImVec2 size = helem::ops(id)->measure(c);
        if (size.x < 2.0f || size.y < 2.0f) continue;
        norm_pos(k_pos[id]);
        float x = right_anchored(id) ? W - k_pos[id][0] - size.x : k_pos[id][0];
        s_r0[id] = ImVec2(x, k_pos[id][1]);
        s_r1[id] = ImVec2(x + size.x, k_pos[id][1] + size.y);
        s_s[id] = c.s;
        s_vis[id] = true;
    }
}

} // namespace

void draw() {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;
    hud_fx::begin_frame();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    if (W < 32.0f || H < 32.0f) return;

    hud_data::poll();
    const bool edit = hud_data::chat_open();
    const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
    const long long now = hud_data::now_us();

    bool any = false;
    for (int id = 0; id < E_COUNT; id++) if (*k_enabled[id]) { any = true; break; }
    const bool f1 = hud_data::hud_f1_shown();
    const float target = ((any && f1) || edit) ? 1.0f : 0.0f;
    s_appear = hstyle::damp(s_appear, target, 0.12f, dt);
    if (s_appear < 0.01f && target <= 0.0f) { hstyle::set_blur_tex(0); return; }

    bool want_blur = edit;
    for (int id = 0; id < E_COUNT && !want_blur; id++)
        if (*k_enabled[id] && helem::ops(id)->blur) want_blur = true;
    update_blur(want_blur, (int)W, (int)H);

    float gs = safe_scale(&globals::hud_scale, 0.5f, 2.0f, 1.0f);

    if (edit) draw_grid(dl, W, H, s_appear);

    measure_all(dl, edit, gs, s_appear, now, W);

    const ImVec2 mouse = io.MousePos;
    const bool click0 = io.MouseClicked[0];
    const bool click2 = io.MouseClicked[1];

    int hover = -1;
    if (edit) {
        for (int id = E_COUNT - 1; id >= 0; id--) {
            if (!s_vis[id]) continue;
            if (mouse.x >= s_r0[id].x && mouse.x <= s_r1[id].x &&
                mouse.y >= s_r0[id].y && mouse.y <= s_r1[id].y) { hover = id; break; }
        }
    } else if (s_ctx >= 0 || s_drag >= 0) {
        s_ctx = -1;
        s_drag = -1;
    }

    // hover animation (edit mode only)
    for (int id = 0; id < E_COUNT; id++) {
        float tv = (edit && (id == hover || id == s_drag)) ? 1.0f : 0.0f;
        s_hover[id] = hstyle::damp(s_hover[id], tv, 0.18f, dt);
    }

    if (edit) {
        // context menu rect for this frame
        bool ctx_visible = s_ctx >= 0 && s_vis[s_ctx];
        if (ctx_visible) {
            s_ctx0 = ctx_menu_pos(s_ctx, s_r0[s_ctx], s_r1[s_ctx]);
            s_ctx1 = ImVec2(s_ctx0.x + k_ctx_w, s_ctx0.y + k_ctx_h);
        }

        bool consumed = false;
        if (click0 && ctx_visible) consumed = ctx_menu_click(s_ctx, mouse);

        if (click2) {
            if (hover >= 0) s_ctx = (s_ctx == hover) ? -1 : hover;
            else if (!(ctx_visible && in_box(mouse, s_ctx0, s_ctx1))) s_ctx = -1;
        }
        if (click0 && !consumed && !(ctx_visible && in_box(mouse, s_ctx0, s_ctx1))) {
            if (hover >= 0) {
                if (s_ctx != hover) s_ctx = -1;
                s_drag = hover;
                s_drag_moved = false;
                norm_pos(k_pos[hover]);
                if (right_anchored(hover)) s_drag_off = ImVec2(s_r1[hover].x - mouse.x, s_r0[hover].y - mouse.y);
                else s_drag_off = ImVec2(s_r0[hover].x - mouse.x, s_r0[hover].y - mouse.y);
            } else {
                s_ctx = -1;
            }
        }

        if (s_drag >= 0) {
            if (!io.MouseDown[0]) {
                if (s_drag_moved) config::save_auto();
                s_drag = -1;
            } else {
                int id = s_drag;
                float new0;
                if (right_anchored(id)) {
                    float right = mouse.x + s_drag_off.x;
                    if (right < 32.0f) right = 32.0f;
                    if (right > W - 4.0f) right = W - 4.0f;
                    new0 = W - right;
                } else {
                    float x = mouse.x + s_drag_off.x;
                    if (x < 0.0f) x = 0.0f;
                    if (x > W - 32.0f) x = W - 32.0f;
                    new0 = x;
                }
                float new1 = mouse.y + s_drag_off.y;
                if (new1 < 0.0f) new1 = 0.0f;
                if (new1 > H - 20.0f) new1 = H - 20.0f;
                if (k_pos[id][0] != new0 || k_pos[id][1] != new1) s_drag_moved = true;
                k_pos[id][0] = new0;
                k_pos[id][1] = new1;
                place_rect(id, W);
            }
        }
        if (s_ctx >= 0 && !s_vis[s_ctx]) s_ctx = -1;
    }

    // draw pass
    for (int id = 0; id < E_COUNT; id++) {
        if (!s_vis[id]) continue;
        bool on = *k_enabled[id];
        helem::Ctx c;
        c.dl = dl;
        c.s = s_s[id];
        c.edit = edit;
        c.ghost = !on;
        c.alpha = s_appear * (on ? 1.0f : 0.38f);
        c.r0 = s_r0[id];
        c.r1 = s_r1[id];
        c.now_us = now;
        helem::ops(id)->draw(c, s_r1[id] - s_r0[id]);
        if (edit && s_hover[id] > 0.02f) hover_glow(dl, s_r0[id], s_r1[id], s_hover[id] * s_appear);
    }

    if (edit && s_ctx >= 0) draw_ctx_menu(dl, s_ctx, s_appear);
}

bool edit_active() { return hud_data::chat_open(); }

bool wants_overlay() {
    for (int id = 0; id < E_COUNT; id++) if (*k_enabled[id]) return true;
    if (hud_data::chat_open()) return true;
    return false;
}

void shutdown() {
    hud_data::shutdown();
    hud_icons::shutdown();
    hud_fx::shutdown();
    if (s_blur_ok) glass_blur::shutdown();
    s_blur_ok = false;
    s_blur_init_done = false;
    hstyle::set_blur_tex(0);
}

// ---- menu API ----
const char* element_name(int id) { return (id >= 0 && id < E_COUNT) ? k_names[id] : ""; }
bool* element_enabled(int id) { return (id >= 0 && id < E_COUNT) ? k_enabled[id] : nullptr; }
float* element_scale(int id) { return (id >= 0 && id < E_COUNT) ? k_scale[id] : nullptr; }
float* element_pos(int id) {
    if (id < 0 || id >= E_COUNT) return nullptr;
    norm_pos(k_pos[id]);
    return k_pos[id];
}
void reset_element(int id) {
    if (id < 0 || id >= E_COUNT) return;
    k_pos[id][0] = 12.0f;
    k_pos[id][1] = 12.0f;
    *k_scale[id] = 1.0f;
}
void reset_all() {
    for (int id = 0; id < E_COUNT; id++) reset_element(id);
}
void save() { config::save_auto(); }

void draw_preview(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1) {
    if (!dl) return;
    hud_fx::begin_frame();
    float W = p1.x - p0.x, H = p1.y - p0.y;
    if (W < 40.0f || H < 40.0f) return;
    hstyle::set_blur_tex(0); // no game frame behind the menu window
    float ps = 1.0f;
    if (W < 470.0f) ps = W / 470.0f;
    if (ps < 0.5f) ps = 0.5f;
    const long long now = hud_data::now_us();

    float ew[E_COUNT], eh[E_COUNT];
    for (int id = 0; id < E_COUNT; id++) {
        helem::Ctx c;
        c.dl = dl;
        c.s = ps;
        c.edit = false;
        c.ghost = true;
        c.alpha = 1.0f;
        c.now_us = now;
        ImVec2 sz = helem::ops(id)->measure(c);
        ew[id] = sz.x >= 2.0f ? sz.x : 0.0f;
        eh[id] = sz.y >= 2.0f ? sz.y : 0.0f;
    }

    auto put = [&](int id, float x, float y, bool right) {
        if (ew[id] <= 0.0f) return;
        float x0 = right ? (p1.x - ew[id] - x) : (p0.x + x);
        float y0 = p0.y + y;
        if (x0 < p0.x) x0 = p0.x;
        if (y0 < p0.y) y0 = p0.y;
        if (x0 + ew[id] > p1.x) x0 = p1.x - ew[id];
        if (y0 + eh[id] > p1.y) y0 = p1.y - eh[id];
        if (x0 < p0.x) x0 = p0.x;
        if (y0 < p0.y) y0 = p0.y;
        helem::Ctx c;
        c.dl = dl;
        c.s = ps;
        c.edit = false;
        c.ghost = true;
        c.alpha = 1.0f;
        c.r0 = ImVec2(x0, y0);
        c.r1 = ImVec2(x0 + ew[id], y0 + eh[id]);
        c.now_us = now;
        helem::ops(id)->draw(c, ImVec2(ew[id], eh[id]));
    };

    put(E_WATERMARK, 0.0f, 0.0f, false);
    put(E_ARRAYLIST, 0.0f, 0.0f, true);
    put(E_KEYBINDS, 0.0f, eh[E_WATERMARK] + 6.0f, false);
    put(E_TARGET, 0.0f, eh[E_ARRAYLIST] + 6.0f, true);
    put(E_PICKUPS, 0.0f, eh[E_ARRAYLIST] + eh[E_TARGET] + 14.0f, true);
    put(E_POISON, 0.0f, H - eh[E_POISON] - eh[E_COORDS] - 14.0f, false);
    put(E_COORDS, 0.0f, H - eh[E_COORDS], false);
}

} // namespace hud
} // namespace flaway
