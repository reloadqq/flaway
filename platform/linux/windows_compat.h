#pragma once

// ============================================================================
// 1. СНАЧАЛА ВСЕ ЗАГОЛОВКИ
// ============================================================================
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include "x11_helper.h"

// ============================================================================
// 2. ЗАТЕМ ВСЕ ТИПЫ (typedef)
// ============================================================================
typedef unsigned long ULONG_PTR;
typedef long LONG_PTR;
typedef uint64_t ULONGLONG;
typedef int64_t LONGLONG;
typedef uint32_t DWORD;
typedef unsigned short WORD;
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned int UINT;
// Windows LONG is ALWAYS 32-bit. On LP64 a C `long` is 64-bit, which silently
// doubles the size of every struct that embeds it (POINT/RECT/MOUSEINPUT).
typedef int32_t LONG;
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* HINSTANCE;
typedef uint64_t WPARAM;
typedef int64_t LPARAM;
typedef intptr_t LRESULT;
typedef unsigned long HDC;
typedef unsigned long HGLRC;
typedef unsigned long HGDIOBJ;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef char CHAR;
typedef wchar_t WCHAR;
typedef unsigned long HWND;
typedef struct { LONG x, y; } POINT;
typedef struct { LONG left, top, right, bottom; } RECT;

// ============================================================================
// 3. ЗАТЕМ ВСЕ КОНСТАНТЫ (#define) - КРИТИЧЕСКИ ВАЖНО!
// ============================================================================
// Virtual Key Codes (ПОЛНЫЙ НАБОР)
#define VK_LBUTTON    0x01
#define VK_RBUTTON    0x02
#define VK_CANCEL     0x03
#define VK_MBUTTON    0x04
#define VK_XBUTTON1   0x05
#define VK_XBUTTON2   0x06
#define VK_BACK       0x08
#define VK_TAB        0x09
#define VK_CLEAR      0x0C
#define VK_RETURN     0x0D
#define VK_SHIFT      0x10
#define VK_CONTROL    0x11
#define VK_MENU       0x12
#define VK_PAUSE      0x13
#define VK_CAPITAL    0x14
#define VK_ESCAPE     0x1B
#define VK_SPACE      0x20
#define VK_PRIOR      0x21
#define VK_NEXT       0x22
#define VK_END        0x23
#define VK_HOME       0x24
#define VK_LEFT       0x25
#define VK_UP         0x26
#define VK_RIGHT      0x27
#define VK_DOWN       0x28
#define VK_SELECT     0x29
#define VK_PRINT      0x2A
#define VK_EXECUTE    0x2B
#define VK_SNAPSHOT   0x2C
#define VK_INSERT     0x2D
#define VK_DELETE     0x2E
#define VK_HELP       0x2F
#define VK_0          0x30
#define VK_1          0x31
#define VK_2          0x32
#define VK_3          0x33
#define VK_4          0x34
#define VK_5          0x35
#define VK_6          0x36
#define VK_7          0x37
#define VK_8          0x38
#define VK_9          0x39
#define VK_A          0x41
#define VK_B          0x42
#define VK_C          0x43
#define VK_D          0x44
#define VK_E          0x45
#define VK_F          0x46
#define VK_G          0x47
#define VK_H          0x48
#define VK_I          0x49
#define VK_J          0x4A
#define VK_K          0x4B
#define VK_L          0x4C
#define VK_M          0x4D
#define VK_N          0x4E
#define VK_O          0x4F
#define VK_P          0x50
#define VK_Q          0x51
#define VK_R          0x52
#define VK_S          0x53
#define VK_T          0x54
#define VK_U          0x55
#define VK_V          0x56
#define VK_W          0x57
#define VK_X          0x58
#define VK_Y          0x59
#define VK_Z          0x5A
#define VK_LWIN       0x5B
#define VK_RWIN       0x5C
#define VK_APPS       0x5D
#define VK_SLEEP      0x5F
#define VK_NUMPAD0    0x60
#define VK_NUMPAD1    0x61
#define VK_NUMPAD2    0x62
#define VK_NUMPAD3    0x63
#define VK_NUMPAD4    0x64
#define VK_NUMPAD5    0x65
#define VK_NUMPAD6    0x66
#define VK_NUMPAD7    0x67
#define VK_NUMPAD8    0x68
#define VK_NUMPAD9    0x69
#define VK_MULTIPLY   0x6A
#define VK_ADD        0x6B
#define VK_SEPARATOR  0x6C
#define VK_SUBTRACT   0x6D
#define VK_DECIMAL    0x6E
#define VK_DIVIDE     0x6F
#define VK_F1         0x70
#define VK_F2         0x71
#define VK_F3         0x72
#define VK_F4         0x73
#define VK_F5         0x74
#define VK_F6         0x75
#define VK_F7         0x76
#define VK_F8         0x77
#define VK_F9         0x78
#define VK_F10        0x79
#define VK_F11        0x7A
#define VK_F12        0x7B
#define VK_F13        0x7C
#define VK_F14        0x7D
#define VK_F15        0x7E
#define VK_F16        0x7F
#define VK_F17        0x80
#define VK_F18        0x81
#define VK_F19        0x82
#define VK_F20        0x83
#define VK_F21        0x84
#define VK_F22        0x85
#define VK_F23        0x86
#define VK_F24        0x87
#define VK_NUMLOCK    0x90
#define VK_SCROLL     0x91
#define VK_LSHIFT     0xA0
#define VK_RSHIFT     0xA1
#define VK_LCONTROL   0xA2
#define VK_RCONTROL   0xA3
#define VK_LMENU      0xA4
#define VK_RMENU      0xA5
#define VK_BROWSER_BACK        0xA6
#define VK_BROWSER_FORWARD     0xA7
#define VK_BROWSER_REFRESH     0xA8
#define VK_BROWSER_STOP        0xA9
#define VK_BROWSER_SEARCH      0xAA
#define VK_BROWSER_FAVORITES   0xAB
#define VK_BROWSER_HOME        0xAC
#define VK_VOLUME_MUTE         0xAD
#define VK_VOLUME_DOWN         0xAE
#define VK_VOLUME_UP           0xAF
#define VK_MEDIA_NEXT_TRACK    0xB0
#define VK_MEDIA_PREV_TRACK    0xB1
#define VK_MEDIA_STOP          0xB2
#define VK_MEDIA_PLAY_PAUSE    0xB3
#define VK_LAUNCH_MAIL         0xB4
#define VK_LAUNCH_MEDIA_SELECT 0xB5
#define VK_LAUNCH_APP1         0xB6
#define VK_LAUNCH_APP2         0xB7
#define VK_OEM_1        0xBA
#define VK_OEM_PLUS     0xBB
#define VK_OEM_COMMA    0xBC
#define VK_OEM_MINUS    0xBD
#define VK_OEM_PERIOD   0xBE
#define VK_OEM_2        0xBF
#define VK_OEM_3        0xC0
#define VK_OEM_4        0xDB
#define VK_OEM_5        0xDC
#define VK_OEM_6        0xDD
#define VK_OEM_7        0xDE
#define VK_OEM_8        0xDF
#define VK_OEM_102      0xE2
#define VK_PROCESSKEY   0xE5
#define VK_PACKET       0xE7
#define VK_ATTN         0xF6
#define VK_CRSEL        0xF7
#define VK_EXSEL        0xF8
#define VK_EREOF        0xF9
#define VK_PLAY         0xFA
#define VK_ZOOM         0xFB
#define VK_NONAME       0xFC
#define VK_PA1          0xFD
#define VK_OEM_CLEAR    0xFE

// Window Messages
#define WM_KEYDOWN        0x0100
#define WM_KEYUP          0x0101
#define WM_CHAR           0x0102
#define WM_DEADCHAR       0x0103
#define WM_SYSKEYDOWN     0x0104
#define WM_SYSKEYUP       0x0105
#define WM_SYSCHAR        0x0106
#define WM_SYSDEADCHAR    0x0107
#define WM_UNICHAR        0x0109
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION   0x010E
#define WM_IME_COMPOSITION      0x010F
#define WM_INITDIALOG     0x0110
#define WM_COMMAND        0x0111
#define WM_SYSCOMMAND     0x0112
#define WM_TIMER          0x0113
#define WM_HSCROLL        0x0114
#define WM_VSCROLL        0x0115
#define WM_INITMENU       0x0116
#define WM_INITMENUPOPUP  0x0117
#define WM_MENUSELECT     0x011F
#define WM_MENUCHAR       0x0120
#define WM_ENTERIDLE      0x0121
#define WM_MENURBUTTONUP  0x0122
#define WM_MENUDRAG       0x0123
#define WM_MENUGETOBJECT  0x0124
#define WM_UNINITMENUPOPUP 0x0125
#define WM_MENUCOMMAND    0x0126
#define WM_CHANGEUISTATE  0x0127
#define WM_UPDATEUISTATE  0x0128
#define WM_QUERYUISTATE   0x0129
#define WM_CTLCOLORMSGBOX 0x0132
#define WM_CTLCOLOREDIT   0x0133
#define WM_CTLCOLORLISTBOX 0x0134
#define WM_CTLCOLORBTN    0x0135
#define WM_CTLCOLORDLG    0x0136
#define WM_CTLCOLORSCROLLBAR 0x0137
#define WM_CTLCOLORSTATIC 0x0138
#define WM_MOUSEMOVE      0x0200
#define WM_LBUTTONDOWN    0x0201
#define WM_LBUTTONUP      0x0202
#define WM_LBUTTONDBLCLK  0x0203
#define WM_RBUTTONDOWN    0x0204
#define WM_RBUTTONUP      0x0205
#define WM_RBUTTONDBLCLK  0x0206
#define WM_MBUTTONDOWN    0x0207
#define WM_MBUTTONUP      0x0208
#define WM_MBUTTONDBLCLK  0x0209
#define WM_MOUSEWHEEL     0x020A
#define WM_XBUTTONDOWN    0x020B
#define WM_XBUTTONUP      0x020C
#define WM_XBUTTONDBLCLK  0x020D
#define WM_MOUSEHWHEEL    0x020E
#define WM_PARENTNOTIFY   0x0210
#define WM_ENTERMENULOOP  0x0211
#define WM_EXITMENULOOP   0x0212
#define WM_NEXTMENU       0x0213
#define WM_SIZING         0x0214
#define WM_CAPTURECHANGED 0x0215
#define WM_MOVING         0x0216
#define WM_POWERBROADCAST 0x0218
#define WM_DEVICECHANGE   0x0219
#define WM_MDICREATE      0x0220
#define WM_MDIDESTROY     0x0221
#define WM_MDIACTIVATE    0x0222
#define WM_MDIRESTORE     0x0223
#define WM_MDINEXT        0x0224
#define WM_MDIMAXIMIZE    0x0225
#define WM_MDITILE        0x0226
#define WM_MDICASCADE     0x0227
#define WM_MDIICONARRANGE 0x0228
#define WM_MDIGETACTIVE   0x0229
#define WM_MDISETMENU     0x0230
#define WM_ENTERSIZEMOVE  0x0231
#define WM_EXITSIZEMOVE   0x0232
#define WM_DROPFILES      0x0233
#define WM_MDIREFRESHMENU 0x0234
#define WM_IME_SETCONTEXT 0x0281
#define WM_IME_NOTIFY     0x0282
#define WM_IME_CONTROL    0x0283
#define WM_IME_COMPOSITIONFULL 0x0284
#define WM_IME_SELECT     0x0285
#define WM_IME_CHAR       0x0286
#define WM_IME_REQUEST    0x0288
#define WM_IME_KEYDOWN    0x0290
#define WM_IME_KEYUP      0x0291
#define WM_NCMOUSEHOVER   0x02A0
#define WM_MOUSEHOVER     0x02A1
#define WM_NCMOUSELEAVE   0x02A2
#define WM_MOUSELEAVE     0x02A3
#define WM_WTSSESSION_CHANGE 0x02B1
#define WM_TABLET_FIRST   0x02C0
#define WM_TABLET_LAST    0x02DF
#define WM_CUT            0x0300
#define WM_COPY           0x0301
#define WM_PASTE          0x0302
#define WM_CLEAR          0x0303
#define WM_UNDO           0x0304
#define WM_RENDERFORMAT   0x0305
#define WM_RENDERALLFORMATS 0x0306
#define WM_DESTROYCLIPBOARD 0x0307
#define WM_DRAWCLIPBOARD  0x0308
#define WM_PAINTCLIPBOARD 0x0309
#define WM_VSCROLLCLIPBOARD 0x030A
#define WM_SIZECLIPBOARD  0x030B
#define WM_ASKCBFORMATNAME 0x030C
#define WM_CHANGECBCHAIN  0x030D
#define WM_HSCROLLCLIPBOARD 0x030E
#define WM_QUERYNEWPALETTE 0x030F
#define WM_PALETTEISCHANGING 0x0310
#define WM_PALETTECHANGED 0x0311
#define WM_HOTKEY         0x0312
#define WM_PRINT          0x0317
#define WM_PRINTCLIENT    0x0318
#define WM_APPCOMMAND     0x0319
#define WM_THEMECHANGED   0x031A
#define WM_CLIPBOARDUPDATE 0x031D
#define WM_DWMCOMPOSITIONCHANGED 0x031E
#define WM_DWMNCRENDERINGCHANGED 0x031F
#define WM_DWMCOLORIZATIONCOLORCHANGED 0x0320
#define WM_DWMWINDOWMAXIMIZEDCHANGE 0x0321
#define WM_DWMSENDICONICTHUMBNAIL 0x0323
#define WM_DWMSENDICONICLIVEPREVIEWBITMAP 0x0326
#define WM_GETTITLEBARINFOEX 0x033F
#define WM_HANDHELDFIRST  0x0358
#define WM_HANDHELDLAST   0x035F
#define WM_AFXFIRST       0x0360
#define WM_AFXLAST        0x037F
#define WM_PENWINFIRST    0x0380
#define WM_PENWINLAST     0x038F
#define WM_APP            0x8000
#define WM_USER           0x0400
#define WM_SIZE           0x0005

// Mouse/Keyboard Input Flags
#define MOUSEEVENTF_MOVE        0x0001
#define MOUSEEVENTF_LEFTDOWN    0x0002
#define MOUSEEVENTF_LEFTUP      0x0004
#define MOUSEEVENTF_RIGHTDOWN   0x0008
#define MOUSEEVENTF_RIGHTUP     0x0010
#define MOUSEEVENTF_MIDDLEDOWN  0x0020
#define MOUSEEVENTF_MIDDLEUP    0x0040
#define MOUSEEVENTF_WHEEL       0x0800
#define MOUSEEVENTF_HWHEEL      0x1000
#define MOUSEEVENTF_XDOWN       0x0080
#define MOUSEEVENTF_XUP         0x0100
#define MOUSEEVENTF_ABSOLUTE    0x8000
#define MOUSEEVENTF_VIRTUALDESK 0x4000

#define KEYEVENTF_KEYUP         0x0002
#define KEYEVENTF_EXTENDEDKEY   0x0001
#define KEYEVENTF_UNICODE       0x0004
#define KEYEVENTF_SCANCODE      0x0008

#define INPUT_MOUSE     0
#define INPUT_KEYBOARD  1
#define INPUT_HARDWARE  2

#define MK_LBUTTON      0x0001
#define MK_RBUTTON      0x0002
#define MK_SHIFT        0x0004
#define MK_CONTROL      0x0008
#define MK_MBUTTON      0x0010
#define MK_XBUTTON1     0x0020
#define MK_XBUTTON2     0x0040

// Input Structures
// Field order matches the real Win32 MOUSEINPUT; positional/memcpy-based
// construction would otherwise silently swap dx/dy with the flags.
typedef struct {
    LONG dx;
    LONG dy;
    DWORD mouseData;
    DWORD dwFlags;
    DWORD dwExtraInfo;
} MOUSEINPUT;

typedef struct {
    WORD wVk;
    WORD wScan;
    DWORD dwFlags;
    DWORD time;
    ULONG_PTR dwExtraInfo;
} KEYBDINPUT;

typedef struct {
    DWORD type;
    union {
        MOUSEINPUT mi;
        KEYBDINPUT ki;
    };
} INPUT;

// Other Constants
#define TRUE 1
#define FALSE 0
#ifndef NULL
#define NULL 0
#endif
#define MAX_PATH 4096

// ============================================================================
// 4. ТОЛЬКО ПОСЛЕ ЭТОГО - ПРОСТРАНСТВО ИМЁН linux_hook
// ============================================================================
namespace linux_hook {
    inline Display* get_display() { return nullptr; }
    inline void get_cursor_pos(int& x, int& y) { x = x11_helper::get_cursor_x(); y = x11_helper::get_cursor_y(); }
    inline void set_cursor_pos(int x, int y) { x11_helper::set_cursor_pos(x, y); }
    inline Window get_active_window() { return (Window)x11_helper::get_window_handle(); }
    inline uint64_t get_tick_count() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    }
    inline void sleep_ms(DWORD ms) {
        if (ms > 0) usleep(ms * 1000);
    }
    inline KeySym vk_to_keysym(int vk) {
        switch (vk) {
            case VK_LBUTTON: return Button1;
            case VK_RBUTTON: return Button3;
            case VK_MBUTTON: return Button2;
            case VK_XBUTTON1: return 0;
            case VK_XBUTTON2: return 0;
            case VK_BACK: return XK_BackSpace;
            case VK_TAB: return XK_Tab;
            case VK_RETURN: return XK_Return;
            case VK_ESCAPE: return XK_Escape;
            case VK_SPACE: return XK_space;
            case VK_INSERT: return XK_Insert;
            case VK_DELETE: return XK_Delete;
            case VK_HOME: return XK_Home;
            case VK_END: return XK_End;
            case VK_PRIOR: return XK_Page_Up;
            case VK_NEXT: return XK_Page_Down;
            case VK_LEFT: return XK_Left;
            case VK_UP: return XK_Up;
            case VK_RIGHT: return XK_Right;
            case VK_DOWN: return XK_Down;
            case VK_CAPITAL: return XK_Caps_Lock;
            case VK_F1: return XK_F1;
            case VK_F2: return XK_F2;
            case VK_F3: return XK_F3;
            case VK_F4: return XK_F4;
            case VK_F5: return XK_F5;
            case VK_F6: return XK_F6;
            case VK_F7: return XK_F7;
            case VK_F8: return XK_F8;
            case VK_F9: return XK_F9;
            case VK_F10: return XK_F10;
            case VK_F11: return XK_F11;
            case VK_F12: return XK_F12;
            case VK_NUMPAD0: return XK_KP_0;
            case VK_NUMPAD1: return XK_KP_1;
            case VK_NUMPAD2: return XK_KP_2;
            case VK_NUMPAD3: return XK_KP_3;
            case VK_NUMPAD4: return XK_KP_4;
            case VK_NUMPAD5: return XK_KP_5;
            case VK_NUMPAD6: return XK_KP_6;
            case VK_NUMPAD7: return XK_KP_7;
            case VK_NUMPAD8: return XK_KP_8;
            case VK_NUMPAD9: return XK_KP_9;
            case VK_0: return XK_0;
            case VK_1: return XK_1;
            case VK_2: return XK_2;
            case VK_3: return XK_3;
            case VK_4: return XK_4;
            case VK_5: return XK_5;
            case VK_6: return XK_6;
            case VK_7: return XK_7;
            case VK_8: return XK_8;
            case VK_9: return XK_9;
            case VK_A: return XK_a;
            case VK_B: return XK_b;
            case VK_C: return XK_c;
            case VK_D: return XK_d;
            case VK_E: return XK_e;
            case VK_F: return XK_f;
            case VK_G: return XK_g;
            case VK_H: return XK_h;
            case VK_I: return XK_i;
            case VK_J: return XK_j;
            case VK_K: return XK_k;
            case VK_L: return XK_l;
            case VK_M: return XK_m;
            case VK_N: return XK_n;
            case VK_O: return XK_o;
            case VK_P: return XK_p;
            case VK_Q: return XK_q;
            case VK_R: return XK_r;
            case VK_S: return XK_s;
            case VK_T: return XK_t;
            case VK_U: return XK_u;
            case VK_V: return XK_v;
            case VK_W: return XK_w;
            case VK_X: return XK_x;
            case VK_Y: return XK_y;
            case VK_Z: return XK_z;
            case VK_LSHIFT: return XK_Shift_L;
            case VK_RSHIFT: return XK_Shift_R;
            case VK_LCONTROL: return XK_Control_L;
            case VK_RCONTROL: return XK_Control_R;
            case VK_LMENU: return XK_Alt_L;
            case VK_RMENU: return XK_Alt_R;
            default: return 0;
        }
    }
    inline bool is_key_pressed(KeySym ks) { 
        (void)ks;
        return false; 
    }
    inline void move_mouse(int dx, int dy) { x11_helper::send_mouse_move_rel(dx, dy); }
}

// ============================================================================
// 5. ФУНКЦИИ WINDOWS API (после пространства имён)
// ============================================================================
inline BOOL ClientToScreen(HWND, POINT* lpPoint) { return lpPoint ? (lpPoint->x+=0, lpPoint->y+=0, TRUE) : FALSE; }
inline BOOL ScreenToClient(HWND, POINT* lpPoint) { return lpPoint ? TRUE : FALSE; }
inline BOOL GetClientRect(HWND, RECT* lpRect) {
    if (!lpRect) return FALSE;
    // Real window size: the hardcoded 1920x1080 put every slot/row computation
    // (autototem) on the wrong rectangle at any other resolution.
    int w = 0, h = 0;
    x11_helper::get_window_dimensions(&w, &h);
    lpRect->left = 0; lpRect->top = 0;
    lpRect->right  = w > 0 ? w : 1920;
    lpRect->bottom = h > 0 ? h : 1080;
    return TRUE;
}
inline BOOL GetCursorPos(POINT* lpPoint) {
    if (!lpPoint) return FALSE;
    int x=0, y=0; linux_hook::get_cursor_pos(x, y);
    lpPoint->x = x; lpPoint->y = y; return TRUE;
}
inline BOOL SetCursorPos(int x, int y) { linux_hook::set_cursor_pos(x, y); return TRUE; }
inline HWND GetForegroundWindow() { return (HWND)linux_hook::get_active_window(); }
inline ULONGLONG GetTickCount64() { return linux_hook::get_tick_count(); }
inline void Sleep(DWORD ms) { linux_hook::sleep_ms(ms); }
inline short GetAsyncKeyState(int vk) {
    return x11_helper::is_key_pressed((unsigned int)vk) ? 0x8001 : 0;
}
inline UINT SendInput(UINT cInputs, INPUT* pInputs, int cbSize) {
    if (!pInputs || cInputs == 0) return 0;
    UINT sent = 0;
    for (UINT i = 0; i < cInputs; i++) {
        if (pInputs[i].type == INPUT_KEYBOARD) {
            bool press = (pInputs[i].ki.dwFlags & KEYEVENTF_KEYUP) == 0;
            if (x11_helper::send_key_press(pInputs[i].ki.wVk, press))
                sent++;
        } else if (pInputs[i].type == INPUT_MOUSE) {
            DWORD flags = pInputs[i].mi.dwFlags;
            if (flags & MOUSEEVENTF_LEFTDOWN)
                if (x11_helper::send_mouse_click(0x01, true)) sent++;
            if (flags & MOUSEEVENTF_LEFTUP)
                if (x11_helper::send_mouse_click(0x01, false)) sent++;
            if (flags & MOUSEEVENTF_RIGHTDOWN)
                if (x11_helper::send_mouse_click(0x02, true)) sent++;
            if (flags & MOUSEEVENTF_RIGHTUP)
                if (x11_helper::send_mouse_click(0x02, false)) sent++;
            if (flags & MOUSEEVENTF_MIDDLEDOWN)
                if (x11_helper::send_mouse_click(0x04, true)) sent++;
            if (flags & MOUSEEVENTF_MIDDLEUP)
                if (x11_helper::send_mouse_click(0x04, false)) sent++;
            if (flags & MOUSEEVENTF_MOVE) {
                if (flags & MOUSEEVENTF_ABSOLUTE) {
                    x11_helper::send_mouse_move_abs(pInputs[i].mi.dx, pInputs[i].mi.dy);
                } else {
                    x11_helper::send_mouse_move_rel(pInputs[i].mi.dx, pInputs[i].mi.dy);
                }
                sent++;
            }
            if (flags & MOUSEEVENTF_WHEEL) {
                short wheel = (short)(pInputs[i].mi.mouseData >> 16);
                // Not directly supported, skip for now
                (void)wheel;
            }
        }
    }
    return sent;
}
inline void mouse_event(DWORD dwFlags, DWORD dx, DWORD dy, DWORD dwData, ULONG_PTR dwExtraInfo) {
    if (dwFlags & MOUSEEVENTF_MOVE) {
        if (dwFlags & MOUSEEVENTF_ABSOLUTE) {
            x11_helper::send_mouse_move_abs((int)dx, (int)dy);
        } else {
            x11_helper::send_mouse_move_rel((int)dx, (int)dy);
        }
    }
    if (dwFlags & MOUSEEVENTF_LEFTDOWN) x11_helper::send_mouse_click(0x01, true);
    if (dwFlags & MOUSEEVENTF_LEFTUP) x11_helper::send_mouse_click(0x01, false);
    if (dwFlags & MOUSEEVENTF_RIGHTDOWN) x11_helper::send_mouse_click(0x02, true);
    if (dwFlags & MOUSEEVENTF_RIGHTUP) x11_helper::send_mouse_click(0x02, false);
    if (dwFlags & MOUSEEVENTF_MIDDLEDOWN) x11_helper::send_mouse_click(0x04, true);
    if (dwFlags & MOUSEEVENTF_MIDDLEUP) x11_helper::send_mouse_click(0x04, false);
}
inline void RtlMoveMemory(void* dest, const void* src, size_t len) { memmove(dest, src, len); }
// GetModuleHandleA / CreateThread / GetProcAddress intentionally NOT declared:
// they had zero call sites and returned nullptr — a landmine for anyone who
// later links against them expecting real behaviour.
inline void WaitForSingleObject(HANDLE, DWORD) {}
inline void CloseHandle(HANDLE) {}
inline void DisableThreadLibraryCalls(HMODULE) {}
inline LRESULT CallWindowProcA(void*, HWND, UINT, WPARAM, LPARAM) { return 0; }
inline LRESULT PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    (void)hWnd;
    switch (Msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            unsigned int vk = (unsigned int)wParam;
            bool is_down = (Msg == WM_KEYDOWN || Msg == WM_SYSKEYDOWN);
            x11_helper::send_key_press(vk, is_down);
            return 1;
        }
        case WM_LBUTTONDOWN: x11_helper::send_mouse_click(0x01, true); return 1;
        case WM_LBUTTONUP:   x11_helper::send_mouse_click(0x01, false); return 1;
        case WM_RBUTTONDOWN: x11_helper::send_mouse_click(0x02, true); return 1;
        case WM_RBUTTONUP:   x11_helper::send_mouse_click(0x02, false); return 1;
        case WM_MBUTTONDOWN: x11_helper::send_mouse_click(0x04, true); return 1;
        case WM_MBUTTONUP:   x11_helper::send_mouse_click(0x04, false); return 1;
        default:
            return 0;
    }
}
#define MAKELPARAM(l, h) ((LPARAM)(((WORD)(l)) | ((DWORD)((WORD)(h))) << 16))
typedef int (*WNDPROC)(void*);
typedef void* (*LPTHREAD_START_ROUTINE)(void*);
#define MH_OK 0
#define MH_Initialize() MH_OK
#define MH_Uninitialize() ((void)0)
#define MH_CreateHook(a,b,c) MH_OK
#define MH_EnableHook(a) ((void)0)
#define MH_DisableHook(a) ((void)0)
#define MH_RemoveHook(a) ((void)0)