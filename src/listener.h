#ifndef _adaapd_listener_h_
#define _adaapd_listener_h_

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

#include <functional>
#include <string>

#include <ev++.h>

namespace adaapd {
	enum FILE_EVENT_TYPE {
		FILE_CREATED,/* file is new or moved in */
		FILE_CHANGED,/* existing file is modified */
		FILE_REMOVED/* file is deleted or moved away */
	};

	/*! Called when a change occurs within the Listener's path. */
	typedef std::function<void(const std::string& path, FILE_EVENT_TYPE type, time_t mtime)> subscriber_t;

	/*! The listener waits for modifications to files within the given root path,
	 * and notifies the subscriber of those changes. */
	class dir_tree;
	class Listener {
	public:
		Listener(ev::default_loop* loop, const std::string& root,
				subscriber_t subscriber);
		virtual ~Listener();

		bool Init();

	private:
		void cb_ready(ev::io &io, int revents);
		void handle_event(struct inotify_event* event);

		const std::string root;
		const subscriber_t subscriber;

		ev::io io;
		ev::default_loop* loop;
		int inotify_fd;
		char* inotify_buf;
		dir_tree* tree;
	};
}

#endif
