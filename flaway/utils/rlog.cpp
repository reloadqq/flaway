#include "rlog.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <mutex>

namespace rlog {
namespace {

// Guards g_fd / g_opened / the token bucket / g_msgs. logf() and frame() are
// reachable from the render thread AND from install/uninstall diagnostics
// (Hook.cpp), so without this two threads can both observe g_opened == false
// and open() the file twice (fd leak + O_TRUNC discarding earlier lines).
std::mutex g_mu;

int g_fd = -1;
bool g_opened = false;
unsigned long long g_frame = 0;
long long g_t0 = -1;
long long g_bucket_ms = 0;
unsigned g_tokens = 0;
unsigned long long g_dropped = 0;

struct Msg {
	char text[176];
	unsigned hits;
};
enum { k_msgs = 384 };
Msg g_msgs[k_msgs];

long long mono_ms() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void ensure_open() {
	// Called with g_mu held.
	if (g_opened) return;
	g_opened = true;
	char path[512];
	const char* home = getenv("HOME");
	if (home) {
		snprintf(path, sizeof(path), "%s/.minecraft/flaway_render.txt", home);
		g_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (g_fd < 0)
			g_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	}
	if (g_fd < 0) // ~/.minecraft missing or not writable: never lose the report
		g_fd = open("/tmp/flaway_render.txt", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
}

void write_str(const char* s, size_t n) {
	if (g_fd < 0) return;
	size_t done = 0;
	while (done < n) {
		ssize_t r = write(g_fd, s + done, n - done);
		if (r <= 0) break;
		done += (size_t)r;
	}
}

void refill(long long now) {
	if (g_bucket_ms == 0) {
		g_bucket_ms = now;
		g_tokens = 1000;
		return;
	}
	long long dt = now - g_bucket_ms;
	if (dt <= 0) return;
	g_bucket_ms = now;
	unsigned add = (unsigned)(dt * 300 / 1000); // 300 lines/s
	if (add) {
		g_tokens += add;
		if (g_tokens > 1000) g_tokens = 1000;
	}
}

// Per-message budget: true = this occurrence may be written.
bool budget(const char* msg) {
	Msg* slot = nullptr;
	for (int i = 0; i < k_msgs; i++) {
		if (g_msgs[i].hits && strncmp(g_msgs[i].text, msg, sizeof(g_msgs[i].text)) == 0) {
			slot = &g_msgs[i];
			break;
		}
	}
	if (!slot) {
		for (int i = 0; i < k_msgs; i++) {
			if (!g_msgs[i].hits) {
				slot = &g_msgs[i];
				break;
			}
		}
	}
	if (!slot) {
		// Table full: do NOT recycle slot 0. Folding unrelated messages into
		// one entry makes g_msgs[0].hits measure strangers and consumes its
		// own 12-hit budget on them. The global token bucket still applies.
		return false;
	}
	if (slot->hits == 0) snprintf(slot->text, sizeof(slot->text), "%.*s", (int)sizeof(slot->text) - 1, msg);
	slot->hits++;
	return slot->hits <= 12 || (slot->hits % 60) == 0;
}

void emit(const char* msg) {
	ensure_open();
	if (g_fd < 0) return;
	long long now = mono_ms();
	if (g_t0 < 0) g_t0 = now;
	refill(now);
	if (g_tokens == 0) {
		g_dropped++;
		return;
	}
	if (!budget(msg)) return;
	g_tokens--;
	char line[700];
	if (g_dropped) {
		int n = snprintf(line, sizeof(line), "[t=%lld f=%llu] rlog: dropped %llu lines (rate cap)\n",
		                 now - g_t0, g_frame, g_dropped);
		if (n > 0) write_str(line, (size_t)n);
		g_dropped = 0;
	}
	int n = snprintf(line, sizeof(line), "[t=%lld f=%llu] %s\n", now - g_t0, g_frame, msg);
	if (n > 0) write_str(line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
}

} // namespace

void logf(const char* fmt, ...) {
	char buf[400];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	std::lock_guard<std::mutex> lk(g_mu);
	emit(buf);
}

void frame() {
	std::lock_guard<std::mutex> lk(g_mu);
	g_frame++;
	long long now = mono_ms();
	if (g_t0 < 0) g_t0 = now;
	refill(now);
}

unsigned long long frame_no() { return g_frame; }

long long now_ms() {
	long long now = mono_ms();
	return g_t0 < 0 ? 0 : now - g_t0;
}

typedef unsigned (*gl_get_error_fn)();
typedef void (*gl_get_integerv_fn)(int, int*);

unsigned gl_drain(const char* where) {
	static gl_get_error_fn fn = nullptr;
	static int tries = 0;
	// GL may not be loaded yet on the very first calls — retry a bounded number
	// of times instead of giving up forever.
	if (!fn && tries < 64) {
		tries++;
		fn = reinterpret_cast<gl_get_error_fn>(dlsym(RTLD_DEFAULT, "glGetError"));
		if (!fn && (tries == 1 || tries == 64))
			logf("gl: %s: glGetError not resolved (GL not loaded yet?)", where);
	}
	if (!fn) return 0;
	unsigned last = 0;
	int count = 0;
	for (int i = 0; i < 16; i++) {
		unsigned e = fn();
		if (e == 0) break;
		last = e;
		count++;
	}
	if (last) logf("gl: %s: error 0x%x x%d", where, last, count);
	return last;
}

void gl_viewport(const char* where) {
	static gl_get_integerv_fn fn = nullptr;
	static int tries = 0;
	if (!fn && tries < 64) {
		tries++;
		fn = reinterpret_cast<gl_get_integerv_fn>(dlsym(RTLD_DEFAULT, "glGetIntegerv"));
		if (!fn && (tries == 1 || tries == 64)) {
			logf("gl: %s: glGetIntegerv not resolved (GL not loaded yet?)", where);
			return;
		}
	}
	if (!fn) return;
	int vp[4] = {0, 0, 0, 0};
	int sc[4] = {0, 0, 0, 0};
	fn(0x0BA2 /*GL_VIEWPORT*/, vp);
	fn(0x0C10 /*GL_SCISSOR_BOX*/, sc);
	logf("gl: %s: viewport=(%d,%d %dx%d) scissor=(%d,%d %dx%d)",
	     where, vp[0], vp[1], vp[2], vp[3], sc[0], sc[1], sc[2], sc[3]);
}

bool gl_state_snapshot(char* out, size_t outsz) {
	typedef void (*gl_get_booleanv_fn)(int, unsigned char*);
	static gl_get_integerv_fn geti = nullptr;
	static gl_get_booleanv_fn getb = nullptr;
	typedef unsigned (*gl_is_enabled_fn)(unsigned);
	static gl_is_enabled_fn isen = nullptr;
	static int tries = 0;
	if ((!geti || !getb || !isen) && tries < 64) {
		tries++;
		if (!geti) geti = reinterpret_cast<gl_get_integerv_fn>(dlsym(RTLD_DEFAULT, "glGetIntegerv"));
		if (!getb) getb = reinterpret_cast<gl_get_booleanv_fn>(dlsym(RTLD_DEFAULT, "glGetBooleanv"));
		if (!isen) isen = reinterpret_cast<gl_is_enabled_fn>(dlsym(RTLD_DEFAULT, "glIsEnabled"));
		if ((!geti || !getb || !isen) && (tries == 1 || tries == 64))
			logf("gl: state snapshot: GL functions not resolved yet");
	}
	if (!out || !outsz) return false;
	out[0] = 0;
	if (!geti || !getb || !isen) return false;

	int vp[4] = {0, 0, 0, 0}, sc[4] = {0, 0, 0, 0};
	int prog = 0, vao = 0, abuf = 0, ebuf = 0, tex = 0, act = 0;
	int fbo = 0, rfbo = 0;
	int up_row = 0, up_skip_rows = 0, up_skip_px = 0, up_align = 0;
	int blend_src = 0, blend_dst = 0, blend_eq = 0;
	unsigned char wm[4] = {0, 0, 0, 0};
	unsigned char depth_wm = 0;

	geti(0x0BA2 /*GL_VIEWPORT*/, vp);
	geti(0x0C10 /*GL_SCISSOR_BOX*/, sc);
	geti(0x8B8D /*GL_CURRENT_PROGRAM*/, &prog);
	geti(0x85B5 /*GL_VERTEX_ARRAY_BINDING*/, &vao);
	geti(0x8894 /*GL_ARRAY_BUFFER_BINDING*/, &abuf);
	geti(0x8895 /*GL_ELEMENT_ARRAY_BUFFER_BINDING*/, &ebuf);
	geti(0x806A /*GL_TEXTURE_BINDING_2D*/, &tex);
	geti(0x84E0 /*GL_ACTIVE_TEXTURE*/, &act);
	geti(0x8CA6 /*GL_DRAW_FRAMEBUFFER_BINDING*/, &fbo);
	geti(0x8CAA /*GL_READ_FRAMEBUFFER_BINDING*/, &rfbo);
	geti(0x80C9 /*GL_BLEND_SRC_RGB*/, &blend_src);
	geti(0x80CA /*GL_BLEND_DST_RGB*/, &blend_dst);
	geti(0x8009 /*GL_BLEND_EQUATION_RGB*/, &blend_eq);
	geti(0x0CF2 /*GL_UNPACK_ROW_LENGTH*/, &up_row);
	geti(0x0CF3 /*GL_UNPACK_SKIP_ROWS*/, &up_skip_rows);
	geti(0x0CF4 /*GL_UNPACK_SKIP_PIXELS*/, &up_skip_px);
	geti(0x0CF5 /*GL_UNPACK_ALIGNMENT*/, &up_align);
	getb(0x0C23 /*GL_COLOR_WRITEMASK*/, wm);
	getb(0x0B72 /*GL_DEPTH_WRITEMASK*/, &depth_wm);

	snprintf(out, outsz,
	         "vp=(%d,%d %dx%d) sc=(%d,%d %dx%d) scr=%u prog=%d vao=%d abuf=%d ebuf=%d "
	         "tex0=%d act=0x%x fbo=%d/%d dep=%u cull=%u bln=%u stc=%u wm=%u%u%u%u dwm=%u "
	         "bf=%d,%d be=%d up=%d/%d/%d/%d",
	         vp[0], vp[1], vp[2], vp[3],
	         sc[0], sc[1], sc[2], sc[3], (unsigned)isen(0x0C11),
	         prog, vao, abuf, ebuf, tex, (unsigned)act, fbo, rfbo,
	         (unsigned)isen(0x0B71), (unsigned)isen(0x0B44),
	         (unsigned)isen(0x0BE2), (unsigned)isen(0x0B90),
	         wm[0], wm[1], wm[2], wm[3], (unsigned)depth_wm,
	         blend_src, blend_dst, blend_eq,
	         up_row, up_skip_rows, up_skip_px, up_align);
	return true;
}

bool describe_vma(unsigned long addr, char* out, size_t outsz) {
	if (!out || !outsz) return false;
	out[0] = 0;
	int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		snprintf(out, outsz, "maps-unavailable");
		return false;
	}
	char buf[4096], line[512];
	int llen = 0;
	bool skip = false, found = false;
	for (;;) {
		ssize_t r = read(fd, buf, sizeof(buf));
		if (r <= 0) break;
		for (ssize_t i = 0; i < r; i++) {
			char c = buf[i];
			if (skip) {
				if (c == '\n') skip = false;
				continue;
			}
			if (c != '\n') {
				if (llen < (int)sizeof(line) - 1) line[llen++] = c;
				else skip = true;
				continue;
			}
			line[llen] = 0;
			llen = 0;
			unsigned long s = 0, e = 0;
			const char* p = line;
			bool any = false;
			while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')) {
				s = (s << 4) | (unsigned long)(*p <= '9' ? *p - '0' : *p - 'a' + 10);
				p++;
				any = true;
			}
			if (any && *p == '-') {
				p++;
				e = 0;
				any = false;
				while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')) {
					e = (e << 4) | (unsigned long)(*p <= '9' ? *p - '0' : *p - 'a' + 10);
					p++;
					any = true;
				}
				if (any && addr >= s && addr < e) {
					snprintf(out, outsz, "%s", line);
					found = true;
				}
			}
			if (found) break;
		}
		if (found) break;
	}
	close(fd);
	if (!found) snprintf(out, outsz, "no-vma(0x%lx)", addr);
	return found;
}

} // namespace rlog
