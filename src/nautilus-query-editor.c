/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * Nautilus is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nautilus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; see the file COPYING.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Alexander Larsson <alexl@redhat.com>
 *
 */

#include <config.h>
#include "nautilus-query-editor.h"

#include <string.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <eel/eel-glib-extensions.h>
#include <libnautilus-private/nautilus-file-utilities.h>

#define SEARCH_ENTRY_WIDTH 320

struct NautilusQueryEditorDetails {
	GtkWidget *entry;
	gboolean change_frozen;
	guint typing_timeout_id;

	char *current_uri;

	NautilusQuery *query;
};

enum {
	ACTIVATED,
	CHANGED,
	CANCEL,
	LAST_SIGNAL
}; 

static guint signals[LAST_SIGNAL];

static void entry_activate_cb (GtkWidget *entry, NautilusQueryEditor *editor);
static void entry_changed_cb  (GtkWidget *entry, NautilusQueryEditor *editor);
static void nautilus_query_editor_changed_force (NautilusQueryEditor *editor,
						 gboolean             force);
static void nautilus_query_editor_changed (NautilusQueryEditor *editor);

G_DEFINE_TYPE (NautilusQueryEditor, nautilus_query_editor, GTK_TYPE_BOX);

gboolean
nautilus_query_editor_handle_event (NautilusQueryEditor *editor,
				    GdkEventKey         *event)
{
	GtkWidget *toplevel;
	GtkWidget *old_focus;
	GdkEvent *new_event;
	gboolean retval;

	/* if we're focused already, no need to handle the event manually */
	if (gtk_widget_has_focus (editor->details->entry)) {
		return FALSE;
	}

	/* never handle these events */
	if (event->keyval == GDK_KEY_slash || event->keyval == GDK_KEY_Delete) {
		return FALSE;
	}

	/* don't activate search for these events */
	if (!gtk_widget_get_visible (GTK_WIDGET (editor)) && event->keyval == GDK_KEY_space) {
		return FALSE;
	}

	/* if it's not printable we don't need it */
	if (!g_unichar_isprint (gdk_keyval_to_unicode (event->keyval))) {
		return FALSE;
	}

	if (!gtk_widget_get_realized (editor->details->entry)) {
		gtk_widget_realize (editor->details->entry);
	}

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (editor));
	if (gtk_widget_is_toplevel (toplevel)) {
		old_focus = gtk_window_get_focus (GTK_WINDOW (toplevel));
	} else {
		old_focus = NULL;
	}

	/* input methods will typically only process events after getting focus */
	gtk_widget_grab_focus (editor->details->entry);

	new_event = gdk_event_copy ((GdkEvent *) event);
	g_object_unref (((GdkEventKey *) new_event)->window);
	((GdkEventKey *) new_event)->window = g_object_ref
		(gtk_widget_get_window (editor->details->entry));
	retval = gtk_widget_event (editor->details->entry, new_event);
	gdk_event_free (new_event);

	if (!retval && old_focus) {
		gtk_widget_grab_focus (old_focus);
	}

	return retval;
}


static void
nautilus_query_editor_dispose (GObject *object)
{
	NautilusQueryEditor *editor;

	editor = NAUTILUS_QUERY_EDITOR (object);

	if (editor->details->typing_timeout_id > 0) {
		g_source_remove (editor->details->typing_timeout_id);
		editor->details->typing_timeout_id = 0;
	}

	g_clear_object (&editor->details->query);

	G_OBJECT_CLASS (nautilus_query_editor_parent_class)->dispose (object);
}

static void
nautilus_query_editor_grab_focus (GtkWidget *widget)
{
	NautilusQueryEditor *editor = NAUTILUS_QUERY_EDITOR (widget);

	if (gtk_widget_get_visible (widget) && !gtk_widget_is_focus (editor->details->entry)) {
		/* avoid selecting the entry text */
		gtk_widget_grab_focus (editor->details->entry);
		gtk_editable_set_position (GTK_EDITABLE (editor->details->entry), -1);
	}
}

static void
entry_activate_cb (GtkWidget *entry, NautilusQueryEditor *editor)
{
	g_signal_emit (editor, signals[ACTIVATED], 0);
}

static gboolean
entry_key_press_event_cb (GtkWidget           *widget,
			  GdkEventKey         *event,
			  NautilusQueryEditor *editor)
{
	if (event->keyval == GDK_KEY_Down) {
		nautilus_window_grab_focus (NAUTILUS_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (widget))));
	}
	return FALSE;
}


static gboolean
typing_timeout_cb (gpointer user_data)
{
	NautilusQueryEditor *editor;

	editor = NAUTILUS_QUERY_EDITOR (user_data);
	editor->details->typing_timeout_id = 0;

	nautilus_query_editor_changed (editor);

	return FALSE;
}

#define TYPING_TIMEOUT 250

static void
entry_changed_cb (GtkWidget *entry, NautilusQueryEditor *editor)
{
	if (editor->details->change_frozen) {
		return;
	}

	if (editor->details->typing_timeout_id > 0) {
		g_source_remove (editor->details->typing_timeout_id);
	}

	editor->details->typing_timeout_id =
		g_timeout_add (TYPING_TIMEOUT,
			       typing_timeout_cb,
			       editor);
}

static void
nautilus_query_editor_constructed (GObject *object)
{
	NautilusQueryEditor *editor = NAUTILUS_QUERY_EDITOR (object);

	G_OBJECT_CLASS (nautilus_query_editor_parent_class)->constructed (object);

	/* create the search entry */
	editor->details->entry = gtk_search_entry_new ();
	gtk_widget_set_size_request (editor->details->entry, SEARCH_ENTRY_WIDTH, -1);
	gtk_widget_set_halign (editor->details->entry, GTK_ALIGN_CENTER);
	gtk_container_add (GTK_CONTAINER (editor), editor->details->entry);

	g_signal_connect (editor->details->entry, "key-press-event",
			  G_CALLBACK (entry_key_press_event_cb), editor);
	g_signal_connect (editor->details->entry, "activate",
			  G_CALLBACK (entry_activate_cb), editor);
	g_signal_connect (editor->details->entry, "changed",
			  G_CALLBACK (entry_changed_cb), editor);

	/* show everything */
	gtk_widget_show_all (GTK_WIDGET (editor));
}

static void
nautilus_query_editor_class_init (NautilusQueryEditorClass *class)
{
	GObjectClass *gobject_class;
	GtkWidgetClass *widget_class;
	GtkBindingSet *binding_set;

	gobject_class = G_OBJECT_CLASS (class);
        gobject_class->dispose = nautilus_query_editor_dispose;
	gobject_class->constructed = nautilus_query_editor_constructed;

	widget_class = GTK_WIDGET_CLASS (class);
	widget_class->grab_focus = nautilus_query_editor_grab_focus;

	signals[CHANGED] =
		g_signal_new ("changed",
		              G_TYPE_FROM_CLASS (class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NautilusQueryEditorClass, changed),
		              NULL, NULL,
		              g_cclosure_marshal_generic,
		              G_TYPE_NONE, 2, NAUTILUS_TYPE_QUERY, G_TYPE_BOOLEAN);

	signals[CANCEL] =
		g_signal_new ("cancel",
		              G_TYPE_FROM_CLASS (class),
		              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		              G_STRUCT_OFFSET (NautilusQueryEditorClass, cancel),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	signals[ACTIVATED] =
		g_signal_new ("activated",
		              G_TYPE_FROM_CLASS (class),
		              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		              G_STRUCT_OFFSET (NautilusQueryEditorClass, activated),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	binding_set = gtk_binding_set_by_class (class);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_Escape, 0, "cancel", 0);

	g_type_class_add_private (class, sizeof (NautilusQueryEditorDetails));
}

GFile *
nautilus_query_editor_get_location (NautilusQueryEditor *editor)
{
	GFile *file = NULL;
	if (editor->details->current_uri != NULL)
		file = g_file_new_for_uri (editor->details->current_uri);
	return file;
}

static void
nautilus_query_editor_init (NautilusQueryEditor *editor)
{
	editor->details = G_TYPE_INSTANCE_GET_PRIVATE (editor, NAUTILUS_TYPE_QUERY_EDITOR,
						       NautilusQueryEditorDetails);

	gtk_orientable_set_orientation (GTK_ORIENTABLE (editor), GTK_ORIENTATION_VERTICAL);
}

static void
nautilus_query_editor_changed_force (NautilusQueryEditor *editor, gboolean force_reload)
{
	NautilusQuery *query;

	if (editor->details->change_frozen) {
		return;
	}

	query = nautilus_query_editor_get_query (editor);
	g_signal_emit (editor, signals[CHANGED], 0,
		       query, force_reload);
	g_object_unref (query);
}

static void
nautilus_query_editor_changed (NautilusQueryEditor *editor)
{
	nautilus_query_editor_changed_force (editor, TRUE);
}

static void
add_location_to_query (NautilusQueryEditor *editor,
		       NautilusQuery       *query)
{
	char *uri;

	uri = nautilus_get_home_directory_uri ();
	nautilus_query_set_location (query, uri);
	g_free (uri);
}

NautilusQuery *
nautilus_query_editor_get_query (NautilusQueryEditor *editor)
{
	const char *query_text;
	NautilusQuery *query;

	if (editor == NULL || editor->details == NULL || editor->details->entry == NULL) {
		return NULL;
	}

	query_text = gtk_entry_get_text (GTK_ENTRY (editor->details->entry));

	query = nautilus_query_new ();
	nautilus_query_set_text (query, query_text);

	add_location_to_query (editor, query);

	return query;
}

GtkWidget *
nautilus_query_editor_new (void)
{
	return g_object_new (NAUTILUS_TYPE_QUERY_EDITOR, NULL);
}

void
nautilus_query_editor_set_location (NautilusQueryEditor *editor,
				    GFile               *location)
{
	g_free (editor->details->current_uri);
	editor->details->current_uri = g_file_get_uri (location);
}

void
nautilus_query_editor_set_query (NautilusQueryEditor	*editor,
				 NautilusQuery		*query)
{
	char *text = NULL;
	char *current_text = NULL;

	if (query != NULL) {
		text = nautilus_query_get_text (query);
	}

	if (!text) {
		text = g_strdup ("");
	}

	editor->details->change_frozen = TRUE;

	current_text = g_strstrip (g_strdup (gtk_entry_get_text (GTK_ENTRY (editor->details->entry))));
	if (!g_str_equal (current_text, text)) {
		gtk_entry_set_text (GTK_ENTRY (editor->details->entry), text);
	}
	g_free (current_text);

	g_free (editor->details->current_uri);
	editor->details->current_uri = NULL;

	g_clear_object (&editor->details->query);

	if (query != NULL) {
		editor->details->query = g_object_ref (query);
		editor->details->current_uri = nautilus_query_get_location (query);
	}

	editor->details->change_frozen = FALSE;

	g_free (text);
}
