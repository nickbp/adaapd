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
#include "logging.h"

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
	/*****
	 * Int field retrieval
	 *****/

	bool string_starts_with(const std::wstring& haystack, const std::wstring& needle) {
		if (haystack.size() < needle.size()) {
			return false;
		}
		const std::wstring& ending = haystack.substr(0, needle.size());
		return ending.compare(needle) == 0;
	}

	bool xiph_rating(TagLib::Ogg::XiphComment* xiphcomment, adaapd::tag_int_t& out) {
		TagLib::Ogg::FieldListMap map = xiphcomment->fieldListMap();
		/* we could've used lower_bound(), except that taglib decided to
		   obfuscate the underlying std::map... */
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
			const TagLib::String& floatstr = val[0];
			double ogg_rating;
			std::istringstream stream(floatstr.toCString());
			stream >> ogg_rating;
			if (stream.fail()) {
				continue;
			}
			if (ogg_rating == 0.5) {// unrated
				return false;
			}
			double d = ogg_rating * 100;// 0.0-1.0 -> 0-100
			out = d + 0.5;// round to nearest int
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
		if (popm_rating == 0) {// unrated
			return false;
		} else if (popm_rating < 0x40) {// 1-63
			out = 20;
		} else if (popm_rating < 0x80) {// 64-127
			out = 40;
		} else if (popm_rating < 0xC0) {// 128-191
			out = 60;
		} else if (popm_rating < 0xFF) {// 192-254
			out =  80;
		} else {// 255
			out = 100;
		}
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
		case adaapd::DISC_COUNT:
		case adaapd::DISC_NUMBER:
		case adaapd::RELATIVE_VOLUME:
		case adaapd::TRACK_COUNT:
		case adaapd::USER_RATING:
			return false;//TODO
		}
		return false;
	}

#define GET_ASF_INT(tag, field, out) { \
	TagLib::ASF::AttributeListMap::ConstIterator iter = tag->attributeListMap().find(field); \
	if (iter == tag->attributeListMap().end() || iter->second.isEmpty()) { \
		return false; \
	} \
	const TagLib::ASF::Attribute& att = iter->second[0]; \
	bool ok = false; \
	out = att.toString().toInt(&ok); \
	return ok; \
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
			return tag_int(tag, id, out);
		case adaapd::YEAR:
			GET_ASF_INT(tag, "year", out);//tag->year() doesnt work on test data
		case adaapd::BPM:
		case adaapd::COMPILATION:
		case adaapd::DISC_COUNT:
		case adaapd::DISC_NUMBER:
		case adaapd::RELATIVE_VOLUME:
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
		case adaapd::DISC_COUNT:
		case adaapd::DISC_NUMBER:
		case adaapd::RELATIVE_VOLUME:
		case adaapd::TRACK_COUNT:
		case adaapd::USER_RATING:
			return false;/* nope */
		}
		return false;
	}

#define GET_ID3V2_INT(tag, field, out) { \
	TagLib::ID3v2::FrameListMap::ConstIterator iter = tag->frameListMap().find(field); \
	if (iter == tag->frameListMap().end() || iter->second.isEmpty()) { \
		return false; \
	} \
	bool ok = false; \
	out = iter->second[0]->toString().toInt(&ok); \
	return ok; \
}

#define GET_ID3V2_NUMER(tag, field, out) { \
	TagLib::ID3v2::FrameListMap::ConstIterator iter = tag->frameListMap().find(field); \
	if (iter == tag->frameListMap().end() || iter->second.isEmpty()) { \
		return false; \
	} \
	/* tmp = "12" or "12/34", extract "12" in both cases */ \
	TagLib::String tmp = iter->second[0]->toString(); \
	int off = tmp.find("/"); \
	bool ok = false; \
	if (off == -1) { \
		/* not found, assume whole thing is an int */ \
		out = tmp.toInt(&ok); \
	} else { \
		out = tmp.substr(0, off).toInt(&ok); \
	} \
	return ok; \
}

#define GET_ID3V2_DENOM(tag, field, out) { \
	TagLib::ID3v2::FrameListMap::ConstIterator iter = tag->frameListMap().find(field); \
	if (iter == tag->frameListMap().end() || iter->second.isEmpty()) { \
		return false; \
	} \
	/* tmp = "12/34", extract "34" */ \
	TagLib::String tmp = iter->second[0]->toString(); \
	int off = tmp.find("/"); \
	if (off == -1) { \
		/* not found, give up */ \
		return false; \
	} \
	bool ok = false; \
	out = tmp.substr(off+1).toInt(&ok); \
	return ok; \
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
			GET_ID3V2_INT(tag, "TBPM", out);
		case adaapd::COMPILATION:
			GET_ID3V2_INT(tag, "TCMP", out);
		case adaapd::DISC_COUNT:
			GET_ID3V2_DENOM(tag, "TPOS", out);
		case adaapd::DISC_NUMBER:
			GET_ID3V2_NUMER(tag, "TPOS", out);
		case adaapd::RELATIVE_VOLUME:
			return false;//TODO RelativeVolumeFrame
		case adaapd::TRACK_COUNT:
			GET_ID3V2_DENOM(tag, "TRCK", out);
		case adaapd::USER_RATING:
			return id3v2_rating(tag, out);
		}
		return false;
	}

#define GET_MP4_INT(tag, field, out) { \
	TagLib::MP4::ItemListMap::ConstIterator iter = tag->itemListMap().find(field); \
	if (iter == tag->itemListMap().end()) { \
		return false; \
	} \
	out = iter->second.toInt(); \
	return true; \
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
			GET_MP4_INT(tag, "tmpo", out);
		case adaapd::COMPILATION:
		case adaapd::DISC_COUNT:
			return false;//TODO
		case adaapd::DISC_NUMBER:
			GET_MP4_INT(tag, "disk", out);
		case adaapd::RELATIVE_VOLUME:
		case adaapd::TRACK_COUNT:
		case adaapd::USER_RATING:
			return false;//TODO
		}
		return false;
	}

#define GET_XIPH_INT(tag, field, out) { \
	TagLib::Ogg::FieldListMap::ConstIterator iter = tag->fieldListMap().find(field); \
	if (iter == tag->fieldListMap().end() || iter->second.isEmpty()) { \
		return false; \
	} \
	bool ok = false; \
	out = iter->second[0].toInt(&ok); \
	return ok; \
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
			GET_XIPH_INT(tag, "TEMPO", out);
		case adaapd::COMPILATION:
			GET_XIPH_INT(tag, "COMPILATION", out);
		case adaapd::DISC_COUNT:
			GET_XIPH_INT(tag, "DISCTOTAL", out);
		case adaapd::DISC_NUMBER:
			GET_XIPH_INT(tag, "DISCNUMBER", out);
		case adaapd::RELATIVE_VOLUME:
			return false;//TODO
		case adaapd::TRACK_COUNT:
			GET_XIPH_INT(tag, "TRACKTOTAL", out);
		case adaapd::USER_RATING:
			return xiph_rating(tag, out);
		}
		return false;
	}

	/*****
	 * String field retrieval
	 *****/

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
		out = tmp.to8Bit(true);/* UTF8 */
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
			return false;//TODO
		}
		return false;
	}

#define GET_ASF_STR(tag, field, out) { \
	TagLib::ASF::AttributeListMap::ConstIterator iter = tag->attributeListMap().find(field); \
	if (iter == tag->attributeListMap().end() || iter->second.isEmpty()) { \
		return false; \
	} \
	out = iter->second[0].toString().to8Bit(true); \
	return true; \
}

	inline bool asf_str(TagLib::ASF::Tag* tag, adaapd::Tag_StrId id,
			adaapd::tag_str_t& out) {
		switch (id) {
		case adaapd::ALBUM:
		case adaapd::ARTIST:
			return tag_str(tag, id, out);
		case adaapd::COMMENT:
			GET_ASF_STR(tag, "Description", out);
		case adaapd::GENRE:
		case adaapd::TITLE:
			return tag_str(tag, id, out);
		case adaapd::COMPOSER:
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
			return false;/* nope */
		}
		return false;
	}

#define GET_ID3V2_STR(tag, field, out) { \
	TagLib::ID3v2::FrameListMap::ConstIterator iter = tag->frameListMap().find(field); \
	if (iter == tag->frameListMap().end() || iter->second.isEmpty()) { \
		return false; \
	} \
	out = iter->second[0]->toString().to8Bit(true); \
	return true; \
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
			GET_ID3V2_STR(tag, "TCOM", out);
		}
		return false;
	}

#define GET_MP4_STR(tag, field, out) { \
	TagLib::MP4::ItemListMap::ConstIterator iter = tag->itemListMap().find(field); \
	if (iter == tag->itemListMap().end()) { \
		LOG_DIR("nope a"); return false;			\
	} \
	TagLib::StringList l = iter->second.toStringList(); \
	if (l.isEmpty()) { \
		LOG_DIR("nope b"); return false;				\
	} \
	out = l[0].to8Bit(true); \
	return true; \
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
			GET_MP4_STR(tag, "\xa9wrt", out);
		}
		return false;
	}

#define GET_XIPH_STR(tag, field, out) { \
	TagLib::Ogg::FieldListMap::ConstIterator iter = tag->fieldListMap().find(field); \
	if (iter == tag->fieldListMap().end() || iter->second.isEmpty()) { \
		return false; \
	} \
	out = iter->second[0].to8Bit(true); \
	return true; \
}

	inline bool xiph_str(TagLib::Ogg::XiphComment* tag, adaapd::Tag_StrId id,
			adaapd::tag_str_t& out) {
		switch (id) {
		case adaapd::ALBUM:
		case adaapd::ARTIST:
			return tag_str(tag, id, out);
		case adaapd::COMMENT:
		case adaapd::GENRE:
		case adaapd::TITLE:
			return tag_str(tag, id, out);
		case adaapd::COMPOSER:
			GET_XIPH_STR(tag, "COMPOSER", out);
		}
		return false;
	}

	/*****
	 * Tag printing (for debug)
	 *****/

	void print(TagLib::APE::Tag* t) {
		if (!t) { LOG_DIR("NO APE TAG"); }
		for (TagLib::APE::ItemListMap::ConstIterator iter = t->itemListMap().begin();
			 iter != t->itemListMap().end(); ++iter) {
			LOG("%s = %s, %s", iter->first.to8Bit(true).c_str(),
					iter->second.key().to8Bit(true).c_str(),
					iter->second.toString().to8Bit(true).c_str());
		}
	}
	void print(TagLib::ASF::Tag* t) {
		if (!t) { LOG_DIR("NO ASF TAG"); }
		for (TagLib::ASF::AttributeListMap::ConstIterator miter = t->attributeListMap().begin();
			 miter != t->attributeListMap().end(); ++miter) {
			LOG("%s", miter->first.to8Bit(true).c_str());
			for (TagLib::ASF::AttributeList::ConstIterator liter =
					 miter->second.begin();
				 liter != miter->second.end(); ++liter) {
				LOG(" %d %s", liter->type(), liter->toString().to8Bit(true).c_str());
			}
		}
	}
	void print(TagLib::ID3v1::Tag* t) {
		if (!t) { LOG_DIR("NO ID3V1 TAG"); }
		LOG("title[%s] artist[%s] album[%s] comment[%s] genre[%s] year[%d] track[%d]",
				t->title().to8Bit(true).c_str(),
				t->artist().to8Bit(true).c_str(),
				t->album().to8Bit(true).c_str(),
				t->comment().to8Bit(true).c_str(),
				t->genre().to8Bit(true).c_str(),
				t->year(), t->track());
	}
	void print(TagLib::ID3v2::Tag* t) {
		if (!t) { LOG_DIR("NO ID3V2 TAG"); }
		for (TagLib::ID3v2::FrameList::ConstIterator iter = t->frameList().begin();
			 iter != t->frameList().end(); ++iter) {
			LOG("%s = %s", (*iter)->frameID().data(),
					(*iter)->toString().to8Bit(true).c_str());
		}
	}
	void print(TagLib::MP4::Tag* t) {
		if (!t) { LOG_DIR("NO MP4 TAG"); }
		for (TagLib::MP4::ItemListMap::ConstIterator miter = t->itemListMap().begin();
			 miter != t->itemListMap().end(); ++miter) {
			LOG("%s", miter->first.to8Bit(true).c_str());
			TagLib::StringList l = miter->second.toStringList();
			if (l.isEmpty()) {
				/* try something else? */
				LOG("  %d", miter->second.toInt());
			} else {
				for (TagLib::StringList::ConstIterator siter = l.begin();
					 siter != l.end(); ++siter) {
					LOG("  %s", siter->to8Bit(true).c_str());
				}
			}
		}
	}
	void print(TagLib::Ogg::XiphComment* t) {
		if (!t) { LOG_DIR("NO OGG TAG"); }
		for (TagLib::Ogg::FieldListMap::ConstIterator miter = t->fieldListMap().begin();
			 miter != t->fieldListMap().end(); ++miter) {
			LOG("%s", miter->first.to8Bit(true).c_str());
			for (TagLib::StringList::ConstIterator liter =
					 miter->second.begin();
				 liter != miter->second.end(); ++liter) {
				LOG(" %s", liter->to8Bit(true).c_str());
			}
		}
	}

	/*****
	 * File to tag mappings
	 *****/

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
			/* nice to have: clear tag ptrs before parent file is wiped */
			tag_ape = NULL;
			tag_id3v2 = NULL;
			tag_id3v1 = NULL;
			delete file;
		}

		bool Value(adaapd::Tag_IntId id, adaapd::tag_int_t& val) {
			if (tag_ape && ape_int(file, tag_ape, id, val)) {
				return true;
			}
			if (tag_id3v2 && id3v2_int(file, tag_id3v2, id, val)) {
				return true;
			}
			if (tag_id3v1 && id3v1_int(file, tag_id3v1, id, val)) {
				return true;
			}
			return false;
		}
		bool Value(adaapd::Tag_StrId id, adaapd::tag_str_t& val) {
			if (tag_ape && ape_str(tag_ape, id, val)) {
				return true;
			}
			if (tag_id3v2 && id3v2_str(tag_id3v2, id, val)) {
				return true;
			}
			if (tag_id3v1 && id3v1_str(tag_id3v1, id, val)) {
				return true;
			}
			return false;
		}

	private:
		Tag_MPEG(TagLib::MPEG::File* f) : file(f) {
			tag_ape = file->APETag();
			tag_id3v2 = file->ID3v2Tag();
			tag_id3v1 = file->ID3v1Tag();
		}

		TagLib::MPEG::File* file;
		TagLib::APE::Tag* tag_ape;
		TagLib::ID3v2::Tag* tag_id3v2;
		TagLib::ID3v1::Tag* tag_id3v1;
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
			/* nice to have: clear tag ptrs before parent file is wiped */
			tag_xiph = NULL;
			tag_id3v2 = NULL;
			tag_id3v1 = NULL;
			delete file;
		}

		bool Value(adaapd::Tag_IntId id, adaapd::tag_int_t& val) {
			if (tag_xiph && xiph_int(file, tag_xiph, id, val)) {
				return true;
			}
			if (tag_id3v2 && id3v2_int(file, tag_id3v2, id, val)) {
				return true;
			}
			if (tag_id3v1 && id3v1_int(file, tag_id3v1, id, val)) {
				return true;
			}
			return false;
		}
		bool Value(adaapd::Tag_StrId id, adaapd::tag_str_t& val) {
			if (tag_xiph && xiph_str(tag_xiph, id, val)) {
				return true;
			}
			if (tag_id3v2 && id3v2_str(tag_id3v2, id, val)) {
				return true;
			}
			if (tag_id3v1 && id3v1_str(tag_id3v1, id, val)) {
				return true;
			}
			return false;
		}

	private:
		Tag_Flac(TagLib::FLAC::File* f) : file(f) {
			tag_xiph = file->xiphComment();
			tag_id3v2 = file->ID3v2Tag();
			tag_id3v1 = file->ID3v1Tag();
		}

		TagLib::FLAC::File* file;
		TagLib::Ogg::XiphComment* tag_xiph;
		TagLib::ID3v2::Tag* tag_id3v2;
		TagLib::ID3v1::Tag* tag_id3v1;
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
}

/*static*/ adaapd::tag_t adaapd::Tag::Create(const std::string& path) {
	tag_t ret;
	if (!check_file(path)) {
		LOG("Not found: %s", path.c_str());
		return ret;
	}

	/* cribbed this extension list from taglib's fileref.cpp.. */

	int dot = path.rfind(".");
	if (dot == -1) {
		LOG("No extension for file %s", path.c_str());
		return ret;
	}
	std::string ext = path.substr(dot+1);
	for (size_t i = 0; i < ext.size(); ++i) {
		ext[i] = toupper(ext[i]);
	}

	if (ext == "MP3") {
		ret = Tag_MPEG::Create(path);
	} else if (ext == "OGG") {
		ret = Tag_OggVorbis::Create(path);
	} else if (ext == "FLAC") {
		ret = Tag_Flac::Create(path);
	} else if (ext == "M4A" || ext == "M4R" || ext == "M4B" || ext == "M4P" ||
			ext == "MP4" || ext == "3G2" || ext == "AAC") {
		ret = Tag_MP4::Create(path);
	} else if (ext == "WMA" || ext == "ASF") {
		ret = Tag_ASF::Create(path);
	} else if (ext == "OGA") {
		/* .oga can be any audio in the Ogg container.
		   First try FLAC, then Vorbis. */
		ret = Tag_OggFlac::Create(path);
		if (!ret) {
			ret = Tag_OggVorbis::Create(path);
		}
	} else if (ext == "SPX") {
		ret = Tag_OggSpeex::Create(path);
	} else if (ext == "WAV") {
		ret = Tag_RiffWav::Create(path);
	} else if (ext == "AIF" || ext == "AIFF") {
		ret = Tag_RiffAiff::Create(path);
	} else if (ext == "APE") {
		ret = Tag_APE::Create(path);
	} else if (ext == "MPC") {
		ret = Tag_MPC::Create(path);
	} else if (ext == "WV") {
		ret = Tag_WavPack::Create(path);
	} else if (ext == "TTA") {
		ret = Tag_TrueAudio::Create(path);
	}

	if (!ret) {
		/* don't be too noisy in case someone's got a bunch of album art files */
		DEBUG("Unsupported/unknown file: %s -> %s", path.c_str(), ext.c_str());
	}
	return ret;
}
