#include "x11_helper.h"
#include "inline_hook.h"
#include "windows_compat.h"
#include <dlfcn.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <iostream>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <algorithm>
#include <cstring>
#include "../../flaway/utils/logger.h"
#include "flaway/utils/no_log.h"

namespace x11_helper {

    typedef int (*XNextEvent_t)(Display*, XEvent*);
    // Atomic: read from the event thread, written by shutdown(). The pointer is
    // deliberately NOT nulled on shutdown (see shutdown()) because a thread
    // already inside the hook must still be able to call through it.
    static std::atomic<XNextEvent_t> o_XNextEvent{nullptr};
    static void* s_xnext_target = nullptr;

    // XInitThreads() is intentionally NOT called: Xlib only honours it if it
    // runs before ANY other Xlib call in the process, and by injection time
    // GLFW/the game/the JVM have already called XOpenDisplay, making it a
    // no-op. Serialisation is therefore the only real fix for the fact that
    // g_display is touched from several threads (module threads here, the
    // event thread inside hooked_XNextEvent).
    static std::recursive_mutex g_xlib_mu;

    static InputData g_input;
    static std::mutex g_input_mutex;

    static std::mutex g_event_mutex;
    static std::vector<MouseButtonEvent> g_mouse_events;

    static std::unordered_set<unsigned int> g_keys_held;
    static std::vector<int> g_key_queue;
    static bool g_toggle_pending = false;

    // Rising-edge tracking for the polling toggle fallback (see check_toggle()).
    static bool g_poll_f6_down = false;
    static bool g_poll_rshift_down = false;

    // Full-keyboard rising-edge polling (see scan_key_events()).
    // Mirrors server key state into g_key_queue so module keybinds fire even
    // when key events never reach hooked_XNextEvent.
    static bool g_scan_prev[256] = {};

    // Window origin in root coordinates, derived from the last MotionNotify.
    // Used to convert XQueryPointer (root) coords into window coords.
    static int g_win_ox = 0;
    static int g_win_oy = 0;
    static bool g_win_offset_known = false;

    static Display* g_display = nullptr;

    InputData poll_input() {
        InputData data{};
        {
            std::lock_guard<std::mutex> lock(g_input_mutex);
            data = g_input;
            g_input.wheel = 0.0f;
            g_input.key_char = 0;
            g_input.key_text.clear();
            g_input.mouse_down[3] = 0;
            g_input.mouse_down[4] = 0;
        }
        return data;
    }

    int poll_new_key_press() {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        if (g_key_queue.empty()) return 0;
        int key = g_key_queue.front();
        g_key_queue.erase(g_key_queue.begin());
        return key;
    }

    bool is_key_just_pressed(int vk) {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        for (size_t i = 0; i < g_key_queue.size(); ++i)
        {
            if (g_key_queue[i] == vk)
            {
                g_key_queue.erase(g_key_queue.begin() + i);
                return true;
            }
        }
        return false;
    }

    bool is_key_held(int vk) {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        return g_keys_held.count((unsigned int)vk) > 0;
    }

    // X11 keysym -> internal VK (mirrors the logic historically inline in
    // hooked_XNextEvent; shared so the polling scan can use the same mapping).
    static unsigned int keysym_to_vk(KeySym ks) {
        if (ks >= XK_F1 && ks <= XK_F12)      return VK_F1 + (ks - XK_F1);
        if (ks >= XK_0 && ks <= XK_9)         return '0' + (ks - XK_0);
        if (ks >= XK_A && ks <= XK_Z)         return 'A' + (ks - XK_A);
        if (ks >= XK_a && ks <= XK_z)         return 'A' + (ks - XK_a);
        switch (ks) {
            case XK_Shift_L:   return VK_LSHIFT;
            case XK_Shift_R:   return VK_RSHIFT;
            case XK_Control_L: return VK_LCONTROL;
            case XK_Control_R: return VK_RCONTROL;
            case XK_Alt_L:     return VK_LMENU;
            case XK_Alt_R:     return VK_RMENU;
            case XK_Return:    return VK_RETURN;
            case XK_Escape:    return VK_ESCAPE;
            case XK_BackSpace: return VK_BACK;
            case XK_Tab:       return VK_TAB;
            case XK_space:     return VK_SPACE;
            default:           return 0;
        }
    }

    // Physical keycode -> internal VK table. Built via XKeysymToKeycode, which
    // returns the physical keycode for a keysym regardless of the active layout
    // (Latin keysyms stay in group 1 even when a Russian/other layout is active).
    // keycodes are layout-independent, so binding/detection works on any layout
    // — unlike keysym_to_vk(), which drops Cyrillic keysyms (returns 0).
    static unsigned int g_kc_to_vk[256] = {};
    static bool g_kc_table_built = false;

    static void build_kc_to_vk_table(Display* dpy) {
        if (!dpy || g_kc_table_built) return;
        auto add = [&](KeySym ks, unsigned int vk) {
            KeyCode kc = XKeysymToKeycode(dpy, ks);
            if (kc > 0 && kc < 256) g_kc_to_vk[kc] = vk;
        };
        for (int i = 0; i < 26; i++) add(XK_A + i, VK_A + i);
        for (int i = 0; i < 10; i++) add(XK_0 + i, VK_0 + i);
        for (int i = 0; i < 12; i++) add(XK_F1 + i, VK_F1 + i);
        add(XK_Shift_L, VK_LSHIFT);   add(XK_Shift_R, VK_RSHIFT);
        add(XK_Control_L, VK_LCONTROL); add(XK_Control_R, VK_RCONTROL);
        add(XK_Alt_L, VK_LMENU);      add(XK_Alt_R, VK_RMENU);
        add(XK_Return, VK_RETURN);    add(XK_Escape, VK_ESCAPE);
        add(XK_BackSpace, VK_BACK);   add(XK_Tab, VK_TAB);
        add(XK_space, VK_SPACE);
        g_kc_table_built = true;
    }

    static unsigned int kc_to_vk(unsigned int kc) {
        if (kc < 256 && g_kc_to_vk[kc]) return g_kc_to_vk[kc];
        // Fallback: layout-dependent keysym path (covers Latin layouts and
        // any keysym the table above doesn't cover).
        if (g_display && kc > 0) return keysym_to_vk(XKeycodeToKeysym(g_display, (KeyCode)kc, 0));
        return 0;
    }

    // Server-side keyboard scan. Called once per frame from the game loop; it
    // mirrors every rising/falling edge of the X keymap into the same
    // g_keys_held/g_key_queue state the XNextEvent hook maintains. This makes
    // module keybinds work regardless of whether the game ever delivers key
    // events to its event loop (the reason check_toggle needed a fallback).
    void scan_key_events() {
        if (!g_display) return;
        build_kc_to_vk_table(g_display);
        char keys[32] = {};
        {
            std::lock_guard<std::recursive_mutex> lk(g_xlib_mu);
            XQueryKeymap(g_display, keys);
        }

        std::lock_guard<std::mutex> lock(g_input_mutex);
        for (int kc = 8; kc < 256; kc++) {
            bool down = ((keys[kc >> 3] >> (kc & 7)) & 1) != 0;
            bool rising = down && !g_scan_prev[kc];
            bool falling = !down && g_scan_prev[kc];
            if (rising || falling) {
                unsigned int vk = kc_to_vk((unsigned int)kc);
                if (vk == 0) { g_scan_prev[kc] = down; continue; }
                g_scan_prev[kc] = down;
                if (vk == VK_F6 || vk == VK_RSHIFT) {
                    // F6/RShift are reserved for the GUI toggle. check_toggle()
                    // owns their rising-edge detection (event path + its own
                    // poll fallback), so this scan must NOT raise
                    // g_toggle_pending too: both detectors firing for the same
                    // physical press made the menu toggle twice (open -> close
                    // in one press), which is the "menu won't close" bug.
                    if (falling) g_keys_held.erase(vk);
                    continue;
                }
                if (rising) {
                    if (g_keys_held.insert(vk).second && g_key_queue.size() < 64)
                        g_key_queue.push_back((int)vk);
                } else {
                    g_keys_held.erase(vk);
                }
            }
        }
    }

    // Batch key-state snapshot under one lock (per-frame feed calls this once
    // instead of holding/unholding the mutex ~128 times).
    bool is_key_held_fast(int vk, const std::vector<int>& held) {
        return std::find(held.begin(), held.end(), vk) != held.end();
    }

    std::vector<int> held_keys_snapshot() {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        std::vector<int> res;
        res.reserve(g_keys_held.size());
        for (auto v : g_keys_held)
            res.push_back((int)v);
        return res;
    }

    void drain_key_presses() {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        g_key_queue.clear();
    }

    // Returns the physical cursor position in window coordinates (via
    // XQueryPointer minus the window origin). This stays accurate even when
    // MotionNotify events are sparse (e.g. right after the game releases the
    // captured cursor), which is what lets menu checkboxes be clickable.
    bool poll_cursor_window(int* x, int* y) {
        if (!g_display) return false;
        Window root, child;
        int rx = 0, ry = 0, wx = 0, wy = 0;
        unsigned int mask = 0;
        if (!XQueryPointer(g_display, DefaultRootWindow(g_display), &root, &child, &rx, &ry, &wx, &wy, &mask))
            return false;
        std::lock_guard<std::mutex> lock(g_input_mutex);
        if (!g_win_offset_known) return false;
        *x = rx - g_win_ox;
        *y = ry - g_win_oy;
        return true;
    }

    std::vector<MouseButtonEvent> consume_mouse_events() {
        std::lock_guard<std::mutex> lock(g_event_mutex);
        auto events = std::move(g_mouse_events);
        g_mouse_events.clear();
        return events;
    }

    bool check_toggle() {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        if (g_toggle_pending) {
            g_toggle_pending = false;
            // Swallow this same physical key-down from the polling fallback so
            // an F6/RShift press is never toggled twice (event + poll both fire).
            g_poll_f6_down = true;
            g_poll_rshift_down = true;
            return true;
        }

        // Polling fallback: the event path only works while the game loops on
        // XNextEvent. If the window/compositor never delivers key presses there
        // (grabbed focus, alternate event loop), the menu would be impossible to
        // open. XQueryKeymap reads the SERVER key state, so it works regardless
        // of which window/display has focus. Rising edge only -> one toggle per
        // physical press.
        if (!g_display) return false;
        static KeyCode f6_kc = 0, rshift_kc = 0;
        if (f6_kc == 0) f6_kc = XKeysymToKeycode(g_display, XK_F6);
        if (rshift_kc == 0) rshift_kc = XKeysymToKeycode(g_display, XK_Shift_R);
        char keys[32] = {};
        XQueryKeymap(g_display, keys);
        if (f6_kc > 0) {
            bool down = ((keys[f6_kc >> 3] >> (f6_kc & 7)) & 1) != 0;
            bool edge = down && !g_poll_f6_down;
            g_poll_f6_down = down;
            if (edge) return true;
        }
        if (rshift_kc > 0) {
            bool down = ((keys[rshift_kc >> 3] >> (rshift_kc & 7)) & 1) != 0;
            bool edge = down && !g_poll_rshift_down;
            g_poll_rshift_down = down;
            if (edge) return true;
        }
        return false;
    }

	int hooked_XNextEvent(Display* display, XEvent* event) {
		// Acquire once into a local: the shutdown() store must not race the
		// call-through, and the original pointer stays valid because the
		// trampoline remains mapped after remove_hook().
		XNextEvent_t orig = o_XNextEvent.load(std::memory_order_acquire);
		if (!orig || !display || !event) return 0;
		int result = orig(display, event);

		if (event) {
            std::lock_guard<std::mutex> lock(g_input_mutex);

            switch (event->type) {
                case MotionNotify: {
                    g_input.mouse_x = (float)event->xmotion.x;
                    g_input.mouse_y = (float)event->xmotion.y;
                    g_win_ox = event->xmotion.x_root - event->xmotion.x;
                    g_win_oy = event->xmotion.y_root - event->xmotion.y;
                    g_win_offset_known = true;
                    break;
                }
                case ButtonPress: {
                    int btn = event->xbutton.button;
                    if (btn >= 1 && btn <= 5) {
                        g_input.mouse_down[btn - 1] = 1;
                    }
                    {
                        std::lock_guard<std::mutex> elock(g_event_mutex);
                        if (btn >= 1 && btn <= 3) {
                            g_mouse_events.push_back({btn - 1, true});
                        }
                    }
                    if (btn == 4) g_input.wheel += 1.0f;
                    if (btn == 5) g_input.wheel -= 1.0f;
                    break;
                }
                case ButtonRelease: {
                    int btn = event->xbutton.button;
                    if (btn >= 1 && btn <= 3) {
                        g_input.mouse_down[btn - 1] = 0;
                        {
                            std::lock_guard<std::mutex> elock(g_event_mutex);
                            g_mouse_events.push_back({btn - 1, false});
                        }
                    }
                    break;
                }
                case KeyPress: {
                    unsigned int vk = kc_to_vk(event->xkey.keycode);

                    if (vk > 0) {
                        if (g_keys_held.empty()) {
                            g_keys_held.reserve(4);
                        }
                        if (g_keys_held.insert(vk).second) {
                            // F6 / RShift are reserved for toggling the GUI.
                            // Do NOT enqueue them so a module keybind can never
                            // be triggered by the menu toggle key.
                            if (vk == VK_F6 || vk == VK_RSHIFT) {
                                // Only raise the toggle if check_toggle()'s poll
                                // fallback has not already consumed this same
                                // press (it sets g_poll_*_down=true when it
                                // does). Without this guard, a press handled by
                                // the poll path first would ALSO flip the menu
                                // via this event a frame later -> double toggle.
                                bool poll_consumed = (vk == VK_F6) ? g_poll_f6_down : g_poll_rshift_down;
                                if (!poll_consumed) g_toggle_pending = true;
                            } else if (g_key_queue.size() < 64) {
                                g_key_queue.push_back((int)vk);
                            }
                        }
                    }

                    char buf[32] = {0};
                    int len = XLookupString(&event->xkey, buf, sizeof(buf) - 1, nullptr, nullptr);
                    if (len > 0) {
                        if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
                        // XLookupString returns multi-byte UTF-8 for composed
                        // and non-ASCII keysyms; keep the whole sequence so the
                        // GUI can decode every codepoint, not just the lead byte.
                        g_input.key_char = (unsigned char)buf[0];
                        g_input.key_text.assign(buf, (size_t)len);
                    }
                    break;
                }
                case KeyRelease: {
                    unsigned int vk = kc_to_vk(event->xkey.keycode);

                    if (vk > 0) {
                        g_keys_held.erase(vk);
                    }
                    break;
                }
                default:
                    break;
            }
        }

        return result;
    }

    void init() {
        // Do NOT call XInitThreads() — it only takes effect before the first
        // Xlib call in the process, which happened long before we were
        // injected. Every Xlib use from a non-event thread holds g_xlib_mu.
        void* handle = dlopen("libX11.so.6", RTLD_LAZY);
        if (!handle) {
            logger::log_error("[X11] Failed to load libX11.so.6");
            fprintf(stderr, "[X11] FAILED to load libX11.so.6\n");
            fflush(stderr);
            return;
        }

        void* target = dlsym(handle, "XNextEvent");
        if (!target) {
            logger::log_error("[X11] Failed to find XNextEvent symbol");
            fprintf(stderr, "[X11] FAILED to find XNextEvent symbol\n");
            fflush(stderr);
            dlclose(handle);
            return;
        }
        s_xnext_target = target;

        o_XNextEvent = (XNextEvent_t)linux_hook::install_hook(target, (void*)hooked_XNextEvent);

        if (o_XNextEvent) {
            logger::log("[X11] XNextEvent hooked successfully");
            fprintf(stderr, "[X11] XNextEvent hooked successfully\n");
        } else {
            s_xnext_target = nullptr;
            logger::log_error("[X11] install_hook returned NULL");
            fprintf(stderr, "[X11] install_hook returned NULL\n");
        }

        // KEEP libX11 open — the trampoline jumps to code inside it
        // dlclose(handle);

        g_display = XOpenDisplay(nullptr);
        if (!g_display) {
            logger::log_error("[X11] Failed to open secondary display for key query");
            fprintf(stderr, "[X11] FAILED to open secondary display — toggle/mouse won't work\n");
        } else {
            logger::log("[X11] Secondary display opened for key query");
            fprintf(stderr, "[X11] Secondary display opened OK\n");
            build_kc_to_vk_table(g_display);
        }
        fflush(stderr);
    }

    // ========== Input simulation via XTest ==========

    static unsigned int vk_to_xbutton(int vk) {
        switch (vk) {
            case 0x01: return Button1;       // VK_LBUTTON
            case 0x02: return Button3;       // VK_RBUTTON  
            case 0x04: return Button2;       // VK_MBUTTON
            case 0x05: return 8;             // VK_XBUTTON1
            case 0x06: return 9;             // VK_XBUTTON2
            default: return 0;
        }
    }

    unsigned long get_window_handle() {
        if (!g_display) return 0;
        Window focus;
        int revert;
        XGetInputFocus(g_display, &focus, &revert);
        return (unsigned long)focus;
    }

    void get_window_dimensions(int* width, int* height) {
        if (!g_display) { *width = 1920; *height = 1080; return; }
        Window focus;
        int revert;
        XGetInputFocus(g_display, &focus, &revert);
        XWindowAttributes attr;
        if (XGetWindowAttributes(g_display, focus, &attr)) {
            *width = attr.width;
            *height = attr.height;
        } else {
            *width = 1920; *height = 1080;
        }
    }

    int get_cursor_x() {
        if (!g_display) return 0;
        Window root, child;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;
        XQueryPointer(g_display, DefaultRootWindow(g_display),
                      &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);
        return root_x;
    }

    int get_cursor_y() {
        if (!g_display) return 0;
        Window root, child;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;
        XQueryPointer(g_display, DefaultRootWindow(g_display),
                      &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);
        return root_y;
    }

    void set_cursor_pos(int x, int y) {
        std::lock_guard<std::recursive_mutex> lk(g_xlib_mu);
        if (!g_display) return;
        XWarpPointer(g_display, None, DefaultRootWindow(g_display), 0, 0, 0, 0, x, y);
        XFlush(g_display);
    }

    bool send_key_press(unsigned int vk, bool press) {
        std::lock_guard<std::recursive_mutex> lk(g_xlib_mu);
        if (!g_display) return false;
        KeySym ks = linux_hook::vk_to_keysym((int)vk);
        if (!ks) return false;
        KeyCode kc = XKeysymToKeycode(g_display, ks);
        if (!kc) return false;
        XTestFakeKeyEvent(g_display, kc, press ? True : False, 0);
        XFlush(g_display);
        return true;
    }

    bool send_mouse_click(int button, bool press) {
        std::lock_guard<std::recursive_mutex> lk(g_xlib_mu);
        if (!g_display) return false;
        unsigned int xb = vk_to_xbutton(button);
        if (!xb) return false;
        XTestFakeButtonEvent(g_display, xb, press ? True : False, 0);
        XFlush(g_display);
        return true;
    }

    bool send_mouse_move_abs(int x, int y) {
        std::lock_guard<std::recursive_mutex> lk(g_xlib_mu);
        if (!g_display) return false;
        XTestFakeMotionEvent(g_display, -1, x, y, 0);
        XFlush(g_display);
        return true;
    }

    bool send_mouse_move_rel(int dx, int dy) {
        std::lock_guard<std::recursive_mutex> lk(g_xlib_mu);
        if (!g_display) return false;
        XTestFakeRelativeMotionEvent(g_display, dx, dy, 0);
        XFlush(g_display);
        return true;
    }

    bool is_key_pressed(unsigned int vk) {
        std::lock_guard<std::recursive_mutex> lk(g_xlib_mu);
        if (!g_display) return false;
        KeySym ks = linux_hook::vk_to_keysym((int)vk);
        if (!ks) return false;
        KeyCode kc = XKeysymToKeycode(g_display, ks);
        if (!kc) return false;
        char keys[32] = {0};
        XQueryKeymap(g_display, keys);
        return (keys[kc / 8] & (1 << (kc % 8))) != 0;
    }

    Display* get_display() { return g_display; }

    void shutdown() {
        if (s_xnext_target) {
            linux_hook::remove_hook(s_xnext_target);
            s_xnext_target = nullptr;
        }
        // Intentionally NOT nulling o_XNextEvent: remove_hook() restores the
        // original bytes but the trampoline stays mapped, so a thread already
        // inside the hook can still call through safely. Nulling it would make
        // the early-return path skip filling *event entirely and leave the
        // game's event loop dispatching an uninitialised XEvent.
        // Intentionally NOT calling XCloseDisplay(g_display). The display
        // connection belongs to the game/GLFW; closing it while other threads
        // (GL, X11 event loop) may still use it causes use-after-close crashes.
        // The connection is leaked on unload, which is acceptable.
    }
}
