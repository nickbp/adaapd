#ifndef MARKY_CONFIG_H
#define MARKY_CONFIG_H

/*
  marky - A Markov chain generator.
  Copyright (C) 2011-2012  Nicholas Parker

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

#include <stdio.h>

#define _PRINT_PREFIX "%s (%s:%d) "
#define _PRINT_ARGS __FUNCTION__, __LINE__

/* Some simple print helpers */

/* Format str and file/line prefix */
#define DEBUG(format, ...) config::_debug(_PRINT_PREFIX format, "DEBUG", _PRINT_ARGS, __VA_ARGS__)
#define LOG(format, ...) config::_log(_PRINT_PREFIX format, "LOG", _PRINT_ARGS, __VA_ARGS__)
#define ERROR(format, ...) config::_error(_PRINT_PREFIX format, "ERR", _PRINT_ARGS, __VA_ARGS__)

/* No file/line prefix ("RAW") */
#define DEBUG_RAW(format, ...) config::_debug(format, __VA_ARGS__)
#define LOG_RAW(format, ...) config::_log(format, __VA_ARGS__)
#define ERROR_RAW(format, ...) config::_error(format, __VA_ARGS__)

/* No format str ("Direct" -> "DIR") */
#define DEBUG_DIR(...) config::_debug(_PRINT_PREFIX "%s", "DEBUG", _PRINT_ARGS, __VA_ARGS__)
#define LOG_DIR(...) config::_log(_PRINT_PREFIX "%s", "LOG", _PRINT_ARGS, __VA_ARGS__)
#define ERROR_DIR(...) config::_error(_PRINT_PREFIX "%s", "ERR", _PRINT_ARGS, __VA_ARGS__)

/* No format str and no file/line prefix */
#define DEBUG_RAWDIR(...) config::_debug(__VA_ARGS__)
#define LOG_RAWDIR(...) config::_log(__VA_ARGS__)
#define ERROR_RAWDIR(...) config::_error(__VA_ARGS__)

#cmakedefine BUILD_BACKEND_SQLITE

namespace config {
	static const int
		VERSION_MAJOR = @marky_VERSION_MAJOR@,
		VERSION_MINOR = @marky_VERSION_MINOR@,
		VERSION_PATCH = @marky_VERSION_PATCH@;


	static const char VERSION_STRING[] = "@marky_VERSION_MAJOR@.@marky_VERSION_MINOR@.@marky_VERSION_PATCH@"
#ifdef BUILD_BACKEND_SQLITE
		"-sqlite"
#else
		"-nodb"
#endif
		;
	static const char BUILD_DATE[] = __TIMESTAMP__;

	extern bool debug_enabled;
	extern FILE *fout;
	extern FILE *ferr;

	/* DONT USE THESE, use DEBUG()/LOG()/ERROR() instead. */
	void _debug(const char* format, ...);
	void _log(const char* format, ...);
	void _error(const char* format, ...);
}

#endif
