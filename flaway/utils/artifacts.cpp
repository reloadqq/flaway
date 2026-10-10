#include "artifacts.h"
#include "rlog.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <system_error>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
	// Written by hide(), read by restore(). Neutral name on purpose: it is the
	// only file in /tmp that says where the data folder went, and it survives
	// a full .so unload (statics do not, this does).
	const char* k_marker = "/tmp/.stash_path";

	std::string minecraft_dir()
	{
		const char* home = getenv("HOME");
		if (home && *home) return std::string(home) + "/.minecraft";
		return std::string();
	}

	// Exactly where config_dir() / friends_dir() keep their files.
	std::string data_dir()
	{
		std::string mc = minecraft_dir();
		if (!mc.empty()) return mc + "/flaway";
		return "/tmp/flaway";
	}

	std::string random_name()
	{
		static const char k_alpha[] = "abcdefghijklmnopqrstuvwxyz0123456789";
		unsigned char rnd[16];
		size_t got = 0;
		int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
		if (fd >= 0)
		{
			while (got < sizeof(rnd))
			{
				ssize_t r = read(fd, rnd + got, sizeof(rnd) - got);
				if (r <= 0) break;
				got += (size_t)r;
			}
			close(fd);
		}
		if (got < sizeof(rnd))
		{
			// No urandom: still produce something unpredictable enough for a
			// temp folder name instead of failing the whole stash.
			unsigned seed = (unsigned)time(nullptr) ^ (unsigned)getpid();
			for (size_t i = got; i < sizeof(rnd); i++)
			{
				seed = seed * 1664525u + 1013904223u;
				rnd[i] = (unsigned char)(seed >> 24);
			}
		}
		std::string name;
		name.reserve(sizeof(rnd));
		for (size_t i = 0; i < sizeof(rnd); i++)
			name.push_back(k_alpha[rnd[i] % 36]);
		return name;
	}

	// Overwrite the bytes, then unlink: unlink alone leaves the content in
	// free blocks until the filesystem reuses them.
	void wipe_file(const std::string& path)
	{
		int fd = open(path.c_str(), O_WRONLY);
		if (fd >= 0)
		{
			off_t size = lseek(fd, 0, SEEK_END);
			// Log files are tiny; the cap only guards against a runaway file
			// turning unhook into a multi-second stall.
			if (size > 16 * 1024 * 1024) size = 16 * 1024 * 1024;
			if (size > 0 && lseek(fd, 0, SEEK_SET) == 0)
			{
				char zeros[4096];
				memset(zeros, 0, sizeof(zeros));
				off_t left = size;
				while (left > 0)
				{
					size_t chunk = left > (off_t)sizeof(zeros) ? sizeof(zeros) : (size_t)left;
					ssize_t w = write(fd, zeros, chunk);
					if (w <= 0) break;
					left -= w;
				}
				fsync(fd);
			}
			close(fd);
		}
		unlink(path.c_str());
	}

	// rename() is atomic and cheap, but /tmp is usually a tmpfs, so a folder
	// coming from $HOME crosses a filesystem boundary -> copy + delete.
	bool move_dir(const fs::path& from, const fs::path& to)
	{
		std::error_code ec;
		fs::rename(from, to, ec);
		if (!ec) return true;
		if (ec != std::errc::cross_device_link) return false;
		ec.clear();
		fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
		if (ec) return false;
		std::error_code rm;
		fs::remove_all(from, rm);
		return true;
	}
}

void flaway::artifacts::hide()
{
	// Stop the render log first: its fd is closed here so the unlink below
	// removes the real file, not an open descriptor's inode. enable() brings
	// it back on the next inject.
	rlog::disable();

	// The writers of these files (crash_dump / Hook / GUI / inline_hook) keep
	// their fds open across unhook: once the path is gone their later writes
	// land in an unlinked inode and never appear on disk again.
	std::string mc = minecraft_dir();
	if (!mc.empty())
	{
		wipe_file(mc + "/flaway_crash.txt");
		wipe_file(mc + "/flaway_diag.txt");
		wipe_file(mc + "/flaway_render.txt");
	}
	wipe_file("/tmp/flaway_render.txt"); // rlog fallback path

	std::string src = data_dir();
	std::error_code ec;
	if (!fs::is_directory(src, ec)) return;

	std::string dst;
	for (int i = 0; i < 8; i++)
	{
		dst = "/tmp/" + random_name();
		if (!fs::exists(dst, ec)) break;
		dst.clear();
	}
	if (dst.empty()) return;
	if (!move_dir(src, dst)) return;

	// Pointer is written only after the move succeeded; if it cannot be
	// written the folder goes straight back so data is never lost.
	int fd = open(k_marker, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
	{
		move_dir(dst, src);
		return;
	}
	size_t len = dst.size();
	size_t done = 0;
	while (done < len)
	{
		ssize_t w = write(fd, dst.data() + done, len - done);
		if (w <= 0) break;
		done += (size_t)w;
	}
	close(fd);
	if (done != len)
	{
		move_dir(dst, src);
		unlink(k_marker);
	}
}

void flaway::artifacts::restore()
{
	// The render log was disabled by hide(); the next session needs it again.
	rlog::enable();

	std::error_code ec;
	if (!fs::is_regular_file(k_marker, ec)) return;

	std::string stash;
	int fd = open(k_marker, O_RDONLY | O_CLOEXEC);
	if (fd >= 0)
	{
		char buf[512];
		ssize_t n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n > 0)
		{
			buf[n] = 0;
			stash = buf;
		}
	}
	while (!stash.empty() && (stash.back() == '\n' || stash.back() == '\r' || stash.back() == ' '))
		stash.pop_back();

	// Sanity: absolute path, no traversal, and it must actually be a folder.
	if (stash.empty() || stash[0] != '/' || stash.find("/../") != std::string::npos)
	{
		fs::remove(k_marker, ec);
		return;
	}
	if (!fs::is_directory(stash, ec))
	{
		fs::remove(k_marker, ec);
		return;
	}

	std::string dst = data_dir();
	std::error_code ec2;
	if (fs::is_directory(dst, ec2))
	{
		// Something recreated the target while we were away: merge the stashed
		// files over it (per entry - a plain copy of the directory itself
		// would nest it as dst/<name>).
		std::error_code iec;
		fs::directory_iterator it(stash, iec), end;
		for (; !iec && it != end; it.increment(iec))
		{
			fs::path target = fs::path(dst) / it->path().filename();
			std::error_code cec;
			fs::copy(it->path(), target,
				fs::copy_options::recursive | fs::copy_options::overwrite_existing, cec);
			if (cec) return; // keep the marker, retry on the next inject
		}
		if (iec) return;
		std::error_code rm;
		fs::remove_all(stash, rm);
	}
	else
	{
		fs::create_directories(fs::path(dst).parent_path(), ec2);
		if (!move_dir(stash, dst)) return; // marker kept for a retry
	}
	fs::remove(k_marker, ec);
}
