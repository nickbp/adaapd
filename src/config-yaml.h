#ifndef _adaapd_config_yaml_h_
#define _adaapd_config_yaml_h_

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

#include "config-playlist.h"
#include "config-file.h"

namespace adaapd {
	/*! Accesses configuration in a file. */
	class ConfigFile_Yaml : public ConfigFile {
	public:
		bool Read(Config& config);
		bool Write(const Config& config);
	};

	/*! Accesses playlist specs in a file. */
	class PlaylistFile_Yaml : public PlaylistFile {
	public:
		bool Read(playlists_t& list);
		bool Write(const playlists_t& list);
	};
}

#endif
