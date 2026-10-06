#pragma once
#ifndef X11_HELPER_H
#define X11_HELPER_H

#include <X11/Xlib.h>
#include <cstdint>
#include <string>
#include <vector>

namespace x11_helper {
    struct InputData {
        float mouse_x = 0;
        float mouse_y = 0;
        int mouse_down[5] = {0};
        float wheel = 0;
        unsigned int key_char = 0;
        // Full UTF-8 result of XLookupString. key_char is only the lead byte,
        // so consumers that need composed/non-ASCII text must use this.
        std::string key_text;
    };

    struct MouseButtonEvent {
        int button;
        bool down;
    };

    void init();
    void shutdown();
    InputData poll_input();
    int poll_new_key_press();
    bool is_key_just_pressed(int vk);
    bool is_key_held(int vk);
    bool is_key_held_fast(int vk, const std::vector<int>& held);
    std::vector<int> held_keys_snapshot();
    void drain_key_presses();
    std::vector<MouseButtonEvent> consume_mouse_events();
    bool check_toggle();
    void scan_key_events();
    bool poll_cursor_window(int* x, int* y);

    // Input simulation via XTest
    unsigned long get_window_handle();
    void get_window_dimensions(int* width, int* height);
    int get_cursor_x();
    int get_cursor_y();
    void set_cursor_pos(int x, int y);
    bool send_key_press(unsigned int vk, bool press);
    bool send_mouse_click(int button, bool press);
    bool send_mouse_move_abs(int x, int y);
    bool send_mouse_move_rel(int dx, int dy);
    bool is_key_pressed(unsigned int vk);
    Display* get_display();
}

#endif
