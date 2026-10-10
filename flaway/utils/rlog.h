#pragma once

#include <stddef.h>

// Render diagnostics log: verbose, per-action logging of the GUI/OpenGL
// pipeline. Every line goes to ~/.minecraft/flaway_render.txt (truncated on
// the first write of each process, so every game launch produces a fresh
// report) with the prefix
//     [t=<ms since first line> f=<render frame>] <message>
//
// Volume control (we want "log every action" without writing gigabytes):
//   * per-message budget: the first 12 hits of a distinct message are written,
//     then only every 60th hit afterwards (long-lived states still leave
//     breadcrumbs without flooding);
//   * global token bucket: 1000 burst, 300 lines/s refill — hard ceiling on
//     the write rate; dropped lines are reported once the bucket recovers.
namespace rlog {

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Close the log file and never reopen it (unhook wipes the file right after,
// so nothing may recreate it). enable() re-arms the logger for a re-inject.
void disable();
void enable();

// Call once at the start of every render frame: advances the frame counter
// and refills the rate-limit bucket.
void frame();

unsigned long long frame_no();

// Milliseconds since the first line written by this process (CLOCK_MONOTONIC).
long long now_ms();

// Drain pending GL errors (a GL context must be current) and log them as
//     gl: <where>: error 0x<code> x<count>
// Returns the last error code, 0 when the pipeline is clean.
unsigned gl_drain(const char* where);

// Log the current GL viewport/scissor box (context must be current) as
//     gl: <where>: viewport=(x,y w h) scissor=(x,y w h)
void gl_viewport(const char* where);

// Snapshot every piece of GL state our overlay touches (viewport, scissor,
// program, VAO/buffer bindings, texture binding + active unit, framebuffer
// bindings, enable flags, color/depth write masks, blend factors, pixel-store
// params) into <out> as one line. Returns false when GL is not resolvable yet.
// Used to diff "state before our draw" vs "state after our draw": any
// difference is a leak that the game (Minecraft/Sodium) will inherit.
bool gl_state_snapshot(char* out, size_t outsz);

// Describe the /proc/self/maps line covering <addr> into <out> as
//     <start>-<end> <perms> <offset> <dev> <inode> <path>
// Returns false (and writes "no-vma") when no mapping covers the address.
// Used to check whether a buffer sits at the edge of its mapping (i.e. an
// over-read would land in the neighbouring page) before uploading it to GL.
bool describe_vma(unsigned long addr, char* out, size_t outsz);

} // namespace rlog
