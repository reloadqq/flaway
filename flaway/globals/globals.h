#pragma once

#include <cstdint>

#ifdef _WIN32
#include <Windows.h>
#else
typedef uint64_t ULONGLONG;
#include "../platform/linux/windows_compat.h"
#endif

namespace globals
{
    // Triggerbot
    extern bool triggerbot_enabled;
    extern int triggerbot_keybind;
    extern int triggerbot_keybind_mode;
    extern bool triggerbot_weapon_only;
    extern float triggerbot_distance;          // max attack distance (0.5-6.0)
    extern int triggerbot_sprint_mode;         // 0 HvH, 1 Normal, 2 Legit, 3 Off
    extern bool triggerbot_jump_only;

    // Aimtarget (Aim Assistant)
    extern bool aimtarget_enabled;
    extern int aimtarget_keybind;
    extern bool aimtarget_players;
    extern bool aimtarget_mobs;
    extern bool aimtarget_animals;
    extern bool aimtarget_friends;
    extern bool aimtarget_through_walls;
    extern float aimtarget_threshold;      // 1.0 - 5.0
    extern float aimtarget_fov;            // max aim cone (10 - 180 deg)
    extern bool aimtarget_weapon_only;

    // Reach
    extern bool reach_enabled;
    extern int reach_keybind;
    extern int reach_mode;
    extern double reach_distance;

    // Hitbox
    extern bool hitbox_enabled;
    extern int hitbox_keybind;
    extern int hitbox_mode;
    extern double hitbox_expand_width;
    extern double hitbox_expand_height;

    // Shield Breaker
    extern bool shield_breaker_enabled;
    extern int shield_breaker_keybind;
    extern int shield_breaker_keybind_mode;
    extern bool shield_breaker_aim;
    extern bool shield_breaker_switch_back;
    extern int shield_breaker_delay_ms;
    extern int shield_breaker_saved_slot;
    extern ULONGLONG shield_breaker_last_attack;

    // Mace
    extern bool mace_enabled;
    extern int mace_keybind;
    extern int mace_keybind_mode;
    extern bool mace_look;
    extern bool mace_switch_back;
    extern bool mace_remove_elytra;
    extern double mace_min_fall_distance;
    extern double mace_height_above_target;
    extern double mace_fall_hitbox_width;
    extern double mace_fall_hitbox_height;
    extern int mace_saved_slot;
    extern ULONGLONG mace_last_attack;
    extern bool mace_elytra_swapped_this_fall;
    extern bool mace_was_in_fall_distance;

    // AutoCrystal
    extern bool autocrystal_enabled;
    extern int autocrystal_keybind;
    extern int autocrystal_mode;
    extern int autocrystal_delay_ms;
    extern bool autocrystal_debug_enabled;

    // AutoTotem
    extern bool autototem_enabled;
    extern int autototem_keybind;
    extern int autototem_mode;
    extern bool autototem_rage_mode;

    // Anchor Macro
    extern bool anchor_macro_enabled;
    extern int anchor_macro_keybind;
    extern int anchor_macro_mode;
    extern bool anchor_macro_break_anchor;
    extern int anchor_macro_swap_delay_ms;
    extern int anchor_macro_charge_delay_ms;
    extern int anchor_macro_break_delay_ms;
    extern bool anchor_macro_toggled;
    extern bool anchor_macro_executing;
    extern int anchor_macro_step;
    extern int anchor_macro_saved_slot;
    extern ULONGLONG anchor_macro_last_action;

    // Backtrack
    extern bool backtrack_enabled;
    extern int backtrack_keybind;
    extern int backtrack_mode;
    extern int backtrack_max_delay_ms;
    extern double backtrack_min_distance;
    extern double backtrack_max_distance;
    extern int backtrack_max_hurt_time_ms;
    extern bool backtrack_disable_on_hit;
    extern bool backtrack_visualization_enabled;
    extern float backtrack_visualization_color[4];
    extern float backtrack_visualization_line_width;
    extern bool backtrack_visualization_filled;
    extern float backtrack_cooldown_seconds;

    // Stun Slam
    extern bool stun_slam_enabled;
    extern float stun_slam_chance;
    extern int stun_slam_swap_delay_ms;
    extern int stun_slam_axe_delay_ms;
    extern int stun_slam_mace_delay_ms;
    extern double stun_slam_min_fall;

    // S-Tap
    extern bool stap_enabled;
    extern int stap_duration_ms;
    extern bool stap_is_active;
    extern ULONGLONG stap_start_time;

    // W-Tap
    extern bool wtap_enabled;
    extern int wtap_duration_ms;
    extern bool wtap_is_active;
    extern ULONGLONG wtap_start_time;

    // Auto Jump Reset
    extern bool autojumpreset_enabled;
    extern int autojumpreset_keybind;
    extern int autojumpreset_mode;
    extern int autojumpreset_cooldown_ms;
    extern float autojumpreset_last_health;
    extern ULONGLONG autojumpreset_last_jump;

    // ESP
    extern bool box_enabled;
    extern bool esp_health_bar;
    extern int esp_keybind;
    extern int esp_mode;
    extern bool esp_name_enabled;
    extern bool esp_item_enabled;
    extern bool esp_hide_vanilla_names;
    extern bool esp_show_fov;
    extern float esp_fov_color[4];
    extern float esp_vertical_offset;
    extern bool mobstats_enabled;
    extern bool esp_tracers;
    extern float esp_tracer_color[4];
    extern bool esp_arrows;

    // Server Rotation
    extern bool server_rotation_enabled;

    // Storage ESP
    extern bool storage_esp_enabled;
    extern bool storage_esp_chest;
    extern bool storage_esp_ender_chest;
    extern bool storage_esp_shulker;
    extern int storage_esp_mode;  // 0 = 2D box, 1 = 3D wireframe

    // BaseFinder
    extern bool base_finder_enabled;
    extern int base_finder_keybind;
    extern int base_finder_mode;       // 0 = Пещеры, 1 = Обход, 2 = Оба
    extern bool base_finder_click;     // ставить блоки в найденные позиции
    extern double base_finder_range;   // радиус поиска (1..128)
    extern int base_finder_min_size;   // мин. размер пещеры
    extern int base_finder_max_size;   // макс. размер пещеры
    extern int base_finder_min_length; // мин. длина пещеры
    extern int base_finder_min_width;  // мин. ширина пещеры
    extern bool base_finder_holy_world; // HolyWorld-режим обхода (не-лава с light>5)
    extern float base_finder_cave_color[4];
    extern float base_finder_bypass_color[4];

    // BaseFinder — поиск конкретных целей (сундуки, шалкеры, спавнеры,
    // рамка/блок энд-портала, обсидиан, игроки) + трейсеры к ним и
    // уведомления в чат/файл.
    extern bool base_finder_targets;          // главный переключатель поиска целей
    extern bool base_finder_target_chest;     // сундуки / сундуки с пряткой / бочки
    extern bool base_finder_target_shulker;   // шалкер-боксы
    extern bool base_finder_target_spawner;   // спавнеры
    extern bool base_finder_target_frame;     // рамка энд-портала
    extern bool base_finder_target_endportal; // блок энд-портала
    extern bool base_finder_target_obsidian;  // обсидиан / плачущий обсидиан
    extern bool base_finder_target_players;   // игроки
    extern bool base_finder_tracers;          // трейсеры от низа экрана к целям
    extern bool base_finder_log_chat;         // писать находки в локальный чат
    extern bool base_finder_log_file;         // писать находки в ~/.minecraft/…
    extern float base_finder_target_color[4];

    // Debug
    extern bool debug_logging_enabled;

    // Misc
    extern bool flight_enabled;
    extern bool sprint_enabled;
    extern bool autosprint_keep_swimming;
    extern int pearl_catch_keybind;
    extern int pearl_catch_mode;
    extern int pearl_catch_aim_mode;
    extern bool pearl_catch_enabled;
    extern bool show_gui;

    // Chest Stealer
    extern bool chest_stealer_enabled;
    extern int chest_stealer_keybind;
    extern int chest_stealer_mode;          // 0 FunTime, 1 HolyWorld, 2 ReallyWorld, 3 Custom
    extern int chest_stealer_start_delay;   // ticks
    extern int chest_stealer_min_delay;     // ticks
    extern int chest_stealer_max_delay;     // ticks
    extern int chest_stealer_close_delay;   // ticks
    extern bool chest_stealer_close_screen;

    // Unhook All (separate from normal shutdown)
    extern bool unhook_all_enabled;
    extern int unhook_all_keybind;

    // Fullbright
    extern bool fullbright_enabled;
    extern int fullbright_keybind;
    extern double fullbright_gamma;

    // Fog visual (screen-space overlay)
    extern bool fog_enabled;
    extern int fog_mode;          // 0 = vignette, 1 = full, 2 = bottom
    extern float fog_color[4];
    extern float fog_strength;    // 0..100

    // HUD
    extern bool hud_watermark_enabled;
    extern bool hud_target_enabled;
    extern bool hud_coords_enabled;
    extern bool hud_keybinds_enabled;
    extern bool hud_pickups_enabled;
    extern bool hud_poison_enabled;
    extern bool hud_arraylist_enabled;
    extern float hud_scale;
    // HUD element positions (pixels, <0 = default 12,12). Draggable while chat is open (edit mode).
    extern float hud_watermark_pos[2];
    extern float hud_keybinds_pos[2];
    extern float hud_target_pos[2];
    extern float hud_coords_pos[2];
    extern float hud_pickups_pos[2];
    extern float hud_poison_pos[2];
    extern float hud_arraylist_pos[2];
    // Per-element scale (multiplied by hud_scale).
    extern float hud_watermark_scale;
    extern float hud_keybinds_scale;
    extern float hud_target_scale;
    extern float hud_coords_scale;
    extern float hud_pickups_scale;
    extern float hud_poison_scale;
    extern float hud_arraylist_scale;

    // GUI theme
    extern int theme_id;
    extern bool gui_background_gradient;
    extern int gui_gradient_style;
    // Accent swatch index (0..5) and gradient preset index (-1 = derive the
    // gradient from the accent, 0..3 = Ocean/Sunset/Fire/Forest).
    extern int theme_accent;
    extern int theme_grad;
    // Light/dark appearance (persisted; applied on the render path).
    extern bool theme_dark;

    // Discord Rich Presence (flaway/utils/discord_rpc.cpp)
    extern bool discord_rpc_enabled;
    // Application (client) ID from https://discord.com/developers/applications
    extern char discord_rpc_client_id[24];
    // Second status line (the first one is the joined server address).
    extern char discord_rpc_state[64];
    // First line fallback while NOT on a server (config only, no GUI field).
    extern char discord_rpc_details[64];
    // Optional asset key uploaded in the Discord app, empty = no image
    // (config only, no GUI field).
    extern char discord_rpc_large_image[32];
}
