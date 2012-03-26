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

#include <sys/inotify.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <assert.h>

#include <list>
#include <unordered_map>
#include <unordered_set>

#include "listener.h"
#include "logging.h"

namespace sp = std::placeholders;

#ifdef _WIN32
#define SEP '\\'
#define SEP_STR "\\"
#else
#define SEP '/'
#define SEP_STR "/"
#endif

/*! Splits a path into its components.
 * Ex 1: /path/to/file -> [path, to, file]
 * Ex 2: path/to/file -> [path, to, file]
 * Ex 3: file -> [file] */
/*
static void split(const std::string& path, std::vector<std::string>& out) {
	out.clear();
	if (path.empty()) {
		return;
	}
	char* path_cpy = (char*)malloc(path.size()+1);
	strncpy(path_cpy, path.c_str(), path.size());
	path_cpy[path.size()] = 0;// insert terminating char

	char* state;
	char* p = strtok_r(path_cpy, SEP_STR, &state);
	while (p != NULL) {
		out.push_back(p);
		p = strtok_r(NULL, SEP_STR, &state);
	}
	free(path_cpy);
}
*/

static inline std::string join(const std::string& dir,
		const std::string& file) {
	//ERR("%s + %s", dir.c_str(), file.c_str());
	return dir + SEP_STR + file;
}

#define INVALID_FD -1

#define WATCH_MODE_DIR IN_DELETE_SELF | IN_MOVED_TO | IN_CREATE | \
	IN_MOVED_FROM | IN_DELETE | IN_MODIFY
#define WATCH_MODE_FILE IN_DELETE_SELF | IN_MOVE_SELF | IN_MODIFY

namespace adaapd {
	/*! Represents a single directory in a tree. Keeps track of its files and
	 * subdirectories, signaling the callback appropriately when changes occur. */
	class dirnode {
	public:
		typedef std::list<std::pair<int, dirnode*> > dirlist_t;

		dirnode(int inotify_fd, subscriber_t cb, const std::string& full_path)
			: cb(cb), path(full_path), inotify_fd(inotify_fd), watch_fd(-1) {
			ERR("%s", full_path.c_str());
		}
		virtual ~dirnode() {
			if (watch_fd != -1) {
				ERR("WARNING: Deleting open watch %s!", path.c_str());
			}
		}

		/*! The full path to this directory. */
		const std::string& Path() const {
			return path;
		}

		/*! Initializes the watches for this directory and any subdirectories. */
		bool Init(int& watch_fd_, dirlist_t& added_subdirs) {
			if (!addAll(added_subdirs)) {
				return false;
			}
			watch_fd_ = watch_fd;
			return true;
		}

		/*! Closes the watches for this directory and any subdirectories. */
		void Close(dirlist_t& removed_subdirs) {
			removeAll(removed_subdirs, false);
		}

		/*! A file was added with a given mtime. Add to tracked list and notify
		 * the callback. */
		void AddFile(const std::string& filename, time_t mtime) {
			std::pair<files_t::const_iterator,bool> result =
				files.insert(filename);
			if (!result.second) {
				ERR("WARNING: %s is already tracking a dir named %s!",
						path.c_str(), filename.c_str());
				return;
			}
			cb(join(path, filename), FILE_CREATED, mtime);
		}

		/*! A file was added. Get its mtime then call AddFile(name, mtime). */
		void AddFile(const std::string& filename) {
			ERR("%s + %s", path.c_str(), filename.c_str());
			std::string filepath = join(path, filename);
			TYPE type;
			time_t mtime;
			if (!fileInfo(filepath, type, mtime)) {
				return;
			}
			if (type != FILE) {
				ERR("Expected file %s in %s, got %d", filename.c_str(), path.c_str(), type);
				return;
			}
			AddFile(filename, mtime);
		}

		/*! A file was moved or deleted. Remove from tracked list and notify the
		 * callback. */
		void RemoveFile(const std::string& filename) {
			ERR("%s + %s", path.c_str(), filename.c_str());
			if (files.erase(filename) == 0) {
				ERR("WARNING: %s told to remove untracked file %s!",
						path.c_str(), filename.c_str());
			}
			cb(join(path, filename), FILE_REMOVED, 0);
		}

		/*! A file was modified. Notify the callback with the new mtime. */
		void ChangeFile(const std::string& filename) {
			ERR("%s + %s", path.c_str(), filename.c_str());
			files_t::const_iterator iter = files.find(filename);
			if (iter == files.end()) {
				ERR("WARNING: %s told to change untracked file %s!",
						path.c_str(), filename.c_str());
				files.insert(filename);
			}
			std::string filepath = join(path, filename);
			TYPE type;
			time_t mtime;
			if (!fileInfo(filepath, type, mtime)) {
				return;
			}
			if (type != FILE) {
				ERR("Expected file %s in %s, got %d", filename.c_str(), path.c_str(), type);
				return;
			}
			cb(filepath, FILE_CHANGED, mtime);
		}

		/*! A directory was added. Recursively track its files/subdirectories,
		 * signalling the callback for each file. */
		void AddDir(const std::string& dirname, dirlist_t& added_subdirs) {
			ERR("%s + %s", path.c_str(), dirname.c_str());
			dirnode* new_node = new dirnode(inotify_fd, cb, join(path, dirname));
			std::pair<dirmap_t::const_iterator,bool> result =
				dirs.insert(std::make_pair(dirname, new_node));
			if (!result.second) {
				ERR("WARNING: %s is already tracking a dir named %s!",
						path.c_str(), dirname.c_str());
				delete new_node;
				return;
			}
			if (!new_node->addAll(added_subdirs)) {
				delete new_node;
				return;
			}
			added_subdirs.push_back(std::make_pair(new_node->watch_fd, new_node));
		}

		/*! A directory was moved or deleted. Recursively remove its
		 * files/subdirectores from the tracked list and signal the callback for
		 * each file. */
		void RemoveDir(const std::string& dirname, dirlist_t& removed_subdirs) {
			ERR("%s + %s", path.c_str(), dirname.c_str());
			dirmap_t::const_iterator iter = dirs.find(dirname);
			if (iter == dirs.end()) {
				ERR("WARNING: %s isn't tracking a dir named %s!",
						path.c_str(), dirname.c_str());
				return;
			}
			iter->second->removeAll(removed_subdirs, true);
			removed_subdirs.push_back(std::make_pair(iter->second->watch_fd, iter->second));
			//CALLER: remove/delete these dirs from your map, CLEAR IS ALREADY CALLED
		}

	private:
		typedef std::unordered_set<std::string> files_t;
		typedef std::unordered_map<std::string, dirnode*> dirmap_t;

		enum TYPE { FILE, DIRECTORY };
		bool fileInfo(const std::string& filepath, TYPE& type, time_t& mtime) {
			struct stat sb;
			if (stat(filepath.c_str(), &sb) != 0) {
				ERR("Unable to stat file %s: %d/%s",
						filepath.c_str(), errno, strerror(errno));
				return false;
			}
			mtime = sb.st_mtime;
			if (S_ISDIR(sb.st_mode)) {
				type = DIRECTORY;
			} else if (S_ISREG(sb.st_mode)) {
				type = FILE;
			} else if (S_ISLNK(sb.st_mode)) {
				ERR("TODO symlinks %s", filepath.c_str());
				return false;
			} else {
				ERR("Unsupported file mode %d: %s", sb.st_mode, filepath.c_str());
				return false;
			}
			return true;
		}

		/*! Add all entries within this directory, recursively calling
		 * AddDir/AddFile for each. */
		bool addAll(dirlist_t& added_subdirs) {
			DIR* dirp = opendir(path.c_str());
			if (dirp == NULL) {
				ERR("Couldn't open directory %s: %d/%s",
						path.c_str(), errno, strerror(errno));
				return false;
			}

			watch_fd = inotify_add_watch(inotify_fd, path.c_str(), WATCH_MODE_DIR);
			if (watch_fd == -1) {
				ERR("Couldn't add watch on %s: %d/%s",
						path.c_str(), errno, strerror(errno));
				return false;
			}

			TYPE file_type;
			time_t file_mtime;
			struct dirent* ep;
			while ((ep = readdir(dirp)) != NULL) {
				/* ignore any files that start with "." */
				if (strncmp(ep->d_name,".",1) == 0) {
					continue;
				}
				/* add files/dirs, automatically recurse into dirs: */
				ERR("%s + %s", path.c_str(), ep->d_name);
				if (!fileInfo(join(path, ep->d_name), file_type, file_mtime)) {
					continue;/* keep going */
				}
				switch (file_type) {
				case DIRECTORY:
					AddDir(ep->d_name, added_subdirs);
					break;
				case FILE:
					AddFile(ep->d_name, file_mtime);
					break;
				}
			}
			closedir(dirp);
			return true;
		}

		/*! Remove all entries within this directory, recursively calling
		 * removeAll for each subdirectory. */
		void removeAll(dirlist_t& removed_subdirs, bool notify) {
			if (notify) {
				for (files_t::const_iterator iter = files.begin();
					 iter != files.end(); ++iter) {
					cb(join(path, *iter), FILE_REMOVED, 0);
				}
			}
			files.clear();

			for (dirmap_t::const_iterator iter = dirs.begin();
				 iter != dirs.end(); ++iter) {
				removed_subdirs.push_back(std::make_pair(iter->second->watch_fd, iter->second));
				iter->second->removeAll(removed_subdirs, notify);
			}
			dirs.clear();

			if (watch_fd != -1) {
				if (inotify_rm_watch(inotify_fd, watch_fd) != 0) {
					ERR("Couldn't remove watch on %s/%d: %d/%s",
							path.c_str(), watch_fd, errno, strerror(errno));
				}
				watch_fd = -1;
			}
		}

		const subscriber_t cb;
		const std::string path;
		files_t files;
		dirmap_t dirs;
		int inotify_fd, watch_fd;
	};

	/*! A map which contains all current dirnodes. */
	class dir_tree {
	public:
		dir_tree() : root_watch_fd(INVALID_FD) { }
		virtual ~dir_tree() {
			if (root_watch_fd == INVALID_FD) {
				return;
			}
			dirnode* dir = find(root_watch_fd);
			if (dir == NULL) {
				return;
			}
			dirnode::dirlist_t subdirs;
			dir->Close(subdirs);
			if (subdirs.size()+1 != dirs.size()) {
				ERR("Got %lu subdirs, expected %lu",
						subdirs.size(), dirs.size()-1);
			}
			subdirs.clear();
			for (dirs_t::const_iterator iter = dirs.begin();
				 iter != dirs.end(); ++iter) {
				delete iter->second;
			}
			dirs.clear();
		}

		bool Init(int inotify_fd, subscriber_t cb,
				const std::string& root_path) {
			dirnode* dir = new dirnode(inotify_fd, cb, root_path);
			dirnode::dirlist_t subdirs;
			if (!dir->Init(root_watch_fd, subdirs)) {
				return false;
			}
			dirs.insert(std::make_pair(root_watch_fd, dir));
			dirs.insert(subdirs.begin(), subdirs.end());
			return true;
		}

		void AddFile(int watch_fd, const std::string& filename) {
			dirnode* dir = find(watch_fd);
			if (dir == NULL) { return; }
			dir->AddFile(filename);
		}

		void RemoveFile(int watch_fd, const std::string& filename) {
			dirnode* dir = find(watch_fd);
			if (dir == NULL) { return; }
			dir->RemoveFile(filename);
		}

		void ChangeFile(int watch_fd, const std::string& filename) {
			dirnode* dir = find(watch_fd);
			if (dir == NULL) { return; }
			dir->ChangeFile(filename);
		}

		void AddDir(int watch_fd, const std::string& dirname) {
			dirnode* dir = find(watch_fd);
			if (dir == NULL) { return; }
			dirnode::dirlist_t addme;
			dir->AddDir(dirname, addme);
			for (dirnode::dirlist_t::const_iterator iter = addme.begin();
				 iter != addme.end(); ++iter) {
				std::pair<dirs_t::const_iterator,bool> result =
					dirs.insert(*iter);
				if (!result.second) {
					ERR("WARNING: Told to add already-present map entry %d->%s",
							iter->first, iter->second->Path().c_str());
				}
			}
		}

		void RemoveDir(int watch_fd, const std::string& dirname) {
			dirnode* dir = find(watch_fd);
			if (dir == NULL) { return; }
			dirnode::dirlist_t delme;
			dir->AddDir(dirname, delme);
			for (dirnode::dirlist_t::const_iterator iter = delme.begin();
				 iter != delme.end(); ++iter) {
				if (dirs.erase(iter->first) == 0) {
					ERR("WARNING: Told to remove non-existent map entry %d->%s",
							iter->first, iter->second->Path().c_str());
				}
				delete iter->second;
			}
		}

	private:
		dirnode* find(int watch_fd) {
			dirs_t::const_iterator iter = dirs.find(watch_fd);
			if (iter == dirs.end()) {
				ERR("Unable to find id %d", watch_fd);
				return NULL;
			}
			return iter->second;
		}

		typedef std::unordered_map<int, dirnode*> dirs_t;
		dirs_t dirs;
		int root_watch_fd;
	};
}

// LISTENER

#define INOTIFY_BUF_LEN (1024 * (sizeof(struct inotify_event) + 16))

adaapd::Listener::Listener(ev::default_loop* loop, const std::string& root,
		subscriber_t subscriber)
	: root(root), subscriber(subscriber),
	  loop(loop), inotify_fd(INVALID_FD), inotify_buf(NULL), tree(NULL) { }

adaapd::Listener::~Listener() {
	if (inotify_fd != INVALID_FD) {
		io.stop();
		close(inotify_fd);
		inotify_fd = INVALID_FD;
	}

	if (inotify_buf != NULL) {
		free(inotify_buf);
		inotify_buf = NULL;
	}

	if (tree != NULL) {
		delete tree;
		tree = NULL;
	}
}

bool adaapd::Listener::Init() {
	inotify_fd = inotify_init();
	if (inotify_fd == INVALID_FD) {
		ERR("Unable to init inotify_fd: %d/%s", errno, strerror(errno));
		return false;
	}

	inotify_buf = (char*)malloc(INOTIFY_BUF_LEN);
	if (inotify_buf == NULL) {
		ERR("Failed to malloc %db inotify buffer.", INOTIFY_BUF_LEN);
		return false;
	}

	tree = new dir_tree;
	if (!tree->Init(inotify_fd, subscriber, root)) {
		return false;
	}

	io.set<Listener, &Listener::cb_ready>(this);
	io.start(inotify_fd, ev::READ);
	return true;
}

void adaapd::Listener::cb_ready(ev::io& /*io*/, int revents) {
	ERR("HEY! READ FROM FD %d", inotify_fd);

	ssize_t len = read(inotify_fd, inotify_buf, INOTIFY_BUF_LEN);
	if (len < 0) {
		ERR("Failed to read from inotify file %d: %d/%s",
				inotify_fd, errno, strerror(errno));
		return;
	}
	ERR("Got %ld bytes...", len);

	ssize_t i = 0;
	while (i < len) {
		struct inotify_event* event = (struct inotify_event*)&inotify_buf[i];
		handle_event(event);
		i += (sizeof(struct inotify_event) + event->len);
	}
}

void adaapd::Listener::handle_event(struct inotify_event* event) {
	ERR("%s -> %d", event->name, event->mask);
	uint32_t mask = event->mask;
	if ((mask & IN_ISDIR) != 0) {
		/* It's a directory */
		if ((mask & IN_DELETE_SELF) != 0) {
			/* note: any children were already deleted
			 * also, DONT remove watch, it's already done for us! TODO is this still the case?? */
			LOG("auto-unwatched dir %s", event->name);
		} else if ((mask & (IN_DELETE | IN_MOVED_FROM)) != 0) {
			LOG("unwatch deled/moved dir %s", event->name);
			tree->RemoveDir(event->wd, event->name);
		} else if ((mask & (IN_MOVED_TO | IN_CREATE)) != 0) {
			LOG("watch new dir %s", event->name);
			tree->AddDir(event->wd, event->name);
		} else {
			ERR("Unknown directory event code: %d", mask);
		}
	} else {
		/* It's a file */
		/* sendme := new(ListenEvent)
		   sendme.Path = ev.Name; */
		if ((mask & IN_DELETE_SELF) != 0) {
			/* for some reason this is hit for deleted directories (with ISDIR off!) */
			LOG("delete self %s", event->name);
		} else if ((mask & (IN_MOVED_FROM | IN_DELETE)) != 0) {
			LOG("deleted file %s", event->name);
			tree->RemoveFile(event->wd, event->name);
		} else if ((mask & (IN_MOVED_TO | IN_CREATE)) != 0) {
			LOG("created file %s", event->name);
			tree->AddFile(event->wd, event->name);
		} else if ((mask & IN_MODIFY) != 0) {
			LOG("modified file %s", event->name);
			tree->ChangeFile(event->wd, event->name);
		} else {
			ERR("Unknown file event code: %d", mask);
		}
	}
}
