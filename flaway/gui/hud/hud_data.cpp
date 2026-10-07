#include "hud_internal.h"
#include "../../flaway.h"
#include "../../utils/rlog.h"
#include <sdk/classloader.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/world/world.h>
#include <sdk/mappings/mappings.hpp>

#include <chrono>
#include <cstring>

// ---------------------------------------------------------------------------
// Throttled JNI data providers for the HUD. Everything runs on the render
// thread (same as the old hud_* code) and caches its results — each poll has
// its own interval so the per-frame cost stays at a few hundred nanoseconds.
// ---------------------------------------------------------------------------
namespace hud_data {
namespace {

long long steady_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool check_exc(JNIEnv* env) {
    if (env && env->ExceptionCheck()) { env->ExceptionClear(); return true; }
    return false;
}
void rel(JNIEnv* env, jobject o) { if (env && o) env->DeleteLocalRef(o); }
std::string from_jstring(JNIEnv* env, jstring s) {
    std::string out;
    if (!env || !s) return out;
    const char* utf = env->GetStringUTFChars(s, nullptr);
    if (utf) { out = utf; env->ReleaseStringUTFChars(s, utf); }
    return out;
}

// state --------------------------------------------------------------------
long long s_frame_now = 0;
long long s_last_chat = -1000000, s_last_f1 = -1000000;
long long s_last_nick = -1000000, s_last_ping = -1000000, s_last_server = -1000000;
long long s_last_coord = -1000000, s_last_poison = -1000000;
long long s_last_target = -1000000, s_last_pickup = -1000000;
long long s_skin_retry_us = 0;

bool s_chat = false;
bool s_f1 = true;
int s_ping = -1;
bool s_ping_ok = false;
std::string s_server;
bool s_server_ok = false;
std::string s_nick;
bool s_poison = false;
double s_x = 0, s_y = 0, s_z = 0, s_bps = 0;
double s_lx = 0, s_lz = 0;
long long s_lt_us = 0;
TargetInfo s_target;
std::string s_skin_for;          // name the skin hash belongs to
std::vector<esp_pickup_entry> s_pickups;

// queries ------------------------------------------------------------------

bool query_chat_open() {
    if (!flaway::instance || !sdk::instance) return false;
    JNIEnv* env = flaway::instance->get_env();
    if (!env) return false;
    jobject mc = sdk::instance->get_minecraft();
    jclass mcc = sdk::instance->klass();
    if (!mc || !mcc) { rel(env, mc); rel(env, mcc); return false; }
    static jfieldID s_screen_fid = nullptr;
    if (!s_screen_fid) {
        s_screen_fid = env->GetFieldID(mcc, sdk::mappings::minecraft_screen_name,
                                       sdk::mappings::minecraft_screen_sig);
        check_exc(env);
    }
    bool open = false;
    if (s_screen_fid) {
        jobject screen = env->GetObjectField(mc, s_screen_fid);
        check_exc(env);
        if (screen) {
            jclass chat = sdk::classloader::find_class(env, sdk::mappings::chat_screen_class_sig);
            if (chat) {
                open = env->IsInstanceOf(screen, chat) == JNI_TRUE;
                env->DeleteLocalRef(chat);
            }
            env->DeleteLocalRef(screen);
        }
    }
    rel(env, mc);
    rel(env, mcc);
    return open;
}

bool query_hud_f1() {
    if (!flaway::instance || !sdk::instance) return true;
    JNIEnv* env = flaway::instance->get_env();
    if (!env) return true;
    jobject mc = sdk::instance->get_minecraft();
    jclass mcc = sdk::instance->klass();
    if (!mc || !mcc) { rel(env, mc); rel(env, mcc); return true; }
    static jmethodID s_mid = nullptr;
    if (!s_mid) {
        s_mid = env->GetMethodID(mcc, sdk::mappings::hud_is_hud_enabled_name,
                                 sdk::mappings::hud_is_hud_enabled_sig);
        check_exc(env);
    }
    bool on = true;
    if (s_mid) {
        jboolean v = env->CallBooleanMethod(mc, s_mid);
        if (!check_exc(env)) on = (v == JNI_TRUE);
    }
    rel(env, mc);
    rel(env, mcc);
    return on;
}

void refresh_nick() {
    s_nick.clear();
    if (!flaway::instance || !sdk::instance) return;
    JNIEnv* env = flaway::instance->get_env();
    if (!env) return;
    jobject pl = sdk::instance->get_player();
    if (!pl) return;
    jclass pc = sdk::classloader::find_class(env, sdk::mappings::player_entity_class_sig);
    if (pc) {
        jmethodID mid = env->GetMethodID(pc, sdk::mappings::player_get_game_profile_name,
                                         sdk::mappings::player_get_game_profile_sig);
        check_exc(env);
        if (mid) {
            jobject profile = env->CallObjectMethod(pl, mid);
            if (!check_exc(env) && profile) {
                jclass gc = env->GetObjectClass(profile);
                if (gc) {
                    jmethodID nm = env->GetMethodID(gc, sdk::mappings::game_profile_get_name_name,
                                                    sdk::mappings::game_profile_get_name_sig);
                    check_exc(env);
                    if (nm) {
                        jstring s = (jstring)env->CallObjectMethod(profile, nm);
                        if (!check_exc(env) && s) { s_nick = from_jstring(env, s); env->DeleteLocalRef(s); }
                    }
                    env->DeleteLocalRef(gc);
                }
                env->DeleteLocalRef(profile);
            }
        }
        env->DeleteLocalRef(pc);
    }
    rel(env, pl);
}

// Minimal base64 decoder. The textures property value is base64, and the
// literal "/texture/" cannot survive base64 intact — searching the raw string
// can never match, so the hash never resolved and the 2 s retry gate at
// refresh_target() never cleared.
static std::string base64_decode(const std::string& in) {
    static unsigned char T[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; i++) T[i] = 0xFF;
        const char* alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; alphabet[i]; i++) T[(unsigned char)alphabet[i]] = (unsigned char)i;
        init = true;
    }
    std::string out;
    out.reserve(in.size() * 3 / 4 + 4);
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') break;
        if (T[c] == 0xFF) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) { out.push_back((char)((val >> valb) & 0xFF)); valb -= 8; }
    }
    return out;
}

// Resolve the skin textures hash for a player name via the tab list:
// GameProfile.getProperties()["textures"] -> base64 JSON -> .../texture/<hash>
std::string hash_from_game_profile(JNIEnv* env, jobject profile) {
    std::string hash;
    if (!env || !profile) return hash;
    jclass gc = env->GetObjectClass(profile);
    if (!gc) return hash;
    jmethodID props = env->GetMethodID(gc, "getProperties", "()Lcom/mojang/authlib/PropertyMap;");
    check_exc(env);
    if (props) {
        jobject map = env->CallObjectMethod(profile, props);
        if (!check_exc(env) && map) {
            jclass mc_cls = env->GetObjectClass(map);
            if (mc_cls) {
                jmethodID getm = env->GetMethodID(mc_cls, "get", "(Ljava/lang/Object;)Ljava/util/Collection;");
                check_exc(env);
                if (getm) {
                    jstring key = env->NewStringUTF("textures");
                    jobject coll = key ? env->CallObjectMethod(map, getm, key) : nullptr;
                    if (key) env->DeleteLocalRef(key);
                    if (!check_exc(env) && coll) {
                        jclass cc = env->GetObjectClass(coll);
                        if (cc) {
                            jmethodID it_m = env->GetMethodID(cc, "iterator", "()Ljava/util/Iterator;");
                            check_exc(env);
                            if (it_m) {
                                jobject it = env->CallObjectMethod(coll, it_m);
                                jclass ic = nullptr;
                                if (!check_exc(env) && it) {
                                    ic = env->GetObjectClass(it);
                                    jmethodID hasNext = ic ? env->GetMethodID(ic, "hasNext", "()Z") : nullptr;
                                    jmethodID next = ic ? env->GetMethodID(ic, "next", "()Ljava/lang/Object;") : nullptr;
                                    check_exc(env);
                                    if (hasNext && next && env->CallBooleanMethod(it, hasNext) == JNI_TRUE) {
                                        jobject prop = env->CallObjectMethod(it, next);
                                        if (!check_exc(env) && prop) {
                                            jclass prc = env->GetObjectClass(prop);
                                            if (prc) {
                                                jmethodID val = env->GetMethodID(prc, "getValue", "()Ljava/lang/String;");
                                                check_exc(env);
                                                if (val) {
                                                    jstring v = (jstring)env->CallObjectMethod(prop, val);
                                                    if (!check_exc(env) && v) {
                                                        std::string json = base64_decode(from_jstring(env, v));
                                                        size_t p = json.find("/texture/");
                                                        if (p != std::string::npos) {
                                                            p += 9;
                                                            size_t e = p;
                                                            while (e < json.size() && ((json[e] >= '0' && json[e] <= '9') ||
                                                            (json[e] >= 'a' && json[e] <= 'f'))) e++;
                                                            if (e - p >= 32) hash = json.substr(p, e - p);
                                                        }
                                                        env->DeleteLocalRef(v);
                                                    }
                                                }
                                                env->DeleteLocalRef(prc);
                                            }
                                            env->DeleteLocalRef(prop);
                                        }
                                    }
                                    rel(env, it);
                                }
                                if (ic) env->DeleteLocalRef(ic);
                            }
                            env->DeleteLocalRef(cc);
                        }
                        rel(env, coll);
                    }
                }
                env->DeleteLocalRef(mc_cls);
            }
            rel(env, map);
        }
    }
    env->DeleteLocalRef(gc);
    return hash;
}

// PlayerListEntry.getProfile() -> GameProfile; fallback: nearby entity GameProfile.
std::string resolve_skin_hash(JNIEnv* env, const std::string& name) {
    std::string hash;
    if (!env || name.empty() || !sdk::instance) return hash;
    jobject handler = sdk::instance->get_network_handler();
    if (handler) {
        jclass hc = sdk::classloader::find_class(env, sdk::mappings::network_handler_class_sig);
        if (hc) {
            jmethodID get_entry = env->GetMethodID(hc, sdk::mappings::network_get_entry_by_name_name,
                                                   sdk::mappings::network_get_entry_by_name_sig);
            check_exc(env);
            if (get_entry) {
                jstring jn = env->NewStringUTF(name.c_str());
                jobject entry = nullptr;
                if (jn) { entry = env->CallObjectMethod(handler, get_entry, jn); env->DeleteLocalRef(jn); }
                if (!check_exc(env) && entry) {
                    jclass ec = sdk::classloader::find_class(env, sdk::mappings::player_list_entry_class_sig);
                    if (ec) {
                        jmethodID gp = env->GetMethodID(ec, sdk::mappings::entry_get_profile_name,
                                                        sdk::mappings::entry_get_profile_sig);
                        check_exc(env);
                        if (gp) {
                            jobject profile = env->CallObjectMethod(entry, gp);
                            if (!check_exc(env) && profile) {
                                hash = hash_from_game_profile(env, profile);
                                rel(env, profile);
                            }
                        }
                        env->DeleteLocalRef(ec);
                    }
                    rel(env, entry);
                }
            }
            env->DeleteLocalRef(hc);
        }
        rel(env, handler);
    }
    if (!hash.empty()) return hash;
    // Tab list had no textures (offline / missing property) — try the entity.
    if (!sdk::instance) return hash;
    jobject world = sdk::instance->get_world();
    if (!world) return hash;
    sdk::world_client wc(world);
    std::vector<jobject> players = wc.get_players();
    jclass player_cls = sdk::classloader::find_class(env, sdk::mappings::player_entity_class_sig);
    for (jobject p : players) {
        if (!p) continue;
        if (player_cls && !env->IsInstanceOf(p, player_cls)) { env->DeleteLocalRef(p); continue; }
        jclass pe = env->GetObjectClass(p);
        jmethodID gp = pe ? env->GetMethodID(pe, sdk::mappings::player_get_game_profile_name,
                                              sdk::mappings::player_get_game_profile_sig) : nullptr;
        check_exc(env);
        if (gp) {
            jobject profile = env->CallObjectMethod(p, gp);
            if (!check_exc(env) && profile) {
                jclass gc = env->GetObjectClass(profile);
                jmethodID gn = gc ? env->GetMethodID(gc, sdk::mappings::game_profile_get_name_name,
                                                      sdk::mappings::game_profile_get_name_sig) : nullptr;
                check_exc(env);
                bool match = false;
                if (gn) {
                    jstring jn2 = (jstring)env->CallObjectMethod(profile, gn);
                    if (!check_exc(env) && jn2) {
                        if (from_jstring(env, jn2) == name) match = true;
                        env->DeleteLocalRef(jn2);
                    }
                }
                if (match) hash = hash_from_game_profile(env, profile);
                if (gc) env->DeleteLocalRef(gc);
                rel(env, profile);
            }
        }
        if (pe) env->DeleteLocalRef(pe);
        env->DeleteLocalRef(p);
        if (!hash.empty()) break;
    }
    if (player_cls) env->DeleteLocalRef(player_cls);
    env->DeleteLocalRef(world);
    return hash;
}

void refresh_ping() {
    s_ping_ok = false;
    if (!flaway::instance || !sdk::instance || s_nick.empty()) return;
    JNIEnv* env = flaway::instance->get_env();
    if (!env) return;
    jobject handler = sdk::instance->get_network_handler();
    if (!handler) return;
    jclass hc = sdk::classloader::find_class(env, sdk::mappings::network_handler_class_sig);
    if (hc) {
        jmethodID mid = env->GetMethodID(hc, sdk::mappings::network_get_entry_by_name_name,
                                         sdk::mappings::network_get_entry_by_name_sig);
        check_exc(env);
        if (mid) {
            jstring jn = env->NewStringUTF(s_nick.c_str());
            jobject entry = jn ? env->CallObjectMethod(handler, mid, jn) : nullptr;
            if (jn) env->DeleteLocalRef(jn);
            if (!check_exc(env) && entry) {
                jclass ec = sdk::classloader::find_class(env, sdk::mappings::player_list_entry_class_sig);
                if (ec) {
                    jmethodID lat = env->GetMethodID(ec, sdk::mappings::entry_get_latency_name,
                                                     sdk::mappings::entry_get_latency_sig);
                    check_exc(env);
                    if (lat) {
                        jint v = env->CallIntMethod(entry, lat);
                        if (!check_exc(env)) { s_ping = (int)v; s_ping_ok = true; }
                    }
                    env->DeleteLocalRef(ec);
                }
                rel(env, entry);
            }
        }
        env->DeleteLocalRef(hc);
    }
    rel(env, handler);
}

void refresh_server() {
    s_server_ok = false;
    s_server.clear();
    if (!flaway::instance || !sdk::instance) return;
    JNIEnv* env = flaway::instance->get_env();
    if (!env) return;
    jobject mc = sdk::instance->get_minecraft();
    jclass mcc = sdk::instance->klass();
    if (!mc || !mcc) { rel(env, mc); rel(env, mcc); return; }
    static jmethodID s_mid = nullptr;
    if (!s_mid) {
        s_mid = env->GetMethodID(mcc, sdk::mappings::current_server_entry_name,
                                 sdk::mappings::current_server_entry_sig);
        check_exc(env);
    }
    if (s_mid) {
        jobject info = env->CallObjectMethod(mc, s_mid);
        if (!check_exc(env) && info) {
            jclass ic = env->GetObjectClass(info);
            if (ic) {
                jfieldID fid = env->GetFieldID(ic, sdk::mappings::server_info_address_name,
                                               sdk::mappings::server_info_address_sig);
                check_exc(env);
                if (fid) {
                    jstring addr = (jstring)env->GetObjectField(info, fid);
                    if (!check_exc(env) && addr) {
                        s_server = from_jstring(env, addr);
                        s_server_ok = !s_server.empty();
                        env->DeleteLocalRef(addr);
                    }
                }
                env->DeleteLocalRef(ic);
            }
            rel(env, info);
        }
    }
    rel(env, mc);
    rel(env, mcc);
}

void refresh_coords() {
    if (!flaway::instance || !sdk::instance) return;
    JNIEnv* env = flaway::instance->get_env();
    if (!env) return;
    jobject pl = sdk::instance->get_player();
    if (!pl) return;
    {
        sdk::entity_client ec(pl);
        s_x = ec.get_x();
        s_y = ec.get_y();
        s_z = ec.get_z();
    }
    rel(env, pl);
    long long t = s_frame_now;
    if (s_lt_us != 0) {
        double dt = (double)(t - s_lt_us) / 1000000.0;
        if (dt > 1e-4 && dt < 1.0) {
            double dx = s_x - s_lx, dz = s_z - s_lz;
            s_bps = sqrt(dx * dx + dz * dz) / dt;
        }
    }
    s_lx = s_x;
    s_lz = s_z;
    s_lt_us = t;
}

void refresh_poison() {
    s_poison = false;
    if (!flaway::instance || !sdk::instance) return;
    JNIEnv* env = flaway::instance->get_env();
    if (!env) return;
    jobject pl = sdk::instance->get_player();
    if (!pl) return;
    {
        sdk::entity_client ec(pl);
        s_poison = ec.has_poison();
    }
    rel(env, pl);
}

void refresh_target() {
    s_target.valid = false;
    s_target.items.clear();
    esp_render_entry e;
    bool found = flaway::modules::esp::snapshot_target(e);
    if (!found) { s_skin_for.clear(); s_target.skin_hash.clear(); s_target.skin_tex = 0; return; }
    s_target.valid = true;
    s_target.name = e.name;
    s_target.hp = e.health;
    s_target.max_hp = e.max_health > 0.0f ? e.max_health : 20.0f;
    s_target.dist = 0.0;
    if (e.smooth.valid) {
        const double* cv = e.smooth.current;
        esp_camera_data cam = flaway::modules::esp::camera();
        double dx = cv[0] - cam.cam_x, dy = cv[1] - cam.cam_y, dz = cv[2] - cam.cam_z;
        s_target.dist = sqrt(dx * dx + dy * dy + dz * dz);
    }
    s_target.items = e.items;

    // Skin hash: tab-list lookup, cached per name (retry every 2s on miss).
    if (s_target.name != s_skin_for ||
        (s_target.skin_hash.empty() && s_frame_now - s_skin_retry_us >= 2000000LL)) {
        if (s_target.name != s_skin_for) {
            s_skin_for = s_target.name;
            s_target.skin_hash.clear();
            s_target.skin_tex = 0;
            s_skin_retry_us = 0;
        }
        if (flaway::instance) {
            JNIEnv* env = flaway::instance->get_env();
            if (env) {
                s_target.skin_hash = resolve_skin_hash(env, s_target.name);
                s_skin_retry_us = s_frame_now;
                if (!s_target.skin_hash.empty())
                    rlog::logf("hud: skin hash for '%s' = %.16s...", s_target.name.c_str(),
                               s_target.skin_hash.c_str());
            }
        }
    }
}

} // namespace

void poll() {
    if (!flaway::instance || !sdk::instance) return;
    s_frame_now = steady_us();

    // F1 is needed by the render loop itself — always polled.
    chat_open(); // self-throttled, see below
    if (s_frame_now - s_last_f1 >= 200000LL) {
        s_last_f1 = s_frame_now;
        s_f1 = query_hud_f1();
    }

    const bool want_wm = globals::hud_watermark_enabled;
    const bool want_coords = globals::hud_coords_enabled;
    const bool want_target = globals::hud_target_enabled;
    const bool want_poison = globals::hud_poison_enabled;
    const bool want_pickups = globals::hud_pickups_enabled;

    if (want_wm) {
        if (s_frame_now - s_last_nick >= 5000000LL) { s_last_nick = s_frame_now; refresh_nick(); }
        if (s_frame_now - s_last_ping >= 500000LL) { s_last_ping = s_frame_now; refresh_ping(); }
        if (s_frame_now - s_last_server >= 1000000LL) { s_last_server = s_frame_now; refresh_server(); }
    }
    if (want_coords && s_frame_now - s_last_coord >= 33000LL) {
        s_last_coord = s_frame_now;
        refresh_coords();
    }
    if (want_poison && s_frame_now - s_last_poison >= 50000LL) {
        s_last_poison = s_frame_now;
        refresh_poison();
    }
    if (want_target && s_frame_now - s_last_target >= 50000LL) {
        s_last_target = s_frame_now;
        refresh_target();
    }
    if (want_pickups && s_frame_now - s_last_pickup >= 50000LL) {
        s_last_pickup = s_frame_now;
        s_pickups = flaway::modules::esp::pickups();
    }
}

void shutdown() {
    s_pickups.clear();
    s_target.items.clear();
    s_server.clear();
    s_nick.clear();
}

bool chat_open() {
    // Self-throttled: called from hud::edit_active() even before the HUD ever
    // renders (GUI::needs_overlay / input feeding run first).
    long long t = steady_us();
    if (t - s_last_chat >= 80000LL) {
        s_last_chat = t;
        bool c = query_chat_open();
        if (c != s_chat) {
            s_chat = c;
            rlog::logf("hud: edit mode %s", c ? "ON (chat open)" : "OFF");
        }
    }
    return s_chat;
}
bool hud_f1_shown() { return s_f1; }
bool ping(int& ms) { if (!s_ping_ok) return false; ms = s_ping; return true; }
bool server(std::string& out) { if (!s_server_ok) return false; out = s_server; return true; }
void coords(double& x, double& y, double& z, double& bps) { x = s_x; y = s_y; z = s_z; bps = s_bps; }
bool poisoned() { return s_poison; }
const TargetInfo& target() { return s_target; }
const std::vector<esp_pickup_entry>& pickups() { return s_pickups; }
long long now_us() { return s_frame_now; }

} // namespace hud_data
