#pragma once

#include <sdk/includes.h>
#include <vector>

namespace sdk
{
	class world_client
	{
	private:
		jobject world;

	public:
		world_client(jobject world);
		world_client(const world_client&) = delete;
		world_client& operator=(const world_client&) = delete;
		~world_client();

		std::vector<jobject> get_players();
		std::vector<jobject> get_entities();
		std::vector<jobject> get_entities_by_class(jclass entity_class);
	};
}

