#pragma once

#include <string>

namespace flaway
{
	namespace config
	{
		// Default auto profile (loads on inject, saves on unload).
		extern const char* AUTO_PROFILE;

		// Full path of the active profile file.
		std::string profile_path(const std::string& name);

		// Serialize every persisted global into "key=value" lines.
		std::string serialize();

		// Apply the serialized config to globals. Returns number of keys applied.
		int apply(const std::string& data);

		// Save the current config state to <name> under ~/.minecraft/flaway/.
		bool save(const std::string& name);

		// Load a profile from ~/.minecraft/flaway/ and apply it.
		bool load(const std::string& name);

		// Convenience: auto profile (used on inject/unload).
		bool save_auto();
		bool load_auto();
	}
}
