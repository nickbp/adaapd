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

#include "tag.h"
#include "log.h"

#include <taglib/taglib.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>

#include <taglib/mpegfile.h>
#include <taglib/vorbisfile.h>
#include <taglib/oggflacfile.h>
#include <taglib/speexfile.h>
#include <taglib/flacfile.h>
#include <taglib/mpcfile.h>
#include <taglib/wavpackfile.h>
#include <taglib/trueaudiofile.h>
#include <taglib/mp4file.h>
#include <taglib/asffile.h>
#include <taglib/aifffile.h>
#include <taglib/wavfile.h>
#include <taglib/apefile.h>

#include <taglib/apetag.h>
#include <taglib/id3v1tag.h>
#include <taglib/id3v2tag.h>
#include <taglib/popularimeterframe.h>
#include <taglib/xiphcomment.h>

#include <taglib/tmap.h>
#include <taglib/tlist.h>

#include <sstream>
#include <queue>

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

namespace {
	bool string_starts_with(const std::wstring& haystack, const std::wstring& needle) {
		if (haystack.size() < needle.size()) {
			return false;
		}
		const std::wstring& ending = haystack.substr(0, needle.size());
		return ending.compare(needle) == 0;
	}

	bool xiph_rating(TagLib::Ogg::XiphComment* xiphcomment, adaapd::tag_int_t& out) {
		TagLib::Ogg::FieldListMap map = xiphcomment->fieldListMap();
		for (TagLib::Ogg::FieldListMap::Iterator
				 it = map.begin(); it != map.end(); ++it) {
			std::wstring key = (*it).first.toWString();
			if (!string_starts_with(key,L"RATING:")) {
				continue;
			}
			const TagLib::List<TagLib::String>& val = (*it).second;
			if (val.size() != 1) {
				continue;
			}
			//expecting [0.0, 0.2, 0.4, 0.6, 0.8, 1.0]
			const TagLib::String& floatstr = val[0];
			double ogg_rating;
			std::istringstream stream(floatstr.toCString());
			stream >> ogg_rating;
			if (stream.fail()) {
				continue;
			}
			if (ogg_rating == 0.5) {//unrated
				return false;
			}
			out = ogg_rating * 100;// 0.0-1.0 -> 0-100
			return true;
		}
		return false;
	}

	bool id3v2_rating(TagLib::ID3v2::Tag* id3v2tag, adaapd::tag_int_t& out) {
		const TagLib::ID3v2::FrameListMap& map = id3v2tag->frameListMap();
		TagLib::ID3v2::FrameListMap::ConstIterator iter = map.find("POPM");
		if (iter == map.end()) {
			return false;
		}
		const TagLib::ID3v2::Frame* first = *iter->second.begin();
		const TagLib::ID3v2::PopularimeterFrame* popmframe =
			static_cast<const TagLib::ID3v2::PopularimeterFrame*>(first);
		int popm_rating = popmframe->rating();
		if (popm_rating == 0) {//unrated
			return false;
		}
		double d = (100 * (popm_rating - 1)) / 254.;// 1-255 -> 0.0-100.0
		out = d + 0.5;// round to nearest int
		return true;
	}

	inline bool file_int(TagLib::File* file, adaapd::Tag_IntId id, adaapd::tag_int_t& out) {
		TagLib::AudioProperties* prop = file->audioProperties();
		switch (id) {
		case adaapd::BIT_RATE:
			if (prop) {
				out = prop->bitrate();//kbit
				return true;
			}
			return false;
		case adaapd::SAMPLE_RATE:
			if (prop) {
				out = prop->sampleRate();//Hz
				return true;
			}
			return false;
		case adaapd::SIZE:
			out = file->length();
			return true;
		case adaapd::TIME:
			if (prop) {
				out = prop->length() * (adaapd::tag_int_t)1000;//s -> ms
				return true;
			}
			return false;
		default:
			ERR("INTERNAL ERROR: Bad id %d!", id);
			break;
		}
		return false;
	}

	inline bool tag_int(TagLib::Tag* tag, adaapd::Tag_IntId id, adaapd::tag_int_t& out) {
		switch (id) {
		case adaapd::TRACK_NUMBER:
			out = tag->track();
			break;
		case adaapd::YEAR:
			out = tag->year();
			break;
		default:
			ERR("INTERNAL ERROR: Bad id %d!", id);
			return false;
		}
		return out != 0;
	}

	inline bool ape_int(TagLib::File* file, TagLib::APE::Tag* tag,
			adaapd::Tag_IntId id, adaapd::tag_int_t& out) {
		switch (id) {
		case adaapd::BIT_RATE:
		case adaapd::SAMPLE_RATE:
		case adaapd::SIZE:
		case adaapd::TIME:
			return file_int(file, id, out);
		case adaapd::TRACK_NUMBER:
		case adaapd::YEAR:
			return tag_int(tag, id, out);
		case adaapd::BPM:
		case adaapd::COMPILATION:
		case adaapd::DATA_KIND:
		case adaapd::DISC_COUNT:
		case adaapd::DISC_NUMBER:
		case adaapd::RELATIVE_VOLUME:
		case adaapd::START_TIME:
		case adaapd::STOP_TIME:
		case adaapd::TRACK_COUNT:
		case adaapd::USER_RATING:
			return false;//TODO
		}
		return false;
	}

	inline bool asf_int(TagLib::File* file, TagLib::ASF::Tag* tag,
			adaapd::Tag_IntId id, adaapd::tag_int_t& out) {
		switch (id) {
		case adaapd::BIT_RATE:
		case adaapd::SAMPLE_RATE:
		case adaapd::SIZE:
		case adaapd::TIME:
			return file_int(file, id, out);
		case adaapd::TRACK_NUMBER:
		case adaapd::YEAR:
			return tag_int(tag, id, out);
		case adaapd::BPM:
		case adaapd::COMPILATION:
		case adaapd::DATA_KIND:
		case adaapd::DISC_COUNT:
		case adaapd::DISC_NUMBER:
		case adaapd::RELATIVE_VOLUME:
		case adaapd::START_TIME:
		case adaapd::STOP_TIME:
		case adaapd::TRACK_COUNT:
		case adaapd::USER_RATING:
			return false;//TODO
		}
		return false;
	}

	inline bool id3v1_int(TagLib::File* file, TagLib::ID3v1::Tag* tag,
			adaapd::Tag_IntId id, adaapd::tag_int_t& out) {
		switch (id) {
		case adaapd::BIT_RATE:
		case adaapd::SAMPLE_RATE:
		case adaapd::SIZE:
		case adaapd::TIME:
			return file_int(file, id, out);
		case adaapd::TRACK_NUMBER:
		case adaapd::YEAR:
			return tag_int(tag, id, out);
		case adaapd::BPM:
		case adaapd::COMPILATION:
		case adaapd::DATA_KIND:
		case adaapd::DISC_COUNT:
		case adaapd::DISC_NUMBER:
		case adaapd::RELATIVE_VOLUME:
		case adaapd::START_TIME:
		case adaapd::STOP_TIME:
		case adaapd::TRACK_COUNT:
		case adaapd::USER_RATING:
			return false;//TODO
		}
		return false;
	}

	inline bool id3v2_int(TagLib::File* file, TagLib::ID3v2::Tag* tag,
			adaapd::Tag_IntId id, adaapd::tag_int_t& out) {
		switch (id) {
		case adaapd::BIT_RATE:
		case adaapd::SAMPLE_RATE:
		case adaapd::SIZE:
		case adaapd::TIME:
			return file_int(file, id, out);
		case adaapd::TRACK_NUMBER:
		case adaapd::YEAR:
			return tag_int(tag, id, out);
		case adaapd::BPM:
		case adaapd::COMPILATION:
		case adaapd::DATA_KIND:
		case adaapd::DISC_COUNT:
		case adaapd::DISC_NUMBER:
		case adaapd::RELATIVE_VOLUME:
		case adaapd::START_TIME:
		case adaapd::STOP_TIME:
		case adaapd::TRACK_COUNT:
			return false;//TODO
		case adaapd::USER_RATING:
			return id3v2_rating(tag, out);
		}
		return false;
	}

	inline bool mp4_int(TagLib::File* file, TagLib::MP4::Tag* tag,
			adaapd::Tag_IntId id, adaapd::tag_int_t& out) {
		switch (id) {
		case adaapd::BIT_RATE:
		case adaapd::SAMPLE_RATE:
		case adaapd::SIZE:
		case adaapd::TIME:
			return file_int(file, id, out);
		case adaapd::TRACK_NUMBER:
		case adaapd::YEAR:
			return tag_int(tag, id, out);
		case adaapd::BPM:
		case adaapd::COMPILATION:
		case adaapd::DATA_KIND:
		case adaapd::DISC_COUNT:
		case adaapd::DISC_NUMBER:
		case adaapd::RELATIVE_VOLUME:
		case adaapd::START_TIME:
		case adaapd::STOP_TIME:
		case adaapd::TRACK_COUNT:
		case adaapd::USER_RATING:
			return false;//TODO
		}
		return false;
	}

	inline bool xiph_int(TagLib::File* file, TagLib::Ogg::XiphComment* tag,
			adaapd::Tag_IntId id, adaapd::tag_int_t& out) {
		switch (id) {
		case adaapd::BIT_RATE:
		case adaapd::SAMPLE_RATE:
		case adaapd::SIZE:
		case adaapd::TIME:
			return file_int(file, id, out);
		case adaapd::TRACK_NUMBER:
		case adaapd::YEAR:
			return tag_int(tag, id, out);
		case adaapd::BPM:
		case adaapd::COMPILATION:
		case adaapd::DATA_KIND:
		case adaapd::DISC_COUNT:
		case adaapd::DISC_NUMBER:
		case adaapd::RELATIVE_VOLUME:
		case adaapd::START_TIME:
		case adaapd::STOP_TIME:
		case adaapd::TRACK_COUNT:
			return false;//TODO
		case adaapd::USER_RATING:
			return xiph_rating(tag, out);
		}
		return false;
	}

	//---

	inline bool tag_str(TagLib::Tag* tag, adaapd::Tag_StrId id,
			adaapd::tag_str_t& out) {
		TagLib::String tmp;
		switch (id) {
		case adaapd::ALBUM:
			tmp = tag->album();
			break;
		case adaapd::ARTIST:
			tmp = tag->artist();
			break;
		case adaapd::COMMENT:
			tmp = tag->comment();
			break;
		case adaapd::GENRE:
			tmp = tag->genre();
			break;
		case adaapd::TITLE:
			tmp = tag->title();
			break;
		default:
			ERR("INTERNAL ERROR: Bad id %d!", id);
			return false;
		}
		if (tmp == TagLib::String::null) {
			return false;
		}
		out = tmp.toWString();
		return true;
	}

	inline bool ape_str(TagLib::APE::Tag* tag, adaapd::Tag_StrId id,
			adaapd::tag_str_t& out) {
		switch (id) {
		case adaapd::ALBUM:
		case adaapd::ARTIST:
		case adaapd::COMMENT:
		case adaapd::GENRE:
		case adaapd::TITLE:
			return tag_str(tag, id, out);
		case adaapd::COMPOSER:
		case adaapd::DESCRIPTION:
		case adaapd::FORMAT:
			return false;//TODO
		}
		return false;
	}

	inline bool asf_str(TagLib::ASF::Tag* tag, adaapd::Tag_StrId id,
			adaapd::tag_str_t& out) {
		switch (id) {
		case adaapd::ALBUM:
		case adaapd::ARTIST:
		case adaapd::COMMENT:
		case adaapd::GENRE:
		case adaapd::TITLE:
			return tag_str(tag, id, out);
		case adaapd::COMPOSER:
		case adaapd::DESCRIPTION:
		case adaapd::FORMAT:
			return false;//TODO
		}
		return false;
	}

	inline bool id3v1_str(TagLib::ID3v1::Tag* tag, adaapd::Tag_StrId id,
			adaapd::tag_str_t& out) {
		switch (id) {
		case adaapd::ALBUM:
		case adaapd::ARTIST:
		case adaapd::COMMENT:
		case adaapd::GENRE:
		case adaapd::TITLE:
			return tag_str(tag, id, out);
		case adaapd::COMPOSER:
		case adaapd::DESCRIPTION:
		case adaapd::FORMAT:
			return false;//TODO
		}
		return false;
	}

	inline bool id3v2_str(TagLib::ID3v2::Tag* tag, adaapd::Tag_StrId id,
			adaapd::tag_str_t& out) {
		switch (id) {
		case adaapd::ALBUM:
		case adaapd::ARTIST:
		case adaapd::COMMENT:
		case adaapd::GENRE:
		case adaapd::TITLE:
			return tag_str(tag, id, out);
		case adaapd::COMPOSER:
		case adaapd::DESCRIPTION:
		case adaapd::FORMAT:
			return false;//TODO
		}
		return false;
	}

	inline bool mp4_str(TagLib::MP4::Tag* tag, adaapd::Tag_StrId id,
			adaapd::tag_str_t& out) {
		switch (id) {
		case adaapd::ALBUM:
		case adaapd::ARTIST:
		case adaapd::COMMENT:
		case adaapd::GENRE:
		case adaapd::TITLE:
			return tag_str(tag, id, out);
		case adaapd::COMPOSER:
		case adaapd::DESCRIPTION:
		case adaapd::FORMAT:
			return false;//TODO
		}
		return false;
	}

	inline bool xiph_str(TagLib::Ogg::XiphComment* tag, adaapd::Tag_StrId id,
			adaapd::tag_str_t& out) {
		switch (id) {
		case adaapd::ALBUM:
		case adaapd::ARTIST:
		case adaapd::COMMENT:
		case adaapd::GENRE:
		case adaapd::TITLE:
			return tag_str(tag, id, out);
		case adaapd::COMPOSER:
		case adaapd::DESCRIPTION:
		case adaapd::FORMAT:
			return false;//TODO
		}
		return false;
	}
}

namespace {
	inline bool check_file(const std::string& filepath) {
		struct stat sb;
		if (stat(filepath.c_str(), &sb) != 0) {
			ERR("Unable to stat file %s.", filepath.c_str());
			return false;
		}
		if (access(filepath.c_str(), R_OK) != 0) {
			ERR("Unable to access file %s: No read access.", filepath.c_str());
			return false;
		}
		if (!S_ISREG(sb.st_mode) && !S_ISLNK(sb.st_mode)) {
			ERR("Unable to access file %s: Not a regular file or symlink.", filepath.c_str());
			return false;
		}
		return true;
	}

	//---

	class Tag_MPEG : public adaapd::Tag {
	public:
		static adaapd::tag_t Create(const std::string& file) {
			TagLib::MPEG::File* f = new TagLib::MPEG::File(file.c_str());
			if (!f->isValid()) {
				delete f;
				return adaapd::tag_t();
			}
			return adaapd::tag_t(new Tag_MPEG(f));
		}

		virtual ~Tag_MPEG() {
			delete file;
		}

		bool Value(adaapd::Tag_IntId id, adaapd::tag_int_t& val) {
			TagLib::ID3v2::Tag* id3v2tag = file->ID3v2Tag();
			if (id3v2tag && id3v2_int(file, id3v2tag, id, val)) {
				return true;
			}
			TagLib::ID3v1::Tag* id3v1tag = file->ID3v1Tag();
			if (id3v1tag && id3v1_int(file, id3v1tag, id, val)) {
				return true;
			}
			TagLib::APE::Tag* apetag = file->APETag();
			if (apetag && ape_int(file, apetag, id, val)) {
				return true;
			}
			return false;
		}
		bool Value(adaapd::Tag_StrId id, adaapd::tag_str_t& val) {
			TagLib::ID3v2::Tag* id3v2tag = file->ID3v2Tag();
			if (id3v2tag && id3v2_str(id3v2tag, id, val)) {
				return true;
			}
			TagLib::ID3v1::Tag* id3v1tag = file->ID3v1Tag();
			if (id3v1tag && id3v1_str(id3v1tag, id, val)) {
				return true;
			}
			TagLib::APE::Tag* apetag = file->APETag();
			if (apetag && ape_str(apetag, id, val)) {
				return true;
			}
			return false;
		}

	private:
		Tag_MPEG(TagLib::MPEG::File* f) : file(f) { }

		TagLib::MPEG::File* file;
	};

	//---

	template <typename FILE>
	class Tag_Ogg : public adaapd::Tag {
	public:
		static adaapd::tag_t Create(const std::string& file) {
			FILE* f = new FILE(file.c_str());
			if (!f->isValid()) {
				delete f;
				return adaapd::tag_t();
			}
			return adaapd::tag_t(new Tag_Ogg<FILE>(f));
		}

		virtual ~Tag_Ogg() {
			delete file;
		}

		bool Value(adaapd::Tag_IntId id, adaapd::tag_int_t& val) {
			TagLib::Ogg::XiphComment* xiphcomment = file->tag();
			return xiphcomment && xiph_int(file, xiphcomment, id, val);
		}
		bool Value(adaapd::Tag_StrId id, adaapd::tag_str_t& val) {
			TagLib::Ogg::XiphComment* xiphcomment = file->tag();
			return xiphcomment && xiph_str(xiphcomment, id, val);
		}

	private:
		Tag_Ogg(FILE* f) : file(f) { }

		FILE* file;
	};
	typedef Tag_Ogg<TagLib::Ogg::Vorbis::File> Tag_OggVorbis;
	typedef Tag_Ogg<TagLib::Ogg::Speex::File> Tag_OggSpeex;
	typedef Tag_Ogg<TagLib::Ogg::FLAC::File> Tag_OggFlac;

	//---

	class Tag_Flac : public adaapd::Tag {
	public:
		static adaapd::tag_t Create(const std::string& file) {
			TagLib::FLAC::File* f = new TagLib::FLAC::File(file.c_str());
			if (!f->isValid()) {
				delete f;
				return adaapd::tag_t();
			}
			return adaapd::tag_t(new Tag_Flac(f));
		}

		virtual ~Tag_Flac() {
			delete file;
		}

		bool Value(adaapd::Tag_IntId id, adaapd::tag_int_t& val) {
			TagLib::Ogg::XiphComment* xiphcomment = file->xiphComment();
			if (xiphcomment && xiph_int(file, xiphcomment, id, val)) {
				return true;
			}
			TagLib::ID3v2::Tag* id3v2tag = file->ID3v2Tag();
			if (id3v2tag && id3v2_int(file, id3v2tag, id, val)) {
				return true;
			}
			TagLib::ID3v1::Tag* id3v1tag = file->ID3v1Tag();
			if (id3v1tag && id3v1_int(file, id3v1tag, id, val)) {
				return true;
			}
			return false;
		}
		bool Value(adaapd::Tag_StrId id, adaapd::tag_str_t& val) {
			TagLib::Ogg::XiphComment* xiphcomment = file->xiphComment();
			if (xiphcomment && xiph_str(xiphcomment, id, val)) {
				return true;
			}
			TagLib::ID3v2::Tag* id3v2tag = file->ID3v2Tag();
			if (id3v2tag && id3v2_str(id3v2tag, id, val)) {
				return true;
			}
			TagLib::ID3v1::Tag* id3v1tag = file->ID3v1Tag();
			if (id3v1tag && id3v1_str(id3v1tag, id, val)) {
				return true;
			}
			return false;
		}

	private:
		Tag_Flac(TagLib::FLAC::File* f) : file(f) { }

		TagLib::FLAC::File* file;
	};

	//---

	template <typename FILE>
	class Tag_Ape3v1 : public adaapd::Tag {
	public:
		static adaapd::tag_t Create(const std::string& file) {
			FILE* f = new FILE(file.c_str());
			if (!f->isValid()) {
				delete f;
				return adaapd::tag_t();
			}
			return adaapd::tag_t(new Tag_Ape3v1(f));
		}

		virtual ~Tag_Ape3v1() {
			delete file;
		}

		bool Value(adaapd::Tag_IntId id, adaapd::tag_int_t& val) {
			TagLib::APE::Tag* apetag = file->APETag();
			if (apetag && ape_int(file, apetag, id, val)) {
				return true;
			}
			TagLib::ID3v1::Tag* id3v1tag = file->ID3v1Tag();
			if (id3v1tag && id3v1_int(file, id3v1tag, id, val)) {
				return true;
			}
			return false;
		}
		bool Value(adaapd::Tag_StrId id, adaapd::tag_str_t& val) {
			TagLib::APE::Tag* apetag = file->APETag();
			if (apetag && ape_str(apetag, id, val)) {
				return true;
			}
			TagLib::ID3v1::Tag* id3v1tag = file->ID3v1Tag();
			if (id3v1tag && id3v1_str(id3v1tag, id, val)) {
				return true;
			}
			return false;
		}

	private:
		Tag_Ape3v1(FILE* f) : file(f) { }

		FILE* file;
	};
	typedef Tag_Ape3v1<TagLib::MPC::File> Tag_MPC;
	typedef Tag_Ape3v1<TagLib::APE::File> Tag_APE;
	typedef Tag_Ape3v1<TagLib::WavPack::File> Tag_WavPack;

	//---

	class Tag_TrueAudio : public adaapd::Tag {
	public:
		static adaapd::tag_t Create(const std::string& file) {
			TagLib::TrueAudio::File* f = new TagLib::TrueAudio::File(file.c_str());
			if (!f->isValid()) {
				delete f;
				return adaapd::tag_t();
			}
			return adaapd::tag_t(new Tag_TrueAudio(f));
		}

		virtual ~Tag_TrueAudio() {
			delete file;
		}

		bool Value(adaapd::Tag_IntId id, adaapd::tag_int_t& val) {
			TagLib::ID3v2::Tag* id3v2tag = file->ID3v2Tag();
			if (id3v2tag && id3v2_int(file, id3v2tag, id, val)) {
				return true;
			}
			TagLib::ID3v1::Tag* id3v1tag = file->ID3v1Tag();
			if (id3v1tag && id3v1_int(file, id3v1tag, id, val)) {
				return true;
			}
			return false;
		}
		bool Value(adaapd::Tag_StrId id, adaapd::tag_str_t& val) {
			TagLib::ID3v2::Tag* id3v2tag = file->ID3v2Tag();
			if (id3v2tag && id3v2_str(id3v2tag, id, val)) {
				return true;
			}
			TagLib::ID3v1::Tag* id3v1tag = file->ID3v1Tag();
			if (id3v1tag && id3v1_str(id3v1tag, id, val)) {
				return true;
			}
			return false;
		}

	private:
		Tag_TrueAudio(TagLib::TrueAudio::File* f) : file(f) { }

		TagLib::TrueAudio::File* file;
	};

	//---

	class Tag_MP4 : public adaapd::Tag {
	public:
		static adaapd::tag_t Create(const std::string& file) {
			TagLib::MP4::File* f = new TagLib::MP4::File(file.c_str());
			if (!f->isValid()) {
				delete f;
				return adaapd::tag_t();
			}
			return adaapd::tag_t(new Tag_MP4(f));
		}

		virtual ~Tag_MP4() {
			delete file;
		}

		bool Value(adaapd::Tag_IntId id, adaapd::tag_int_t& val) {
			TagLib::MP4::Tag* mp4tag = file->tag();
			return mp4tag && mp4_int(file, mp4tag, id, val);
		}
		bool Value(adaapd::Tag_StrId id, adaapd::tag_str_t& val) {
			TagLib::MP4::Tag* mp4tag = file->tag();
			return mp4tag && mp4_str(mp4tag, id, val);
		}

	private:
		Tag_MP4(TagLib::MP4::File* f) : file(f) { }

		TagLib::MP4::File* file;
	};

	//---

	class Tag_ASF : public adaapd::Tag {
	public:
		static adaapd::tag_t Create(const std::string& file) {
			TagLib::ASF::File* f = new TagLib::ASF::File(file.c_str());
			if (!f->isValid()) {
				delete f;
				return adaapd::tag_t();
			}
			return adaapd::tag_t(new Tag_ASF(f));
		}

		virtual ~Tag_ASF() {
			delete file;
		}

		bool Value(adaapd::Tag_IntId id, adaapd::tag_int_t& val) {
			TagLib::ASF::Tag* asftag = file->tag();
			return asftag && asf_int(file, asftag, id, val);
		}
		bool Value(adaapd::Tag_StrId id, adaapd::tag_str_t& val) {
			TagLib::ASF::Tag* asftag = file->tag();
			return asftag && asf_str(asftag, id, val);
		}

	private:
		Tag_ASF(TagLib::ASF::File* f) : file(f) { }

		TagLib::ASF::File* file;
	};

	//---

	template <typename FILE>
	class Tag_Riff : public adaapd::Tag {
	public:
		static adaapd::tag_t Create(const std::string& file) {
			FILE* f = new FILE(file.c_str());
			if (!f->isValid()) {
				delete f;
				return adaapd::tag_t();
			}
			return adaapd::tag_t(new Tag_Riff<FILE>(f));
		}

		virtual ~Tag_Riff() {
			delete file;
		}

		bool Value(adaapd::Tag_IntId id, adaapd::tag_int_t& val) {
			TagLib::ID3v2::Tag* id3v2tag = file->tag();
			return id3v2tag && id3v2_int(file, id3v2tag, id, val);
		}
		bool Value(adaapd::Tag_StrId id, adaapd::tag_str_t& val) {
			TagLib::ID3v2::Tag* id3v2tag = file->tag();
			return id3v2tag && id3v2_str(id3v2tag, id, val);
		}

	private:
		Tag_Riff(FILE* f) : file(f) { }

		FILE* file;
	};
	typedef Tag_Riff<TagLib::RIFF::AIFF::File> Tag_RiffAiff;
	typedef Tag_Riff<TagLib::RIFF::WAV::File> Tag_RiffWav;
}

/*static*/ adaapd::tag_t adaapd::Tag::Create(const std::string& path) {
	tag_t ret;
	if (!check_file(path)) {
		return ret;
	}

	/* cribbed this extension list from taglib's fileref.cpp.. */

	int dot = path.rfind(".");
	if (dot == -1) {
		LOG("No extension for file %s", path.c_str());
		return ret;
	}
	std::string ext = path.substr(dot);
	for (size_t i = 0; i < ext.size(); ++i) {
		ext[i] = toupper(ext[i]);
	}

    if (ext == "MP3") {
		ret = Tag_MPEG::Create(path);
    } else if (ext == "OGG") {
		ret = Tag_OggVorbis::Create(path);
    } else if (ext == "OGA") {
		/* .oga can be any audio in the Ogg container. First try FLAC, then Vorbis. */
		ret = Tag_OggFlac::Create(path);
		if (!ret) {
			ret = Tag_OggVorbis::Create(path);
		}
    } else if (ext == "SPX") {
		ret = Tag_OggSpeex::Create(path);
    } else if (ext == "FLAC") {
		ret = Tag_Flac::Create(path);
    } else if (ext == "MPC") {
		ret = Tag_MPC::Create(path);
    } else if (ext == "WV") {
		ret = Tag_WavPack::Create(path);
    } else if (ext == "TTA") {
		ret = Tag_TrueAudio::Create(path);
    } else if (ext == "M4A" || ext == "M4R" || ext == "M4B" || ext == "M4P" || ext == "MP4" || ext == "3G2") {
		ret = Tag_MP4::Create(path);
    } else if (ext == "WMA" || ext == "ASF") {
		ret = Tag_ASF::Create(path);
    } else if (ext == "AIF" || ext == "AIFF") {
		ret = Tag_RiffAiff::Create(path);
    } else if (ext == "WAV") {
		ret = Tag_RiffWav::Create(path);
    } else if (ext == "APE") {
		ret = Tag_APE::Create(path);
	}

	if (!ret) {
		/* don't be too noisy in case someone's got a bunch of album art files */
		DEBUG("Unsupported/unknown file: %s", path.c_str());
	}
	return ret;
}
