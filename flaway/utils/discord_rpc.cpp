#include "discord_rpc.h"

#include "../flaway.h"
#include "../globals/globals.h"
#include "../hooks/Hook.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Discord IPC (local unix socket, framed JSON). Opcodes:
//   0 HANDSHAKE, 1 FRAME, 2 CLOSE, 3 PING, 4 PONG
// Frame layout: u32le opcode, u32le payload length, payload.
// ---------------------------------------------------------------------------

namespace
{
	// NB: not named "Status" — X11/Xlib.h typedefs Status to int and gets
	// pulled in through the hook headers.
	enum RpcStatus
	{
		ST_OFF = 0,
		ST_WAITING,
		ST_CONNECTED,
		ST_NO_ID,
		ST_BAD_FMT,
		ST_BAD_ID,
	};

	// Settings snapshot handed to the worker thread: it never reads globals,
	// so the GUI can keep editing the buffers while a worker is running.
	// Heap allocated on start(): no non-trivial object with dynamic
	// initialization at static scope (see the note in utils/logger.cpp).
	struct Config
	{
		std::string client_id;
		std::string details;
		std::string state;
		std::string large_image;
		long long start_ts = 0;

		bool same_settings(const Config& o) const
		{
			// details is live (joined server / fallback): never a restart
			// trigger, the worker re-sends the activity on its own.
			return client_id == o.client_id && state == o.state &&
			       large_image == o.large_image;
		}
	};

	struct Worker
	{
		Config cfg;
		int wake_rd = -1;
	};

	// start()/stop()/tick() all run under this mutex (render thread + the
	// detached unhook thread). The worker itself never takes it, so joining
	// the worker while holding it can not deadlock.
	std::mutex        g_api;
	bool              g_running = false;   // guarded by g_api
	pthread_t         g_thread{};
	int               g_wake_wr = -1;      // guarded by g_api
	Config*           g_cfg = nullptr;     // guarded by g_api (running settings)

	std::atomic<bool> g_worker_quit{false};
	std::atomic<bool> g_shut{true};        // stop() latches this so tick() can
	                                       // not resurrect the worker after unhook
	std::atomic<bool> g_connected{false};
	std::atomic<int>  g_status{ST_OFF};

	// Live first status line (server address / "in menu"), written by the
	// render thread and read by the worker. Its own mutex: the worker must
	// never take g_api (joining it would deadlock).
	std::mutex  g_txt;
	std::string g_details = "in menu";
	bool        g_details_dirty = false;

	long long         g_nonce = 0;         // worker thread only

	// Read + clear the dirty flag. Lock order when combined with the API
	// mutex is always g_api -> g_txt.
	std::string poll_details()
	{
		std::lock_guard<std::mutex> lk(g_txt);
		g_details_dirty = false;
		return g_details;
	}

	long long mono_ms()
	{
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
	}

	const char* status_text(int s)
	{
		switch (s)
		{
			case ST_WAITING: return "waiting for Discord...";
			case ST_CONNECTED: return "connected";
			case ST_NO_ID: return "no Application ID (discord.com/developers)";
			case ST_BAD_FMT: return "invalid Application ID (digits only)";
			case ST_BAD_ID: return "Discord rejected the Application ID";
			default: return "off";
		}
	}

	void set_status(int s)
	{
		int prev = g_status.exchange(s);
		if (prev != s)
			fprintf(stderr, "[discord_rpc] %s\n", status_text(s));
	}

	// --- JSON ---------------------------------------------------------------

	std::string json_escape(const std::string& s)
	{
		std::string out;
		out.reserve(s.size() + 8);
		for (size_t i = 0; i < s.size(); i++)
		{
			char c = s[i];
			switch (c)
			{
				case '"':  out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				default:
					if ((unsigned char)c < 0x20)
					{
						char buf[8];
						snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
						out += buf;
					}
					else
						out += c;
					break;
			}
		}
		return out;
	}

	std::string activity_json(const Config& cfg)
	{
		std::string a = "{";
		if (!cfg.details.empty())
			a += "\"details\":\"" + json_escape(cfg.details) + "\",";
		if (!cfg.state.empty())
			a += "\"state\":\"" + json_escape(cfg.state) + "\",";
		a += "\"timestamps\":{\"start\":" + std::to_string(cfg.start_ts) + "}";
		if (!cfg.large_image.empty())
			a += ",\"assets\":{\"large_image\":\"" + json_escape(cfg.large_image) + "\"}";
		// instance:false keeps Discord from offering a "Launch Game" button.
		a += ",\"instance\":false}";
		return a;
	}

	// --- transport ----------------------------------------------------------

	bool send_all(int fd, const char* data, size_t len)
	{
		size_t done = 0;
		while (done < len)
		{
			// MSG_NOSIGNAL: a write to a socket Discord already closed must
			// return EPIPE instead of SIGSEGV-killing the game process.
			ssize_t r = send(fd, data + done, len - done, MSG_NOSIGNAL);
			if (r < 0)
			{
				if (errno == EINTR) continue;
				return false;
			}
			if (r == 0) return false;
			done += (size_t)r;
		}
		return true;
	}

	bool send_frame(int fd, unsigned opcode, const std::string& payload)
	{
		unsigned char hdr[8];
		hdr[0] = (unsigned char)(opcode & 0xff);
		hdr[1] = (unsigned char)((opcode >> 8) & 0xff);
		hdr[2] = (unsigned char)((opcode >> 16) & 0xff);
		hdr[3] = (unsigned char)((opcode >> 24) & 0xff);
		unsigned len = (unsigned)payload.size();
		hdr[4] = (unsigned char)(len & 0xff);
		hdr[5] = (unsigned char)((len >> 8) & 0xff);
		hdr[6] = (unsigned char)((len >> 16) & 0xff);
		hdr[7] = (unsigned char)((len >> 24) & 0xff);
		if (!send_all(fd, (const char*)hdr, 8)) return false;
		if (len && !send_all(fd, payload.data(), payload.size())) return false;
		return true;
	}

	// Poll the socket together with the wake pipe so stop() can interrupt any
	// blocking point of the worker instantly.
	//   >0 socket ready, 0 timeout, -1 wake (quit requested)
	int wait_socket(int sock, int wake, int timeout_ms)
	{
		if (sock < 0 && wake < 0) return 0;
		struct pollfd p[2];
		int n = 0;
		int wake_i = -1, sock_i = -1;
		if (wake >= 0)
		{
			wake_i = n;
			p[n].fd = wake;
			p[n].events = POLLIN;
			p[n].revents = 0;
			n++;
		}
		if (sock >= 0)
		{
			sock_i = n;
			p[n].fd = sock;
			p[n].events = POLLIN;
			p[n].revents = 0;
			n++;
		}
		for (;;)
		{
			int r = ::poll(p, (nfds_t)n, timeout_ms);
			if (r < 0 && errno == EINTR) continue;
			if (r <= 0) return 0;
			if (wake_i >= 0 && (p[wake_i].revents & (POLLIN | POLLHUP | POLLERR)))
			{
				char drain[32];
				while (::read(wake, drain, sizeof(drain)) > 0) {}
				return -1;
			}
			if (sock_i >= 0 && p[sock_i].revents) return 1;
			return 0;
		}
	}

	// 1 = ok, 0 = deadline passed, -1 = closed/error, -2 = quit requested
	int read_exact(int sock, int wake, unsigned char* buf, size_t n, long long deadline)
	{
		size_t got = 0;
		while (got < n)
		{
			long long remain = deadline - mono_ms();
			if (remain <= 0) return 0;
			int w = wait_socket(sock, wake, remain > 250 ? 250 : (int)remain);
			if (w < 0) return -2;
			if (w == 0) continue;
			ssize_t r = recv(sock, buf + got, n - got, 0);
			if (r == 0) return -1;
			if (r < 0)
			{
				if (errno == EINTR) continue;
				return -1;
			}
			got += (size_t)r;
		}
		return 1;
	}

	enum RecvRes
	{
		RECV_CLOSED = -1,
		RECV_QUIT = -2,
		RECV_TIMEOUT = 0,
		RECV_FRAME = 1,
	};

	int recv_frame(int sock, int wake, unsigned& opcode, std::string& payload, int timeout_ms)
	{
		int w = wait_socket(sock, wake, timeout_ms);
		if (w < 0) return RECV_QUIT;
		if (w == 0) return RECV_TIMEOUT;

		unsigned char hdr[8];
		int r = read_exact(sock, wake, hdr, 8, mono_ms() + 5000);
		if (r == -2) return RECV_QUIT;
		if (r != 1) return RECV_CLOSED;

		opcode = (unsigned)hdr[0] | ((unsigned)hdr[1] << 8) |
		         ((unsigned)hdr[2] << 16) | ((unsigned)hdr[3] << 24);
		unsigned len = (unsigned)hdr[4] | ((unsigned)hdr[5] << 8) |
		               ((unsigned)hdr[6] << 16) | ((unsigned)hdr[7] << 24);
		if (len > 4u * 1024u * 1024u) return RECV_CLOSED;

		payload.assign(len, '\0');
		if (len)
		{
			r = read_exact(sock, wake, (unsigned char*)&payload[0], len, mono_ms() + 5000);
			if (r == -2) return RECV_QUIT;
			if (r != 1) return RECV_CLOSED;
		}
		return RECV_FRAME;
	}

	// Sleep while staying interruptible by the wake pipe.
	// Returns false when a quit was requested during the wait.
	bool wait_ms(int wake, int timeout_ms)
	{
		long long deadline = mono_ms() + timeout_ms;
		for (;;)
		{
			long long remain = deadline - mono_ms();
			if (remain <= 0) return !g_worker_quit.load();
			int w = wait_socket(-1, wake, remain > 250 ? 250 : (int)remain);
			if (w < 0) return false;
		}
	}

	// --- socket discovery ---------------------------------------------------

	int try_connect(const std::string& path)
	{
		sockaddr_un addr;
		if (path.empty() || path.size() >= sizeof(addr.sun_path)) return -1;
		struct stat st;
		if (stat(path.c_str(), &st) != 0) return -1;
		int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		if (fd < 0) return -1;
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		memcpy(addr.sun_path, path.c_str(), path.size());
		if (connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0)
		{
			close(fd);
			return -1;
		}
		return fd;
	}

	int connect_discord()
	{
		std::vector<std::string> bases;
		const char* xdg = getenv("XDG_RUNTIME_DIR");
		if (xdg && *xdg) bases.push_back(xdg);
		const char* tmp = getenv("TMPDIR");
		if (tmp && *tmp) bases.push_back(tmp);
		bases.push_back(std::string("/run/user/") + std::to_string((long long)getuid()));
		bases.push_back("/tmp");

		// Flatpak / Snap / Vesktop keep the socket in a subdirectory.
		static const char* k_subs[] = {
			"",
			"discord",
			"app/com.discordapp.Discord",
			"app/com.discordapp.DiscordCanary",
			"app/com.discordapp.DiscordPTB",
			"app/com.discordapp.DiscordDevelopment",
			"snap.discord",
			"app/dev.vencord.Vesktop",
			"app/com.vencord.Vesktop",
		};

		for (size_t b = 0; b < bases.size(); b++)
		{
			for (size_t s = 0; s < sizeof(k_subs) / sizeof(k_subs[0]); s++)
			{
				std::string dir = bases[b];
				if (k_subs[s][0])
				{
					dir += "/";
					dir += k_subs[s];
				}
				for (int i = 0; i < 10; i++)
				{
					std::string path = dir + "/discord-ipc-" + std::to_string(i);
					int fd = try_connect(path);
					if (fd >= 0) return fd;
				}
			}
		}
		return -1;
	}

	// --- protocol -----------------------------------------------------------

	bool send_set_activity(int fd, const Config& cfg, bool present)
	{
		std::string act = present ? activity_json(cfg) : std::string("null");
		g_nonce++;
		std::string payload =
			"{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"" + std::to_string(g_nonce) +
			"\",\"args\":{\"pid\":" + std::to_string((long long)getpid()) +
			",\"activity\":" + act + "}}";
		return send_frame(fd, 1 /*FRAME*/, payload);
	}

	// Handshake + wait for READY. Returns 1 ready, 0 timeout/other, -1 bad id.
	int do_handshake(int fd, int wake, const Config& cfg)
	{
		std::string hs = "{\"v\":1,\"client_id\":\"" + json_escape(cfg.client_id) + "\"}";
		if (!send_frame(fd, 0 /*HANDSHAKE*/, hs)) return 0;

		long long deadline = mono_ms() + 5000;
		for (;;)
		{
			long long remain = deadline - mono_ms();
			if (remain <= 0) return 0;
			unsigned op = 0;
			std::string payload;
			int r = recv_frame(fd, wake, op, payload, remain > 250 ? 250 : (int)remain);
			if (r == RECV_QUIT) return 0;
			if (r == RECV_CLOSED) return 0;
			if (r == RECV_TIMEOUT) continue;

			if (op == 3 /*PING*/)
				send_frame(fd, 4 /*PONG*/, payload);
			else if (op == 1)
			{
				if (payload.find("\"READY\"") != std::string::npos) return 1;
				if (payload.find("\"evt\":\"ERROR\"") != std::string::npos)
					return payload.find("Invalid Application ID") != std::string::npos ? -1 : 0;
			}
		}
	}

	void run_worker(Worker self)
	{
		const int wake = self.wake_rd;
		Config cfg = self.cfg;   // mutable: details gets refreshed live

		while (!g_worker_quit.load())
		{
			int fd = connect_discord();
			if (fd < 0)
			{
				set_status(ST_WAITING);
				if (!wait_ms(wake, 2000)) break;
				continue;
			}

			int hs = do_handshake(fd, wake, cfg);
			if (hs <= 0)
			{
				close(fd);
				if (hs < 0) set_status(ST_BAD_ID);
				else set_status(ST_WAITING);
				if (!wait_ms(wake, hs < 0 ? 5000 : 2000)) break;
				continue;
			}

			cfg.details = poll_details();
			send_set_activity(fd, cfg, true);
			g_connected.store(true);
			set_status(ST_CONNECTED);

			long long last_rx = mono_ms();
			long long last_ping = last_rx;
			bool reported_error = false;

			while (!g_worker_quit.load())
			{
				int w = wait_socket(fd, wake, 250);
				if (w < 0) break;
				long long now = mono_ms();

				if (w == 1)
				{
					unsigned op = 0;
					std::string payload;
					int r = recv_frame(fd, wake, op, payload, 250);
					if (r == RECV_QUIT || r == RECV_CLOSED) break;
					if (r == RECV_FRAME)
					{
						last_rx = now;
						if (op == 3 /*PING*/)
							send_frame(fd, 4 /*PONG*/, payload);
						else if (op == 2 /*CLOSE*/)
							break;
						else if (op == 1 && !reported_error &&
						         payload.find("\"evt\":\"ERROR\"") != std::string::npos)
						{
							// Once per session: Discord refused a command
							// (bad asset key, bad activity field, ...).
							reported_error = true;
							fprintf(stderr, "[discord_rpc] discord error: %.300s\n",
							        payload.c_str());
						}
						// 4 PONG / 1 FRAME (activity echo): liveness only.
					}
				}

				// Joined / left a server: push the new first status line.
				std::string d = poll_details();
				if (d != cfg.details)
				{
					cfg.details = d;
					if (!send_set_activity(fd, cfg, true)) break;
				}

				if (now - last_ping > 30000)
				{
					if (!send_frame(fd, 3 /*PING*/, std::to_string(now)))
						break;
					last_ping = now;
				}
				// No traffic at all for 90s: Discord is gone or wedged.
				if (now - last_rx > 90000) break;
			}

			// Drop the presence BEFORE releasing the socket, so an unhook
			// leaves nothing behind in the Discord status line.
			send_set_activity(fd, cfg, false);
			send_frame(fd, 2 /*CLOSE*/, "{}");
			usleep(80 * 1000);
			close(fd);

			g_connected.store(false);
			set_status(ST_WAITING);
			if (g_worker_quit.load()) break;
			if (!wait_ms(wake, 2000)) break;
		}

		g_connected.store(false);
		if (wake >= 0) close(wake);
	}

	void* worker_main(void* arg)
	{
		Worker* w = static_cast<Worker*>(arg);
		Worker self = *w;
		delete w;
		run_worker(self);
		return nullptr;
	}

	// --- api ----------------------------------------------------------------

	Config resolve()
	{
		Config c;
		const char* env = getenv("FLAWAY_DISCORD_CLIENT_ID");
		if (env && *env)
			c.client_id = env;
		else
			c.client_id = globals::discord_rpc_client_id;
		// Trim stray whitespace/newlines from a hand-edited config.
		while (!c.client_id.empty() &&
		       (c.client_id.back() == ' ' || c.client_id.back() == '\n' ||
		        c.client_id.back() == '\r' || c.client_id.back() == '\t'))
			c.client_id.pop_back();
		size_t start = c.client_id.find_first_not_of(" \t\r\n");
		if (start == std::string::npos) c.client_id.clear();
		else if (start) c.client_id = c.client_id.substr(start);

		c.details = poll_details();
		c.state = globals::discord_rpc_state;
		c.large_image = globals::discord_rpc_large_image;
		c.start_ts = (long long)time(nullptr);
		return c;
	}

	bool id_looks_valid(const std::string& id)
	{
		if (id.size() < 17 || id.size() > 21) return false;
		for (size_t i = 0; i < id.size(); i++)
			if (id[i] < '0' || id[i] > '9') return false;
		return true;
	}

	// Caller must hold g_api.
	void stop_locked(bool latch)
	{
		if (latch) g_shut.store(true);
		if (!g_running) return;

		g_worker_quit.store(true);
		if (g_wake_wr >= 0)
		{
			char b = 1;
			ssize_t ignored = write(g_wake_wr, &b, 1);
			(void)ignored;
		}
		pthread_join(g_thread, nullptr);
		g_running = false;
		g_thread = pthread_t{};
		if (g_wake_wr >= 0)
		{
			close(g_wake_wr);
			g_wake_wr = -1;
		}
		delete g_cfg;
		g_cfg = nullptr;
		g_connected.store(false);
		set_status(latch ? ST_OFF : ST_WAITING);
	}

	// Caller must hold g_api.
	void start_locked(const Config& cfg)
	{
		int wake[2] = { -1, -1 };
		if (pipe2(wake, O_NONBLOCK | O_CLOEXEC) != 0)
		{
			fprintf(stderr, "[discord_rpc] pipe2 failed: %s\n", strerror(errno));
			return;
		}
		Worker* w = new Worker();
		w->cfg = cfg;
		w->wake_rd = wake[0];
		g_cfg = new Config(cfg);
		g_wake_wr = wake[1];
		g_worker_quit.store(false);
		g_shut.store(false);
		set_status(ST_WAITING);

		if (pthread_create(&g_thread, nullptr, worker_main, w) != 0)
		{
			fprintf(stderr, "[discord_rpc] pthread_create failed: %s\n", strerror(errno));
			delete w;
			close(wake[0]);
			close(wake[1]);
			g_wake_wr = -1;
			delete g_cfg;
			g_cfg = nullptr;
			set_status(ST_OFF);
			return;
		}
		g_running = true;
	}
}

void discord_rpc::start()
{
	std::lock_guard<std::mutex> lk(g_api);
	if (g_running) return;
	if (!globals::discord_rpc_enabled) { g_shut.store(true); return; }

	Config cfg = resolve();
	if (!id_looks_valid(cfg.client_id))
	{
		set_status(cfg.client_id.empty() ? ST_NO_ID : ST_BAD_FMT);
		g_shut.store(false); // tick() keeps watching so a fix in the GUI takes effect
		return;
	}
	start_locked(cfg);
}

void discord_rpc::stop()
{
	std::lock_guard<std::mutex> lk(g_api);
	stop_locked(true);
}

void discord_rpc::tick()
{
	std::lock_guard<std::mutex> lk(g_api);
	// stop() latched us: an unhook/teardown already ran, only a fresh
	// start() (from init() on the next inject) may bring the worker back.
	if (g_shut.load()) return;
	// The swap hook is gated off: nothing may resurrect the RPC behind the
	// teardown's back.
	if (linux_hook::get_unhooked()) { stop_locked(true); return; }

	// The status is shown ONLY while the injected client is actually up.
	// Before init() finished (or after a teardown dropped initialized) there
	// is nothing to advertise, so the presence must be gone from Discord.
	if (!flaway::instance ||
	    !flaway::instance->initialized.load(std::memory_order_acquire))
	{
		if (g_running) stop_locked(true);
		return;
	}

	if (!globals::discord_rpc_enabled)
	{
		stop_locked(false);
		set_status(ST_OFF);
		return;
	}

	Config want = resolve();
	if (!id_looks_valid(want.client_id))
	{
		stop_locked(false);
		set_status(want.client_id.empty() ? ST_NO_ID : ST_BAD_FMT);
		return;
	}

	if (!g_running)
	{
		start_locked(want);
		return;
	}
	// Running: restart only when the user actually changed something.
	if (g_cfg && !g_cfg->same_settings(want))
	{
		stop_locked(false);
		start_locked(want);
	}
}

const char* discord_rpc::status()
{
	return status_text(g_status.load());
}

bool discord_rpc::connected()
{
	return g_connected.load();
}

void discord_rpc::set_details(const char* s)
{
	std::string d = (s && *s) ? s : "in menu";
	std::lock_guard<std::mutex> lk(g_txt);
	if (g_details == d) return;
	g_details = d;
	g_details_dirty = true;
	// No wake-up here: the worker polls the flag every 250 ms, which is far
	// below anything a server change needs, and the render thread must stay
	// free of socket work.
}
