#include "classloader.h"
#include <flaway/flaway.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <vector>
#include <map>
#include <string>
#include "flaway/utils/no_log.h"

namespace sdk
{
	namespace classloader
	{
		static jobject classloader_obj = nullptr;
		static jmethodID findclass_md = nullptr;
		static bool fabric_detected = false;
		static std::atomic<bool> initialized{false};
		static std::mutex g_mutex;
		// Cache of resolved classes, held as GLOBAL refs. find_class returns a
		// Cache of resolved classes, held as GLOBAL refs. find_class returns a
		// fresh LOCAL ref to the cached global so callers can keep using
		// DeleteLocalRef() on the result. This avoids re-running reflection
		// (loadClass) every frame and the resulting JNI/local-ref churn.
		static std::map<std::string, jclass> g_cache;

		// Try to load a known Minecraft class via a candidate loader.
		// Returns true if the loader can actually resolve it.
		static bool test_loader(JNIEnv* env, jobject loader, const char* test_class_dotted)
		{
			if (!loader) return false;
			jclass loaderCls = env->GetObjectClass(loader);
			if (!loaderCls) return false;
			jmethodID loadMd = env->GetMethodID(loaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
			if (env->ExceptionCheck()) env->ExceptionClear();
			bool ok = false;
			if (loadMd)
			{
				jstring jname = env->NewStringUTF(test_class_dotted);
				if (jname)
				{
					jclass test = (jclass)env->CallObjectMethod(loader, loadMd, jname);
					if (test && !env->ExceptionCheck())
						ok = true;
					if (env->ExceptionCheck()) env->ExceptionClear();
					if (test) env->DeleteLocalRef(test);
					env->DeleteLocalRef(jname);
				}
			}
			env->DeleteLocalRef(loaderCls);
			return ok;
		}

		// Check whether a loader's class name matches a known game class loader
		// (KnotClassLoader / TransformingClassLoader / LaunchClassLoader)
		static bool is_known_loader_name(JNIEnv* env, jobject loader)
		{
			if (!loader) return false;
			jclass loaderCls = env->GetObjectClass(loader);
			if (!loaderCls) return false;
			jclass classCls = env->FindClass("java/lang/Class");
			if (!classCls) { env->DeleteLocalRef(loaderCls); return false; }
			jmethodID getNameMd = env->GetMethodID(classCls, "getName", "()Ljava/lang/String;");
			if (env->ExceptionCheck()) env->ExceptionClear();
			bool ok = false;
			if (getNameMd)
			{
				jstring nameStr = (jstring)env->CallObjectMethod(loaderCls, getNameMd);
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (nameStr)
				{
					const char* nameC = env->GetStringUTFChars(nameStr, nullptr);
					if (nameC)
					{
						// NOTE: do NOT match the bare "ClassLoader" substring —
						// that would accept AppClassLoader / PlatformClassLoader
						// etc. and short-circuit test_loader(), after which the
						// thread-scan fallback may adopt a non-game loader and
						// every find_class() silently returns null.
						if (strstr(nameC, "KnotClassLoader") ||
							strstr(nameC, "TransformingClassLoader") ||
							strstr(nameC, "LaunchClassLoader"))
						{
							ok = true;
						}
						env->ReleaseStringUTFChars(nameStr, nameC);
					}
					env->DeleteLocalRef(nameStr);
				}
			}
			env->DeleteLocalRef(classCls);
			env->DeleteLocalRef(loaderCls);
			return ok;
		}

		// A candidate loader is usable if either its class name is a known game
		// class loader, or it can actually load a known Minecraft class.
		static bool candidate_usable(JNIEnv* env, jobject loader, const char* test_class_dotted)
		{
			if (is_known_loader_name(env, loader)) return true;
			return test_loader(env, loader, test_class_dotted);
		}

		// Adopt a loader as our class loader (store global ref + resolve loadClass method)
		static bool adopt_loader(JNIEnv* env, jobject loader)
		{
			jclass loaderCls = env->GetObjectClass(loader);
			if (!loaderCls) return false;
			findclass_md = env->GetMethodID(loaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(loaderCls);
			if (!findclass_md) return false;
			if (classloader_obj) env->DeleteGlobalRef(classloader_obj);
			classloader_obj = env->NewGlobalRef(loader);
			return true;
		}

		// Find class loader by iterating through all threads
		static bool findClsLoaderByThreads(JNIEnv* env, const char* test_class_dotted)
		{
			jclass threadCls = env->FindClass("java/lang/Thread");
			if (!threadCls) return false;

			jmethodID allStackTracesMd = env->GetStaticMethodID(threadCls, "getAllStackTraces", "()Ljava/util/Map;");
			if (!allStackTracesMd)
			{
				env->DeleteLocalRef(threadCls);
				return false;
			}
			jobject threadMap = env->CallStaticObjectMethod(threadCls, allStackTracesMd);
			if (!threadMap)
			{
				env->DeleteLocalRef(threadCls);
				return false;
			}

			jclass mapCls = env->FindClass("java/util/Map");
			if (!mapCls) { env->DeleteLocalRef(threadCls); env->DeleteLocalRef(threadMap); return false; }
			jmethodID entrySetMd = env->GetMethodID(mapCls, "entrySet", "()Ljava/util/Set;");
			if (!entrySetMd) { env->DeleteLocalRef(mapCls); env->DeleteLocalRef(threadCls); env->DeleteLocalRef(threadMap); return false; }
			jobject entrySet = env->CallObjectMethod(threadMap, entrySetMd);
			if (!entrySet) { env->DeleteLocalRef(mapCls); env->DeleteLocalRef(threadCls); env->DeleteLocalRef(threadMap); return false; }
			jclass setCls = env->FindClass("java/util/Set");
			if (!setCls) { env->DeleteLocalRef(entrySet); env->DeleteLocalRef(mapCls); env->DeleteLocalRef(threadCls); env->DeleteLocalRef(threadMap); return false; }
			jmethodID iteratorMd = env->GetMethodID(setCls, "iterator", "()Ljava/util/Iterator;");
			if (!iteratorMd) { env->DeleteLocalRef(setCls); env->DeleteLocalRef(entrySet); env->DeleteLocalRef(mapCls); env->DeleteLocalRef(threadCls); env->DeleteLocalRef(threadMap); return false; }
			jobject it = env->CallObjectMethod(entrySet, iteratorMd);
			if (!it) { env->DeleteLocalRef(setCls); env->DeleteLocalRef(entrySet); env->DeleteLocalRef(mapCls); env->DeleteLocalRef(threadCls); env->DeleteLocalRef(threadMap); return false; }
			jclass itCls = env->FindClass("java/util/Iterator");
			if (!itCls) { env->DeleteLocalRef(it); env->DeleteLocalRef(setCls); env->DeleteLocalRef(entrySet); env->DeleteLocalRef(mapCls); env->DeleteLocalRef(threadCls); env->DeleteLocalRef(threadMap); return false; }
			jmethodID hasNextMd = env->GetMethodID(itCls, "hasNext", "()Z");
			if (env->ExceptionCheck()) env->ExceptionClear();
			jmethodID nextMd = env->GetMethodID(itCls, "next", "()Ljava/lang/Object;");
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (!hasNextMd || !nextMd) {
				env->DeleteLocalRef(itCls); env->DeleteLocalRef(it);
				env->DeleteLocalRef(setCls); env->DeleteLocalRef(entrySet);
				env->DeleteLocalRef(mapCls); env->DeleteLocalRef(threadCls);
				env->DeleteLocalRef(threadMap); return false;
			}

			jclass entryCls = env->FindClass("java/util/Map$Entry");
			if (!entryCls) { env->DeleteLocalRef(itCls); env->DeleteLocalRef(it); env->DeleteLocalRef(setCls); env->DeleteLocalRef(entrySet); env->DeleteLocalRef(mapCls); env->DeleteLocalRef(threadCls); env->DeleteLocalRef(threadMap); return false; }
			jmethodID getKeyMd = env->GetMethodID(entryCls, "getKey", "()Ljava/lang/Object;");
			if (env->ExceptionCheck()) env->ExceptionClear();
			jmethodID getContextMd = env->GetMethodID(threadCls, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
			if (env->ExceptionCheck()) env->ExceptionClear();

			if (!getKeyMd || !getContextMd) {
				env->DeleteLocalRef(entryCls);
				env->DeleteLocalRef(itCls); env->DeleteLocalRef(it);
				env->DeleteLocalRef(setCls); env->DeleteLocalRef(entrySet);
				env->DeleteLocalRef(mapCls); env->DeleteLocalRef(threadCls);
				env->DeleteLocalRef(threadMap);
				return false;
			}

			std::vector<jobject> candidates;

			while (env->CallBooleanMethod(it, hasNextMd))
			{
				if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
				jobject entry = env->CallObjectMethod(it, nextMd);
				if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
				if (!entry) break;
				jobject threadObj = env->CallObjectMethod(entry, getKeyMd);
				if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(entry); break; }
				jobject loader = threadObj ? env->CallObjectMethod(threadObj, getContextMd) : nullptr;
				if (env->ExceptionCheck()) { env->ExceptionClear(); loader = nullptr; }
				if (loader)
					candidates.push_back(env->NewGlobalRef(loader));
				if (entry) env->DeleteLocalRef(entry);
				if (threadObj) env->DeleteLocalRef(threadObj);
				if (loader) env->DeleteLocalRef(loader);
			}

			env->DeleteLocalRef(threadCls);
			env->DeleteLocalRef(threadMap);
			env->DeleteLocalRef(mapCls);
			env->DeleteLocalRef(entrySet);
			env->DeleteLocalRef(setCls);
			env->DeleteLocalRef(it);
			env->DeleteLocalRef(itCls);
			env->DeleteLocalRef(entryCls);

			if (env->ExceptionCheck()) env->ExceptionClear();

		bool found = false;
		for (jobject loader : candidates)
		{
			if (candidate_usable(env, loader, test_class_dotted))
			{
				found = adopt_loader(env, loader);
				if (found) break;
			}
		}
		for (jobject loader : candidates)
			env->DeleteGlobalRef(loader);

		return found;
	}

	// Resolve a class via the game class loader or FindClass. The result is
	// cached as a global ref and a NEW local ref is returned each call.
	static jclass resolve_class(JNIEnv* env, const char* class_name)
	{
		if (classloader_obj && findclass_md)
		{
			std::string class_name_format(class_name);
			std::replace(class_name_format.begin(), class_name_format.end(), '/', '.');

			jstring jname = env->NewStringUTF(class_name_format.c_str());
			if (jname)
			{
				jclass cls = reinterpret_cast<jclass>(env->CallObjectMethod(classloader_obj, findclass_md, jname));
				env->DeleteLocalRef(jname);
				if (env->ExceptionCheck())
					env->ExceptionClear();
				if (cls)
					return cls; // local ref
			}
		}
		jclass cls = env->FindClass(class_name);
		if (env->ExceptionCheck())
			env->ExceptionClear();
		return cls; // local ref (may be null)
	}

		// Find class loader from the current thread's context class loader
		static bool findClsLoaderByCurrentThread(JNIEnv* env, const char* test_class_dotted)
		{
			jclass threadCls = env->FindClass("java/lang/Thread");
			if (!threadCls) return false;
			jmethodID currentThreadMd = env->GetStaticMethodID(threadCls, "currentThread", "()Ljava/lang/Thread;");
			if (!currentThreadMd)
			{
				env->DeleteLocalRef(threadCls);
				return false;
			}
			jobject currentThread = env->CallStaticObjectMethod(threadCls, currentThreadMd);
			if (env->ExceptionCheck()) env->ExceptionClear();
			jmethodID getContextMd = env->GetMethodID(threadCls, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
			if (env->ExceptionCheck()) env->ExceptionClear();
			bool found = false;
			if (currentThread && getContextMd)
			{
				jobject loader = env->CallObjectMethod(currentThread, getContextMd);
				if (loader)
				{
					if (candidate_usable(env, loader, test_class_dotted))
						found = adopt_loader(env, loader);
					env->DeleteLocalRef(loader);
				}
			}
			env->DeleteLocalRef(currentThread);
			env->DeleteLocalRef(threadCls);
			return found;
		}

		bool init(JNIEnv* env)
		{
			fprintf(stderr, "[CLLOADER] init called\n"); fflush(stderr);
			if (!env)
			{
				fprintf(stderr, "[CLLOADER] null env!\n"); fflush(stderr);
				return false;
			}

			{
				std::lock_guard<std::mutex> lock(g_mutex);
				if (initialized)
				{
					fprintf(stderr, "[CLLOADER] already initialized\n"); fflush(stderr);
					return true;
				}

				// A known Minecraft class to verify that a loader can actually resolve game classes
				const char* test_class = "net.minecraft.class_310";

				// Preferred: current (render) thread's context class loader (KnotClassLoader on Fabric)
				if (findClsLoaderByCurrentThread(env, test_class))
				{
					fprintf(stderr, "[CLLOADER] found loader via current thread\n"); fflush(stderr);
				}
				// Fallback: search all threads' context class loaders
				else if (findClsLoaderByThreads(env, test_class))
				{
					fprintf(stderr, "[CLLOADER] found loader via thread scan\n"); fflush(stderr);
				}
				else
				{
					fprintf(stderr, "[CLLOADER] WARNING: no usable class loader found, will fall back to FindClass\n"); fflush(stderr);
				}

				// Detect Fabric by inspecting the loader's class name
				if (classloader_obj && env)
				{
					jclass loaderCls = env->GetObjectClass(classloader_obj);
					if (loaderCls)
					{
						jclass classCls = env->FindClass("java/lang/Class");
						if (env->ExceptionCheck()) env->ExceptionClear();
						if (classCls)
						{
							jmethodID getNameMd = env->GetMethodID(classCls, "getName", "()Ljava/lang/String;");
							if (env->ExceptionCheck()) env->ExceptionClear();
							if (getNameMd)
							{
								jstring nameStr = (jstring)env->CallObjectMethod(loaderCls, getNameMd);
								if (env->ExceptionCheck()) env->ExceptionClear();
								if (nameStr)
								{
									const char* nameC = env->GetStringUTFChars(nameStr, nullptr);
									if (nameC)
									{
										fabric_detected = (strstr(nameC, "KnotClassLoader") != nullptr);
										env->ReleaseStringUTFChars(nameStr, nameC);
									}
									env->DeleteLocalRef(nameStr);
								}
							}
							env->DeleteLocalRef(classCls);
						}
						env->DeleteLocalRef(loaderCls);
					}
				}

				initialized = true;
			}
			return true;
		}

		bool is_fabric()
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			return fabric_detected;
		}

 jclass find_class(JNIEnv* env, const char* class_name)
		{
			if (!env || !class_name)
				return nullptr;

			// Ensure we're initialized (atomic check avoids data race)
			if (!initialized.load(std::memory_order_acquire))
				init(env);

			std::lock_guard<std::mutex> lock(g_mutex);

			std::string key(class_name);
			auto it = g_cache.find(key);
			if (it != g_cache.end() && it->second)
			{
				// Return a fresh local ref wrapping the cached global so
				// callers can safely DeleteLocalRef() the result.
				jclass local = reinterpret_cast<jclass>(env->NewLocalRef(it->second));
				return local;
			}

			jclass cls = resolve_class(env, class_name);
			if (!cls)
				return nullptr;

			// Promote to a cached global ref.
			jclass global = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
			env->DeleteLocalRef(cls);
			if (!global)
				return nullptr;

			g_cache[key] = global;
			return reinterpret_cast<jclass>(env->NewLocalRef(global));
		}

		// Cleanup function (can be called during shutdown)
		void cleanup(JNIEnv* env)
		{
			static std::atomic<bool> cleaning = false;
			if (cleaning.exchange(true)) return;

			std::lock_guard<std::mutex> lock(g_mutex);
			for (auto& kv : g_cache)
			{
				if (kv.second)
				{
					if (env && env->functions)
						env->DeleteGlobalRef(kv.second);
					kv.second = nullptr;
				}
			}
			g_cache.clear();
			if (classloader_obj)
			{
				if (env && env->functions)
					env->DeleteGlobalRef(classloader_obj);
				classloader_obj = nullptr;
			}
			findclass_md = nullptr;
			fabric_detected = false;
			initialized = false;
			cleaning = false;
		}
	}
}
