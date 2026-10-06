#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <mutex>
#include <jni.h>

namespace flaway
{
    namespace modules
    {
        class friend_manager
        {
        public:
            static void add(const std::string& nick);
            static void remove(std::string nick);
            static void clear();
            static bool is_friend(const std::string& nick);
            static bool is_entity_friend(JNIEnv* env, jobject entity);
            // Returns a COPY: callers must never hold a reference into the live
            // container, and the copy is taken under the lock.
            static std::vector<std::string> get_list();
            static void save();
            static void load();

        private:
            static std::vector<std::string> s_friends;
            static std::mutex s_mutex;
            // Disk I/O - never called while s_mutex is held.
            static void save_list(const std::vector<std::string>& friends);
            static std::string resolve_entity_name(JNIEnv* env, jobject entity);
        };
    }
}
