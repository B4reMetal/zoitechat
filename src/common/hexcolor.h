/* ZoiteChat
 * Parsing of \004 (hex color) formatting codes: RRGGBB[,RRGGBB].
 * See https://modern.ircdocs.horse/formatting
 * Distributed under GPL-2.0-or-later; see COPYING. */

#ifndef ZOITECHAT_HEXCOLOR_H
#define ZOITECHAT_HEXCOLOR_H

#include <glib.h>

/* Parses the parameters following a \004 byte. Returns the number of bytes
 * consumed (6 or 13), or 0 if no valid RRGGBB follows. fg, bg and has_bg
 * may be NULL. */
int hexcolor_parse (const char *s, int avail, guint32 *fg, guint32 *bg, int *has_bg);

#endif
