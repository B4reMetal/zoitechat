/* XText regression tests. Distributed under GPL-2.0-or-later; see COPYING.
 * Include the implementation to check layout/selection invariants without
 * adding test-only symbols to the widget's public API. Application services
 * are stubbed; GTK, Pango, Cairo and the widget itself are exercised normally. */
#include "../xtext.c"
#include <glib/gstdio.h>

struct zoitechatprefs prefs;

void emoji_font_apply (GtkWidget *widget) { }
int url_last (int *start, int *end) { return 0; }
gsize strftime_utf8 (char *dest, gsize size, const char *format, time_t stamp)
{
	return strftime (dest, size, format, localtime (&stamp));
}
int xtext_get_stamp_str (time_t stamp, char **out)
{
	*out = g_strdup ("[12:34] ");
	return strlen (*out);
}
void theme_get_xtext_colors_for_widget (GtkWidget *widget, XTextColor *colors, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++)
	{
		colors[i].red = colors[i].green = colors[i].blue = 0.0;
		colors[i].alpha = 1.0;
	}
	colors[XTEXT_FG].red = colors[XTEXT_FG].green = colors[XTEXT_FG].blue = 1.0;
	colors[XTEXT_MARK_BG].blue = 1.0;
	colors[XTEXT_MARK_FG] = colors[XTEXT_FG];
	colors[XTEXT_MARKER].red = 1.0;
}

typedef struct { GtkWidget *window; GtkXText *xtext; } Fixture;

static void drain_events (void)
{
	while (g_main_context_iteration (NULL, FALSE)) { }
}

static void fixture_setup (Fixture *f, gconstpointer data)
{
	XTextColor colors[XTEXT_COLS];
	GtkWidget *scrolled = gtk_scrolled_window_new (NULL, NULL);
	theme_get_xtext_colors_for_widget (NULL, colors, G_N_ELEMENTS (colors));
	f->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (f->window), 640, 360);
	f->xtext = GTK_XTEXT (gtk_xtext_new (colors, TRUE));
	g_object_ref (f->xtext);
	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (f->xtext));
	gtk_container_add (GTK_CONTAINER (f->window), scrolled);
	g_assert_true (gtk_xtext_set_font (f->xtext, "monospace 12"));
	gtk_widget_show_all (f->window);
	drain_events ();
}

static void fixture_teardown (Fixture *f, gconstpointer data)
{
	gtk_widget_destroy (f->window);
	g_object_unref (f->xtext);
	drain_events ();
	g_assert_null (image_pending_list);
}

static void append (Fixture *f, const char *text)
{
	gtk_xtext_append (f->xtext->buffer, (guchar *)text, -1, 1234);
}

static cairo_surface_t *snapshot (Fixture *f, gboolean partial)
{
	GtkWidget *widget = GTK_WIDGET (f->xtext);
	int width = gtk_widget_get_allocated_width (widget);
	int height = gtk_widget_get_allocated_height (widget);
	cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
	cairo_t *cr = cairo_create (surface);
	if (partial)
	{
		cairo_rectangle (cr, 0, f->xtext->fontsize, width, height - f->xtext->fontsize);
		cairo_clip (cr);
	}
	gtk_widget_draw (widget, cr);
	g_assert_cmpint (cairo_status (cr), ==, CAIRO_STATUS_SUCCESS);
	cairo_destroy (cr);
	cairo_surface_flush (surface);
	return surface;
}

static void test_empty_and_bounded_append (Fixture *f, gconstpointer data)
{
	const guchar bytes[] = {'a', 'b', 'c'}; /* deliberately no terminating NUL */
	append (f, "");
	g_assert_cmpstr ((char *)f->xtext->buffer->text_last->str, ==, "");
	g_assert_cmpint (f->xtext->buffer->text_last->subline_count, ==, 1);
	append (f, "\n");
	g_assert_cmpint (f->xtext->buffer->text_last->str_len, ==, 0);
	gtk_xtext_append (f->xtext->buffer, (guchar *)bytes, sizeof (bytes), 1234);
	g_assert_cmpstr ((char *)f->xtext->buffer->text_last->str, ==, "abc");
	gtk_xtext_append_indent (f->xtext->buffer, (guchar *)"", 0, (guchar *)"", 0, 1234);
	g_assert_cmpstr ((char *)f->xtext->buffer->text_last->str, ==, " ");
}

static void test_utf8_limits (Fixture *f, gconstpointer data)
{
	GString *text = g_string_new (NULL);
	int i;
	for (i = 0; i < 1500; i++) g_string_append (text, "猫");
	gtk_xtext_append (f->xtext->buffer, (guchar *)text->str, text->len, 1234);
	g_assert_cmpint (f->xtext->buffer->text_last->str_len, ==, 4095);
	g_assert_true (g_utf8_validate ((char *)f->xtext->buffer->text_last->str, -1, NULL));
	gtk_xtext_append_indent (f->xtext->buffer, (guchar *)text->str, text->len,
		(guchar *)"猫", -1, 1234);
	g_assert_cmpint (f->xtext->buffer->text_last->str_len, <=, 4095);
	g_assert_true (g_utf8_validate ((char *)f->xtext->buffer->text_last->str, -1, NULL));
	gtk_xtext_append (f->xtext->buffer, (guchar *)"hello 猫", 8, 1234);
	g_assert_cmpstr ((char *)f->xtext->buffer->text_last->str, ==, "hello ");
	gtk_xtext_append (f->xtext->buffer, (guchar *)"ok\xff" "bad", 6, 1234);
	g_assert_cmpstr ((char *)f->xtext->buffer->text_last->str, ==, "ok");
	g_string_free (text, TRUE);
}

static void test_null_empty_prefix (Fixture *f, gconstpointer data)
{
	xtext_buffer *buf = f->xtext->buffer;
	/* PrintTextLine passes NULL/0 for indented server text without a tab. */
	gtk_xtext_append_indent (buf, NULL, 0, (guchar *)"server notice", -1, 1234);
	g_assert_cmpstr ((char *)buf->text_last->str, ==, " server notice");
	g_assert_cmpint (buf->text_last->left_len, ==, 0);
	g_assert_cmpint (buf->text_last->stamp, ==, 1234);
	gtk_xtext_append_indent (buf, (guchar *)"nick", -1, NULL, 0, 1234);
	g_assert_cmpstr ((char *)buf->text_last->str, ==, "nick ");
	gtk_xtext_append_indent (buf, NULL, 0, NULL, 0, 1234);
	g_assert_cmpstr ((char *)buf->text_last->str, ==, " ");
	g_assert_cmpint (buf->num_lines, ==, 3);
	cairo_surface_destroy (snapshot (f, FALSE));
}

static void test_wrapping (Fixture *f, gconstpointer data)
{
	xtext_buffer *buf = f->xtext->buffer;
	textentry *ent;
	int start = 0, i;
	append (f, "猫猫猫猫猫猫猫猫猫猫");
	ent = buf->text_last;
	/* A character wider than the row must still be kept whole. */
	buf->window_width = 3;
	gtk_xtext_lines_taken (buf, ent);
	g_assert_cmpint (ent->subline_count, ==, 10);
	for (i = 0; i < ent->subline_count; i++)
	{
		int end = gtk_xtext_subline_end (ent, i);
		g_assert_cmpint (end, >, start);
		g_assert_true (g_utf8_validate ((char *)ent->str + start, end - start, NULL));
		start = end;
	}
	buf->window_width = 120;
	gtk_xtext_lines_taken (buf, ent);
	g_assert_cmpint (ent->subline_count, >, 1);
	buf->window_width = 640;
	gtk_xtext_lines_taken (buf, ent);
	g_assert_cmpint (ent->subline_count, ==, 1);
}

static void test_font_isolation (Fixture *f, gconstpointer data)
{
	Fixture second;
	int original = gtk_xtext_text_width (f->xtext, (guchar *)"WWii hello", 10);
	fixture_setup (&second, NULL);
	g_assert_true (gtk_xtext_set_font (second.xtext, "sans 30"));
	g_assert_cmpint (gtk_xtext_text_width (second.xtext, (guchar *)"WWii hello", 10), >, original);
	g_assert_cmpint (gtk_xtext_text_width (f->xtext, (guchar *)"WWii hello", 10), ==, original);
	fixture_teardown (&second, NULL);
	g_assert_cmpint (gtk_xtext_text_width (f->xtext, (guchar *)"WWii hello", 10), ==, original);
}

static void test_wide_metrics (Fixture *f, gconstpointer data)
{
	char *text = g_strnfill (4000, 'W');
	g_assert_true (gtk_xtext_set_font (f->xtext, "sans 40"));
	append (f, text);
	g_assert_cmpint (f->xtext->buffer->text_last->str_width, >, 65535);
	g_assert_cmpint (f->xtext->buffer->text_last->subline_count, >, 1);
	g_free (text);
}

static void test_formatting (Fixture *f, gconstpointer data)
{
	const char *formatted = "a\002bold\002 \035italic\035 \00304,02red\017 \037u\037 \036s\036 \026r\026";
	char *plain;
	GSList *runs = NULL;
	int len;
	plain = (char *)gtk_xtext_strip_color ((guchar *)formatted, strlen (formatted), NULL, &len, &runs, FALSE);
	g_assert_cmpstr (plain, ==, "abold italic red u s r");
	g_assert_cmpint (len, ==, strlen (plain));
	g_assert_cmpint (((offlen_t *)runs->data)->off, ==, 0);
	g_slist_free_full (runs, g_free);
	g_free (plain);
	plain = (char *)gtk_xtext_strip_color ((guchar *)"a\010secret\010b", 10, NULL, &len, NULL, TRUE);
	g_assert_cmpstr (plain, ==, "ab");
	g_free (plain);
	append (f, formatted);
	gtk_xtext_append_indent (f->xtext->buffer, (guchar *)"nick", -1, (guchar *)formatted, -1, 1234);
	f->xtext->auto_indent = TRUE;
	f->xtext->buffer->time_stamp = TRUE;
	f->xtext->buffer->indent = 130;
	gtk_xtext_recalc_widths (f->xtext->buffer, FALSE);
	cairo_surface_destroy (snapshot (f, FALSE));
	g_assert_cmpstr ((char *)f->xtext->buffer->text_last->str, ==,
		"nick a\002bold\002 \035italic\035 \00304,02red\017 \037u\037 \036s\036 \026r\026");
}

static void select_entries (Fixture *f, textentry *start, textentry *end)
{
	textentry *ent;
	for (ent = start; ent; ent = ent->next)
	{
		ent->mark_start = 0;
		ent->mark_end = ent->str_len;
		if (ent == end) break;
	}
	gtk_xtext_selection_render (f->xtext, start, end);
	f->xtext->selection_buffer = f->xtext->buffer;
}

static void test_selection_and_clipboard (Fixture *f, gconstpointer data)
{
	char *selected;
	int len;
	append (f, "");
	append (f, "\002bold\002 猫");
	append (f, "last");
	select_entries (f, f->xtext->buffer->text_first, f->xtext->buffer->text_last);
	selected = gtk_xtext_selection_get_text (f->xtext, &len);
	g_assert_cmpstr (selected, ==, "\nbold 猫\nlast");
	g_assert_cmpint (len, ==, strlen (selected));
	g_free (selected);
	f->xtext->color_paste = TRUE;
	f->xtext->mark_stamp = TRUE;
	selected = gtk_xtext_selection_get_text (f->xtext, &len);
	g_assert_cmpstr (selected, ==, "\n[12:34] \002bold\002 猫\n[12:34] last");
	g_free (selected);
	f->xtext->color_paste = f->xtext->mark_stamp = FALSE;
	gtk_xtext_copy_selection (f->xtext);
	selected = gtk_clipboard_wait_for_text (gtk_widget_get_clipboard (GTK_WIDGET (f->xtext), GDK_SELECTION_CLIPBOARD));
	g_assert_cmpstr (selected, ==, "\nbold 猫\nlast");
	g_free (selected);
	selected = gtk_clipboard_wait_for_text (gtk_widget_get_clipboard (GTK_WIDGET (f->xtext), GDK_SELECTION_PRIMARY));
	g_assert_cmpstr (selected, ==, "\nbold 猫\nlast");
	g_free (selected);
	gtk_xtext_unselect (f->xtext);
	g_assert_false (gtk_xtext_is_selecting (f->xtext));
}

static void test_search (Fixture *f, gconstpointer data)
{
	GError *error = NULL;
	textentry *first;
	xtext_buffer *buf = f->xtext->buffer;
	append (f, "one \002needle\002 then NEEDLE");
	first = buf->text_last;
	append (f, "second needle");
	g_assert_true (gtk_xtext_search (f->xtext, "needle", highlight, &error) == first);
	g_assert_no_error (error);
	g_assert_cmpint (g_list_length (first->marks), ==, 2);
	g_assert_true (gtk_xtext_search (f->xtext, "needle", highlight, &error) == first);
	g_assert_true (gtk_xtext_search (f->xtext, "needle", highlight, &error) == buf->text_last);
	gtk_xtext_search (f->xtext, "", 0, &error);
	g_assert_null (buf->search_found);
	g_assert_true (gtk_xtext_search (f->xtext, "needle", backward | case_match, &error) == buf->text_last);
	gtk_xtext_search (f->xtext, "n[a-z]+", regexp | case_match, &error);
	g_assert_no_error (error);
	g_assert_nonnull (buf->search_re);
	gtk_xtext_search (f->xtext, "[", regexp, &error);
	g_assert_error (error, G_REGEX_ERROR, G_REGEX_ERROR_UNTERMINATED_CHARACTER_CLASS);
	g_clear_error (&error);
	/* A no-match search must also release its regex/text on clear. */
	gtk_xtext_search (f->xtext, "nomatch", regexp, &error);
	g_assert_null (buf->search_found);
	gtk_xtext_clear (buf, 0);
	g_assert_null (buf->search_re);
	g_assert_null (buf->search_text);
}

static void test_follow_search (Fixture *f, gconstpointer data)
{
	xtext_buffer *buf = f->xtext->buffer;
	append (f, "match first");
	gtk_xtext_search (f->xtext, "match", 0, NULL);
	append (f, "match second");
	gtk_xtext_search (f->xtext, NULL, follow, NULL);
	g_assert_cmpint (g_list_length (buf->search_found), ==, 2);
	GList *current = buf->curmark;
	gtk_xtext_search (f->xtext, NULL, 0, NULL);
	gtk_xtext_search (f->xtext, NULL, follow, NULL);
	g_assert_cmpint (g_list_length (buf->search_found), ==, 2);
	g_assert_true (buf->curmark == current);
	append (f, "match third");
	g_assert_cmpint (g_list_length (buf->search_found), ==, 3);
}

static void test_search_unicode_offsets (Fixture *f, gconstpointer data)
{
	xtext_buffer *buf = f->xtext->buffer;
	offsets_t mark;
	append (f, "İ \002NEEDLE\002 Straße");
	gtk_xtext_search (f->xtext, "needle", 0, NULL);
	mark.u = GPOINTER_TO_UINT (buf->text_first->marks->data);
	g_assert_cmpint (mark.o.start, ==, 4);
	/* The established color-offset mapping includes the closing control. */
	g_assert_cmpint (mark.o.end, ==, 11);
	gtk_xtext_search (f->xtext, "ss", 0, NULL);
	mark.u = GPOINTER_TO_UINT (buf->text_first->marks->data);
	g_assert_cmpint (mark.o.start, ==, 16);
	g_assert_cmpint (mark.o.end, ==, 18);
	gtk_xtext_clear (buf, 0);
	append (f, "ſ needle");
	gtk_xtext_search (f->xtext, "needle", 0, NULL);
	mark.u = GPOINTER_TO_UINT (buf->text_first->marks->data);
	g_assert_cmpint (mark.o.start, ==, 3);
	g_assert_cmpint (mark.o.end, ==, 9);
}

static void test_rawlog_hidden_text (Fixture *f, gconstpointer data)
{
	int hidden, visible;
	append (f, "a\010secret\010b");
	hidden = f->xtext->buffer->text_last->str_width;
	f->xtext->ignore_hidden = TRUE;
	gtk_xtext_recalc_widths (f->xtext->buffer, TRUE);
	visible = f->xtext->buffer->text_last->str_width;
	g_assert_cmpint (visible, >, hidden);
	g_assert_cmpint (visible, ==, gtk_xtext_text_width (f->xtext, (guchar *)"asecretb", 8));
}

static void test_save (Fixture *f, gconstpointer data)
{
	char *path, *saved;
	GError *error = NULL;
	int fd = g_file_open_tmp ("zoitechat-xtext-XXXXXX", &path, &error);
	g_assert_no_error (error);
	append (f, "\002bold\002 猫");
	append (f, "");
	append (f, "last");
	gtk_xtext_save (f->xtext, fd);
	close (fd);
	g_assert_true (g_file_get_contents (path, &saved, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (saved, ==, "bold 猫\n\nlast\n");
	g_free (saved);
	g_unlink (path);
	g_free (path);
}

static void test_scrollable_ownership (Fixture *f, gconstpointer data)
{
	GtkAdjustment *horizontal = GTK_ADJUSTMENT (gtk_adjustment_new (0, 0, 1, 1, 1, 1));
	GtkAdjustment *vertical = GTK_ADJUSTMENT (gtk_adjustment_new (0, 0, 100, 1, 10, 10));
	GtkAdjustment *old = g_object_ref (f->xtext->adj);
	g_object_set (f->xtext, "hadjustment", horizontal, "vadjustment", vertical, NULL);
	g_assert_false (g_object_is_floating (horizontal));
	g_assert_false (g_object_is_floating (vertical));
	g_assert_true (f->xtext->hadj == horizontal);
	g_assert_true (f->xtext->adj == vertical);
	/* A replaced adjustment must no longer call into the widget. */
	g_assert_cmpuint (g_signal_handler_find (old, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, f->xtext), ==, 0);
	g_signal_emit_by_name (f->xtext, "set-scroll-adjustments", horizontal, vertical);
	g_assert_true (f->xtext->adj == vertical);
	g_object_set (f->xtext, "hadjustment", NULL, "vadjustment", NULL, NULL);
	g_assert_true (GTK_IS_ADJUSTMENT (f->xtext->hadj));
	g_assert_true (GTK_IS_ADJUSTMENT (f->xtext->adj));
	g_object_unref (old);
}

static void test_clear_and_marker (Fixture *f, gconstpointer data)
{
	xtext_buffer *buf = f->xtext->buffer;
	append (f, "first"); append (f, "second"); append (f, "third");
	buf->marker_pos = buf->text_first;
	buf->marker_state = MARKER_IS_SET;
	gtk_xtext_clear (buf, 1);
	g_assert_cmpstr ((char *)buf->text_first->str, ==, "second");
	g_assert_cmpint (buf->marker_state, ==, MARKER_RESET_BY_CLEAR);
	gtk_xtext_clear (buf, -1);
	g_assert_cmpstr ((char *)buf->text_last->str, ==, "second");
	select_entries (f, buf->text_first, buf->text_last);
	f->xtext->hilight_ent = buf->text_first;
	gtk_xtext_clear (buf, 0);
	g_assert_null (f->xtext->hilight_ent);
	g_assert_null (buf->pagetop_ent);
	g_assert_null (buf->last_ent_start);
	g_assert_cmpint (buf->num_lines, ==, 0);
	g_assert_true (gtk_xtext_is_empty (buf));
	append (f, "after clear");
	g_assert_cmpint (buf->num_lines, ==, 1);
}

static void test_scrollback_limit (Fixture *f, gconstpointer data)
{
	int i;
	xtext_buffer *buf = f->xtext->buffer;
	gtk_xtext_set_max_lines (f->xtext, 10);
	for (i = 0; i < 100; i++) append (f, "line");
	g_assert_cmpint (buf->num_lines, ==, 10);
	g_assert_null (buf->text_first->prev);
	g_assert_null (buf->text_last->next);
	g_assert_cmpfloat (gtk_adjustment_get_value (f->xtext->adj), >=, 0);
}

static void test_follow_trim_oversized_entry (Fixture *f, gconstpointer data)
{
	char *long_text = g_strnfill (4000, 'W');
	gtk_xtext_set_max_lines (f->xtext, 3);
	append (f, "needle");
	gtk_xtext_search (f->xtext, "W", follow, NULL);
	gtk_xtext_clear (f->xtext->buffer, 1);
	append (f, long_text);
	g_assert_true (gtk_xtext_is_empty (f->xtext->buffer));
	g_assert_null (f->xtext->buffer->search_found);
	g_free (long_text);
	append (f, "after oversized");
	gtk_xtext_clear (f->xtext->buffer, G_MININT);
	g_assert_true (gtk_xtext_is_empty (f->xtext->buffer));
}

static void test_stress (Fixture *f, gconstpointer data)
{
	static const char *tokens[] = {"text ", "猫", "😀", "\002bold\002", "\00304,02red\017", "\010hidden\010", "\037u\037", "\036s\036"};
	GRand *random = g_rand_new_with_seed (0x78746578);
	xtext_buffer *buf = f->xtext->buffer;
	int i;
	gtk_xtext_set_max_lines (f->xtext, 80);
	for (i = 0; i < 2000; i++)
	{
		GString *text = g_string_new (NULL);
		textentry *ent;
		int j, lines = 0;
		for (j = 0; j < 30; j++)
			g_string_append (text, tokens[g_rand_int_range (random, 0, G_N_ELEMENTS (tokens))]);
		if (i % 2)
			gtk_xtext_append_indent (buf, (guchar *)"nick", -1, (guchar *)text->str, text->len, 1234);
		else
			gtk_xtext_append (buf, (guchar *)text->str, text->len, 1234);
		g_string_free (text, TRUE);
		if (i % 29 == 0) gtk_xtext_search (f->xtext, "text", follow | highlight, NULL);
		if (i % 31 == 0) gtk_xtext_clear (buf, 1);
		if (i % 37 == 0) gtk_xtext_clear (buf, -1);
		if (i % 101 == 0) gtk_xtext_clear (buf, 0);
		for (ent = buf->text_first; ent; ent = ent->next)
		{
			g_assert_true (g_utf8_validate ((char *)ent->str, ent->str_len, NULL));
			g_assert_cmpint (ent->subline_count, >, 0);
			lines += ent->subline_count;
		}
		g_assert_cmpint (buf->num_lines, ==, lines);
		if (i % 43 == 0) cairo_surface_destroy (snapshot (f, FALSE));
	}
	g_rand_free (random);
}

static void test_buffers (Fixture *f, gconstpointer data)
{
	xtext_buffer *original = f->xtext->buffer;
	xtext_buffer *second = gtk_xtext_buffer_new (f->xtext);
	append (f, "original");
	gtk_xtext_append (second, (guchar *)"alternate", -1, 1234);
	gtk_xtext_buffer_show (f->xtext, second, TRUE);
	g_assert_true (f->xtext->buffer == second);
	cairo_surface_destroy (snapshot (f, FALSE));
	gtk_xtext_buffer_show (f->xtext, original, TRUE);
	g_assert_cmpstr ((char *)f->xtext->buffer->text_first->str, ==, "original");
	gtk_xtext_buffer_free (second);
}

static void test_partial_draw_after_scroll (Fixture *f, gconstpointer data)
{
	cairo_surface_t *partial, *full;
	int i, stride, height, y;
	for (i = 0; i < 80; i++)
	{
		char *line = g_strdup_printf ("line %d — 猫 \002bold\002", i);
		append (f, line);
		g_free (line);
	}
	cairo_surface_destroy (snapshot (f, FALSE));
	gtk_adjustment_set_value (f->xtext->adj, 12.5);
	partial = snapshot (f, TRUE);
	full = snapshot (f, FALSE);
	stride = cairo_image_surface_get_stride (full);
	height = cairo_image_surface_get_height (full);
	for (y = f->xtext->fontsize; y < height; y++)
		g_assert_cmpmem (cairo_image_surface_get_data (partial) + y * stride, stride,
			cairo_image_surface_get_data (full) + y * stride, stride);
	cairo_surface_destroy (partial);
	cairo_surface_destroy (full);
}

static void test_image_lifecycle (Fixture *f, gconstpointer data)
{
	GdkPixbuf *image = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 800, 600);
	xtext_buffer *buf = f->xtext->buffer;
	textentry *ent;
	guint handle;
	int before;
	gdk_pixbuf_fill (image, 0xff0000ff);
	append (f, "image");
	ent = buf->text_last;
	before = buf->num_lines;
	handle = gtk_xtext_image_load_begin (f->xtext, ent, "https://example.invalid/image.png");
	g_assert_cmpuint (handle, !=, 0);
	g_assert_cmpuint (gtk_xtext_image_load_begin (f->xtext, ent, "duplicate"), ==, 0);
	g_assert_true (gtk_xtext_image_load_finish (handle, image));
	g_assert_cmpint (buf->num_lines, >, before);
	g_assert_true (gtk_xtext_entry_has_image (f->xtext, ent));
	g_assert_cmpint (gdk_pixbuf_get_width (ent->image), <=, XTEXT_IMAGE_MAX_WIDTH);
	g_assert_true (gtk_xtext_entry_get_full_image (f->xtext, ent) == image);
	cairo_surface_destroy (snapshot (f, FALSE));
	g_assert_true (gtk_xtext_image_remove (f->xtext, ent));
	g_assert_cmpint (buf->num_lines, ==, before);
	handle = gtk_xtext_image_load_begin (f->xtext, ent, "late");
	gtk_xtext_clear (buf, 0);
	g_assert_false (gtk_xtext_image_load_finish (handle, image));
	g_assert_false (gtk_xtext_entry_has_image (f->xtext, ent));
	g_assert_null (gtk_xtext_entry_get_full_image (f->xtext, ent));
	g_object_unref (image);
}

static void test_background (Fixture *f, gconstpointer data)
{
	cairo_surface_t *background = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 32, 32);
	cairo_t *cr = cairo_create (background);
	cairo_set_source_rgb (cr, 0.2, 0.3, 0.4);
	cairo_paint (cr);
	cairo_destroy (cr);
	gtk_xtext_set_background (f->xtext, background);
	cairo_surface_destroy (background);
	gtk_xtext_set_background (f->xtext, f->xtext->background_surface);
	append (f, "background text");
	cairo_surface_destroy (snapshot (f, FALSE));
	gtk_xtext_set_background (f->xtext, NULL);
	cairo_surface_destroy (snapshot (f, FALSE));
}

static void test_destroy_pending_sources (Fixture *f, gconstpointer data)
{
	guint image;
	append (f, "pending");
	image = gtk_xtext_image_load_begin (f->xtext, f->xtext->buffer->text_last, "late");
	f->xtext->io_tag = g_timeout_add (10000, gtk_xtext_adjustment_timeout, f->xtext);
	f->xtext->add_io_tag = g_timeout_add (10000, gtk_xtext_render_page_timeout, f->xtext);
	f->xtext->scroll_tag = g_timeout_add (10000, gtk_xtext_scrollup_timeout, f->xtext);
	gtk_widget_destroy (f->window);
	g_assert_cmpint (f->xtext->io_tag, ==, 0);
	g_assert_cmpint (f->xtext->add_io_tag, ==, 0);
	g_assert_cmpint (f->xtext->scroll_tag, ==, 0);
	g_assert_false (gtk_xtext_image_load_finish (image, NULL));
	/* GtkWidget destruction must be safe if GObject dispose runs again. */
	g_object_run_dispose (G_OBJECT (f->xtext));
	f->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
}

int main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	if (!gtk_init_check (&argc, &argv))
	{
		g_test_message ("GTK display unavailable; run under Xvfb for widget coverage");
		return 77;
	}
	prefs.hex_gui_mouse_scroll_speed = 10;
#define ADD(name, function) g_test_add ("/xtext/" name, Fixture, NULL, fixture_setup, function, fixture_teardown)
	ADD ("append/empty-bounded", test_empty_and_bounded_append);
	ADD ("append/utf8-limits", test_utf8_limits);
	ADD ("append/null-empty-prefix", test_null_empty_prefix);
	ADD ("layout/wrapping", test_wrapping);
	ADD ("layout/font-isolation", test_font_isolation);
	ADD ("layout/wide-metrics", test_wide_metrics);
	ADD ("render/formatting-timestamps", test_formatting);
	ADD ("selection/clipboard", test_selection_and_clipboard);
	ADD ("search/navigation-regex-clear", test_search);
	ADD ("search/follow", test_follow_search);
	ADD ("search/unicode-offsets", test_search_unicode_offsets);
	ADD ("render/rawlog-hidden", test_rawlog_hidden_text);
	ADD ("buffer/save", test_save);
	ADD ("lifecycle/scrollable-ownership", test_scrollable_ownership);
	ADD ("buffer/clear-marker", test_clear_and_marker);
	ADD ("buffer/scrollback-limit", test_scrollback_limit);
	ADD ("buffer/follow-trim-oversized", test_follow_trim_oversized_entry);
	ADD ("buffer/stress", test_stress);
	ADD ("buffer/switch", test_buffers);
	ADD ("render/partial-scroll", test_partial_draw_after_scroll);
	ADD ("images/lifecycle", test_image_lifecycle);
	ADD ("render/background", test_background);
	ADD ("lifecycle/pending-sources", test_destroy_pending_sources);
#undef ADD
	return g_test_run ();
}
