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
            static void remove(const std::string& nick);
            static bool is_friend(const std::string& nick);
            static bool is_entity_friend(JNIEnv* env, jobject entity);
            static const std::vector<std::string>& get_list();
            static void save();
            static void load();

        private:
            static std::vector<std::string> s_friends;
            static std::mutex s_mutex;
            static std::string resolve_entity_name(JNIEnv* env, jobject entity);
        };
    }
}
