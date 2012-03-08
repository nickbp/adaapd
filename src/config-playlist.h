#ifndef _adaapd_config_playlist_h_
#define _adaapd_config_playlist_h_

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
#include <list>
#include <memory>

namespace adaapd {
	/*! Representation of a playlist spec. */
	class Playlist {
	};
	typedef std::list<Playlist> playlists_t;

	class PlaylistFile;
	typedef std::shared_ptr<PlaylistFile> playlist_file_t;

	/*! Accesses playlist specs in a file. */
	class PlaylistFile {
	public:
		static playlist_file_t Create(std::string& path, Subscriber& subscriber, libev_loop& loop);

		virtual bool Read(playlists_t& list) = 0;
		virtual bool Write(const playlists_t& list) = 0;

	private:
		const std::string path;
		Listener listener;
		Subscriber& subscriber;
	};
}

#endif
