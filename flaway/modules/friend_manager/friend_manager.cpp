#include "friend_manager.h"
#include "../../utils/logger.h"
#include <sdk/classloader.h>
#include <sdk/mappings/mappings.hpp>

#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#include <direct.h>
#define ENH_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define ENH_MKDIR(p) mkdir(p, 0755)
#endif

namespace
{
    std::string friends_dir()
    {
        const char* home = getenv("HOME");
        if (home && *home)
            return std::string(home) + "/.minecraft/flaway";
#ifdef _WIN32
        return "flaway";
#else
        return "/tmp/flaway";
#endif
    }

    std::string friends_path()
    {
        return friends_dir() + "/friends.txt";
    }
}

namespace flaway
{
    namespace modules
    {
        std::vector<std::string> friend_manager::s_friends;
        std::mutex friend_manager::s_mutex;

        void friend_manager::add(const std::string& nick)
        {
            if (nick.empty()) return;
            std::vector<std::string> snapshot;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                for (const auto& f : s_friends)
                {
                    if (f == nick) return;
                }
                s_friends.push_back(nick);
                snapshot = s_friends;
            }
            // Disk I/O outside the lock: is_friend() runs per-entity on the
            // render thread and must not stall behind a file write.
            logger::log("[friend] added: " + nick);
            save_list(snapshot);
        }

        void friend_manager::remove(std::string nick)
        {
            // By value: the argument must not alias an element of s_friends,
            // otherwise std::remove() may move over the very string it compares.
            std::vector<std::string> snapshot;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                auto it = std::remove(s_friends.begin(), s_friends.end(), nick);
                if (it == s_friends.end()) return;
                s_friends.erase(it, s_friends.end());
                snapshot = s_friends;
            }
            logger::log("[friend] removed: " + nick);
            save_list(snapshot);
        }

        void friend_manager::clear()
        {
            std::vector<std::string> snapshot;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                if (s_friends.empty()) return;
                s_friends.clear();
            }
            logger::log("[friend] list cleared");
            save_list(snapshot);
        }

        bool friend_manager::is_friend(const std::string& nick)
        {
            if (nick.empty()) return false;
            std::lock_guard<std::mutex> lock(s_mutex);
            for (const auto& f : s_friends)
            {
                if (f == nick) return true;
                // ESP caches clean_name() which turns '_' into ' ' — also try
                // the underscore-stripped form so "VANOOOO_777" still matches
                // when the caller passes "VANOOOO 777".
                if (f.find('_') != std::string::npos)
                {
                    std::string f2 = f;
                    for (char& c : f2) if (c == '_') c = ' ';
                    if (f2 == nick) return true;
                }
                if (nick.find('_') != std::string::npos)
                {
                    std::string n2 = nick;
                    for (char& c : n2) if (c == '_') c = ' ';
                    if (f == n2) return true;
                }
            }
            return false;
        }

        std::vector<std::string> friend_manager::get_list()
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            return s_friends;
        }

        void friend_manager::save_list(const std::vector<std::string>& friends)
        {
            ENH_MKDIR(friends_dir().c_str());
            std::ofstream f(friends_path(), std::ios::out | std::ios::trunc);
            if (!f.is_open()) return;
            for (const auto& nick : friends)
            {
                f << nick << "\n";
            }
            f.close();
        }

        void friend_manager::save()
        {
            std::vector<std::string> snapshot;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                snapshot = s_friends;
            }
            save_list(snapshot);
        }

        void friend_manager::load()
        {
            std::ifstream f(friends_path(), std::ios::in);
            if (!f.is_open()) return;
            std::lock_guard<std::mutex> lock(s_mutex);
            s_friends.clear();
            std::string line;
            while (std::getline(f, line))
            {
                if (!line.empty())
                    s_friends.push_back(line);
            }
            f.close();
        }

        std::string friend_manager::resolve_entity_name(JNIEnv* env, jobject entity)
        {
            std::string result;
            if (!env || !entity) return result;

            // Try PlayerEntity.getGameProfile().getName() first
            jclass pe = sdk::classloader::find_class(env, sdk::mappings::player_entity_class_sig);
            if (pe && env->IsInstanceOf(entity, pe))
            {
                jmethodID mid = env->GetMethodID(pe,
                    sdk::mappings::player_get_game_profile_name,
                    sdk::mappings::player_get_game_profile_sig);
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(pe);
                if (mid)
                {
                    jobject profile = env->CallObjectMethod(entity, mid);
                    if (env->ExceptionCheck()) { env->ExceptionClear(); return result; }
                    if (profile)
                    {
                        jclass gc = env->GetObjectClass(profile);
                        if (gc)
                        {
                            jmethodID gn_mid = env->GetMethodID(gc,
                                sdk::mappings::game_profile_get_name_name,
                                sdk::mappings::game_profile_get_name_sig);
                            if (env->ExceptionCheck()) env->ExceptionClear();
                            if (gn_mid)
                            {
                                jstring name = (jstring)env->CallObjectMethod(profile, gn_mid);
                                if (env->ExceptionCheck()) env->ExceptionClear();
                                if (name)
                                {
                                    const char* utf = env->GetStringUTFChars(name, nullptr);
                                    if (utf) { result = utf; env->ReleaseStringUTFChars(name, utf); }
                                    env->DeleteLocalRef(name);
                                }
                            }
                            env->DeleteLocalRef(gc);
                        }
                        env->DeleteLocalRef(profile);
                    }
                }
                if (!result.empty()) return result;
            }
            else if (pe)
            {
                env->DeleteLocalRef(pe);
            }

            // Fallback: Entity.getName()
            jclass ec = sdk::classloader::find_class(env, sdk::mappings::entity_class_sig);
            if (!ec) return result;
            jmethodID mid = env->GetMethodID(ec,
                sdk::mappings::entity_get_name_name,
                sdk::mappings::entity_get_name_sig);
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(ec);
            if (!mid) return result;

            jobject name_text = env->CallObjectMethod(entity, mid);
            if (env->ExceptionCheck()) { env->ExceptionClear(); return result; }
            if (!name_text) return result;

            jclass tc = sdk::classloader::find_class(env, sdk::mappings::text_class_sig);
            if (tc)
            {
                jmethodID str_mid = env->GetMethodID(tc,
                    sdk::mappings::text_get_string_name,
                    sdk::mappings::text_get_string_sig);
                if (env->ExceptionCheck()) env->ExceptionClear();
                if (str_mid)
                {
                    jstring s = (jstring)env->CallObjectMethod(name_text, str_mid, 0x7FFFFFFF);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                    if (s)
                    {
                        const char* utf = env->GetStringUTFChars(s, nullptr);
                        if (utf) { result = utf; env->ReleaseStringUTFChars(s, utf); }
                        env->DeleteLocalRef(s);
                    }
                }
                env->DeleteLocalRef(tc);
            }
            env->DeleteLocalRef(name_text);
            return result;
        }

        bool friend_manager::is_entity_friend(JNIEnv* env, jobject entity)
        {
            std::string name = resolve_entity_name(env, entity);
            return is_friend(name);
        }
    }
}
