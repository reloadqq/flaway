/*
 *  -----------------------------------
 * |         JNIHook - by rdbo         |
 * |      Java VM Hooking Library      |
 *  -----------------------------------
 */

/*
 * Copyright (C) 2023    Rdbo
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License version 3
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <jnihook.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <mutex>
#include "classfile.hpp"
#include "uuid.hpp"
#include "flaway/utils/no_log.h"
#include "flaway/utils/rlog.h"

typedef struct jnihook_t {
        JavaVM   *jvm;
        jvmtiEnv *jvmti;
} jnihook_t;

// Guards ALL access to g_jnihook, g_hooks, g_class_file_cache and
// g_original_classes. The ClassFileLoadHook callback can fire on the JVM's
// VMThread (or on ANY thread that triggers RetransformClasses) while another
// thread is inside JNIHook_Attach/Detach/Shutdown; without this mutex those
// concurrent std::unordered_map mutations corrupt the heap (observed as
// random SIGSEGVs in libc memmove / stack corruption far away from the race).
// RECURSIVE on purpose: JNIHook_Attach holds the lock across
// RetransformClasses, and the synchronous ClassFileLoadHook callback
// (same thread) re-locks without deadlocking.
static std::recursive_mutex g_jnihook_mtx;

// RAII guard so map mutation is exception-safe.
static std::unique_lock<std::recursive_mutex> lock_jnihook()
{
        return std::unique_lock<std::recursive_mutex>(g_jnihook_mtx);
}

typedef struct method_info_t {
        std::string name;
        std::string signature;
        jint access_flags;
} method_info_t;

typedef struct hook_info_t {
        method_info_t method_info;
        void *native_hook_method;
} hook_info_t;

static std::unique_ptr<jnihook_t> g_jnihook = nullptr;
static std::unordered_map<std::string, std::vector<hook_info_t>> g_hooks;
static std::unordered_map<std::string, std::unique_ptr<ClassFile>> g_class_file_cache;
static std::unordered_map<std::string, jclass> g_original_classes;

// Returns true only if `jvm` is still a live, queryable JavaVM.
// During JVM shutdown (System.exit -> DestroyJavaVM) the JavaVM vtable can
// become invalid; calling jvm->GetEnv() on it then SIGSEGVs (observed in
// JNIHook_Detach+0xe7 when the game window closes while we are unloading).
// JNI_GetCreatedJavaVMs is a plain libjvm export, safe to call at any time.
static bool
jvm_is_alive(JavaVM *jvm)
{
        if (!jvm) return false;
        JavaVM *vms[4];
        jsize count = 0;
        if (JNI_GetCreatedJavaVMs(vms, 4, &count) != JNI_OK)
                return false;
        for (jsize i = 0; i < count; ++i)
                if (vms[i] == jvm)
                        return true;
        return false;
}

static std::string
get_class_name(JNIEnv *env, jclass clazz)
{


        jclass klass = env->FindClass("java/lang/Class");
        if (!klass)
                return "";

        jmethodID getName_method = env->GetMethodID(klass, "getName", "()Ljava/lang/String;");
        if (!getName_method)
                return "";

        jstring name_obj = reinterpret_cast<jstring>(env->CallObjectMethod(clazz, getName_method));
        if (!name_obj)
                return "";

        const char *c_name = env->GetStringUTFChars(name_obj, 0);
        if (!c_name)
                return "";

        std::string name = std::string(c_name, &c_name[strlen(c_name)]);

        env->ReleaseStringUTFChars(name_obj, c_name);

        // Replace dots with slashes to match contents of ClassFile
        for (size_t i = 0; i < name.length(); ++i) {
                if (name[i] == '.')
                        name[i] = '/';
        }
        
        return name;
}

static std::unique_ptr<method_info_t>
get_method_info(jvmtiEnv *jvmti, jmethodID method)
{
        char *name;
        char *sig;
        jint access_flags;
        
        if (jvmti->GetMethodName(method, &name, &sig, NULL) != JVMTI_ERROR_NONE)
                return nullptr;

        if (jvmti->GetMethodModifiers(method, &access_flags) != JVMTI_ERROR_NONE)
                return nullptr;

        std::string name_str(name, &name[strlen(name)]);
        std::string signature_str(sig, &sig[strlen(sig)]);

        jvmti->Deallocate(reinterpret_cast<unsigned char *>(name));
        jvmti->Deallocate(reinterpret_cast<unsigned char *>(sig));

        return std::make_unique<method_info_t>(method_info_t { name_str, signature_str, access_flags });
}

void JNICALL JNIHook_ClassFileLoadHook(jvmtiEnv *jvmti_env,
                                       JNIEnv* jni_env,
                                       jclass class_being_redefined,
                                       jobject loader,
                                       const char* name,
                                       jobject protection_domain,
                                       jint class_data_len,
                                       const unsigned char* class_data,
                                       jint* new_class_data_len,
                                       unsigned char** new_class_data)
{
        auto guard = lock_jnihook();

        fprintf(stderr, "[JNIHOOK] ClassFileLoadHook: clazz=%p name=%s data_len=%d\n",
                (void*)class_being_redefined, name ? name : "(null)", (int)class_data_len);
        fflush(stderr);

        std::string class_name;
        if (name) {
                class_name = name;
        } else if (class_being_redefined) {
                class_name = get_class_name(jni_env, class_being_redefined);
        } else {
                fprintf(stderr, "[JNIHOOK] ClassFileLoadHook: no name or class_being_redefined, skipping\n");
                fflush(stderr);
                return;
        }

        fprintf(stderr, "[JNIHOOK] ClassFileLoadHook: resolved class_name=%s\n", class_name.c_str());
        fflush(stderr);

        // Don't do anything for unhooked classes
        if (g_hooks.find(class_name) == g_hooks.end() || g_hooks[class_name].size() == 0) {
                return;
        }

        rlog::logf("[JNIHOOK] ClassFileLoadHook: caching class '%s' len=%d", class_name.c_str(), (int)class_data_len);

        // Cache parsed ClassFile if it's not cached yet
        if (g_class_file_cache.find(class_name) == g_class_file_cache.end()) {
                auto cf = ClassFile::load(class_data);
                if (!cf) {
                        rlog::logf("[JNIHOOK] ClassFile::load FAILED for '%s'", class_name.c_str());
                        return;
                }

                rlog::logf("[JNIHOOK] ClassFile::load OK for '%s' (%zu methods)",
                        class_name.c_str(), cf->get_methods().size());

                g_class_file_cache[class_name] = std::move(cf);
        }

        return;
}

// Patches up a class with the current hooks (if any)
// and redefines it using JVMTI
jnihook_result_t
ReapplyClass(jclass clazz, std::string clazz_name)
{
        auto guard = lock_jnihook();

        fprintf(stderr, "[JNIHOOK] ReapplyClass: class=%s\n", clazz_name.c_str());
        fflush(stderr);

        jvmtiClassDefinition class_definition;

        if (g_class_file_cache.find(clazz_name) == g_class_file_cache.end()) {
                rlog::logf("[JNIHOOK] ReapplyClass: class '%s' not in cache!", clazz_name.c_str());
                return JNIHOOK_ERR_CLASS_FILE_CACHE;
        }

        auto cf = *g_class_file_cache[clazz_name];

        auto constant_pool = cf.get_constant_pool();
        fprintf(stderr, "[JNIHOOK] ReapplyClass: constant_pool size=%zu, methods=%zu\n",
                constant_pool.size(), cf.get_methods().size());
        fflush(stderr);

        // Patch class file
        // NOTE: The `methods` attribute only has the methods defined by the main class of this ClassFile
        //       Method references are not included here
        //       If the source file has more than one class, they are compiled as separate ClassFiles
        for (auto &method : cf.get_methods()) {
                auto name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                        cf.get_constant_pool_item(method.name_index).bytes.data()
                );

                auto descriptor_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                        cf.get_constant_pool_item(method.descriptor_index).bytes.data()
                );

                auto name = std::string(name_ci->bytes, &name_ci->bytes[name_ci->length]);
                auto descriptor = std::string(descriptor_ci->bytes, &descriptor_ci->bytes[descriptor_ci->length]);

                // Check if the current method is a method that should be hooked
                // TODO: Use hashmap for faster lookup
                bool should_hook = false;
                for (auto &hk_info : g_hooks[clazz_name]) {
                        auto &minfo = hk_info.method_info;
                        if (minfo.name == name && minfo.signature == descriptor) {
                                should_hook = true;
                                break;
                        }
                }
                if (!should_hook)
                        continue;

                // Set method to native
                method.access_flags |= ACC_NATIVE;

                // Remove "Code" attribute
                for (size_t i = 0; i < method.attributes.size(); ++i) {
                        auto attr = method.attributes[i];
                        auto attr_name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                                cf.get_constant_pool_item(attr.attribute_name_index).bytes.data()
                        );
                        auto attr_name = std::string(attr_name_ci->bytes, &attr_name_ci->bytes[attr_name_ci->length]);
                        if (attr_name == "Code") {
                                method.attributes.erase(method.attributes.begin() + i);
                                break;
                        }
                }
        }

        // Redefine class with modified ClassFile
        auto cf_bytes = cf.bytes();

        fprintf(stderr, "[JNIHOOK] ReapplyClass: redefining '%s' with %zu bytes\n",
                clazz_name.c_str(), cf_bytes.size());
        fflush(stderr);

        class_definition.klass = clazz;
        class_definition.class_byte_count = cf_bytes.size();
        class_definition.class_bytes = cf_bytes.data();
        jvmtiError redef_err = g_jnihook->jvmti->RedefineClasses(1, &class_definition);
        if (redef_err != JVMTI_ERROR_NONE) {
                rlog::logf("[JNIHOOK] RedefineClasses FAILED for %s error=%d", clazz_name.c_str(), (int)redef_err);
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        rlog::logf("[JNIHOOK] ReapplyClass OK for %s", clazz_name.c_str());

        return JNIHOOK_OK;
}

JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Init(JavaVM *jvm)
{
        auto guard = lock_jnihook();

        fprintf(stderr, "[JNIHOOK] JNIHook_Init called, jvm=%p\n", (void*)jvm);
        fflush(stderr);

        if (!jvm) {
                fprintf(stderr, "[JNIHOOK] JNIHook_Init: null jvm, aborting\n");
                fflush(stderr);
                return JNIHOOK_ERR_GET_JVMTI;
        }

        if (g_jnihook) {
                fprintf(stderr, "[JNIHOOK] JNIHook_Init: already initialized\n");
                return JNIHOOK_OK;
        }

        jvmtiEnv *jvmti;
        jvmtiCapabilities capabilities;
        jvmtiEventCallbacks callbacks = {};

        if (jvm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION_1_2) != JNI_OK) {
                fprintf(stderr, "[JNIHOOK] JNIHook_Init: GetEnv FAILED\n");
                fflush(stderr);
                return JNIHOOK_ERR_GET_JVMTI;
        }

        if (jvmti->GetPotentialCapabilities(&capabilities) != JVMTI_ERROR_NONE) {
                fprintf(stderr, "[JNIHOOK] JNIHook_Init: GetPotentialCapabilities FAILED\n");
                fflush(stderr);
                return JNIHOOK_ERR_ADD_JVMTI_CAPS;
        }

        capabilities.can_redefine_classes = 1;
        capabilities.can_redefine_any_class = 1;
        capabilities.can_retransform_classes = 1;
        capabilities.can_retransform_any_class = 1;
        capabilities.can_suspend = 1;

        if (jvmti->AddCapabilities(&capabilities) != JVMTI_ERROR_NONE) {
                fprintf(stderr, "[JNIHOOK] JNIHook_Init: AddCapabilities FAILED\n");
                fflush(stderr);
                return JNIHOOK_ERR_ADD_JVMTI_CAPS;
        }

        callbacks.ClassFileLoadHook = JNIHook_ClassFileLoadHook;
        if (jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks)) != JVMTI_ERROR_NONE) {
                fprintf(stderr, "[JNIHOOK] JNIHook_Init: SetEventCallbacks FAILED\n");
                fflush(stderr);
                return JNIHOOK_ERR_SETUP_CLASS_FILE_LOAD_HOOK;
        }

        g_jnihook = std::make_unique<jnihook_t>(jnihook_t { jvm, jvmti });
        fprintf(stderr, "[JNIHOOK] JNIHook_Init: OK\n");
        fflush(stderr);

        return JNIHOOK_OK;
}

JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Attach(jmethodID method, void *native_hook_method, jmethodID *original_method)
{
        // Held across RetransformClasses: the synchronous ClassFileLoadHook
        // callback re-locks the (recursive) mutex on this same thread, while
        // any other thread is serialized out of g_hooks/g_class_file_cache.
        auto guard = lock_jnihook();

        fprintf(stderr, "[JNIHOOK] JNIHook_Attach: method=%p native_hook=%p\n",
                (void*)method, native_hook_method);
        fflush(stderr);

        jclass clazz;
        std::string clazz_name;
        hook_info_t hook_info;
        jobject class_loader;
        JNIEnv *env;

        if (!g_jnihook || !g_jnihook->jvm || !g_jnihook->jvmti) {
                fprintf(stderr, "[JNIHOOK] JNIHook_Attach: g_jnihook not initialized\n");
                fflush(stderr);
                return JNIHOOK_ERR_GET_JNI;
        }

        if (g_jnihook->jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8)) {
                fprintf(stderr, "[JNIHOOK] JNIHook_Attach: GetEnv FAILED\n");
                fflush(stderr);
                return JNIHOOK_ERR_GET_JNI;
        }

        if (g_jnihook->jvmti->GetMethodDeclaringClass(method, &clazz) != JVMTI_ERROR_NONE) {
                rlog::logf("[JNIHOOK] GetMethodDeclaringClass FAILED");
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        clazz_name = get_class_name(env, clazz);
        if (clazz_name.length() == 0) {
                rlog::logf("[JNIHOOK] get_class_name FAILED");
                return JNIHOOK_ERR_JNI_OPERATION;
        }

        auto method_info = get_method_info(g_jnihook->jvmti, method);
        if (!method_info) {
                rlog::logf("[JNIHOOK] get_method_info FAILED");
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        rlog::logf("[JNIHOOK] class=%s method=%s sig=%s",
                clazz_name.c_str(), method_info->name.c_str(), method_info->signature.c_str());

        hook_info.method_info = *method_info;
        hook_info.native_hook_method = native_hook_method;

        // Force caching of the class being hooked
        if (g_class_file_cache.find(clazz_name) == g_class_file_cache.end()) {
                fprintf(stderr, "[JNIHOOK] JNIHook_Attach: class '%s' not cached, triggering RetransformClasses\n",
                        clazz_name.c_str());
                fflush(stderr);

                if (g_jnihook->jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL) != JVMTI_ERROR_NONE) {
                        fprintf(stderr, "[JNIHOOK] JNIHook_Attach: Enable ClassFileLoadHook FAILED\n");
                        fflush(stderr);
                        return JNIHOOK_ERR_SETUP_CLASS_FILE_LOAD_HOOK;
                }

                // Temporarily register hook in g_hooks so that `ClassFileLoadHook` can see it
                // Leaving it there could be a problem if this hook fails, it will still patch
                // the class when JNIHook_Attach is called again for that same class, but won't
                // register the native method, causing `java.lang.UnsatisfiedLinkError`.
                g_hooks[clazz_name].push_back(hook_info);
                auto result = g_jnihook->jvmti->RetransformClasses(1, &clazz);
                g_hooks[clazz_name].pop_back();

                rlog::logf("[JNIHOOK] RetransformClasses result=%d", (int)result);

                // NOTE: We disable the ClassFileLoadHook here because it breaks
                //       any `env->DefineClass()` calls. Also, it's not necessary
                //       to keep it active at all times, we just have to use it for caching
                //       uncached hooked classes.
                // TODO: Investigate why it breaks it (possibly NullPointerException in
                //       JNIHook_ClassFileLoadHook)
                if (g_jnihook->jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL) != JVMTI_ERROR_NONE) {
                        fprintf(stderr, "[JNIHOOK] JNIHook_Attach: Disable ClassFileLoadHook FAILED\n");
                        fflush(stderr);
                        return JNIHOOK_ERR_SETUP_CLASS_FILE_LOAD_HOOK;
                }

                if (result != JVMTI_ERROR_NONE) {
                        rlog::logf("[JNIHOOK] RetransformClasses failed with %d", (int)result);
                        return JNIHOOK_ERR_CLASS_FILE_CACHE;
                }

                if (g_class_file_cache.find(clazz_name) == g_class_file_cache.end()) {
                        rlog::logf("[JNIHOOK] class '%s' still not cached after retransform!",
                                clazz_name.c_str());
                        return JNIHOOK_ERR_CLASS_FILE_CACHE;
                }

                rlog::logf("[JNIHOOK] class '%s' cached OK", clazz_name.c_str());
        }

        // Make copy of the class prior to hooking it
        // (allows calling the original functions)
        if (g_original_classes.find(clazz_name) == g_original_classes.end()) {
                std::string class_copy_name = clazz_name + "_" + GenerateUuid();
                std::string class_shortname = class_copy_name.substr(class_copy_name.find_last_of('/') + 1);
                std::string class_copy_source_name = class_shortname + ".java";
                jclass class_copy;
                auto cf = *g_class_file_cache[clazz_name];

                // Patch source file name (Java will refuse to define the class otherwise)
                for (auto &attr : cf.get_attributes()) {
                        auto attr_name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                                cf.get_constant_pool_item(attr.attribute_name_index).bytes.data()
                        );
                        auto attr_name = std::string(attr_name_ci->bytes, &attr_name_ci->bytes[attr_name_ci->length]);
                        if (attr_name != "SourceFile")
                                continue;

                        u2 attr_index_be = ((attr.attribute_name_index >> 8) & 0xff) |
                                           ((attr.attribute_name_index & 0xff) << 8);
                        u2 source = *reinterpret_cast<u2 *>(attr.info.data());

                        // Some classes have 'SourceFile' attribute be equal to 'SourceFile',
                        // and not 'ClassName.java'. For those, we won't set a custom source.
                        if (source == attr_index_be)
                                break;

                        // Overwrite constant pool item
                        CONSTANT_Utf8_info ci;
                        cp_info sourcefile_cpi;
                        ci.tag = CONSTANT_Utf8;
                        ci.length = static_cast<u2>(class_copy_source_name.size());

                        sourcefile_cpi.bytes = std::vector<uint8_t>(sizeof(ci) + ci.length);
                        memcpy(sourcefile_cpi.bytes.data(), &ci, sizeof(ci));
                        memcpy(&sourcefile_cpi.bytes.data()[sizeof(ci)], class_copy_source_name.c_str(), ci.length);

                        cf.set_constant_pool_item_be(source, sourcefile_cpi);
                }

                // Patch class name (Java will refuse to define the class otherwise)
                for (auto &cpi : cf.get_constant_pool()) {
                        if (cpi.bytes[0] != CONSTANT_Class)
                                continue;

                        auto class_ci = reinterpret_cast<CONSTANT_Class_info *>(
                                cpi.bytes.data()
                        );

                        auto name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                                cf.get_constant_pool_item(class_ci->name_index).bytes.data()
                        );

                        auto name = std::string(name_ci->bytes, &name_ci->bytes[name_ci->length]);

                        if (name == clazz_name) {
                                // Overwrite constant pool item
                                CONSTANT_Utf8_info ci;
                                cp_info cpi;

                                ci.tag = CONSTANT_Utf8;
                                ci.length = static_cast<u2>(class_copy_name.size());

                                cpi.bytes = std::vector<uint8_t>(sizeof(ci) + ci.length);
                                memcpy(cpi.bytes.data(), &ci, sizeof(ci));
                                memcpy(&cpi.bytes.data()[sizeof(ci)], class_copy_name.c_str(), ci.length);

                                cf.set_constant_pool_item(class_ci->name_index, cpi);
                                // NO break: large classes (Screen, 110 methods)
                                // have multiple CONSTANT_Class entries pointing
                                // to the same Utf8 name. Patching only one leaves
                                // the copy inconsistent → DefineClass succeeds but
                                // the class is erroneous → GetMethodID fails.
                        }
                }

                // Patch NameAndType things that instance the current class
                // NOTE: This is an attempt to fix the following exception when
                //       trying to get the method ID after defining the class:
                //
                // Type 'OrigClass' (current frame, stack[0]) is not assignable to 'OrigClass_<UUID>'
                auto constant_pool = cf.get_constant_pool();
                for (auto &item : constant_pool) {
                        if (item.bytes[0] != CONSTANT_NameAndType)
                                continue;

                        auto nt_ci = reinterpret_cast<CONSTANT_NameAndType_info *>(item.bytes.data());
                        auto descriptor_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                                cf.get_constant_pool_item(nt_ci->descriptor_index).bytes.data()
                        );
                        auto descriptor = std::string(descriptor_ci->bytes, &descriptor_ci->bytes[descriptor_ci->length]);

                        std::string clazz_desc = std::string("L") + clazz_name + ";";
                        std::string clazz_copy_desc = std::string("L") + class_copy_name + ";";
                        if (auto index = descriptor.find(clazz_desc); index != descriptor.npos) {
                                // Overwrite constant pool item
                                CONSTANT_Utf8_info ci;
                                cp_info cpi;
                                std::string new_descriptor = descriptor.replace(index, clazz_desc.size(), clazz_copy_desc);

                                ci.tag = CONSTANT_Utf8;
                                ci.length = static_cast<u2>(new_descriptor.size());

                                cpi.bytes = std::vector<uint8_t>(sizeof(ci) + ci.length);
                                memcpy(cpi.bytes.data(), &ci, sizeof(ci));
                                memcpy(&cpi.bytes.data()[sizeof(ci)], new_descriptor.c_str(), ci.length);

                                cf.set_constant_pool_item(nt_ci->descriptor_index, cpi);
                        }
                }

                // Patch method descriptors
                // NOTE: Not every Type or return Type is referenced by a NameAndType
                //       So we have to check the method descriptors as well
                auto methods = cf.get_methods();
                for (auto& method : methods)
                {
                        auto descriptor_index = method.descriptor_index;

                        auto descriptor_ci = reinterpret_cast<CONSTANT_Utf8_info*>(
                                cf.get_constant_pool_item(descriptor_index).bytes.data()
                                );

                        auto descriptor = std::string(descriptor_ci->bytes, &descriptor_ci->bytes[descriptor_ci->length]);

                        std::string clazz_desc = std::string("L") + clazz_name + ";";
                        std::string clazz_copy_desc = std::string("L") + class_copy_name + ";";

                        for (size_t index; (index = descriptor.find(clazz_desc)) != descriptor.npos;)
                        {
                                CONSTANT_Utf8_info ci;
                                cp_info cpi;
                                std::string new_descriptor = descriptor.replace(index, clazz_desc.size(), clazz_copy_desc);

                                ci.tag = CONSTANT_Utf8;
                                ci.length = static_cast<u2>(new_descriptor.size());

                                cpi.bytes = std::vector<uint8_t>(sizeof(ci) + ci.length);
                                memcpy(cpi.bytes.data(), &ci, sizeof(ci));
                                memcpy(&cpi.bytes.data()[sizeof(ci)], new_descriptor.c_str(), ci.length);

                                cf.set_constant_pool_item(descriptor_index, cpi);
                        }
                }

                auto class_data = cf.bytes();

                if (g_jnihook->jvmti->GetClassLoader(clazz, &class_loader) != JVMTI_ERROR_NONE) {
                        rlog::logf("[JNIHOOK] GetClassLoader FAILED for %s", clazz_name.c_str());
                        return JNIHOOK_ERR_JVMTI_OPERATION;
                }

                class_copy = env->DefineClass(NULL, class_loader,
                                              reinterpret_cast<const jbyte *>(class_data.data()),
                                              class_data.size());

                if (!class_copy) {
                        jthrowable exc = env->ExceptionOccurred();
                        env->ExceptionClear();
                        rlog::logf("[JNIHOOK] DefineClass FAILED for %s (exc=%p)", clazz_name.c_str(), (void*)exc);
                        return JNIHOOK_ERR_JNI_OPERATION;
                }

                // DefineClass returns a LOCAL reference valid only until the
                // current native method returns. The class is needed later (in
                // JNIHook_Unhook on a different thread/frame), so promote it to
                // a global ref and drop the local one immediately.
                g_original_classes[clazz_name] = (jclass)env->NewGlobalRef(class_copy);
                env->DeleteLocalRef(class_copy);
                if (!g_original_classes[clazz_name])
                        return JNIHOOK_ERR_JNI_OPERATION;
        }

        // Verify that everything was cached correctly
        if (g_original_classes.find(clazz_name) == g_original_classes.end()) {
                return JNIHOOK_ERR_CLASS_FILE_CACHE;
        }

        // Get original method before applying hooks, because this is fallible
        if (original_method) {
                jclass orig_class = g_original_classes[clazz_name];
                jmethodID orig;

                if ((method_info->access_flags & ACC_STATIC) == ACC_STATIC) {
                        orig = env->GetStaticMethodID(orig_class, method_info->name.c_str(),
                                                      method_info->signature.c_str());
                } else {
                        orig = env->GetMethodID(orig_class, method_info->name.c_str(),
                                                method_info->signature.c_str());
                }

                if (!orig || env->ExceptionOccurred()) {
                        jthrowable exc = env->ExceptionOccurred();
                        env->ExceptionClear();
                        // Log the actual Java exception class+message so we
                        // can diagnose why the copied class is erroneous.
                        if (exc) {
                                jclass exc_cls = env->GetObjectClass(exc);
                                jmethodID get_cls_name = nullptr;
                                if (exc_cls) {
                                        jclass class_cls = env->FindClass("java/lang/Class");
                                        if (class_cls) {
                                                get_cls_name = env->GetMethodID(class_cls, "getName", "()Ljava/lang/String;");
                                                env->DeleteLocalRef(class_cls);
                                        }
                                }
                                jclass throwable_cls = env->FindClass("java/lang/Throwable");
                                jmethodID get_msg = nullptr;
                                if (throwable_cls) {
                                        get_msg = env->GetMethodID(throwable_cls, "getMessage", "()Ljava/lang/String;");
                                        env->DeleteLocalRef(throwable_cls);
                                }
                                const char* exc_cls_name = nullptr;
                                const char* exc_msg = nullptr;
                                jstring j_cls_name = nullptr;
                                jstring j_msg = nullptr;
                                if (exc_cls && get_cls_name) {
                                        j_cls_name = (jstring)env->CallObjectMethod(exc_cls, get_cls_name);
                                        if (!env->ExceptionCheck() && j_cls_name)
                                                exc_cls_name = env->GetStringUTFChars(j_cls_name, nullptr);
                                        else env->ExceptionClear();
                                }
                                if (exc && get_msg) {
                                        j_msg = (jstring)env->CallObjectMethod(exc, get_msg);
                                        if (!env->ExceptionCheck() && j_msg)
                                                exc_msg = env->GetStringUTFChars(j_msg, nullptr);
                                        else env->ExceptionClear();
                                }
                                rlog::logf("[JNIHOOK] GetMethodID on orig FAILED %s.%s%s exc=%s: %s",
                                        clazz_name.c_str(), method_info->name.c_str(),
                                        method_info->signature.c_str(),
                                        exc_cls_name ? exc_cls_name : "?",
                                        exc_msg ? exc_msg : "?");
                                if (exc_cls_name) env->ReleaseStringUTFChars(j_cls_name, exc_cls_name);
                                if (exc_msg) env->ReleaseStringUTFChars(j_msg, exc_msg);
                                if (j_cls_name) env->DeleteLocalRef(j_cls_name);
                                if (j_msg) env->DeleteLocalRef(j_msg);
                                if (exc_cls) env->DeleteLocalRef(exc_cls);
                        } else {
                                rlog::logf("[JNIHOOK] GetMethodID on orig FAILED %s.%s%s (no exception)",
                                        clazz_name.c_str(), method_info->name.c_str(),
                                        method_info->signature.c_str());
                        }
                        return JNIHOOK_ERR_JAVA_EXCEPTION;
                }

                *original_method = orig;
        }

        // Suspend other threads while the hook is being set up
        jthread curthread;
        jthread *threads;
        jint thread_count;

        env->PushLocalFrame(16);
        
        if (g_jnihook->jvmti->GetCurrentThread(&curthread) != JVMTI_ERROR_NONE)
                return JNIHOOK_ERR_JVMTI_OPERATION;

        if (g_jnihook->jvmti->GetAllThreads(&thread_count, &threads) != JVMTI_ERROR_NONE)
                return JNIHOOK_ERR_JVMTI_OPERATION;

        // TODO: Only suspend/resume threads that are actually active
        for (jint i = 0; i < thread_count; ++i) {
                if (env->IsSameObject(threads[i], curthread))
                        continue;

                g_jnihook->jvmti->SuspendThread(threads[i]);
        }

        // Apply current hooks
        jnihook_result_t ret;
        g_hooks[clazz_name].push_back(hook_info);
        if (ret = ReapplyClass(clazz, clazz_name); ret != JNIHOOK_OK) {
                g_hooks[clazz_name].pop_back();
                rlog::logf("[JNIHOOK] ReapplyClass FAILED for %s rc=%d", clazz_name.c_str(), (int)ret);
                goto RESUME_THREADS;
        }

        // Register native method for JVM lookup
        JNINativeMethod native_method;
        native_method.name = const_cast<char *>(method_info->name.c_str());
        native_method.signature = const_cast<char *>(method_info->signature.c_str());
        native_method.fnPtr = native_hook_method;

        if (env->RegisterNatives(clazz, &native_method, 1) < 0) {
                jthrowable exc = env->ExceptionOccurred();
                env->ExceptionClear();
                g_hooks[clazz_name].pop_back();
                ReapplyClass(clazz, clazz_name); // Attempt to restore class to previous state
                rlog::logf("[JNIHOOK] RegisterNatives FAILED for %s.%s%s (exc=%p)",
                        clazz_name.c_str(), method_info->name.c_str(),
                        method_info->signature.c_str(), (void*)exc);
                ret = JNIHOOK_ERR_JNI_OPERATION;
                goto RESUME_THREADS;
        }

        ret = JNIHOOK_OK;

RESUME_THREADS:
        // Resume other threads, hook already placed succesfully
        for (jint i = 0; i < thread_count; ++i) {
                if (env->IsSameObject(threads[i], curthread))
                        continue;

                g_jnihook->jvmti->ResumeThread(threads[i]);
        }

        g_jnihook->jvmti->Deallocate(reinterpret_cast<unsigned char *>(threads));
        env->PopLocalFrame(NULL);

        return ret;
}

JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Detach(jmethodID method)
{
        auto guard = lock_jnihook();

        JNIEnv *env;
        jclass clazz;
        std::string clazz_name;
        hook_info_t hook_info;

        if (!g_jnihook || !g_jnihook->jvm || !g_jnihook->jvmti) {
                return JNIHOOK_ERR_GET_JNI;
        }

        // The JVM may already be in teardown when this runs (game exiting).
        // GetEnv on a dying JavaVM can SIGSEGV, so bail out cleanly instead.
        if (!jvm_is_alive(g_jnihook->jvm)) {
                return JNIHOOK_ERR_GET_JNI;
        }

        if (g_jnihook->jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8)) {
                return JNIHOOK_ERR_GET_JNI;
        }

        if (g_jnihook->jvmti->GetMethodDeclaringClass(method, &clazz) != JVMTI_ERROR_NONE) {
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        clazz_name = get_class_name(env, clazz);
        if (clazz_name.length() == 0) {
                return JNIHOOK_ERR_JNI_OPERATION;
        }

        if (g_hooks.find(clazz_name) == g_hooks.end() || g_hooks[clazz_name].size() == 0) {
                return JNIHOOK_OK;
        }

        auto method_info = get_method_info(g_jnihook->jvmti, method);
        if (!method_info) {
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        for (size_t i = 0; i < g_hooks[clazz_name].size(); ++i) {
                auto &hook_info = g_hooks[clazz_name][i];
                if (hook_info.method_info.name != method_info->name ||
                    hook_info.method_info.signature != method_info->signature)
                        continue;

                g_hooks[clazz_name].erase(g_hooks[clazz_name].begin() + i);
        }

        return ReapplyClass(clazz, clazz_name);
}


JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Shutdown()
{
        auto guard = lock_jnihook();

        JNIEnv *env;
        jvmtiEventCallbacks callbacks = {};

        if (!g_jnihook || !g_jnihook->jvm || !g_jnihook->jvmti) {
                return JNIHOOK_OK;
        }

        // If the JVM is already tearing down (process exit), bail out: the
        // class redefinition loop below would touch a dying runtime.
        if (!jvm_is_alive(g_jnihook->jvm)) {
                return JNIHOOK_OK;
        }

        if (g_jnihook->jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8)) {
                return JNIHOOK_ERR_GET_JNI;
        }

        // GATE-ONLY shutdown: DO NOT ReapplyClass any hooked class here.
        // Class redefinition is what triggers the delta-JRE perfdata
        // guard-page SIGSEGV storm (recurring fault that raced the teardown
        // and killed the game). The module shutdowns already gated their native
        // hooks to pass through to the originals, so leaving the classes
        // redefined is harmless for the rest of the session. We only clear the
        // bookkeeping and remove the ClassFileLoadHook callback (so the JVM's
        // own shutdown never invokes our stale callback).
        g_hooks.clear();
        g_class_file_cache.clear();

        // Free the global refs of the original-class copies we defined. Without
        // this the copies would leak (and the dangling local-ref form was a UAF
        // when re-applying/unhooking from a different thread later).
        for (auto& kv : g_original_classes)
                if (kv.second)
                        env->DeleteGlobalRef(kv.second);
        g_original_classes.clear();

        g_jnihook->jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
        g_jnihook->jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));

        g_jnihook = nullptr;

        return JNIHOOK_OK;
}
