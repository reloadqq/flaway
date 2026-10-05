#pragma once

#include <string>

// Client-side notifications produced by the modules.
//
// Nothing here ever talks to the server: `add()` paints a line in the local
// chat window only (MinecraftClient.inGameHud -> ChatHud.addMessage), and
// `append_file()` writes an append-only report under ~/.minecraft.
namespace chat_notify {

// Add one line to the local chat HUD. Best effort: every JNI failure is
// swallowed (a notification must never take the game down), and the call is a
// no-op before the client is fully up.
void add(const std::string& line);

// Append one line to ~/.minecraft/<file_name> (created on first use, 0644).
// Returns false when the file cannot be opened.
bool append_file(const std::string& file_name, const std::string& line);

} // namespace chat_notify
