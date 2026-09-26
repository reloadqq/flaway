#pragma once

#include <sdk/includes.h>
#include <sdk/minecraft/util/box.h>
#include <string>
#include <unordered_map>
#include <vector>

struct esp_player_data
{
    int id;
    double x, y, z;
    double min_x, min_y, min_z;
    double max_x, max_y, max_z;
    float health;
    float max_health;
    std::string name;
};

struct esp_item_data
{
    int id;
    double x, y, z;
    std::string name;
    unsigned char icon_r, icon_g, icon_b;
};

struct esp_camera_data
{
    double cam_x, cam_y, cam_z;
    float yaw, pitch;
    float fov;
};

struct esp_pickup_entry
{
    std::string name;
    unsigned char icon_r, icon_g, icon_b;
    long long time_us;
};

struct esp_smooth_state
{
    double current[9];
    double target[9];
    bool valid = false;
};

struct esp_item_slot
{
    std::string name;
    unsigned char icon_r = 88, icon_g = 140, icon_b = 255;
    int count = 0;
};

struct esp_render_entry
{
    esp_smooth_state smooth;
    std::string name;
    float health = 20.0f;
    float max_health = 20.0f;
    int hurt_time = 0;
    unsigned char icon_r = 88, icon_g = 140, icon_b = 255;
    std::vector<esp_item_slot> items;
    long long last_seen_us = 0;
    long long created_us = 0;
    bool is_target = false;
    bool is_friend = false;
};

namespace flaway
{
    namespace modules
    {
        class esp
        {
        public:
            static void run();
            static void draw_boxes();
            static void cleanup();

            // HUD accessors (render-thread safe, internally locked)
            static std::unordered_map<int, esp_render_entry> snapshot_players();
            static bool snapshot_target(esp_render_entry& out);
            static bool snapshot_entry(int id, esp_render_entry& out);
            static esp_camera_data camera();
            static std::vector<esp_pickup_entry> pickups();
        };
    }
}
