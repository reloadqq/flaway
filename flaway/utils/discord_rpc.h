#pragma once

// Discord Rich Presence for the injected client.
//
// Speaks the local Discord IPC protocol (unix socket + framed JSON) directly,
// so the .so gains no new link dependency and nothing has to be installed.
//
// Lifecycle:
//   start()  - called from instance_t::init() on inject: spawns the worker,
//              which connects to Discord, sends the handshake and pushes the
//              activity, then keeps it alive with pings.
//   tick()   - called once per render frame: starts/stops/restarts the worker
//              when the enabled flag or the RPC settings change in the GUI.
//   stop()   - called from unhook_all()/shutdown(): clears the presence,
//              closes the socket and joins the worker. After stop() returns
//              Discord no longer shows the activity.
namespace discord_rpc
{
	// Spawn the worker (idempotent). Only starts a thread, never blocks on
	// the socket, so it is safe to call from the render thread.
	void start();

	// Clear the activity, disconnect and join the worker (idempotent).
	// Blocks until the worker thread is gone.
	void stop();

	// Per-frame maintenance: reacts to globals::discord_rpc_* changes.
	void tick();

	// First status line: the joined server address, or "in menu" when the
	// player is not on a server. Cheap to call every frame, the worker only
	// re-sends the activity when the text actually changed.
	void set_details(const char* s);

	// Short human readable state for the menu status line.
	const char* status();

	// True while the handshake with Discord succeeded.
	bool connected();
}
