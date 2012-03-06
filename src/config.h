#ifndef _adaapd_config_h_
#define _adaapd_config_h_

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
#include <memory>

namespace adaapd {
	/*! Representation of a configuration. */
	class Config {
		//TODO...
	};

	class ConfigFile;
	typedef std::shared_ptr<ConfigFile> config_file_t;

	/*! Accesses configuration in a file. */
	class ConfigFile {
	public:
		static config_file_t Create(std::string& path);

		virtual bool Read(Config& config) = 0;
		virtual bool Write(const Config& config) = 0;
	};

}

#endif
