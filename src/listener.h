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

#include <string>
#include <vector>

namespace adaapd {
	/*! Interface to be followed by Listener subscribers. */
	class Subscriber {
	public:
		/*! Called when a file is added or modified */
		virtual void Change(const std::string& path) = 0;
		/*! Called when a file is deleted or moved */
		virtual void Remove(const std::string& path) = 0;
	};

	/*! The listener waits for modifications to files within the given path,
	 * and notifies the subscriber of those changes. */
	class Listener {
	public:
		Listener(const std::string& path, Subscriber& subscriber, libev_loop& loop);
		virtual ~Listener();

	private:
		void cb_ready(libev_event* ev);

		Subscriber& subscriber;
	};
}

#endif
