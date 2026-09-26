#pragma once

#include <sdk/includes.h>

namespace sdk
{
	class entity_client
	{
	private:
		jobject entity;

	public:
		entity_client(jobject entity);
		entity_client(const entity_client&) = delete;
		entity_client& operator=(const entity_client&) = delete;
		~entity_client();

		jobject get_entity();
		jobject get_bounding_box();
		void set_bounding_box(jobject box);
		bool is_same_object(jobject other);
		double get_x();
		double get_y();
		double get_z();
		float get_yaw();
		float get_pitch();
		void set_yaw(float yaw);
		void set_pitch(float pitch);
		bool is_on_ground();
		double get_fall_distance();
jobject get_velocity();
		double get_velocity_y();
		int get_entity_id();

		bool has_poison();

		// Release JNI global refs held by entity/status caches.
		// Must be called during unhook with a valid JNIEnv.
		static void cleanup_cache(JNIEnv* env);
	};
}

