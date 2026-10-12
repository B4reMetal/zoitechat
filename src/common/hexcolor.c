/* ZoiteChat
 * Parsing of \004 (hex color) formatting codes: RRGGBB[,RRGGBB].
 * Kept free of other application dependencies so the xtext widget tests can
 * compile it on its own.
 * Distributed under GPL-2.0-or-later; see COPYING. */

#include "hexcolor.h"

static int
hexcolor_component (const char *s, int avail, guint32 *rgb)
{
	int i;
	guint32 value = 0;

	if (avail < 6)
		return FALSE;
	for (i = 0; i < 6; i++)
	{
		if (!g_ascii_isxdigit (s[i]))
			return FALSE;
		value = (value << 4) | g_ascii_xdigit_value (s[i]);
	}
	if (rgb)
		*rgb = value;
	return TRUE;
}

int
hexcolor_parse (const char *s, int avail, guint32 *fg, guint32 *bg, int *has_bg)
{
	if (has_bg)
		*has_bg = FALSE;
	if (!hexcolor_component (s, avail, fg))
		return 0;
	if (avail >= 7 && s[6] == ',' && hexcolor_component (s + 7, avail - 7, bg))
	{
		if (has_bg)
			*has_bg = TRUE;
		return 13;
	}
	return 6;
}
