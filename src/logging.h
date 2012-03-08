#ifndef _adaapd_logging_h_
#define _adaapd_logging_h_

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

#include <stdio.h>

#define _PRINT_PREFIX "%s %s  "
#define _PRINT_ARGS __FUNCTION__

/* Some simple print helpers */

/* Format str and file/line prefix */
#define DEBUG(format, ...) logging::_debug(_PRINT_PREFIX format, "DEBUG", _PRINT_ARGS, __VA_ARGS__)
#define LOG(format, ...) logging::_log(_PRINT_PREFIX format, "LOG", _PRINT_ARGS, __VA_ARGS__)
#define ERR(format, ...) logging::_error(_PRINT_PREFIX format, "ERR", _PRINT_ARGS, __VA_ARGS__)

/* No file/line prefix ("RAW") */
#define DEBUG_RAW(format, ...) logging::_debug(format, __VA_ARGS__)
#define LOG_RAW(format, ...) logging::_log(format, __VA_ARGS__)
#define ERR_RAW(format, ...) logging::_error(format, __VA_ARGS__)

/* No format str ("Direct" -> "DIR") */
#define DEBUG_DIR(...) logging::_debug(_PRINT_PREFIX "%s", "DEBUG", _PRINT_ARGS, __VA_ARGS__)
#define LOG_DIR(...) logging::_log(_PRINT_PREFIX "%s", "LOG", _PRINT_ARGS, __VA_ARGS__)
#define ERR_DIR(...) logging::_error(_PRINT_PREFIX "%s", "ERR", _PRINT_ARGS, __VA_ARGS__)

/* No format str and no file/line prefix */
#define DEBUG_RAWDIR(...) logging::_debug(__VA_ARGS__)
#define LOG_RAWDIR(...) logging::_log(__VA_ARGS__)
#define ERR_RAWDIR(...) logging::_error(__VA_ARGS__)

/* "log"'s taken by the math func */
namespace logging {
	/* configuration */
	extern FILE *fout;
	extern FILE *ferr;
	extern bool debug_enabled;

	/* DONT USE THESE, use DEBUG()/LOG()/ERR() instead: */
	void _debug(const char* format, ...);
	void _log(const char* format, ...);
	void _error(const char* format, ...);
}

#endif
