/* ZoiteChat
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <string.h>

#include <glib.h>

#include "../util.h"

static void
test_parse_foreground (void)
{
	guint32 fg = 0, bg = 0xDEAD;
	int has_bg = TRUE;

	g_assert_cmpint (hexcolor_parse ("FF8800text", 10, &fg, &bg, &has_bg), ==, 6);
	g_assert_cmphex (fg, ==, 0xFF8800);
	g_assert_false (has_bg);
	g_assert_cmphex (bg, ==, 0xDEAD);
}

static void
test_parse_foreground_and_background (void)
{
	guint32 fg = 0, bg = 0;
	int has_bg = FALSE;

	g_assert_cmpint (hexcolor_parse ("112233,a0b0c0x", 14, &fg, &bg, &has_bg), ==, 13);
	g_assert_cmphex (fg, ==, 0x112233);
	g_assert_cmphex (bg, ==, 0xA0B0C0);
	g_assert_true (has_bg);
}

static void
test_parse_bad_background_keeps_foreground (void)
{
	guint32 fg = 0;
	int has_bg = TRUE;

	/* ",12" is plain text after a valid foreground, not a background */
	g_assert_cmpint (hexcolor_parse ("112233,12 hi", 12, &fg, NULL, &has_bg), ==, 6);
	g_assert_cmphex (fg, ==, 0x112233);
	g_assert_false (has_bg);
}

static void
test_parse_bare (void)
{
	/* a bare \004 resets colors, like a bare \003 */
	g_assert_cmpint (hexcolor_parse (" text", 5, NULL, NULL, NULL), ==, 0);
	g_assert_cmpint (hexcolor_parse ("12345G", 6, NULL, NULL, NULL), ==, 0);
	g_assert_cmpint (hexcolor_parse ("", 0, NULL, NULL, NULL), ==, 0);
}

static void
test_parse_respects_length (void)
{
	g_assert_cmpint (hexcolor_parse ("FF8800", 5, NULL, NULL, NULL), ==, 0);
	g_assert_cmpint (hexcolor_parse ("FF8800,112233", 12, NULL, NULL, NULL), ==, 6);
}

static void
assert_stripped (const char *in, int flags, const char *expected)
{
	char out[128];

	strip_color2 (in, -1, out, flags);
	g_assert_cmpstr (out, ==, expected);
}

static void
test_strip (void)
{
	assert_stripped ("\004FF8800alice\017: hi", STRIP_ALL, "alice: hi");
	assert_stripped ("\004112233,445566bg", STRIP_ALL, "bg");
	assert_stripped ("a\004 b", STRIP_ALL, "a b");
	assert_stripped ("\004FF8800\00312x", STRIP_ALL, "x");
	/* a trailing code with too few digits leaves the rest as text */
	assert_stripped ("x\004FF88", STRIP_ALL, "xFF88");
}

static void
test_strip_keeps_hex_without_strip_color (void)
{
	assert_stripped ("\004FF8800red\002", STRIP_ATTRIB, "\004FF8800red");
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/hexcolor/parse/foreground", test_parse_foreground);
	g_test_add_func ("/hexcolor/parse/foreground-and-background", test_parse_foreground_and_background);
	g_test_add_func ("/hexcolor/parse/bad-background", test_parse_bad_background_keeps_foreground);
	g_test_add_func ("/hexcolor/parse/bare", test_parse_bare);
	g_test_add_func ("/hexcolor/parse/length", test_parse_respects_length);
	g_test_add_func ("/hexcolor/strip/all", test_strip);
	g_test_add_func ("/hexcolor/strip/attrib-only", test_strip_keeps_hex_without_strip_color);

	return g_test_run ();
}
