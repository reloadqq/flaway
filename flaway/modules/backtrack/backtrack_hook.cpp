#include "backtrack_hook.h"
#include "../../flaway.h"
#include "../../utils/logger.h"
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <utils/jnihook-master/include/jnihook.h>
#include <cstdio>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <sstream>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "flaway/utils/no_log.h"

struct DelayedPacket
{
	jobject packet;
	jobject context;
	jobject handler;
	ULONGLONG release_time_ms;
};

static std::queue<DelayedPacket> g_packet_queue;
static std::mutex g_packet_queue_mutex;

// Guards g_hook_ready / ORIG_channelRead0 / g_channel_handler_class, which are
// shared between the Netty IO thread (hkChannelRead0), the packet processor
// thread, and the render thread (shutdown/set_delay_*). Recursive so the hook
// can safely re-enter on the same thread. Without this, shutdown() could delete
// the channel-handler global ref while hkChannelRead0 is still executing and
// then call a freed jclass -> SIGSEGV.
static std::recursive_mutex g_hook_mutex;
// Written/read from different threads; keep them lock-free atomics so there is
// no data race (plain bool/int would be UB).
static std::atomic<bool> g_delay_enabled{false};
static std::atomic<int> g_delay_ms{200};
static jmethodID ORIG_channelRead0 = nullptr;
static jmethodID g_channelRead_methodID = nullptr;
static jclass g_channel_handler_class = nullptr;
static bool jnihook_initialized = false;

// Set while the packet processor thread is re-delivering packets through the
// pipeline. hkChannelRead0 checks it and passes through (instead of re-queueing)
// so released packets reach the game handler exactly once. Without this the
// release's ORIG call fires the next handler's hooked channelRead0, which sees
// backtrack still enabled and re-queues the packet -> infinite queue ping-pong
// and unbounded re-entrant JNI pipeline walks (stack overflow / stale refs).
static std::atomic<bool> g_releasing_packets{false};

// Packet processor runs on its own OS thread with a 64MB stack (see init()).
static pthread_t g_packet_thread = 0;
static bool g_packet_thread_started = false;
static std::atomic<bool> g_should_stop_processor{false};
static std::atomic<bool> g_hook_ready{false};

// Cached packet classes for fast instanceof checks
static jclass g_entity_position_packet_class = nullptr;
static jclass g_entity_move_packet_class = nullptr;
static jclass g_entity_teleport_packet_class = nullptr;
static jclass g_entity_s2c_packet_class = nullptr;

// Check if packet is an entity position update packet (fast version using cached classes)
static bool is_entity_position_packet(JNIEnv *env, jobject packet)
{
	if (!env || !packet || !g_hook_ready) return false;
	
	// Fast instanceof checks using cached classes
	// Try EntityPositionS2CPacket (most common)
	if (g_entity_position_packet_class)
	{
		if (env->IsInstanceOf(packet, g_entity_position_packet_class))
		{
			return true;
		}
	}
	
	// Try EntityMoveS2CPacket (if cached)
	if (g_entity_move_packet_class)
	{
		if (env->IsInstanceOf(packet, g_entity_move_packet_class))
		{
			return true;
		}
	}
	
	// Try EntityTeleportS2CPacket (if cached)
	if (g_entity_teleport_packet_class)
	{
		if (env->IsInstanceOf(packet, g_entity_teleport_packet_class))
		{
			return true;
		}
	}
	
	// Fallback: Check if it's an EntityS2CPacket (parent class)
	// Only use this if we have at least one specific mapping
	if (g_entity_s2c_packet_class && g_entity_position_packet_class)
	{
		if (env->IsInstanceOf(packet, g_entity_s2c_packet_class))
		{
			return true;
		}
	}
	
	return false;
}

// Hook function for channelRead0
void hkChannelRead0(JNIEnv *env, jobject thiz, jobject ctx, jobject msg)
{
	// CRITICAL: called for EVERY packet; must be FAST and SAFE.
	// Delayed packets are released by the packet processor thread.
	// The hook mutex serializes us against shutdown() so the render thread
	// cannot delete the channel-handler global ref / null ORIG while we are
	// mid-call. Uncontended in normal operation.
	std::lock_guard<std::recursive_mutex> lock(g_hook_mutex);

	// Safety checks - if anything is invalid, just pass through immediately
	if (!env || !thiz || !ctx || !msg || !ORIG_channelRead0 || !g_channel_handler_class || !g_hook_ready)
	{
		// If we have the original method, try to call it anyway
		if (ORIG_channelRead0 && thiz && ctx && g_channel_handler_class && env)
		{
			try
			{
				env->CallNonvirtualVoidMethod(thiz, g_channel_handler_class, ORIG_channelRead0, ctx, msg);
				if (env->ExceptionCheck())
				{
					env->ExceptionClear();
				}
			}
			catch (...)
			{
				// Silent catch - don't log in hook (too frequent)
			}
		}
		return;
	}

	// While the packet processor thread is re-delivering a batch through the
	// pipeline, never delay/re-queue: the packet was already delayed once and
	// must reach the game handler exactly once. Prevents the queue ping-pong.
	if (g_releasing_packets.load(std::memory_order_acquire))
	{
		try
		{
			env->CallNonvirtualVoidMethod(thiz, g_channel_handler_class, ORIG_channelRead0, ctx, msg);
			if (env->ExceptionCheck())
			{
				env->ExceptionClear();
			}
		}
		catch (...)
		{
		}
		return;
	}

	// Fast path: if backtrack is disabled, pass through immediately
	if (!g_delay_enabled || g_delay_ms <= 0 || g_delay_ms > 10000)
	{
		try
		{
			env->CallNonvirtualVoidMethod(thiz, g_channel_handler_class, ORIG_channelRead0, ctx, msg);
			if (env->ExceptionCheck())
			{
				env->ExceptionClear();
			}
		}
		catch (...)
		{
			// Silent catch - don't crash
		}
		return;
	}

	// Check if this is an entity position packet
	bool should_delay = false;
	try
	{
		should_delay = is_entity_position_packet(env, msg);
		if (env->ExceptionCheck())
		{
			env->ExceptionClear();
			should_delay = false; // On error, don't delay
		}
	}
	catch (...)
	{
		should_delay = false; // On exception, don't delay
		// Silent catch - don't log in hook
	}
	
	if (should_delay)
	{
		try
		{
			// Store packet for delayed processing
			ULONGLONG current_time = GetTickCount64();
			ULONGLONG release_time = current_time + g_delay_ms;
			
			// Create global references
			jobject global_packet = nullptr;
			jobject global_ctx = nullptr;
			jobject global_handler = nullptr;
			
			global_packet = env->NewGlobalRef(msg);
			if (env->ExceptionCheck())
			{
				env->ExceptionClear();
				global_packet = nullptr;
			}
			
			if (global_packet)
			{
				if (ctx)
				{
					global_ctx = env->NewGlobalRef(ctx);
					if (env->ExceptionCheck())
					{
						env->ExceptionClear();
						// Continue without ctx
					}
				}
				
				if (thiz)
				{
					global_handler = env->NewGlobalRef(thiz);
					if (env->ExceptionCheck())
					{
						env->ExceptionClear();
						// Clean up and fail
						if (global_packet)
						{
							env->DeleteGlobalRef(global_packet);
							global_packet = nullptr;
						}
						if (global_ctx)
						{
							env->DeleteGlobalRef(global_ctx);
							global_ctx = nullptr;
						}
					}
				}
			}
			
			// Need all three global refs to delay — without context we can't
			// call channelRead0 later, so pass through immediately instead.
			if (global_packet && global_ctx && global_handler)
			{
				// Limit queue size to prevent memory issues
				{
					std::lock_guard<std::mutex> lock(g_packet_queue_mutex);
					if (g_packet_queue.size() > 1000) // Max 1000 queued packets
					{
						// Queue too large, don't delay this packet
						// Clean up global refs
						if (global_packet) env->DeleteGlobalRef(global_packet);
						if (global_ctx) env->DeleteGlobalRef(global_ctx);
						if (global_handler) env->DeleteGlobalRef(global_handler);
						
						// Pass through immediately (fall through to end of function)
						global_packet = nullptr; // Mark as not queued
					}
					
					if (global_packet)
					{
						DelayedPacket delayed;
						delayed.packet = global_packet;
						delayed.context = global_ctx;
						delayed.handler = global_handler;
						delayed.release_time_ms = release_time;
						
						g_packet_queue.push(delayed);
						
						// Don't call original immediately - packet will be released later
						return;
					}
				}
			}
			else
			{
				// Failed to create global refs, clean up what we have
				if (global_packet) env->DeleteGlobalRef(global_packet);
				if (global_ctx) env->DeleteGlobalRef(global_ctx);
				if (global_handler) env->DeleteGlobalRef(global_handler);
			}
		}
		catch (...)
		{
			// Silent catch - if anything fails, just pass through
		}
	}
	
	// Not an entity position packet or failed to create global ref - pass through immediately
	try
	{
		env->CallNonvirtualVoidMethod(thiz, g_channel_handler_class, ORIG_channelRead0, ctx, msg);
		if (env->ExceptionCheck())
		{
			env->ExceptionClear();
		}
	}
	catch (...)
	{
		// Silent catch - don't crash
	}
}

// Packet processor thread function.
// Runs on its own (attached) thread and releases due packets
// by calling the original Netty channelRead0.
//
// FIX (was crashing): this is a long-lived attahed thread.
// Every JNI call (CallNonvirtualVoidMethod and the Netty
// allocations it triggers) creates LOCAL refs that are only
// freed when a local frame is popped. This thread never
// pushed/popped a frame, so local refs piled up every
// iteration until the local-ref table overflowed -> the JVM
// corrupted JNI handles (heap pointers landed in saved
// stack return addresses) -> native SIGSEGV.
// Wrapping each release in PushLocalFrame/PopLocalFrame
// bounds that. We also re-check g_channel_handler_class and
// ORIG_channelRead0 defensively.
static void packet_processor_thread()
{
	logger::log_debug("[BacktrackHook] Packet processor thread starting");

	JavaVM* jvm = flaway::instance ? flaway::instance->get_java_vm() : nullptr;
	if (!jvm)
	{
		logger::log_error("[BacktrackHook] Packet processor: No JVM");
		return;
	}

	JNIEnv* thread_env = nullptr;
	jint attach_result = jvm->AttachCurrentThread(reinterpret_cast<void**>(&thread_env), nullptr);
	if (attach_result != JNI_OK || !thread_env)
	{
		logger::log_error("[BacktrackHook] Packet processor: Failed to attach thread");
		return;
	}

	logger::log_debug("[BacktrackHook] Packet processor thread attached successfully");
	fprintf(stderr, "[DEBUG] [BacktrackHook] Packet processor tid=%ld\n", (long)syscall(SYS_gettid));
	fflush(stderr);

	while (!g_should_stop_processor)
	{

		try
		{
			ULONGLONG current_time = GetTickCount64();
			std::vector<DelayedPacket> packets_to_release;

			{
				std::lock_guard<std::mutex> lock(g_packet_queue_mutex);
				// When backtrack is disabled, flush the whole queue immediately
				// so no packet is stranded (previously the queue was only
				// drained on full shutdown, so toggling the module off kept
				// stale packets indefinitely and dumped the entire backlog at
				// once on re-enable, causing rubber-banding).
				if (!g_delay_enabled.load())
				{
					while (!g_packet_queue.empty())
					{
						packets_to_release.push_back(g_packet_queue.front());
						g_packet_queue.pop();
					}
				}
				else
				{
					while (!g_packet_queue.empty())
					{
						DelayedPacket& packet = g_packet_queue.front();
						if (current_time >= packet.release_time_ms)
						{
							packets_to_release.push_back(packet);
							g_packet_queue.pop();
						}
						else
						{
							break;
						}
					}
				}
			}

			// Release inside a bounded local frame so Netty/JNI local
			// refs cannot accumulate on this long-lived thread.
			g_releasing_packets.store(true, std::memory_order_release);
			{
				std::lock_guard<std::recursive_mutex> hlock(g_hook_mutex);
				if (!packets_to_release.empty() && thread_env &&
					ORIG_channelRead0 && g_channel_handler_class)
				{
					if (thread_env->PushLocalFrame(64) == 0)
					{
						for (auto& packet : packets_to_release)
						{
							try
							{
								if (packet.packet && packet.handler && packet.context &&
									ORIG_channelRead0 && g_channel_handler_class)
								{
									thread_env->CallNonvirtualVoidMethod(
										packet.handler, g_channel_handler_class,
										ORIG_channelRead0, packet.context, packet.packet);
									if (thread_env->ExceptionCheck())
										thread_env->ExceptionClear();
								}
							}
							catch (...) {}
						}
						thread_env->PopLocalFrame(nullptr);
					}
				}

				// Free the global refs (these live outside the local frame).
				for (auto& packet : packets_to_release)
				{
					if (packet.packet) thread_env->DeleteGlobalRef(packet.packet);
					if (packet.context) thread_env->DeleteGlobalRef(packet.context);
					if (packet.handler) thread_env->DeleteGlobalRef(packet.handler);
				}
			}
			g_releasing_packets.store(false, std::memory_order_release);

			if (packets_to_release.empty())
				Sleep(5);
		}
		catch (...)
		{
			Sleep(5);
		}
	}

	logger::log_debug("[BacktrackHook] Packet processor thread stopping");
	try { jvm->DetachCurrentThread(); } catch (...) {}
}

static void* packet_processor_trampoline(void*)
{
	packet_processor_thread();
	return nullptr;
}

bool flaway::modules::backtrack_hook::init()
{
	logger::log_debug("[BacktrackHook] init() called");
	
	if (g_channel_handler_class != nullptr)
	{
		// RE-INJECT after gate-only unhook: the channelRead0 native is still
		// bound and passing through, and the packet class refs are still valid
		// (gate-only teardown keeps them). Just re-arm the gate + packet
		// processor thread so the delay pipeline works again — NO JVMTI
		// redefinition (that is what triggered the delta-JRE crash storm).
		logger::log_debug("[BacktrackHook] Already initialized - re-arming after re-inject");
		{
			std::lock_guard<std::recursive_mutex> lock(g_hook_mutex);
			g_should_stop_processor = false;
			g_hook_ready = true;
		}
		if (!g_packet_thread_started)
		{
			logger::log_debug("[BacktrackHook] Starting packet processor thread (re-arm)");
			pthread_attr_t attr;
			pthread_attr_init(&attr);
			pthread_attr_setstacksize(&attr, 64 * 1024 * 1024);
			int rc = pthread_create(&g_packet_thread, &attr, packet_processor_trampoline, nullptr);
			pthread_attr_destroy(&attr);
			if (rc == 0)
			{
				g_packet_thread_started = true;
				logger::log_debug("[BacktrackHook] Packet processor thread restarted");
			}
			else
			{
				g_hook_ready = false;
				logger::log_error("[BacktrackHook] Failed to restart packet processor thread");
			}
		}
		return true;
	}

	if (!flaway::instance) { logger::log_error("[BacktrackHook] init() failed: No instance"); return false; }
	auto env = flaway::instance->get_env();
	auto jvm = flaway::instance->get_java_vm();
	if (!env || !jvm) 
	{
		logger::log_error("[BacktrackHook] init() failed: No env or jvm");
		return false;
	}
	
	logger::log_debug("[BacktrackHook] Got env and jvm");
	
	// Ensure mappings are loaded
	if (!sdk::mappings::channel_inbound_handler_adapter_class_sig ||
	    !sdk::mappings::channel_read0_name ||
	    !sdk::mappings::channel_read0_sig)
	{
		logger::log_error("[BacktrackHook] init() failed: Mappings not loaded");
		return false;
	}
	
	logger::log_debug("[BacktrackHook] Mappings loaded");

	g_delay_enabled = false;
	g_delay_ms = 200;
	ORIG_channelRead0 = nullptr;

	// Use refcounted JNIHook initialization shared with reach_hook (and others).
	// If another module already initialized it, result 3 (ALREADY_INIT) is fine.
	if (!jnihook_initialized)
	{
		logger::log_debug("[BacktrackHook] Initializing JNIHook");
		logger::log_rss("backtrack-before-jnihook");
		jnihook_result_t result = JNIHook_Init(jvm);
		logger::log_rss("backtrack-after-jnihook");
		
		if (result == JNIHOOK_OK)
		{
			jnihook_initialized = true;
			logger::log_debug("[BacktrackHook] JNIHook initialized successfully");
		}
		else
		{
			std::stringstream ss;
			ss << "[BacktrackHook] JNIHook_Init failed with result=" << result;
			logger::log_error(ss.str());
			return false;
		}
	}

	// Find ChannelInboundHandlerAdapter class
	// Note: This is a Netty class (not obfuscated), so we can use it directly
	logger::log_debug("[BacktrackHook] Finding ChannelInboundHandlerAdapter class");
	logger::log_rss("backtrack-before-find-class");
	jclass channel_handler_class = sdk::classloader::find_class(env, sdk::mappings::channel_inbound_handler_adapter_class_sig);
	logger::log_rss("backtrack-after-find-class");
	if (!channel_handler_class)
	{
		// Try alternative: use JNI FindClass directly since Netty classes are in system classloader
		logger::log_debug("[BacktrackHook] ClassLoader failed, trying FindClass directly");
		channel_handler_class = env->FindClass("io/netty/channel/ChannelInboundHandlerAdapter");
		if (env->ExceptionCheck())
		{
			env->ExceptionClear();
			channel_handler_class = nullptr;
		}
	}
	
	if (!channel_handler_class)
	{
		logger::log_error("[BacktrackHook] Failed to find ChannelInboundHandlerAdapter class (both methods failed)");
		return false;
	}
	logger::log_debug("[BacktrackHook] Found ChannelInboundHandlerAdapter class");

	// Find channelRead method (public method that calls channelRead0)
	// channelRead0 is abstract/protected and not accessible, but channelRead is public
	logger::log_debug("[BacktrackHook] Finding channelRead method (public wrapper)");
	
	// channelRead signature: (Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;)V
	const char* channel_read_name = "channelRead";
	const char* channel_read_sig = "(Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;)V";
	
	jmethodID method_id = env->GetMethodID(channel_handler_class, channel_read_name, channel_read_sig);
	
	if (env->ExceptionCheck())
	{
		env->ExceptionDescribe();
		env->ExceptionClear();
		logger::log_error("[BacktrackHook] Exception getting channelRead method");
	}
	
	if (!method_id)
	{
		logger::log_error("[BacktrackHook] Failed to find channelRead method - GetMethodID returned NULL");
		env->DeleteLocalRef(channel_handler_class);
		return false;
	}
	logger::log_debug("[BacktrackHook] Found channelRead method");

	logger::log_debug("[BacktrackHook] Attaching hook to channelRead");
	g_channelRead_methodID = method_id;
	jnihook_result_t result = JNIHook_Attach(method_id, reinterpret_cast<void*>(hkChannelRead0), &ORIG_channelRead0);
	if (result != JNIHOOK_OK)
	{
		std::stringstream ss;
		ss << "[BacktrackHook] JNIHook_Attach failed with result=" << result;
		logger::log_error(ss.str());
		env->DeleteLocalRef(channel_handler_class);
		return false;
	}
	logger::log_debug("[BacktrackHook] Hook attached successfully");

	g_channel_handler_class = reinterpret_cast<jclass>(env->NewGlobalRef(channel_handler_class));
	env->DeleteLocalRef(channel_handler_class);
	
	if (!g_channel_handler_class)
	{
		return false;
	}
	
	// Cache packet classes for fast instanceof checks
	logger::log_debug("[BacktrackHook] Caching packet classes");
	
	// EntityPositionS2CPacket (most common)
	if (sdk::mappings::entity_position_s2c_packet_class_sig)
	{
		jclass cls = sdk::classloader::find_class(env, sdk::mappings::entity_position_s2c_packet_class_sig);
		if (cls)
		{
			g_entity_position_packet_class = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
			if (!g_entity_position_packet_class)
				logger::log_error("[BacktrackHook] Failed to create global ref for EntityPositionS2CPacket");
			env->DeleteLocalRef(cls);
			logger::log_debug("[BacktrackHook] Cached EntityPositionS2CPacket");
		}
		else
		{
			logger::log_error("[BacktrackHook] Failed to find EntityPositionS2CPacket");
		}
	}
	
	// EntityMoveS2CPacket
	if (sdk::mappings::entity_move_s2c_packet_class_sig)
	{
		jclass cls = sdk::classloader::find_class(env, sdk::mappings::entity_move_s2c_packet_class_sig);
		if (cls)
		{
			g_entity_move_packet_class = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
			if (!g_entity_move_packet_class)
				logger::log_error("[BacktrackHook] Failed to create global ref for EntityMoveS2CPacket");
			env->DeleteLocalRef(cls);
			logger::log_debug("[BacktrackHook] Cached EntityMoveS2CPacket");
		}
	}
	
	// EntityTeleportS2CPacket
	if (sdk::mappings::entity_teleport_s2c_packet_class_sig)
	{
		jclass cls = sdk::classloader::find_class(env, sdk::mappings::entity_teleport_s2c_packet_class_sig);
		if (cls)
		{
			g_entity_teleport_packet_class = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
			if (!g_entity_teleport_packet_class)
				logger::log_error("[BacktrackHook] Failed to create global ref for EntityTeleportS2CPacket");
			env->DeleteLocalRef(cls);
			logger::log_debug("[BacktrackHook] Cached EntityTeleportS2CPacket");
		}
	}
	
	// EntityS2CPacket (parent class)
	if (sdk::mappings::entity_s2c_packet_class_sig)
	{
		jclass cls = sdk::classloader::find_class(env, sdk::mappings::entity_s2c_packet_class_sig);
		if (cls)
		{
			g_entity_s2c_packet_class = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
			if (!g_entity_s2c_packet_class)
				logger::log_error("[BacktrackHook] Failed to create global ref for EntityS2CPacket");
			env->DeleteLocalRef(cls);
			logger::log_debug("[BacktrackHook] Cached EntityS2CPacket");
		}
	}
	
	// Mark hook as ready only if we have at least one packet class
	if (g_entity_position_packet_class || g_entity_move_packet_class || g_entity_teleport_packet_class)
	{
		g_hook_ready = true;
		logger::log_debug("[BacktrackHook] Hook marked as ready");
	}
	else
	{
		logger::log_error("[BacktrackHook] No packet classes cached, hook not ready");
	}
	
	logger::log_debug("[BacktrackHook] init() completed successfully");

	// Start packet processor thread with a large stack: re-delivering packets
	// re-enters the Netty pipeline through the hooked channelRead0, which on a
	// deep pipeline (many mod handlers) accumulates interpreter + JNI frames.
	// The glibc default (8MB) was occasionally exhausted -> SIGSEGV at the
	// thread's stack guard page. 64MB is cheap (mmap, demand-paged).
	if (!g_packet_thread_started)
	{
		logger::log_debug("[BacktrackHook] Starting packet processor thread");
		g_should_stop_processor = false;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, 64 * 1024 * 1024);
		int rc = pthread_create(&g_packet_thread, &attr, packet_processor_trampoline, nullptr);
		pthread_attr_destroy(&attr);
		if (rc == 0)
		{
			g_packet_thread_started = true;
			logger::log_debug("[BacktrackHook] Packet processor thread started");
		}
		else
		{
			logger::log_error("[BacktrackHook] Failed to start packet processor thread");
		}
	}

	return true;
}

void flaway::modules::backtrack_hook::shutdown()
{
	logger::log_debug("[BacktrackHook] shutdown() called");
	
	{
		std::lock_guard<std::recursive_mutex> lock(g_hook_mutex);
		g_hook_ready = false;
		g_delay_enabled = false;
		g_delay_ms = 200;
	}

	// Stop packet processor thread FIRST so it stops calling ORIG_channelRead0.
	// The mutex must NOT be held here: the processor thread takes it while it
	// flushes a batch, and joining with it held would deadlock.
	if (g_packet_thread_started)
	{
		g_should_stop_processor = true;
		pthread_join(g_packet_thread, nullptr);
		g_packet_thread = 0;
		g_packet_thread_started = false;
	}

	// GATE-ONLY teardown: the channelRead0 class is deliberately NOT restored
	// via JNIHook_Detach. Class redefinition (ReapplyClass) at unhook triggers
	// the delta-JRE perfdata guard-page SIGSEGV storm (recurring fault that
	// raced the teardown and killed the game). Instead we leave the native
	// method bound and flip g_hook_ready off: hkChannelRead0 then pass-throughs
	// every packet straight to ORIG_channelRead0, so the game keeps working.
	// ORIG_channelRead0 / g_channel_handler_class / packet class refs must stay
	// valid for that pass-through (the Netty IO thread still calls us) — do NOT
	// null them or delete the refs here.

	// Clean up queued packet global refs (released here on the calling thread)
	{
		std::lock_guard<std::mutex> lock(g_packet_queue_mutex);
		auto env = flaway::instance ? flaway::instance->get_env() : nullptr;
		if (env)
		{
			while (!g_packet_queue.empty())
			{
				DelayedPacket packet = g_packet_queue.front();
				if (packet.packet)
				{
					env->DeleteGlobalRef(packet.packet);
				}
				if (packet.context)
				{
					env->DeleteGlobalRef(packet.context);
				}
				if (packet.handler)
				{
					env->DeleteGlobalRef(packet.handler);
				}
				g_packet_queue.pop();
			}
		}
	}
	
	// Don't shutdown JNIHook here - it might be used by other modules (like reach_hook)
	// Only mark as not initialized locally
	jnihook_initialized = false;
}

void flaway::modules::backtrack_hook::set_delay_enabled(bool enabled)
{
	g_delay_enabled = enabled;
}

void flaway::modules::backtrack_hook::set_delay_ms(int delay_ms)
{
	g_delay_ms = delay_ms;
}
