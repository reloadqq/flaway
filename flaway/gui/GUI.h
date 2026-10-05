#pragma once
#include <cstdint>
#include <string>

struct ImFont;

namespace GUI {
    bool init();
    bool render(int window_width, int window_height, const uint8_t* captured_frame = nullptr);
    bool get_is_init();
    bool needs_overlay();
    bool is_keybind_waiting();
    void cancel_keybind_capture();
    void shutdown();

    // Theme color accessors (shared with HUD / ESP rendering)
    unsigned int accent_a();
    unsigned int accent_b();
    // Active gradient preset (Ocean/Sunset/Fire/Forest). Returns false when no
    // preset is selected and the caller should derive the gradient from
    // accent_a() instead.
    bool gradient_pair(unsigned int* a, unsigned int* b);
    unsigned int sidebar_a();
    unsigned int sidebar_b();
    unsigned int text_primary();
    unsigned int text_dim();
    unsigned int text_faint();
    unsigned int border_color();
    unsigned int card_color();

    // HUD fonts (Monocraft regular/bold, 15px)
    ImFont* font_hud();
    ImFont* font_hud_bold();

    // Key code -> display name ("RShift", "F6", ...; "—" for none)
    std::string key_name(int key);
}
