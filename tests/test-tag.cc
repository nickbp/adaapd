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

#include <gtest/gtest.h>
#include <tag.h>
#include <logging.h>

#define PATH(filename) "tagdata/" filename
#define CHECK_MISSING(tag, field)

using namespace adaapd;

static bool missing(const tag_t& t, Tag_IntId id) {
	tag_int_t got;
	if (t->Value(id, got)) {
		LOG("expected missing, got %ld", got);
		return false;
	}
	return true;
}
static bool missing(const tag_t& t, Tag_StrId id) {
	tag_str_t got;
	if (t->Value(id, got)) {
		LOG("expected missing, got %s", got.c_str());
		return false;
	}
	return true;
}

static bool eq(const tag_t& t, Tag_IntId id, tag_int_t val) {
	tag_int_t got;
	if (!t->Value(id, got)) {
		LOG("expected %ld, got missing", val);
		return false;
	}
	if (got != val) {
		LOG("expected %ld, got %ld", val, got);
		return false;
	}
	return true;
}
static bool eq(const tag_t& t, Tag_StrId id, tag_str_t val) {
	tag_str_t got;
	if (!t->Value(id, got)) {
		LOG("expected %s, got missing", val.c_str());
		return false;
	}
	if (got != val) {
		LOG("expected %s, got %s", val.c_str(), got.c_str());
		return false;
	}
	return true;
}

TEST(Tag, aiff) {
	tag_t t = Tag::Create(PATH("empty.aiff"));
	ASSERT_TRUE((bool)t);

	EXPECT_TRUE(missing(t, BPM));
	EXPECT_TRUE(eq(t, BIT_RATE, 705));
	EXPECT_TRUE(missing(t, COMPILATION));
	EXPECT_TRUE(missing(t, DISC_COUNT));
	EXPECT_TRUE(missing(t, DISC_NUMBER));
	EXPECT_TRUE(missing(t, RELATIVE_VOLUME));
	EXPECT_TRUE(eq(t, SAMPLE_RATE, 44100));
	EXPECT_TRUE(eq(t, SIZE, 252));
	EXPECT_TRUE(eq(t, TIME, 0));
	EXPECT_TRUE(missing(t, TRACK_COUNT));
	EXPECT_TRUE(eq(t, TRACK_NUMBER, 98));
	EXPECT_TRUE(missing(t, USER_RATING));
	EXPECT_TRUE(eq(t, YEAR, 1492));

	EXPECT_TRUE(eq(t, ALBUM, "albyߝ"));
	EXPECT_TRUE(missing(t, ARTIST));
	EXPECT_TRUE(eq(t, COMMENT, "empty"));
	EXPECT_TRUE(missing(t, COMPOSER));
	EXPECT_TRUE(eq(t, GENRE, "Polka"));
	EXPECT_TRUE(eq(t, TITLE, "tracky"));
}

TEST(Tag, flac) {
	/* empty.flac doesn't work */
	tag_t t = Tag::Create(PATH("short.flac"));
	ASSERT_TRUE((bool)t);

	EXPECT_TRUE(eq(t, BPM, 999));
	EXPECT_TRUE(eq(t, BIT_RATE, 0));//???
	EXPECT_TRUE(eq(t, COMPILATION, 1));
	EXPECT_TRUE(eq(t, DISC_COUNT, 13));
	EXPECT_TRUE(eq(t, DISC_NUMBER, 12));
	EXPECT_TRUE(missing(t, RELATIVE_VOLUME));
	EXPECT_TRUE(eq(t, SAMPLE_RATE, 44100));
	EXPECT_TRUE(eq(t, SIZE, 10661));
	EXPECT_TRUE(eq(t, TIME, 0));
	EXPECT_TRUE(eq(t, TRACK_COUNT, 99));
	EXPECT_TRUE(eq(t, TRACK_NUMBER, 98));
	EXPECT_TRUE(eq(t, USER_RATING, 40));
	EXPECT_TRUE(eq(t, YEAR, 1492));

	EXPECT_TRUE(eq(t, ALBUM, "albyߝ"));
	EXPECT_TRUE(eq(t, ARTIST, "àrty"));
	EXPECT_TRUE(eq(t, COMMENT, "commenty"));
	EXPECT_TRUE(eq(t, COMPOSER, "compy"));
	EXPECT_TRUE(eq(t, GENRE, "Polka"));
	EXPECT_TRUE(eq(t, TITLE, "tracky"));
}

TEST(Tag, gsm_wav) {/* no tag data, only stream info */
	tag_t t = Tag::Create(PATH("empty_gsm.wav"));
	ASSERT_TRUE((bool)t);

	EXPECT_TRUE(missing(t, BPM));
	EXPECT_TRUE(eq(t, BIT_RATE, 71));
	EXPECT_TRUE(missing(t, COMPILATION));
	EXPECT_TRUE(missing(t, DISC_COUNT));
	EXPECT_TRUE(missing(t, DISC_NUMBER));
	EXPECT_TRUE(missing(t, RELATIVE_VOLUME));
	EXPECT_TRUE(eq(t, SAMPLE_RATE, 44100));
	EXPECT_TRUE(eq(t, SIZE, 1494));
	EXPECT_TRUE(eq(t, TIME, 0));
	EXPECT_TRUE(missing(t, TRACK_COUNT));
	EXPECT_TRUE(missing(t, TRACK_NUMBER));
	EXPECT_TRUE(missing(t, USER_RATING));
	EXPECT_TRUE(missing(t, YEAR));

	EXPECT_TRUE(missing(t, ALBUM));
	EXPECT_TRUE(missing(t, ARTIST));
	EXPECT_TRUE(missing(t, COMMENT));
	EXPECT_TRUE(missing(t, COMPOSER));
	EXPECT_TRUE(missing(t, GENRE));
	EXPECT_TRUE(missing(t, TITLE));
}

TEST(Tag, m4a) {//TODO test file is crappy, add more fields!
	/* empty m4a causes taglib to print warnings */
	tag_t t = Tag::Create(PATH("short.m4a"));
	ASSERT_TRUE((bool)t);

	EXPECT_TRUE(eq(t, BPM, 999));
	EXPECT_TRUE(eq(t, BIT_RATE, 0));
	EXPECT_TRUE(missing(t, COMPILATION));
	EXPECT_TRUE(missing(t, DISC_COUNT));
	EXPECT_TRUE(eq(t, DISC_NUMBER, 12));
	EXPECT_TRUE(missing(t, RELATIVE_VOLUME));
	EXPECT_TRUE(eq(t, SAMPLE_RATE, 44100));
	EXPECT_TRUE(eq(t, SIZE, 5683));
	EXPECT_TRUE(eq(t, TIME, 0));
	EXPECT_TRUE(missing(t, TRACK_COUNT));
	EXPECT_TRUE(eq(t, TRACK_NUMBER, 98));
	EXPECT_TRUE(missing(t, USER_RATING));
	EXPECT_TRUE(eq(t, YEAR, 1984));

	EXPECT_TRUE(eq(t, ALBUM, "albyߝ"));
	EXPECT_TRUE(eq(t, ARTIST, "àrty"));
	EXPECT_TRUE(eq(t, COMMENT, "empty"));
	EXPECT_TRUE(eq(t, COMPOSER, "compy"));
	EXPECT_TRUE(eq(t, GENRE, "Polka"));
	EXPECT_TRUE(eq(t, TITLE, "tracky"));
}

TEST(Tag, mp3) {
	tag_t t = Tag::Create(PATH("empty.mp3"));
	ASSERT_TRUE((bool)t);

	EXPECT_TRUE(eq(t, BPM, 999));
	EXPECT_TRUE(eq(t, BIT_RATE, 0));
	EXPECT_TRUE(eq(t, COMPILATION, 1));
	EXPECT_TRUE(eq(t, DISC_COUNT, 13));
	EXPECT_TRUE(eq(t, DISC_NUMBER, 12));
	EXPECT_TRUE(missing(t, RELATIVE_VOLUME));
	EXPECT_TRUE(eq(t, SAMPLE_RATE, 44100));
	EXPECT_TRUE(eq(t, SIZE, 1986));
	EXPECT_TRUE(eq(t, TIME, 0));
	EXPECT_TRUE(eq(t, TRACK_COUNT, 99));
	EXPECT_TRUE(eq(t, TRACK_NUMBER, 98));
	EXPECT_TRUE(eq(t, USER_RATING, 80));
	EXPECT_TRUE(eq(t, YEAR, 1492));

	EXPECT_TRUE(eq(t, ALBUM, "albyߝ"));
	EXPECT_TRUE(eq(t, ARTIST, "àrty"));
	EXPECT_TRUE(eq(t, COMMENT, "empty"));
	EXPECT_TRUE(eq(t, COMPOSER, "compy"));
	EXPECT_TRUE(eq(t, GENRE, "Polka"));
	EXPECT_TRUE(eq(t, TITLE, "tracky"));
}

TEST(Tag, ms16_wav) {/* no tag data, only stream info */
	tag_t t = Tag::Create(PATH("empty_ms16.wav"));
	ASSERT_TRUE((bool)t);

	EXPECT_TRUE(missing(t, BPM));
	EXPECT_TRUE(eq(t, BIT_RATE, 705));
	EXPECT_TRUE(missing(t, COMPILATION));
	EXPECT_TRUE(missing(t, DISC_COUNT));
	EXPECT_TRUE(missing(t, DISC_NUMBER));
	EXPECT_TRUE(missing(t, RELATIVE_VOLUME));
	EXPECT_TRUE(eq(t, SAMPLE_RATE, 44100));
	EXPECT_TRUE(eq(t, SIZE, 1478));
	EXPECT_TRUE(eq(t, TIME, 0));
	EXPECT_TRUE(missing(t, TRACK_COUNT));
	EXPECT_TRUE(missing(t, TRACK_NUMBER));
	EXPECT_TRUE(missing(t, USER_RATING));
	EXPECT_TRUE(missing(t, YEAR));

	EXPECT_TRUE(missing(t, ALBUM));
	EXPECT_TRUE(missing(t, ARTIST));
	EXPECT_TRUE(missing(t, COMMENT));
	EXPECT_TRUE(missing(t, COMPOSER));
	EXPECT_TRUE(missing(t, GENRE));
	EXPECT_TRUE(missing(t, TITLE));
}

TEST(Tag, ogg) {
	tag_t t = Tag::Create(PATH("empty.ogg"));
	ASSERT_TRUE((bool)t);

	EXPECT_TRUE(eq(t, BPM, 999));
	EXPECT_TRUE(eq(t, BIT_RATE, 96));
	EXPECT_TRUE(eq(t, COMPILATION, 1));
	EXPECT_TRUE(eq(t, DISC_COUNT, 13));
	EXPECT_TRUE(eq(t, DISC_NUMBER, 12));
	EXPECT_TRUE(missing(t, RELATIVE_VOLUME));
	EXPECT_TRUE(eq(t, SAMPLE_RATE, 44100));
	EXPECT_TRUE(eq(t, SIZE, 4480));
	EXPECT_TRUE(eq(t, TIME, 0));
	EXPECT_TRUE(eq(t, TRACK_COUNT, 99));
	EXPECT_TRUE(eq(t, TRACK_NUMBER, 98));
	EXPECT_TRUE(eq(t, USER_RATING, 60));
	EXPECT_TRUE(eq(t, YEAR, 1492));

	EXPECT_TRUE(eq(t, ALBUM, "albyߝ"));
	EXPECT_TRUE(eq(t, ARTIST, "àrty"));
	EXPECT_TRUE(eq(t, COMMENT, "empty"));
	EXPECT_TRUE(eq(t, COMPOSER, "compy"));
	EXPECT_TRUE(eq(t, GENRE, "Polka"));
	EXPECT_TRUE(eq(t, TITLE, "tracky"));
}

TEST(Tag, wma) {
	tag_t t = Tag::Create(PATH("empty.wma"));
	ASSERT_TRUE((bool)t);

	EXPECT_TRUE(missing(t, BPM));
	EXPECT_TRUE(eq(t, BIT_RATE, 198));
	EXPECT_TRUE(missing(t, COMPILATION));
	EXPECT_TRUE(missing(t, DISC_COUNT));
	EXPECT_TRUE(missing(t, DISC_NUMBER));
	EXPECT_TRUE(missing(t, RELATIVE_VOLUME));
	EXPECT_TRUE(eq(t, SAMPLE_RATE, 44100));
	EXPECT_TRUE(eq(t, SIZE, 5024));
	EXPECT_TRUE(eq(t, TIME, 0));
	EXPECT_TRUE(missing(t, TRACK_COUNT));
	EXPECT_TRUE(eq(t, TRACK_NUMBER, 98));
	EXPECT_TRUE(missing(t, USER_RATING));
	EXPECT_TRUE(eq(t, YEAR, 1492));

	EXPECT_TRUE(eq(t, ALBUM, "albyߝ"));
	EXPECT_TRUE(eq(t, ARTIST, "àrty"));
	EXPECT_TRUE(eq(t, COMMENT, "empty"));
	EXPECT_TRUE(missing(t, COMPOSER));
	EXPECT_TRUE(eq(t, GENRE, "Polka"));
	EXPECT_TRUE(eq(t, TITLE, "tracky"));
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest( &argc, argv );
	return RUN_ALL_TESTS();
}
