#include "hud_internal.h"
#include "../../hooks/Hook.h"

#include <cctype>
#include <cstdio>
#include <unordered_map>

// ---------------------------------------------------------------------------
// The seven HUD elements: each one measures its content (position-independent)
// and then draws it into the rect the core computed. All sizes are multiplied
// by the element scale so the S/M/L presets and the global HUD scale work.
// ---------------------------------------------------------------------------
namespace helem {
namespace {

const char* k_version = "1.21.10";

ImVec2 tsize(ImFont* f, float fs, const char* s) {
    if (!f) return ImVec2(0, 0);
    return f->CalcTextSizeA(fs, FLT_MAX, 0.0f, s);
}

float row_damp(float cur, float target, float speed) {
    return hstyle::damp(cur, target, speed, ImGui::GetIO().DeltaTime);
}

void draw_text(ImDrawList* dl, ImFont* f, float fs, const ImVec2& p, ImU32 col, const char* s) {
    if (!f || !s || !*s) return;
    dl->AddText(f, fs, p, col, s);
}

// Rounded item icon: vanilla PNG when available, rarity-colored tile otherwise.
void draw_item_icon(Ctx& c, const ImVec2& p0, float sz, const std::string& tex,
                    int r, int g, int b, float alpha) {
    ImDrawList* dl = c.dl;
    int a = (int)(hstyle::clamp01(alpha) * 255.0f);
    if (a <= 0) return;
    // Snap to whole pixels: a fractional rect makes the sampler straddle texels
    // and the 16px item art picks up shimmer/garbage-looking edges.
    sz = floorf(sz + 0.5f);
    if (sz < 4.0f) return;
    unsigned id = tex.empty() ? 0 : hud_icons::item(tex);
    if (id) {
        dl->AddImageRounded((ImTextureID)(intptr_t)id, p0, ImVec2(p0.x + sz, p0.y + sz),
                            ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32(255, 255, 255, a), 3.5f * c.s);
        return;
    }
    ImVec2 p1(p0.x + sz, p0.y + sz);
    dl->AddRectFilled(p0, p1, IM_COL32((int)(r * 0.16f), (int)(g * 0.16f), (int)(b * 0.16f), a),
                      3.5f * c.s);
    dl->AddRectFilled(p0, p1, IM_COL32(r, g, b, (int)(a * 0.28f)), 3.5f * c.s);
    dl->AddRect(p0, p1, IM_COL32(r, g, b, (int)(a * 0.85f)), 3.5f * c.s, 0, 1.0f);
    // small inner glyph so the tile does not look empty
    float q = sz * 0.28f;
    dl->AddRectFilled(ImVec2(p0.x + (sz - q) * 0.5f, p0.y + (sz - q) * 0.5f),
                      ImVec2(p0.x + (sz + q) * 0.5f, p0.y + (sz + q) * 0.5f),
                      IM_COL32(r, g, b, (int)(a * 0.9f)), 2.0f * c.s);
}

// ===========================================================================
// Watermark — separate glass chips: [flaway v] [FPS] [ping] [server]
// ===========================================================================
struct WmChip {
    int kind;                 // 0 logo, 1 fps, 2 ping, 3 server
    float w;
    std::string a, b;
    float fs_a, fs_b;
};

std::vector<WmChip> wm_layout(Ctx& c, float& h) {
    ImFont* f = hstyle::font();
    ImFont* fb = hstyle::font_bold();
    float s = c.s;
    h = 28.0f * s;
    std::vector<WmChip> out;
    if (!f) return out;
    const float px = 11.0f * s, gap = 7.0f * s;
    float fs_a = 10.5f * s, fs_b = 13.0f * s;

    // logo
    {
        WmChip ch{0, 0, "flaway", k_version, 14.0f * s, 11.0f * s};
        ch.w = px + tsize(fb ? fb : f, ch.fs_a, ch.a.c_str()).x + gap +
               tsize(f, ch.fs_b, ch.b.c_str()).x + px;
        out.push_back(std::move(ch));
    }
    // fps
    {
        float fps = Hook::get_game_fps();
        if (fps <= 0.0f) fps = ImGui::GetIO().Framerate;
        char num[32];
        snprintf(num, sizeof(num), "%.0f", fps);
        WmChip ch{1, 0, "FPS", num, fs_a, fs_b};
        ch.w = px + tsize(f, fs_a, ch.a.c_str()).x + 5.0f * s +
               tsize(fb ? fb : f, fs_b, ch.b.c_str()).x + px;
        out.push_back(std::move(ch));
    }
    // ping
    {
        int ms = 0;
        if (hud_data::ping(ms)) {
            char num[32];
            snprintf(num, sizeof(num), "%d ms", ms);
            WmChip ch{2, 0, "Ping", num, fs_a, fs_b};
            ch.w = px + tsize(f, fs_a, ch.a.c_str()).x + 5.0f * s +
                   tsize(fb ? fb : f, fs_b, ch.b.c_str()).x + px;
            out.push_back(std::move(ch));
        }
    }
    // server
    {
        std::string addr;
        if (hud_data::server(addr)) {
            if (addr.size() > 18) addr = addr.substr(0, 17) + "\xE2\x80\xA6";
            WmChip ch{3, 0, addr, "", 11.5f * s, 0.0f};
            ch.w = px + tsize(f, fs_a + 1.0f * s, ch.a.c_str()).x + px;
            out.push_back(std::move(ch));
        }
    }
    return out;
}

ImVec2 m_watermark(Ctx& c) {
    float h = 0;
    std::vector<WmChip> chips = wm_layout(c, h);
    if (chips.empty()) return ImVec2(0, 0);
    float w = 5.0f * c.s;
    for (auto& ch : chips) w += ch.w;
    return ImVec2(w, h);
}

void d_watermark(Ctx& c, const ImVec2& size) {
    ImFont* f = hstyle::font();
    ImFont* fb = hstyle::font_bold();
    if (!f) return;
    float h = 0;
    std::vector<WmChip> chips = wm_layout(c, h);
    float s = c.s;
    const float px = 11.0f * s, gap = 7.0f * s, cgap = 5.0f * s;
    float x = c.r0.x + cgap;
    float cy = c.r0.y + (size.y - h) * 0.5f;
    for (auto& ch : chips) {
        ImVec2 p0(x, cy), p1(x + ch.w, cy + h);
        hstyle::pill(c.dl, p0, p1, c.alpha);
        hud_fx::fx(c.dl, p0, p1, h * 0.5f, 0.15f * c.alpha, (float)ch.kind * 1.3f,
                   hud_fx::FX_SWEEP);
        float tx = x + px;
        float ty = cy + (h - ch.fs_b) * 0.5f;
        if (ch.kind == 0) {
            ImFont* fbold = fb ? fb : f;
            float wa = tsize(fbold, ch.fs_a, ch.a.c_str()).x;
            hstyle::gradient_text(c.dl, ImVec2(tx, cy + (h - ch.fs_a) * 0.5f),
                                  ch.a.c_str(), fbold, ch.fs_a,
                                  hstyle::grad_a(), hstyle::grad_b(), (int)(c.alpha * 255));
            draw_text(c.dl, f, ch.fs_b, ImVec2(tx + wa + gap, ty),
                      hstyle::with_a(hstyle::text_faint(), (int)(c.alpha * 220)), ch.b.c_str());
        } else if (ch.kind == 1 || ch.kind == 2) {
            float wa = tsize(f, ch.fs_a, ch.a.c_str()).x;
            draw_text(c.dl, f, ch.fs_a, ImVec2(tx, cy + (h - ch.fs_a) * 0.5f),
                      hstyle::with_a(hstyle::text_faint(), (int)(c.alpha * 230)), ch.a.c_str());
            hstyle::rolling_text(c.dl, fb ? fb : f, ch.fs_b, ImVec2(tx + wa + 5.0f * s, ty),
                                 hstyle::with_a(ch.kind == 1 ? hstyle::text_main() : hstyle::grad_a(),
                                                (int)(c.alpha * 245)),
                                 ch.b.c_str(), ch.kind == 1 ? "wm.fps" : "wm.ping",
                                 p0, p1);
        } else {
            draw_text(c.dl, f, ch.fs_a + 1.0f * s, ImVec2(tx, cy + (h - ch.fs_a) * 0.5f),
                      hstyle::with_a(hstyle::text_dim(), (int)(c.alpha * 235)), ch.a.c_str());
        }
        x += ch.w + cgap;
    }
}

// ===========================================================================
// Keybinds — card with Russian header + "module | key" rows, slide in/out
// ===========================================================================
struct KbRow {
    std::string name;
    int key;
    float a;
};

struct KbMod { const char* name; bool* enabled; int* key; };
const KbMod k_kb[] = {
    {"Aim Assist", &globals::aimassist_enabled, &globals::aimassist_keybind},
    {"Triggerbot", &globals::triggerbot_enabled, &globals::triggerbot_keybind},
    {"Reach", &globals::reach_enabled, &globals::reach_keybind},
    {"Hitbox", &globals::hitbox_enabled, &globals::hitbox_keybind},
    {"Shield Breaker", &globals::shield_breaker_enabled, &globals::shield_breaker_keybind},
    {"Mace", &globals::mace_enabled, &globals::mace_keybind},
    {"AutoCrystal", &globals::autocrystal_enabled, &globals::autocrystal_keybind},
    {"AutoTotem", &globals::autototem_enabled, &globals::autototem_keybind},
    {"Anchor Macro", &globals::anchor_macro_enabled, &globals::anchor_macro_keybind},
    {"Pearl Catch", &globals::pearl_catch_enabled, &globals::pearl_catch_keybind},
    {"Backtrack", &globals::backtrack_enabled, &globals::backtrack_keybind},
    {"ESP", &globals::box_enabled, &globals::esp_keybind},
    {"Fullbright", &globals::fullbright_enabled, &globals::fullbright_keybind},
    {"Chest Stealer", &globals::chest_stealer_enabled, &globals::chest_stealer_keybind},
};

// hud_core runs two passes per frame (measure_all then draw), and both the
// measure and draw pass call these row builders. row_damp() is stateful, so
// without the per-frame cache the animation ran at ~2x the intended speed.
static int                 s_kb_rows_frame = -1;
static std::vector<KbRow>  s_kb_rows_cache;

static std::vector<KbRow> kb_rows_damped() {
    static std::unordered_map<std::string, float> anim;
    std::vector<KbRow> rows;
    for (auto& kb : k_kb) {
        bool on = kb.enabled && *kb.enabled && kb.key && *kb.key > 0;
        float& a = anim[kb.name];
        a = row_damp(a, on ? 1.0f : 0.0f, 0.16f);
        if (a < 0.02f) { a = on ? a : 0.0f; continue; }
        rows.push_back({kb.name, kb.key ? *kb.key : 0, a});
    }
    return rows;
}

std::vector<KbRow> kb_rows() {
    const int frame = (int)ImGui::GetFrameCount();
    if (frame == s_kb_rows_frame) return s_kb_rows_cache;
    s_kb_rows_frame = frame;
    s_kb_rows_cache = kb_rows_damped();
    return s_kb_rows_cache;
}

ImVec2 m_keybinds(Ctx& c) {
    ImFont* f = hstyle::font();
    if (!f) return ImVec2(0, 0);
    float s = c.s;
    std::vector<KbRow> rows = kb_rows();
    const float pad = 9.0f * s;
    const float fs = 12.0f * s;
    const float fs_h = 9.5f * s;
    const float fs_k = 10.5f * s;
    float maxw = 0;
    for (auto& r : rows) {
        std::string key = GUI::key_name(r.key);
        float w = pad + 6.0f * s + 6.0f * s + tsize(f, fs, r.name.c_str()).x + 12.0f * s +
                  tsize(f, fs_k, key.c_str()).x + 10.0f * s + pad;
        if (w > maxw) maxw = w;
    }
    if (maxw < 168.0f * s) maxw = 168.0f * s;
    float header_h = tsize(f, fs_h, "\xD0\x9A\xD0\x9B\xD0\x90\xD0\x92\xD0\xA8\xD0\x98").y + 3.0f * s;
    if (rows.empty()) {
        // No binds to show: do not emit a grey placeholder card — an empty
        // card floating above other HUD elements just reads as junk.
        return ImVec2(0, 0);
    }
    float h = pad + header_h + 7.0f * s;
    for (size_t i = 0; i < rows.size(); i++) {
        h += fs + 5.0f * s;
        if (i + 1 < rows.size()) h += 3.0f * s;
    }
    h += pad;
    return ImVec2(maxw, h);
}

void d_keybinds(Ctx& c, const ImVec2& size) {
    ImFont* f = hstyle::font();
    if (!f) return;
    float s = c.s;
    hstyle::Card card;
    card.rounding = 12.0f * s;
    card.alpha = c.alpha;
    card.blur = true;
    card.hover = c.edit ? 0.0f : 0.0f;
    hstyle::card(c.dl, c.r0, c.r1, card);
    hud_fx::sheen(c.dl, c.r0, c.r1, card.rounding, 0.22f * c.alpha, 1.7f);
    hud_fx::fx(c.dl, c.r0, c.r1, card.rounding, 0.14f * c.alpha, 0.6f, hud_fx::FX_EDGE);

    const float pad = 9.0f * s;
    const float fs = 12.0f * s;
    const float fs_h = 9.5f * s;
    const float fs_k = 10.5f * s;

    // header
    const char* hdr = "\xD0\x9A\xD0\x9B\xD0\x90\xD0\x92\xD0\xA8\xD0\x98"; // "КЛАВИШИ"
    ImVec2 hs = tsize(f, fs_h, hdr);
    draw_text(c.dl, hstyle::font_bold() ? hstyle::font_bold() : f, fs_h,
              ImVec2(c.r0.x + pad, c.r0.y + pad),
              hstyle::with_a(hstyle::text_faint(), (int)(c.alpha * 220)), hdr);
    float y = c.r0.y + pad + hs.y + 7.0f * s;

    std::vector<KbRow> rows = kb_rows();
    if (rows.empty()) {
        draw_text(c.dl, f, fs, ImVec2(c.r0.x + pad, y),
                  hstyle::with_a(hstyle::text_faint(), (int)(c.alpha * 160)), "\xE2\x80\x94");
        return;
    }
    for (auto& r : rows) {
        std::string key = GUI::key_name(r.key);
        float slide = (1.0f - r.a) * 10.0f * s;
        float row_a = c.alpha * r.a;
        float cx = c.r0.x + pad + slide;
        float cy = y + (fs + 5.0f * s - fs) * 0.5f;
        // status dot
        float dot_r = 3.0f * s;
        ImVec2 dc(cx + dot_r, cy + fs * 0.5f);
        c.dl->AddCircleFilled(dc, dot_r, hstyle::with_a(hstyle::accent(), (int)(row_a * 255)), 12);
        c.dl->AddCircle(dc, dot_r + 2.0f * s, hstyle::with_a(hstyle::accent(), (int)(row_a * 60)), 12, 1.0f);
        draw_text(c.dl, f, fs, ImVec2(cx + dot_r * 2 + 6.0f * s, cy),
                  hstyle::with_a(hstyle::text_main(), (int)(row_a * 245)), r.name.c_str());
        // key cap
        float kw = tsize(f, fs_k, key.c_str()).x + 10.0f * s;
        float kh = fs_k + 6.0f * s;
        float kx = c.r1.x - pad - kw;
        float ky = y + (fs + 5.0f * s - kh) * 0.5f;
        c.dl->AddRectFilled(ImVec2(kx, ky), ImVec2(kx + kw, ky + kh),
                            hstyle::with_a(IM_COL32(255, 255, 255, 255), (int)(row_a * 16)), 4.0f * s);
        c.dl->AddRect(ImVec2(kx, ky), ImVec2(kx + kw, ky + kh),
                      hstyle::with_a(IM_COL32(255, 255, 255, 255), (int)(row_a * 26)), 4.0f * s, 0, 1.0f);
        draw_text(c.dl, f, fs_k, ImVec2(kx + (kw - tsize(f, fs_k, key.c_str()).x) * 0.5f, ky + 3.0f * s),
                  hstyle::with_a(hstyle::text_dim(), (int)(row_a * 240)), key.c_str());
        y += fs + 5.0f * s + 3.0f * s;
    }
}

// ===========================================================================
// Target HUD — avatar + name + HP bar + distance + armor/weapon icons
// ===========================================================================
ImVec2 m_target(Ctx& c) {
    const hud_data::TargetInfo& t = hud_data::target();
    if (!t.valid && !c.ghost && !c.edit) return ImVec2(0, 0);
    ImFont* f = hstyle::font();
    if (!f) return ImVec2(0, 0);
    float s = c.s;
    const float pad = 11.0f * s;
    const float av = 36.0f * s;
    const float fs_n = 14.0f * s;
    const float fs_s = 11.5f * s;

    const char* nm = t.valid ? t.name.c_str() : "Target";
    float hp = t.valid ? t.hp : 15.0f, mx = t.valid ? t.max_hp : 20.0f;
    char hpt[48];
    snprintf(hpt, sizeof(hpt), "%.1f / %.0f", hp, mx);
    char dst[32];
    snprintf(dst, sizeof(dst), "%.1fm", t.valid ? t.dist : 0.0);
    float col_w = tsize(f, fs_n, nm).x;
    float w2 = tsize(f, fs_s, hpt).x + 10.0f * s + tsize(f, fs_s, dst).x;
    if (w2 > col_w) col_w = w2;
    if (col_w < 130.0f * s) col_w = 130.0f * s;

    float col_h = fs_n + 6.0f * s + 5.0f * s + 4.0f * s + fs_s;
    float body_h = av > col_h ? av : col_h;

    float w = pad + av + 9.0f * s + col_w + pad;
    // armor / weapon row
    int icons = (int)t.items.size();
    if (icons > 6) icons = 6;
    float body_h_extra = 0.0f;
    if (icons > 0) {
        float icons_w = icons * 16.0f * s + (icons - 1) * 4.0f * s;
        float w_icons = pad + icons_w + pad;
        if (w_icons > w) w = w_icons;
        body_h_extra = 16.0f * s + 6.0f * s;
    }
    return ImVec2(w, pad + body_h + body_h_extra + pad);
}

void d_target(Ctx& c, const ImVec2& size) {
    const hud_data::TargetInfo& t = hud_data::target();
    ImFont* f = hstyle::font();
    ImFont* fb = hstyle::font_bold();
    if (!f) return;
    float s = c.s;
    float time = hstyle::time_now();

    hstyle::Card card;
    card.rounding = 14.0f * s;
    card.alpha = c.alpha;
    card.blur = true;
    card.accent_border = true;
    // breathing glow — accent ring pulses while a target is locked
    card.glow = (0.55f + 0.45f * sinf(time * 2.4f)) * c.alpha;
    hstyle::card(c.dl, c.r0, c.r1, card);
    hud_fx::sheen(c.dl, c.r0, c.r1, card.rounding, 0.30f * c.alpha, 0.0f);
    hud_fx::fx(c.dl, c.r0, c.r1, card.rounding, 0.20f * c.alpha, 1.4f, hud_fx::FX_EDGE);

    const float pad = 11.0f * s;
    const float av = 36.0f * s;
    const float fs_n = 14.0f * s;
    const float fs_s = 11.5f * s;
    int a = (int)(c.alpha * 255);

    // avatar (skin face, fallback — first letter)
    ImVec2 av0(c.r0.x + pad, c.r0.y + pad);
    ImVec2 av1(av0.x + av, av0.y + av);
    unsigned skin_tex = t.skin_tex;
    if (!skin_tex && !t.skin_hash.empty()) {
        skin_tex = hud_icons::skin(t.skin_hash);
        const_cast<hud_data::TargetInfo&>(t).skin_tex = skin_tex;
    }
    if (skin_tex) {
        c.dl->AddImageRounded((ImTextureID)(intptr_t)skin_tex, av0, av1,
                              ImVec2(8.0f / 64.0f, 8.0f / 64.0f),
                              ImVec2(16.0f / 64.0f, 16.0f / 64.0f),
                              IM_COL32(255, 255, 255, a), 8.0f * s);
        c.dl->AddRect(av0, av1, hstyle::with_a(hstyle::accent(), (int)(c.alpha * 120)), 8.0f * s, 0, 1.0f);
    } else {
        c.dl->AddRectFilled(av0, av1, hstyle::with_a(hstyle::accent(), (int)(c.alpha * 46)), 8.0f * s);
        c.dl->AddRect(av0, av1, hstyle::with_a(hstyle::accent(), (int)(c.alpha * 150)), 8.0f * s, 0, 1.0f);
        const char* nm = t.valid && !t.name.empty() ? t.name.c_str() : "T";
        char ini[8] = {nm[0] ? (char)toupper((unsigned char)nm[0]) : 'T', 0};
        ImVec2 is = tsize(fb ? fb : f, 16.0f * s, ini);
        draw_text(c.dl, fb ? fb : f, 16.0f * s,
                  ImVec2(av0.x + (av - is.x) * 0.5f, av0.y + (av - is.y) * 0.5f),
                  hstyle::with_a(hstyle::text_main(), a), ini);
    }

    float tx = av1.x + 9.0f * s;
    float col_right = c.r1.x - pad;

    const char* nm = t.valid ? t.name.c_str() : "Target";
    draw_text(c.dl, fb ? fb : f, fs_n, ImVec2(tx, c.r0.y + pad),
              hstyle::with_a(hstyle::text_main(), a), nm);

    // HP bar (width catches up smoothly, gradient red→green + moving shine)
    float hp = t.valid ? t.hp : 15.0f, mx = t.valid && t.max_hp > 0.0f ? t.max_hp : 20.0f;
    float frac = hstyle::clamp01(mx > 0.0f ? hp / mx : 0.0f);
    static float s_bar = 1.0f;
    s_bar = row_damp(s_bar, frac, 0.14f);
    float by = c.r0.y + pad + fs_n + 6.0f * s;
    float bar_h = 5.0f * s;
    ImVec2 b0(tx, by), b1(col_right, by + bar_h);
    c.dl->AddRectFilled(b0, b1, hstyle::with_a(IM_COL32(255, 255, 255, 255), (int)(c.alpha * 22)),
                        bar_h * 0.5f);
    if (s_bar > 0.01f) {
        ImU32 lo = IM_COL32(255, 82, 82, 255), hi = IM_COL32(96, 232, 120, 255);
        ImU32 hc = hstyle::lerp(lo, hi, frac);
        ImVec2 bf(b0.x + (b1.x - b0.x) * s_bar, b1.y);
        c.dl->AddRectFilled(b0, bf, hstyle::with_a(hc, a), bar_h * 0.5f);
        // top sheen
        c.dl->AddRectFilled(ImVec2(b0.x, b0.y), ImVec2(bf.x, b0.y + bar_h * 0.42f),
                            hstyle::with_a(hstyle::lerp(hc, IM_COL32(255, 255, 255, 255), 0.35f),
                                           (int)(c.alpha * 120)),
                            bar_h * 0.5f);
        // moving glint
        float gx = fmodf(time * 0.55f, 1.6f) - 0.3f;
        if (gx > -0.2f && gx < 1.0f) {
            float cx0 = b0.x + (bf.x - b0.x) * hstyle::clamp01(gx);
            float bw = 16.0f * s;
            if (cx0 + bw > bf.x) bw = bf.x - cx0;
            if (bw > 1.0f) {
                c.dl->AddRectFilledMultiColor(ImVec2(cx0, b0.y), ImVec2(cx0 + bw, b1.y),
                                              hstyle::with_a(IM_COL32(255, 255, 255, 255), 0),
                                              hstyle::with_a(IM_COL32(255, 255, 255, 255), (int)(c.alpha * 70)),
                                              hstyle::with_a(IM_COL32(255, 255, 255, 255), 0),
                                              hstyle::with_a(IM_COL32(255, 255, 255, 255), (int)(c.alpha * 70)));
            }
        }
    }

    char hpt[48];
    snprintf(hpt, sizeof(hpt), "%.1f / %.0f", hp, mx);
    char dst[32];
    snprintf(dst, sizeof(dst), "%.1fm", t.valid ? t.dist : 0.0);
    float sy = by + bar_h + 4.0f * s;
    ImVec2 hp_c0(tx, sy - 2.0f * s), hp_c1(col_right, sy + fs_s + 2.0f * s);
    hstyle::rolling_text(c.dl, f, fs_s, ImVec2(tx, sy),
                         hstyle::with_a(hstyle::text_dim(), (int)(c.alpha * 235)),
                         hpt, "tgt.hp", hp_c0, hp_c1);
    ImVec2 ds = tsize(f, fs_s, dst);
    hstyle::rolling_text(c.dl, f, fs_s, ImVec2(col_right - ds.x, sy),
                         hstyle::with_a(hstyle::grad_a(), (int)(c.alpha * 245)),
                         dst, "tgt.dist", hp_c0, hp_c1);

    // armor / weapon icons
    int icons = (int)t.items.size();
    if (icons > 6) icons = 6;
    if (icons > 0) {
        float iy = c.r1.y - pad - 16.0f * s;
        float ix = c.r0.x + pad;
        for (int i = 0; i < icons; i++) {
            const esp_item_slot& sl = t.items[(size_t)i];
            draw_item_icon(c, ImVec2(ix, iy), 16.0f * s, sl.tex, sl.icon_r, sl.icon_g, sl.icon_b,
                           c.alpha);
            ix += 16.0f * s + 4.0f * s;
        }
    }
}

// ===========================================================================
// Coords — chips: X / Y / Z / BPS
// ===========================================================================
struct CoordChip { std::string label, value; bool accent; float w; };

std::vector<CoordChip> coords_layout(Ctx& c, float& h) {
    ImFont* f = hstyle::font();
    ImFont* fb = hstyle::font_bold();
    float s = c.s;
    h = 27.0f * s;
    std::vector<CoordChip> out;
    if (!f) return out;
    double x, y, z, bps;
    hud_data::coords(x, y, z, bps);
    if (c.ghost) { x = 128.5; y = 64.0; z = -243.2; bps = 12.4; }
    const float px = 9.0f * s;
    float fs = 11.5f * s, fs_v = 12.5f * s;
    char xb[48], yb[48], zb[48], bb[48];
    snprintf(xb, sizeof(xb), "%.1f", x);
    snprintf(yb, sizeof(yb), "%.1f", y);
    snprintf(zb, sizeof(zb), "%.1f", z);
    snprintf(bb, sizeof(bb), "%.1f", bps);
    struct Row { const char* l; const char* v; bool acc; };
    Row rows[4] = {{"X", xb, false}, {"Y", yb, false}, {"Z", zb, false}, {"BPS", bb, true}};
    for (auto& r : rows) {
        float wl = r.l[0] ? tsize(f, fs, r.l).x + 3.0f * s : 0.0f;
        float wv = tsize(fb ? fb : f, fs_v, r.v).x;
        float w = px + wl + wv + (r.acc ? tsize(f, fs, " BPS").x + 2.0f * s : 0.0f) + px;
        out.push_back({r.l, r.v, r.acc, w});
    }
    return out;
}

ImVec2 m_coords(Ctx& c) {
    float h = 0;
    std::vector<CoordChip> chips = coords_layout(c, h);
    if (chips.empty()) return ImVec2(0, 0);
    float w = 5.0f * c.s;
    for (auto& ch : chips) w += ch.w + 5.0f * c.s;
    return ImVec2(w, h);
}

void d_coords(Ctx& c, const ImVec2& size) {
    ImFont* f = hstyle::font();
    ImFont* fb = hstyle::font_bold();
    if (!f) return;
    float h = 0;
    std::vector<CoordChip> chips = coords_layout(c, h);
    float s = c.s;
    const float px = 9.0f * s, gap = 5.0f * s;
    float fs = 11.5f * s, fs_v = 12.5f * s;
    float x = c.r0.x;
    float cy = c.r0.y + (size.y - h) * 0.5f;
    for (auto& ch : chips) {
        ImVec2 p0(x, cy), p1(x + ch.w, cy + h);
        hstyle::pill(c.dl, p0, p1, c.alpha);
        hud_fx::fx(c.dl, p0, p1, h * 0.5f, 0.12f * c.alpha, x * 0.035f, hud_fx::FX_SWEEP);
        float tx = x + px;
        float ty = cy + (h - fs_v) * 0.5f;
        if (ch.label[0]) {
            draw_text(c.dl, f, fs, ImVec2(tx, cy + (h - fs) * 0.5f),
                      hstyle::with_a(hstyle::text_faint(), (int)(c.alpha * 230)), ch.label.c_str());
            tx += tsize(f, fs, ch.label.c_str()).x + 3.0f * s;
        }
        hstyle::rolling_text(c.dl, fb ? fb : f, fs_v, ImVec2(tx, ty),
                             hstyle::with_a(ch.accent ? hstyle::grad_a() : hstyle::text_main(),
                                            (int)(c.alpha * 245)),
                             ch.value.c_str(),
                             ch.label.empty() ? "cd.v" : (std::string("cd.") + ch.label).c_str(),
                             p0, p1);
        if (ch.accent) {
            tx += tsize(fb ? fb : f, fs_v, ch.value.c_str()).x + 2.0f * s;
            draw_text(c.dl, f, fs, ImVec2(tx, cy + (h - fs) * 0.5f),
                      hstyle::with_a(hstyle::text_faint(), (int)(c.alpha * 230)), "BPS");
        }
        x += ch.w + gap;
    }
}

// ===========================================================================
// Pickups — column of recent item pickups (icon + name), slide in/out
// ===========================================================================
struct PRow {
    std::string name, tex;
    int r, g, b;
    float a, slide, w;
};

std::vector<PRow> pickup_rows(Ctx& c) {
    std::vector<PRow> out;
    float s = c.s;
    ImFont* f = hstyle::font();
    if (!f) return out;
    const float fs = 12.0f * s, px = 7.0f * s, icon = 16.0f * s;
    long long now = c.now_us;
    constexpr long long k_life = 4000000LL, k_fade = 400000LL, k_in = 250000LL;
    for (const auto& it : hud_data::pickups()) {
        long long age = now - it.time_us;
        if (age < 0) age = 0;
        if (age > k_life) continue;
        float a_in = age < k_in ? (float)age / (float)k_in : 1.0f;
        float a_out = (k_life - age) < k_fade ? (float)(k_life - age) / (float)k_fade : 1.0f;
        float a = std::min(std::min(a_in, a_out), 0.95f);
        if (a <= 0.01f) continue;
        float slide = age < k_in ? hstyle::ease_out((float)age / (float)k_in) : 1.0f;
        float w = px + icon + 6.0f * s + tsize(f, fs, it.name.c_str()).x + px;
        out.push_back({it.name, it.tex, it.icon_r, it.icon_g, it.icon_b, a, slide, w});
    }
    if (out.empty() && (c.ghost || c.edit)) {
        const char* sample = "Diamond Sword";
        float w = px + icon + 6.0f * s + tsize(f, fs, sample).x + px;
        out.push_back({sample, "diamond_sword", 94, 196, 255, 0.55f, 1.0f, w});
    }
    return out;
}

ImVec2 m_pickups(Ctx& c) {
    std::vector<PRow> rows = pickup_rows(c);
    if (rows.empty()) return ImVec2(0, 0);
    float s = c.s;
    float row_h = 16.0f * s + 12.0f * s;
    float w = 0;
    for (auto& r : rows) if (r.w > w) w = r.w;
    float h = (float)rows.size() * row_h + (float)(rows.size() - 1) * 5.0f * s;
    return ImVec2(w, h);
}

void d_pickups(Ctx& c, const ImVec2& size) {
    ImFont* f = hstyle::font();
    if (!f) return;
    std::vector<PRow> rows = pickup_rows(c);
    if (rows.empty()) return;
    float s = c.s;
    const float fs = 12.0f * s, px = 7.0f * s, icon = 16.0f * s;
    float row_h = icon + 12.0f * s;
    float y = c.r0.y;
    for (auto& r : rows) {
        float w = r.w + (1.0f - r.slide) * 18.0f * s;
        float a = r.a * c.alpha;
        float x1 = c.r0.x + size.x;
        ImVec2 p0(x1 - w, y), p1(x1, y + row_h);
        hstyle::Card card;
        card.rounding = 9.0f * s;
        card.alpha = a;
        card.blur = false;
        hstyle::card(c.dl, p0, p1, card);
        hud_fx::sheen(c.dl, p0, p1, card.rounding, 0.18f * a, 4.6f);
        draw_item_icon(c, ImVec2(p0.x + px, p0.y + (row_h - icon) * 0.5f), icon, r.tex,
                       r.r, r.g, r.b, a);
        draw_text(c.dl, f, fs, ImVec2(p0.x + px + icon + 6.0f * s, p0.y + (row_h - fs) * 0.5f),
                  hstyle::with_a(hstyle::text_main(), (int)(a * 245)), r.name.c_str());
        y += row_h + 5.0f * s;
    }
}

// ===========================================================================
// Poison — pulsing status pill
// ===========================================================================
ImVec2 m_poison(Ctx& c) {
    if (!hud_data::poisoned() && !c.ghost && !c.edit) return ImVec2(0, 0);
    ImFont* f = hstyle::font();
    if (!f) return ImVec2(0, 0);
    float s = c.s;
    // No ☠ glyph — Inter/Monocraft lack U+2620; skull is drawn with rects.
    const char* label = "Poison";
    float fs = 12.0f * s;
    float px = 9.0f * s;
    float skull_w = 10.0f * s, gap = 5.0f * s;
    return ImVec2(px + 8.0f * s + skull_w + gap + tsize(f, fs, label).x + px,
                  fs + 13.0f * s);
}

void d_poison(Ctx& c, const ImVec2& size) {
    ImFont* f = hstyle::font();
    if (!f) return;
    float s = c.s;
    float time = hstyle::time_now();
    hstyle::Card card;
    card.rounding = 9.0f * s;
    card.alpha = c.alpha;
    card.blur = false;
    card.glow = 0.7f * c.alpha;
    card.accent_border = true;
    hstyle::card(c.dl, c.r0, c.r1, card);
    hud_fx::sheen(c.dl, c.r0, c.r1, card.rounding, 0.34f * c.alpha, 3.1f);
    hud_fx::fx(c.dl, c.r0, c.r1, card.rounding, 0.24f * c.alpha, 0.9f, hud_fx::FX_SWEEP);

    ImU32 green = IM_COL32(118, 255, 158, 255);
    const float px = 9.0f * s;
    float pulse = 1.0f + 0.28f * sinf(time * 6.0f);
    float dr = 3.4f * s * pulse;
    ImVec2 dc(c.r0.x + px + dr, c.r0.y + size.y * 0.5f);
    c.dl->AddCircleFilled(dc, dr, hstyle::with_a(green, (int)(c.alpha * 255)), 16);
    c.dl->AddCircle(dc, dr + 3.5f * s, hstyle::with_a(green, (int)(c.alpha * 70)), 16, 1.2f);

    // Skull icon (rects): head + eyes + jaw
    float sk = 10.0f * s;
    float skx = c.r0.x + px + 8.0f * s + dr + 4.0f * s;
    float sky = c.r0.y + (size.y - sk) * 0.5f;
    ImU32 skc = hstyle::with_a(green, (int)(c.alpha * 230));
    c.dl->AddRectFilled(ImVec2(skx, sky), ImVec2(skx + sk, sky + sk * 0.72f), skc, 2.2f * s);
    float ew = 2.0f * s, eh = 2.2f * s;
    c.dl->AddRectFilled(ImVec2(skx + sk * 0.18f, sky + sk * 0.22f),
                        ImVec2(skx + sk * 0.18f + ew, sky + sk * 0.22f + eh),
                        IM_COL32(10, 14, 16, (int)(c.alpha * 255)), 0.4f * s);
    c.dl->AddRectFilled(ImVec2(skx + sk * 0.58f, sky + sk * 0.22f),
                        ImVec2(skx + sk * 0.58f + ew, sky + sk * 0.22f + eh),
                        IM_COL32(10, 14, 16, (int)(c.alpha * 255)), 0.4f * s);
    c.dl->AddRectFilled(ImVec2(skx + sk * 0.28f, sky + sk * 0.70f),
                        ImVec2(skx + sk * 0.72f, sky + sk),
                        hstyle::with_a(green, (int)(c.alpha * 180)), 1.0f * s);

    float fs = 12.0f * s;
    draw_text(c.dl, f, fs,
              ImVec2(skx + sk + 5.0f * s, c.r0.y + (size.y - fs) * 0.5f),
              hstyle::with_a(green, (int)(c.alpha * 245)), "Poison");
}

// ===========================================================================
// Array List — enabled modules, right-aligned pills, animated gradient
// ===========================================================================
struct ModRow { const char* name; bool* enabled; };
const ModRow k_modules[] = {
    {"Aim Assist", &globals::aimassist_enabled},
    {"Triggerbot", &globals::triggerbot_enabled},
    {"Reach", &globals::reach_enabled},
    {"Hitbox", &globals::hitbox_enabled},
    {"Shield Breaker", &globals::shield_breaker_enabled},
    {"Mace", &globals::mace_enabled},
    {"AutoCrystal", &globals::autocrystal_enabled},
    {"AutoTotem", &globals::autototem_enabled},
    {"Anchor Macro", &globals::anchor_macro_enabled},
    {"Pearl Catch", &globals::pearl_catch_enabled},
    {"Backtrack", &globals::backtrack_enabled},
    {"Stun Slam", &globals::stun_slam_enabled},
    {"S-Tap", &globals::stap_enabled},
    {"W-Tap", &globals::wtap_enabled},
    {"Jump Reset", &globals::autojumpreset_enabled},
    {"ESP", &globals::box_enabled},
    {"Fullbright", &globals::fullbright_enabled},
    {"Chest Stealer", &globals::chest_stealer_enabled},
    {"Storage ESP", &globals::storage_esp_enabled},
    {"BaseFinder", &globals::base_finder_enabled},
    {"AutoSprint", &globals::autosprint_keep_swimming},
    {"Server Rotation", &globals::server_rotation_enabled},
    {"Flight", &globals::flight_enabled},
    {"Sprint", &globals::sprint_enabled},
};

struct ArRow {
    std::string name;
    float a, w;
};

static int                 s_ar_rows_frame = -1;
static std::vector<ArRow>  s_ar_rows_cache;

static std::vector<ArRow> array_rows_damped(Ctx& c) {
    static std::unordered_map<std::string, float> anim;
    std::vector<ArRow> out;
    ImFont* f = hstyle::font();
    if (!f) return out;
    float s = c.s;
    const float fs = 12.0f * s, px = 9.0f * s;
    for (auto& m : k_modules) {
        bool on = m.enabled && *m.enabled;
        float& a = anim[m.name];
        a = row_damp(a, on ? 1.0f : 0.0f, 0.16f);
        if (a < 0.02f) { if (!on) a = 0.0f; continue; }
        out.push_back({m.name, a, tsize(f, fs, m.name).x + px * 2.0f});
    }
    std::sort(out.begin(), out.end(), [](const ArRow& x, const ArRow& y) { return x.w > y.w; });
    if (out.empty() && (c.ghost || c.edit)) {
        const char* sample = "Array List";
        out.push_back({sample, 0.6f, tsize(f, fs, sample).x + px * 2.0f});
    }
    return out;
}

std::vector<ArRow> array_rows(Ctx& c) {
    const int frame = (int)ImGui::GetFrameCount();
    if (frame == s_ar_rows_frame) return s_ar_rows_cache;
    s_ar_rows_frame = frame;
    s_ar_rows_cache = array_rows_damped(c);
    return s_ar_rows_cache;
}

ImVec2 m_arraylist(Ctx& c) {
    std::vector<ArRow> rows = array_rows(c);
    if (rows.empty()) return ImVec2(0, 0);
    float s = c.s;
    float row_h = 12.0f * s + 11.0f * s;
    float w = 0;
    for (auto& r : rows) if (r.w > w) w = r.w;
    float h = (float)rows.size() * row_h + (float)(rows.size() - 1) * 3.5f * s;
    return ImVec2(w, h);
}

void d_arraylist(Ctx& c, const ImVec2& size) {
    ImFont* f = hstyle::font();
    if (!f) return;
    std::vector<ArRow> rows = array_rows(c);
    if (rows.empty()) return;
    float s = c.s;
    float time = hstyle::time_now();
    const float fs = 12.0f * s;
    float row_h = 12.0f * s + 11.0f * s;
    float n = (float)(rows.size() > 1 ? rows.size() - 1 : 1);
    float y = c.r0.y;
    for (size_t i = 0; i < rows.size(); i++) {
        auto& r = rows[i];
        float a = r.a * c.alpha;
        float w = r.w + (1.0f - r.a) * 22.0f * s;
        float x1 = c.r0.x + size.x;
        ImVec2 p0(x1 - w, y), p1(x1, y + row_h);
        float phase = (float)i / n;
        ImU32 accent = hstyle::grad(phase, time * 0.06f);
        // glass row (full capsule, same silhouette as the other HUD pills)
        float rr = row_h * 0.5f;
        c.dl->AddRectFilled(p0, p1, hstyle::with_a(IM_COL32(14, 16, 22, 255), (int)(a * 150)), rr);
        c.dl->AddRect(p0, p1, hstyle::with_a(IM_COL32(255, 255, 255, 255), (int)(a * 18)),
                      rr, 0, 1.0f);
        hud_fx::fx(c.dl, p0, p1, rr, 0.20f * a, 2.4f + (float)i * 0.9f, hud_fx::FX_SWEEP);
        // left accent tick
        c.dl->AddRectFilled(ImVec2(p0.x, p0.y + 3.0f * s), ImVec2(p0.x + 2.0f * s, p1.y - 3.0f * s),
                            hstyle::with_a(accent, (int)(a * 220)), 1.0f);
        ImU32 txt = hstyle::lerp(accent, IM_COL32(255, 255, 255, 255), 0.25f);
        draw_text(c.dl, f, fs, ImVec2(p0.x + 8.0f * s, p0.y + (row_h - fs) * 0.5f),
                  hstyle::with_a(txt, (int)(a * 250)), r.name.c_str());
        y += row_h + 3.5f * s;
    }
}

} // namespace

const Ops* ops(int id) {
    static const Ops k_ops[flaway::hud::E_COUNT] = {
        {m_watermark, d_watermark, false},  // watermark: small pills, no blur
        {m_keybinds, d_keybinds, true},
        {m_target, d_target, true},
        {m_coords, d_coords, false},        // coords: small pills, no blur
        {m_pickups, d_pickups, true},
        {m_poison, d_poison, false},        // poison: tiny pill, no blur
        {m_arraylist, d_arraylist, false},  // array list: individual pills, no blur
    };
    if (id < 0 || id >= flaway::hud::E_COUNT) id = 0;
    return &k_ops[id];
}

} // namespace helem
