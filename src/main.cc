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

#include "listener.h"
#include "logging.h"

namespace sp = std::placeholders;

void announce(const std::string& path, adaapd::FILE_EVENT_TYPE type) {
	switch (type) {
	case adaapd::FILE_CREATED:
		ERR("NEW: %s", path.c_str());
		break;
	case adaapd::FILE_CHANGED:
		ERR("CHG: %s", path.c_str());
		break;
	case adaapd::FILE_REMOVED:
		ERR("REM: %s", path.c_str());
		break;
	default:
		ERR("???: %s", path.c_str());
		break;
	}
};

time_t lookup(const std::string& path) {
	ERR("LOOKUP %s", path.c_str());
	return 0;
}

int main(int argc, char* argv[]) {
	ev::default_loop loop;
	{
/*
		adaapd::Listener l(loop, "hey", std::bind(&announce, sp::_1, sp::_2),
				std::bind(&lookup, sp::_1));
		if (!l.Init()) {
			return EXIT_FAILURE;
		}
*/
		loop.run();
	}
	return EXIT_SUCCESS;
}
