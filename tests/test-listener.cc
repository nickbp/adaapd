/*
  adaapd - A DAAP daemon.
  Copyright (C) 2012  Nicholas Parker

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <dirent.h>

#include <queue>
#include <memory>

#include <gtest/gtest.h>
#include <listener.h>
#include <logging.h>

using namespace adaapd;
namespace sp = std::placeholders;

#ifdef _WIN32
#define SEP '\\'
#define SEP_STR "\\"
#else
#define SEP '/'
#define SEP_STR "/"
#endif

#define TEST_DIR "testfiles"

static inline std::string join(const std::string& dir, const std::string& file) {
	return dir + SEP_STR + file;
}

static void rm_all(const std::string& dirpath) {
	DIR* dirp = opendir(dirpath.c_str());
	if (dirp == NULL) {
		if (errno != ENOENT) {
			ERR("Couldn't open directory %s: %d/%s",
					dirpath.c_str(), errno, strerror(errno));
		}
		return;
	}

	struct dirent* ep;
	while ((ep = readdir(dirp)) != NULL) {
		/* ignore any files that start with "." */
		if (strncmp(ep->d_name,".",1) == 0) {
			continue;
		}
		/* add files/dirs, automatically recurse into dirs: */
		std::string filepath = join(dirpath, ep->d_name);
		struct stat sb;
		if (stat(filepath.c_str(), &sb) != 0) {
			ERR("Unable to stat file %s: %d/%s",
					filepath.c_str(), errno, strerror(errno));
			continue;
		}
		if (S_ISDIR(sb.st_mode)) {
			rm_all(filepath);
		} else if (S_ISREG(sb.st_mode) || S_ISLNK(sb.st_mode)) {
			ERR("unlink %s", filepath.c_str());
			if (unlink(filepath.c_str()) != 0) {
				ERR("Unable to unlink file %s: %d/%s",
						filepath.c_str(), errno, strerror(errno));
			}
		} else {
			ERR("Unsupported file mode %d: %s", sb.st_mode, filepath.c_str());
		}
	}
	closedir(dirp);
	ERR("rmdir %s", dirpath.c_str());
	rmdir(dirpath.c_str());
}

struct file_event {
	file_event(const std::string& path, adaapd::FILE_EVENT_TYPE type, bool dir)
		: path(path), type(type), dir(dir) { }
	std::string path;
	adaapd::FILE_EVENT_TYPE type;
	bool dir;
};

std::unique_ptr<adaapd::Listener> listener;

class ListenerTest : public testing::Test {
protected:
	virtual void SetUp() {
		rm_all(TEST_DIR);
		mkdir(TEST_DIR, 0755);

		adaapd::subscriber_t cb =
			std::bind(&ListenerTest::callback_event, this,
					sp::_1, sp::_2, sp::_3);

		listener.reset(new adaapd::Listener(&loop, TEST_DIR, cb));
		ASSERT_TRUE(listener->Init());

		timeout.set(loop);
		timeout.set<ListenerTest, &ListenerTest::callback_timeout>(this);
		timeout.start(3.0);

		runme.set(loop);
		runme.set<ListenerTest, &ListenerTest::callback_run>(this);
	}
	virtual void TearDown() {
		listener.reset();
		rm_all(TEST_DIR);
		loop.unloop();
	}

	void add_event(const std::string& path, adaapd::FILE_EVENT_TYPE type, bool dir) {
		/* defer until the loop is being executed */
		tosend.push(file_event(join(TEST_DIR, path), type, dir));
		if (tosend.size() == 1) {
			runme.start(0);
		}
		ERR("%lu added -> %s", tosend.size(), path.c_str());
	}

	ev::default_loop loop;
	ev::timer timeout, runme;

private:
	void callback_run() {
		file_event e = tosend.front();
		ERR("%lu left -> %s", tosend.size(), e.path.c_str());
		tosend.pop();
		switch (e.type) {
		case adaapd::FILE_CREATED:
			ERR("create %s", e.path.c_str());
			if (e.dir) {
				if (mkdir(e.path.c_str(), 0755) != 0) {
					EXPECT_TRUE(false) << "Failed to mkdir " << e.path << ": "
									   << errno << "/" << strerror(errno);
				}
			} else {
				FILE* f = fopen(e.path.c_str(), "w");
				if (f == NULL) {
					EXPECT_TRUE(false) << "Failed to fopen " << e.path << ": "
									   << errno << "/" << strerror(errno);
				} else {
					fclose(f);
				}
			}
			break;
		case adaapd::FILE_CHANGED:
			ERR("change %s", e.path.c_str());
			if (e.dir) {
				EXPECT_TRUE(false);
			} else {
				FILE* f = fopen(e.path.c_str(), "w");
				if (f == NULL) {
					EXPECT_TRUE(false) << "Failed to fopen " << e.path << ": "
									   << errno << "/" << strerror(errno);
				} else {
					char data[]{":)"};
					fwrite(data, strlen(data), 1, f);
					fclose(f);
				}
			}
			break;
		case adaapd::FILE_REMOVED:
			ERR("remove %s", e.path.c_str());
			if (e.dir) {
				if (rmdir(e.path.c_str()) != 0) {
					EXPECT_TRUE(false) << "Failed to rmdir " << e.path << ": "
									   << errno << "/" << strerror(errno);
				}
			} else {
				if (unlink(e.path.c_str()) != 0) {
					EXPECT_TRUE(false) << "Failed to unlink " << e.path << ": "
									   << errno << "/" << strerror(errno);
				}
			}
			break;
		}
		if (!e.dir) {
			/* add to expected responses for callback_event */
			torecv.push(e);
		}
		if (!tosend.empty()) {
			ERR_DIR("AGAIN");
			runme.again();
			runme.start(0);
		}
	}

	void callback_event(const std::string path, adaapd::FILE_EVENT_TYPE type, time_t mtime) {
		ERR("%s [mtime %ld, type %d]", path.c_str(), mtime, type);
		ASSERT_FALSE(torecv.empty());
		struct file_event event = torecv.front();
		torecv.pop();
		EXPECT_EQ(event.path, path);
		EXPECT_EQ(event.type, type);
		if (torecv.empty()) {
			ERR_DIR("KAY ITS TIME TO GO");
			loop.unloop();
		}
	}

	void callback_timeout(ev::timer&, int) {
		EXPECT_TRUE(false) << "timed out with "
						   << tosend.size() << "+" << torecv.size()
						   << " events left";
		while (!tosend.empty()) {
			tosend.pop();
		}
		while (!torecv.empty()) {
			torecv.pop();
		}
		loop.unloop();
	}

	std::queue<file_event> tosend, torecv;
};

TEST_F(ListenerTest, create_file) {
	add_event("hey", adaapd::FILE_CREATED, false);

	add_event("hey_dir", adaapd::FILE_CREATED, true);
	add_event("hey_dir/hey2", adaapd::FILE_CREATED, false);

	add_event("hey_dir/hey_dir", adaapd::FILE_CREATED, true);
	add_event("hey_dir/hey_dir/hey3", adaapd::FILE_CREATED, false);

	loop.run();
}

TEST_F(ListenerTest, touch_file) {
	add_event("hey", adaapd::FILE_CREATED, false);

	add_event("hey_dir", adaapd::FILE_CREATED, true);
	add_event("hey_dir/hey2", adaapd::FILE_CREATED, false);

	add_event("hey_dir/hey_dir", adaapd::FILE_CREATED, true);
	add_event("hey_dir/hey_dir/hey3", adaapd::FILE_CREATED, false);

	add_event("hey", adaapd::FILE_CHANGED, false);
	add_event("hey_dir/hey2", adaapd::FILE_CHANGED, false);
	add_event("hey_dir/hey_dir/hey3", adaapd::FILE_CHANGED, false);

	loop.run();
}

TEST_F(ListenerTest, delete_file) {
	add_event("hey", adaapd::FILE_CREATED, false);

	add_event("hey_dir", adaapd::FILE_CREATED, true);
	add_event("hey_dir/hey2", adaapd::FILE_CREATED, false);

	add_event("hey_dir/hey_dir", adaapd::FILE_CREATED, true);
	add_event("hey_dir/hey_dir/hey3", adaapd::FILE_CREATED, false);

	add_event("hey", adaapd::FILE_REMOVED, false);
	add_event("hey_dir/hey2", adaapd::FILE_REMOVED, false);
	add_event("hey_dir/hey_dir/hey3", adaapd::FILE_REMOVED, false);

	loop.run();
}

TEST_F(ListenerTest, create_dir) {
	//TODO same action on a dir and a symlink to a dir
	EXPECT_TRUE(false);
}

TEST_F(ListenerTest, delete_dir) {
	//TODO same action on a dir and a symlink to a dir
	EXPECT_TRUE(false);
}

TEST_F(ListenerTest, move_dir_in) {
	//TODO same action on a dir and a symlink to a dir
	EXPECT_TRUE(false);
}

TEST_F(ListenerTest, move_dir_out) {
	//TODO same action on a dir and a symlink to a dir
	EXPECT_TRUE(false);
}

TEST_F(ListenerTest, delete_root_dir) {
	//TODO same action on a dir and a symlink to a dir
	EXPECT_TRUE(false);
}

TEST_F(ListenerTest, move_root_dir) {
	//TODO same action on a dir and a symlink to a dir
	EXPECT_TRUE(false);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest( &argc, argv );
	return RUN_ALL_TESTS();
}
