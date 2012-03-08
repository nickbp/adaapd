#ifndef _adaapd_tag_h_
#define _adaapd_tag_h_

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
	enum Tag_IntId {
		BPM,//asbt: short
		BIT_RATE,//asbr: short kbit (256)
		COMPILATION,//asco: byte true/false
		DISC_COUNT,//asdc: short
		DISC_NUMBER,//asdn: short
		RELATIVE_VOLUME,//asrv: byte
		SAMPLE_RATE,//assr: int hz (44100)
		SIZE,//assz int size bytes
		TIME,//astm: int ms
		TRACK_COUNT,//astc: short
		TRACK_NUMBER,//astn: short
		USER_RATING,//asur: byte 20=1star, 40=2, 60=3, 80=4, 100=5
		YEAR//asyr: short
	};

	enum Tag_StrId {
		ALBUM,//asal
		ARTIST,//asar
		COMMENT,//ascm (from file, eg encoder name)
		COMPOSER,//ascp
		GENRE,//asgn
		TITLE//minm
	};

	/* db fields (wouldnt be in file's tag):
	   DISABLED asdb: byte true/false
	   ITEM_ID miid: int arbitrary unique id?
	   ITEM_KIND mikd: byte 2=music
	   PERSISTENT_ID mper: long arbitrary unique id?
	   DATE_ADDED asda: date
	   DATE_MODIFIED asdm: date
	   DATA_URL asul: string

	   maybe these appear in tags?:
	   EQ_PRESET aseq: string ??
	   RELATIVE_VOLUME asrv: byte ??
	   NORM_VOLUME aeNV: int ??

	   derived from the filename:
	   DATA_KIND,//asdk: byte ??
	   DESCRIPTION,//asdt eg "MPEG audio file"
	   FORMAT,//asfm eg "mp3"

	   customized by the user?:
	   START_TIME,//asst: int ms
	   STOP_TIME,//assp: int ms
	*/

	typedef int64_t tag_int_t;
	typedef std::string tag_str_t;

	class Tag;
	typedef std::shared_ptr<Tag> tag_t;

	class Tag {
	public:
		static tag_t Create(const std::string& path);

		virtual ~Tag() { }

		virtual bool Value(Tag_IntId id, tag_int_t& val) = 0;
		virtual bool Value(Tag_StrId id, tag_str_t& val) = 0;
	};
}

#endif
