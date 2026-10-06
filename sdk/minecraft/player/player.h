#pragma once

#include <sdk/includes.h>

class player_client
{
private:
	jobject player;
public:
	player_client();
	player_client(jobject player);
	// Owns a global ref freed in the destructor: an implicit copy would
	// double-free. (Same rule as entity_client / box_client / world_client.)
	player_client(const player_client&) = delete;
	player_client& operator=(const player_client&) = delete;
	~player_client();

	jobject get_capabilities();
	void set_flying(bool state);
	void set_sprinting(bool state);
	float get_attack_cooldown_progress(float base_time = 0.5f);

	jobject get_player();
};
