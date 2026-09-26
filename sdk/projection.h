#pragma once

#include <cmath>

// Single, shared world->screen projection used by every ESP-style overlay
// (player ESP, storage ESP, backtrack, FOV). Using ONE consistent basis
// means boxes drawn by different modules land on exactly the same pixels,
// so they no longer overlap/mirror each other ("the mess"). The matrix is
// recomputed only when the camera/viewport actually changes, so all
// renderers in a frame share one cheap computation.
namespace flaway
{
	namespace projection
	{
		struct view_state
		{
			float right[3];
			float up[3];
			float forward[3];
			float cam_pos[3];
			float fov_x, fov_y, half_w, half_h;
			float cam_x, cam_y, cam_z, yaw, pitch, fov;
			int sw, sh;
			bool valid;
		};

		inline view_state& state()
		{
			static view_state s{};
			return s;
		}

		inline void set_view(float cam_x, float cam_y, float cam_z,
			float yaw, float pitch, float fov,
			int screen_w, int screen_h)
		{
			view_state& s = state();
			if (s.valid && s.cam_x == cam_x && s.cam_y == cam_y && s.cam_z == cam_z &&
				s.yaw == yaw && s.pitch == pitch && s.fov == fov &&
				s.sw == screen_w && s.sh == screen_h)
				return;

			s.cam_x = cam_x; s.cam_y = cam_y; s.cam_z = cam_z;
			s.yaw = yaw; s.pitch = pitch; s.fov = fov;
			s.sw = screen_w; s.sh = screen_h;

			float yr = yaw * (float)(M_PI / 180.0);
			float pr = pitch * (float)(M_PI / 180.0);
			float cyaw = cosf(yr), syaw = sinf(yr);
			float cpitch = cosf(pr), spitch = sinf(pr);

			// forward = camera look direction
			s.forward[0] = -syaw * cpitch;
			s.forward[1] = -spitch;
			s.forward[2] = cyaw * cpitch;

			// right = horizontal perpendicular to forward
			s.right[0] = -s.forward[2];
			s.right[1] = 0.0f;
			s.right[2] = s.forward[0];
			float rlen = sqrtf(s.right[0] * s.right[0] + s.right[2] * s.right[2]);
			if (rlen > 1e-6f) { s.right[0] /= rlen; s.right[2] /= rlen; }

			// up = cross(right, forward)
			s.up[0] = s.right[1] * s.forward[2] - s.right[2] * s.forward[1];
			s.up[1] = s.right[2] * s.forward[0] - s.right[0] * s.forward[2];
			s.up[2] = s.right[0] * s.forward[1] - s.right[1] * s.forward[0];

			s.cam_pos[0] = cam_x; s.cam_pos[1] = cam_y; s.cam_pos[2] = cam_z;

			float f = fov;
			if (f < 1.0f) f = 1.0f;
			if (f > 179.0f) f = 179.0f;
			float fov_rad = f * (float)(M_PI / 180.0);
			float aspect = (float)screen_w / (float)screen_h;
			float tan_hf = tanf(fov_rad * 0.5f);
			s.fov_x = 1.0f / (tan_hf * aspect);
			s.fov_y = 1.0f / tan_hf;
			s.half_w = screen_w * 0.5f;
			s.half_h = screen_h * 0.5f;
			s.valid = true;
		}

		inline bool world_to_screen(float x, float y, float z, float& sx, float& sy)
		{
			view_state& s = state();
			if (!s.valid) return false;
			float lx = x - s.cam_pos[0];
			float ly = y - s.cam_pos[1];
			float lz = z - s.cam_pos[2];
			float px = lx * s.right[0] + ly * s.right[1] + lz * s.right[2];
			float py = lx * s.up[0] + ly * s.up[1] + lz * s.up[2];
			float pz = lx * s.forward[0] + ly * s.forward[1] + lz * s.forward[2];
			if (pz < 0.01f) return false;
			float inv = 1.0f / pz;
			sx = s.half_w + (px * inv * s.fov_x) * s.half_w;
			sy = s.half_h - (py * inv * s.fov_y) * s.half_h;
			return true;
		}
	}
}
