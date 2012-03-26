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
#include <list>
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
			if (unlink(filepath.c_str()) != 0) {
				ERR("Unable to unlink file %s: %d/%s",
						filepath.c_str(), errno, strerror(errno));
			}
		} else {
			ERR("Unsupported file mode %d: %s", sb.st_mode, filepath.c_str());
		}
	}
	closedir(dirp);
	rmdir(dirpath.c_str());
}

struct file_event {
	file_event(const std::string& path, adaapd::FILE_EVENT_TYPE type, bool dir)
		: path(path), type(type), dir(dir) { }
	file_event(const std::string& path, const std::string& path2)
		: path(path), path2(path2) { }
	std::string path, path2;
	adaapd::FILE_EVENT_TYPE type;
	bool dir;
	std::list<file_event> recvs;
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

	void add_event(const file_event& e) {
		tosend.push(e);
		if (tosend.size() == 1) {
			runme.start(0);
		}
	}

	ev::default_loop loop;
	ev::timer timeout, runme;

private:
	void callback_run() {
		file_event e = tosend.front();
		tosend.pop();
		if (e.path2.empty()) {
			switch (e.type) {
			case adaapd::FILE_CREATED:
				LOG("create %s", e.path.c_str());
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
				LOG("change %s", e.path.c_str());
				if (e.dir) {
					EXPECT_TRUE(false);
				} else {
					FILE* f = fopen(e.path.c_str(), "a");
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
				LOG("remove %s", e.path.c_str());
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
		} else {
			LOG("move %s -> %s", e.path.c_str(), e.path2.c_str());
			if (rename(e.path.c_str(), e.path2.c_str()) != 0) {
				EXPECT_TRUE(false) << "Failed to move "
								   << e.path << " -> " << e.path2 << ": "
								   << errno << "/" << strerror(errno);
			}
			for (std::list<file_event>::const_iterator iter = e.recvs.begin();
				 iter != e.recvs.end(); ++iter) {
				torecv.push(*iter);
			}
		}
		if (!tosend.empty()) {
			runme.again();
			runme.start(0);
		}
	}

	void callback_event(const std::string path, adaapd::FILE_EVENT_TYPE type, time_t mtime) {
		LOG("%s [mtime %ld, type %d]", path.c_str(), mtime, type);
		ASSERT_FALSE(torecv.empty());
		struct file_event event = torecv.front();
		torecv.pop();
		EXPECT_EQ(event.path, path);
		EXPECT_EQ(event.type, type);
		if (tosend.empty() && torecv.empty()) {
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
	add_event(file_event(join(TEST_DIR, "hey"), adaapd::FILE_CREATED, false));

	add_event(file_event(join(TEST_DIR, "hey_dir"), adaapd::FILE_CREATED, true));
	add_event(file_event(join(TEST_DIR, "hey_dir/hey2"), adaapd::FILE_CREATED, false));

	add_event(file_event(join(TEST_DIR, "hey_dir/hey_dir"), adaapd::FILE_CREATED, true));
	add_event(file_event(join(TEST_DIR, "hey_dir/hey_dir/hey3"), adaapd::FILE_CREATED, false));

	loop.run();
}

TEST_F(ListenerTest, touch_file) {
	add_event(file_event(join(TEST_DIR, "hey"), adaapd::FILE_CREATED, false));

	add_event(file_event(join(TEST_DIR, "hey_dir"), adaapd::FILE_CREATED, true));
	add_event(file_event(join(TEST_DIR, "hey_dir/hey2"), adaapd::FILE_CREATED, false));

	add_event(file_event(join(TEST_DIR, "hey_dir/hey_dir"), adaapd::FILE_CREATED, true));
	add_event(file_event(join(TEST_DIR, "hey_dir/hey_dir/hey3"), adaapd::FILE_CREATED, false));

	add_event(file_event(join(TEST_DIR, "hey"), adaapd::FILE_CHANGED, false));
	add_event(file_event(join(TEST_DIR, "hey_dir/hey2"), adaapd::FILE_CHANGED, false));
	add_event(file_event(join(TEST_DIR, "hey_dir/hey_dir/hey3"), adaapd::FILE_CHANGED, false));

	loop.run();
}

TEST_F(ListenerTest, delete_file) {
	add_event(file_event(join(TEST_DIR, "hey"), adaapd::FILE_CREATED, false));

	add_event(file_event(join(TEST_DIR, "hey_dir"), adaapd::FILE_CREATED, true));
	add_event(file_event(join(TEST_DIR, "hey_dir/hey2"), adaapd::FILE_CREATED, false));

	add_event(file_event(join(TEST_DIR, "hey_dir/hey_dir"), adaapd::FILE_CREATED, true));
	add_event(file_event(join(TEST_DIR, "hey_dir/hey_dir/hey3"), adaapd::FILE_CREATED, false));

	add_event(file_event(join(TEST_DIR, "hey_dir/hey2"), adaapd::FILE_REMOVED, false));
	add_event(file_event(join(TEST_DIR, "hey_dir/hey_dir/hey3"), adaapd::FILE_REMOVED, false));

	add_event(file_event(join(TEST_DIR, "hey_dir/hey_dir"), adaapd::FILE_REMOVED, true));
	add_event(file_event(join(TEST_DIR, "hey_dir"), adaapd::FILE_REMOVED, true));

	/* have a file event last, so that callback_event is notified when we're done */
	add_event(file_event(join(TEST_DIR, "hey"), adaapd::FILE_REMOVED, false));

	loop.run();
}

TEST_F(ListenerTest, move) {
	add_event(file_event(join(TEST_DIR, "hey_dir"), adaapd::FILE_CREATED, true));
	add_event(file_event(join(TEST_DIR, "hey_dir/hey2"), adaapd::FILE_CREATED, false));

	add_event(file_event(join(TEST_DIR, "hey_dir/hey_dir"), adaapd::FILE_CREATED, true));
	add_event(file_event(join(TEST_DIR, "hey_dir/hey_dir/hey3"), adaapd::FILE_CREATED, false));

	add_event(file_event(join(TEST_DIR, "hey"), adaapd::FILE_CREATED, false));

	{
		file_event e(join(TEST_DIR, "hey_dir/hey_dir/hey3"),
				join(TEST_DIR, "hey3"));
		e.recvs.push_back(file_event(join(TEST_DIR, "hey_dir/hey_dir/hey3"), adaapd::FILE_REMOVED, false));
		e.recvs.push_back(file_event(join(TEST_DIR, "hey3"), adaapd::FILE_CREATED, false));
		add_event(e);
	}

	{
		file_event e(join(TEST_DIR, "hey_dir"),
				join(TEST_DIR, "hey_dir2"));
		e.recvs.push_back(file_event(join(TEST_DIR, "hey_dir/hey2"), adaapd::FILE_REMOVED, false));
		e.recvs.push_back(file_event(join(TEST_DIR, "hey_dir2/hey2"), adaapd::FILE_CREATED, false));
		add_event(e);
	}

	loop.run();
}

TEST_F(ListenerTest, create_symlink) {
	//TODO same action on a dir and a symlink to a dir
	EXPECT_TRUE(false);
}

TEST_F(ListenerTest, delete_symlink) {
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
