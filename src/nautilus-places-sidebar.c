/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 *  Nautilus
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this library; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Authors : Mr Jamie McCracken (jamiemcc at blueyonder dot co dot uk)
 *            Cosimo Cecchi <cosimoc@gnome.org>
 *
 */
 
#include <config.h>

#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include <libnautilus-private/nautilus-dnd.h>
#include <libnautilus-private/nautilus-bookmark.h>
#include <libnautilus-private/nautilus-global-preferences.h>
#include <libnautilus-private/nautilus-file.h>
#include <libnautilus-private/nautilus-file-utilities.h>
#include <libnautilus-private/nautilus-file-operations.h>
#include <libnautilus-private/nautilus-trash-monitor.h>
#include <libnautilus-private/nautilus-icon-names.h>

#include <eel/eel-debug.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-glib-extensions.h>
#include <eel/eel-graphic-effects.h>
#include <eel/eel-string.h>
#include <eel/eel-stock-dialogs.h>

#include "empathy-cell-renderer-expander.h"
#include "nautilus-application.h"
#include "nautilus-bookmark-list.h"
#include "nautilus-places-sidebar.h"
#include "nautilus-properties-window.h"
#include "nautilus-window.h"
#include "nautilus-window-slot.h"

#define DEBUG_FLAG NAUTILUS_DEBUG_PLACES
#include <libnautilus-private/nautilus-debug.h>

#define TREE_VIEW_INDENT_XPAD 12
#define INITIAL_XPAD 8
#define INITIAL_YPAD 6
#define EJECT_BUTTON_XPAD 6

typedef struct SidebarTreeData SidebarTreeData;

typedef struct {
	GtkBox              parent;

	SidebarTreeData    *tree_data;
	SidebarTreeData    *secondary_tree_data;

	char 	           *uri;
	NautilusWindow *window;
	NautilusBookmarkList *bookmarks;
	GVolumeMonitor *volume_monitor;

	/* DnD */
	GList     *drag_list;
	gboolean  drag_data_received;
	int       drag_data_info;
	gboolean  drop_occured;
	char     *target_uri;

	GtkWidget *popup_menu;
	GtkWidget *popup_menu_open_in_new_tab_item;
	GtkWidget *popup_menu_add_shortcut_item;
	GtkWidget *popup_menu_remove_item;
	GtkWidget *popup_menu_rename_item;
	GtkWidget *popup_menu_separator_item;
	GtkWidget *popup_menu_mount_item;
	GtkWidget *popup_menu_unmount_item;
	GtkWidget *popup_menu_eject_item;
	GtkWidget *popup_menu_rescan_item;
	GtkWidget *popup_menu_empty_trash_item;
	GtkWidget *popup_menu_start_item;
	GtkWidget *popup_menu_stop_item;
	GtkWidget *popup_menu_properties_separator_item;
	GtkWidget *popup_menu_properties_item;
	GtkWidget *popup_menu_format_item;

	/* volume mounting - delayed open process */
	gboolean mounting;
	NautilusWindowSlot *go_to_after_mount_slot;
	NautilusWindowOpenFlags go_to_after_mount_flags;

	GDBusProxy *hostnamed_proxy;
	GCancellable *hostnamed_cancellable;
	char *hostname;

	guint bookmarks_changed_id;
	guint switch_location_timer;
} NautilusPlacesSidebar;

struct SidebarTreeData {
	GtkTreeView *tree_view;
	GtkTreeStore *store;
	GtkCellRenderer *normal_text_cell_renderer;
	GtkCellRenderer *eject_icon_cell_renderer;
	NautilusPlacesSidebar *sidebar;

	gboolean devices_separator_added;
	gboolean bookmarks_separator_added;
};

typedef struct {
	GtkBoxClass parent;
} NautilusPlacesSidebarClass;

typedef struct {
        GObject parent;
} NautilusPlacesSidebarProvider;

typedef struct {
        GObjectClass parent;
} NautilusPlacesSidebarProviderClass;

enum {
	PLACES_SIDEBAR_COLUMN_ROW_TYPE,
	PLACES_SIDEBAR_COLUMN_URI,
	PLACES_SIDEBAR_COLUMN_DRIVE,
	PLACES_SIDEBAR_COLUMN_VOLUME,
	PLACES_SIDEBAR_COLUMN_MOUNT,
	PLACES_SIDEBAR_COLUMN_NAME,
	PLACES_SIDEBAR_COLUMN_GICON,
	PLACES_SIDEBAR_COLUMN_INDEX,
	PLACES_SIDEBAR_COLUMN_EJECT,
	PLACES_SIDEBAR_COLUMN_NO_EJECT,
	PLACES_SIDEBAR_COLUMN_BOOKMARK,
	PLACES_SIDEBAR_COLUMN_TOOLTIP,
	PLACES_SIDEBAR_COLUMN_SECTION_TYPE,

	PLACES_SIDEBAR_COLUMN_COUNT
};

typedef enum {
	PLACES_BUILT_IN,
	PLACES_XDG_DIR,
	PLACES_MOUNTED_VOLUME,
	PLACES_BOOKMARK,
	PLACES_SEPARATOR,
	PLACES_CONNECT_SERVER,
	PLACES_EXPANDER
} PlaceType;

typedef enum {
	SECTION_DEVICES,
	SECTION_BOOKMARKS,
	SECTION_COMPUTER,
	SECTION_NETWORK,
} SectionType;

static void  nautilus_places_sidebar_style_set         (GtkWidget                    *widget,
							GtkStyle                     *previous_style);

/* Identifiers for target types */
enum {
	GTK_TREE_MODEL_ROW,
	TEXT_URI_LIST
};

/* Target types for dragging from the shortcuts list */
static const GtkTargetEntry nautilus_shortcuts_source_targets[] = {
	{ "GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_WIDGET, GTK_TREE_MODEL_ROW }
};

/* Target types for dropping into the shortcuts list */
static const GtkTargetEntry nautilus_shortcuts_drop_targets [] = {
	{ "GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_WIDGET, GTK_TREE_MODEL_ROW },
	{ "text/uri-list", 0, TEXT_URI_LIST }
};

/* Drag and drop interface declarations */
typedef struct {
	GtkTreeStore parent;

	NautilusPlacesSidebar *sidebar;
} NautilusShortcutsModel;

typedef struct {
	GtkTreeStoreClass parent_class;
} NautilusShortcutsModelClass;

GType _nautilus_shortcuts_model_get_type (void);
static void _nautilus_shortcuts_model_drag_source_init (GtkTreeDragSourceIface *iface);
G_DEFINE_TYPE_WITH_CODE (NautilusShortcutsModel, _nautilus_shortcuts_model, GTK_TYPE_TREE_STORE,
			 G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_DRAG_SOURCE,
						_nautilus_shortcuts_model_drag_source_init));
static GtkTreeStore *nautilus_shortcuts_model_new (NautilusPlacesSidebar *sidebar);

G_DEFINE_TYPE (NautilusPlacesSidebar, nautilus_places_sidebar, GTK_TYPE_BOX);

static GtkTreeIter
add_separator (SidebarTreeData *data,
	       SectionType section_type)
{
	GtkTreeIter iter;

	gtk_tree_store_append (data->store, &iter, NULL);
	gtk_tree_store_set (data->store, &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, PLACES_SEPARATOR,
			    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, section_type,	
			    -1);

	return iter;
}

static GtkTreeIter
add_expander (SidebarTreeData *data,
	      SectionType section_type)
{
	GtkTreeIter iter;

	gtk_tree_store_append (data->store, &iter, NULL);
	gtk_tree_store_set (data->store, &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, PLACES_EXPANDER,
			    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, section_type,	
			    -1);

	return iter;
}

static void
check_separator_for_section (SidebarTreeData *data,
			     SectionType section_type)
{
	switch (section_type) {
	case SECTION_DEVICES:
		if (!data->devices_separator_added) {
			add_separator (data, SECTION_DEVICES);
			data->devices_separator_added = TRUE;
		}

		break;
	case SECTION_BOOKMARKS:
		if (!data->bookmarks_separator_added) {
			add_separator (data, SECTION_BOOKMARKS);
			data->bookmarks_separator_added = TRUE;
		}

		break;
	default:
		break;
	}
}

static void
check_unmount_and_eject (GMount *mount,
			 GVolume *volume,
			 GDrive *drive,
			 gboolean *show_unmount,
			 gboolean *show_eject)
{
	*show_unmount = FALSE;
	*show_eject = FALSE;

	if (drive != NULL) {
		*show_eject = g_drive_can_eject (drive);
	}

	if (volume != NULL) {
		*show_eject |= g_volume_can_eject (volume);
	}
	if (mount != NULL) {
		*show_eject |= g_mount_can_eject (mount);
		*show_unmount = g_mount_can_unmount (mount) && !*show_eject;
	}
}

static void
add_place (SidebarTreeData *data,
	   GtkTreeIter *parent_iter,
	   PlaceType place_type,
	   SectionType section_type,
	   const char *name,
	   GIcon *icon,
	   const char *uri,
	   GDrive *drive,
	   GVolume *volume,
	   GMount *mount,
	   const int index,
	   const char *tooltip)
{
	GtkTreeIter iter;
	gboolean show_eject, show_unmount;
	gboolean show_eject_button;

	check_separator_for_section (data, section_type);

	check_unmount_and_eject (mount, volume, drive,
				 &show_unmount, &show_eject);

	if (show_unmount || show_eject) {
		g_assert (place_type != PLACES_BOOKMARK);
	}

	if (mount == NULL) {
		show_eject_button = FALSE;
	} else {
		show_eject_button = (show_unmount || show_eject);
	}

	gtk_tree_store_append (data->store, &iter, parent_iter);
	gtk_tree_store_set (data->store, &iter,
			    PLACES_SIDEBAR_COLUMN_GICON, icon,
			    PLACES_SIDEBAR_COLUMN_NAME, name,
			    PLACES_SIDEBAR_COLUMN_URI, uri,
			    PLACES_SIDEBAR_COLUMN_DRIVE, drive,
			    PLACES_SIDEBAR_COLUMN_VOLUME, volume,
			    PLACES_SIDEBAR_COLUMN_MOUNT, mount,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, place_type,
			    PLACES_SIDEBAR_COLUMN_INDEX, index,
			    PLACES_SIDEBAR_COLUMN_EJECT, show_eject_button,
			    PLACES_SIDEBAR_COLUMN_NO_EJECT, !show_eject_button,
			    PLACES_SIDEBAR_COLUMN_BOOKMARK, place_type != PLACES_BOOKMARK,
			    PLACES_SIDEBAR_COLUMN_TOOLTIP, tooltip,
			    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, section_type,
			    -1);
}

typedef struct {
	const gchar *location;
	const gchar *last_uri;
	GtkTreePath *path;
} RestoreLocationData;

static gboolean
restore_selection_foreach (GtkTreeModel *model,
			   GtkTreePath *path,
			   GtkTreeIter *iter,
			   gpointer user_data)
{
	RestoreLocationData *data = user_data;
	gchar *uri;

	gtk_tree_model_get (model, iter,
			    PLACES_SIDEBAR_COLUMN_URI, &uri,
			    -1);

	if (g_strcmp0 (uri, data->last_uri) == 0 ||
	    g_strcmp0 (uri, data->location) == 0) {
		data->path = gtk_tree_path_copy (path);
	}

	g_free (uri);

	return (data->path != NULL);
}

static gboolean
sidebar_update_restore_selection_for_tree_data (SidebarTreeData *tree_data,
						const gchar *location,
						const gchar *last_uri)
{
	RestoreLocationData data;
	GtkTreeSelection *selection;

	data.location = location;
	data.last_uri = last_uri;
	data.path = NULL;

	gtk_tree_model_foreach (GTK_TREE_MODEL (tree_data->store),
				restore_selection_foreach, &data);

	if (data.path != NULL) {
		selection = gtk_tree_view_get_selection (tree_data->tree_view);
		gtk_tree_selection_select_path (selection, data.path);
		gtk_tree_path_free (data.path);

		return TRUE;
	}

	return FALSE;
}

static void
sidebar_update_restore_selection (NautilusPlacesSidebar *sidebar,
				  const gchar *location,
				  const gchar *last_uri)
{
	if (!sidebar_update_restore_selection_for_tree_data (sidebar->tree_data, location, last_uri)) {
		sidebar_update_restore_selection_for_tree_data
			(sidebar->secondary_tree_data, location, last_uri);
	}
}

static gchar *
places_sidebar_get_last_selected_uri_for_tree_data (SidebarTreeData *data)
{
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeIter last_iter;
	gchar *last_uri = NULL;

	selection = gtk_tree_view_get_selection (data->tree_view);
	if (gtk_tree_selection_get_selected (selection, &model, &last_iter)) {
		gtk_tree_model_get (model,
				    &last_iter,
				    PLACES_SIDEBAR_COLUMN_URI, &last_uri, -1);
	}

	return last_uri;
}

static gchar *
places_sidebar_get_last_selected_uri (NautilusPlacesSidebar *sidebar)
{
	gchar *last_uri;

	last_uri = places_sidebar_get_last_selected_uri_for_tree_data (sidebar->tree_data);
	if (last_uri == NULL) {
		last_uri = places_sidebar_get_last_selected_uri_for_tree_data
			(sidebar->secondary_tree_data);
	}

	return last_uri;
}

static void
add_special_dirs (SidebarTreeData *data)
{
	GList *dirs, *l;
	NautilusPlacesSidebar *sidebar;

	sidebar = data->sidebar;
	dirs = nautilus_get_default_xdg_directories ();

	for (l = dirs; l != NULL; l = l->next) {
		char *name;
		GFile *root;
		GIcon *icon;
		char *mount_uri;
		char *tooltip;
		NautilusBookmark *bookmark;
		guint idx;
		GUserDirectory user_dir;

		user_dir = GPOINTER_TO_INT (l->data);
		root = g_file_new_for_path (g_get_user_special_dir (user_dir));

		/* Don't add the bookmark to the sidebar if it was removed from
		 * the user dir list, or if its location doesn't exist.
		 */
		bookmark = nautilus_bookmark_list_item_with_location (sidebar->bookmarks, root, &idx);
		if (bookmark && !nautilus_bookmark_get_exists (bookmark)) {
			g_object_unref (root);
			continue;
		}

		if (bookmark) {
			name = g_strdup (nautilus_bookmark_get_name (bookmark));
			icon = nautilus_bookmark_get_symbolic_icon (bookmark);
		} else {
			name = g_file_get_basename (root);
			icon = nautilus_special_directory_get_symbolic_icon (user_dir);
			idx = -1;
		}

		mount_uri = g_file_get_uri (root);
		tooltip = g_file_get_parse_name (root);

		add_place (data, NULL,
			   PLACES_XDG_DIR,
			   SECTION_COMPUTER,
			   name, icon, mount_uri,
			   NULL, NULL, NULL, idx,
			   tooltip);

		g_object_unref (root);
		g_object_unref (icon);
		g_free (name);
		g_free (mount_uri);
		g_free (tooltip);
	}

	g_list_free (dirs);
}

static void
clear_tree_data (SidebarTreeData *data)
{
	gtk_tree_store_clear (data->store);

	data->devices_separator_added = FALSE;
	data->bookmarks_separator_added = FALSE;
}

static void
update_places (NautilusPlacesSidebar *sidebar)
{
	NautilusBookmark *bookmark;
	GtkTreeIter network_iter;
	GVolumeMonitor *volume_monitor;
	GList *mounts, *l, *ll;
	GMount *mount;
	GList *drives;
	GDrive *drive;
	GList *volumes;
	GVolume *volume;
	int bookmark_count, index;
	char *location, *mount_uri, *name, *last_uri, *identifier;
	const gchar *bookmark_name;
	GIcon *icon;
	GFile *root;
	NautilusWindowSlot *slot;
	char *tooltip;
	GList *network_mounts, *network_volumes;

	DEBUG ("Updating places sidebar");

	last_uri = places_sidebar_get_last_selected_uri (sidebar);

	clear_tree_data (sidebar->tree_data);
	clear_tree_data (sidebar->secondary_tree_data);

	slot = nautilus_window_get_active_slot (sidebar->window);
	location = nautilus_window_slot_get_current_uri (slot);

	network_mounts = network_volumes = NULL;
	volume_monitor = sidebar->volume_monitor;

	/* home folder */
	mount_uri = nautilus_get_home_directory_uri ();
	icon = g_themed_icon_new (NAUTILUS_ICON_HOME);
	add_place (sidebar->tree_data, NULL,
		   PLACES_BUILT_IN,
		   SECTION_COMPUTER,
		   _("Home"), icon, mount_uri,
		   NULL, NULL, NULL, 0,
		   _("Open your personal folder"));
	g_object_unref (icon);
	g_free (mount_uri);

	/* add built in bookmarks (XDG directories) */
	add_special_dirs (sidebar->tree_data);

	/* go through all connected drives */
	drives = g_volume_monitor_get_connected_drives (volume_monitor);

	for (l = drives; l != NULL; l = l->next) {
		drive = l->data;

		volumes = g_drive_get_volumes (drive);
		if (volumes != NULL) {
			for (ll = volumes; ll != NULL; ll = ll->next) {
				volume = ll->data;
				identifier = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_CLASS);

				if (g_strcmp0 (identifier, "network") == 0) {
					g_free (identifier);
					network_volumes = g_list_prepend (network_volumes, volume);
					continue;
				}
				g_free (identifier);

				mount = g_volume_get_mount (volume);
				if (mount != NULL) {
					/* Show mounted volume in the sidebar */
					icon = g_mount_get_symbolic_icon (mount);
					root = g_mount_get_default_location (mount);
					mount_uri = g_file_get_uri (root);
					name = g_mount_get_name (mount);
					tooltip = g_file_get_parse_name (root);

					add_place (sidebar->tree_data, NULL,
						   PLACES_MOUNTED_VOLUME,
						   SECTION_DEVICES,
						   name, icon, mount_uri,
						   drive, volume, mount, 0, tooltip);
					g_object_unref (root);
					g_object_unref (mount);
					g_object_unref (icon);
					g_free (tooltip);
					g_free (name);
					g_free (mount_uri);
				} else {
					/* Do show the unmounted volumes in the sidebar;
					 * this is so the user can mount it (in case automounting
					 * is off).
					 *
					 * Also, even if automounting is enabled, this gives a visual
					 * cue that the user should remember to yank out the media if
					 * he just unmounted it.
					 */
					icon = g_volume_get_symbolic_icon (volume);
					name = g_volume_get_name (volume);
					tooltip = g_strdup_printf (_("Mount and open %s"), name);

					add_place (sidebar->tree_data, NULL,
						   PLACES_MOUNTED_VOLUME,
						   SECTION_DEVICES,
						   name, icon, NULL,
						   drive, volume, NULL, 0, tooltip);
					g_object_unref (icon);
					g_free (name);
					g_free (tooltip);
				}
				g_object_unref (volume);
			}
			g_list_free (volumes);
		} else {
			if (g_drive_is_media_removable (drive) && !g_drive_is_media_check_automatic (drive)) {
				/* If the drive has no mountable volumes and we cannot detect media change.. we
				 * display the drive in the sidebar so the user can manually poll the drive by
				 * right clicking and selecting "Rescan..."
				 *
				 * This is mainly for drives like floppies where media detection doesn't
				 * work.. but it's also for human beings who like to turn off media detection
				 * in the OS to save battery juice.
				 */
				icon = g_drive_get_symbolic_icon (drive);
				name = g_drive_get_name (drive);
				tooltip = g_strdup_printf (_("Mount and open %s"), name);

				add_place (sidebar->tree_data, NULL,
					   PLACES_BUILT_IN,
					   SECTION_DEVICES,
					   name, icon, NULL,
					   drive, NULL, NULL, 0, tooltip);
				g_object_unref (icon);
				g_free (tooltip);
				g_free (name);
			}
		}
		g_object_unref (drive);
	}
	g_list_free (drives);

	/* add all volumes that is not associated with a drive */
	volumes = g_volume_monitor_get_volumes (volume_monitor);
	for (l = volumes; l != NULL; l = l->next) {
		volume = l->data;
		drive = g_volume_get_drive (volume);
		if (drive != NULL) {
		    	g_object_unref (volume);
			g_object_unref (drive);
			continue;
		}

		identifier = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_CLASS);

		if (g_strcmp0 (identifier, "network") == 0) {
			g_free (identifier);
			network_volumes = g_list_prepend (network_volumes, volume);
			continue;
		}
		g_free (identifier);

		mount = g_volume_get_mount (volume);
		if (mount != NULL) {
			icon = g_mount_get_symbolic_icon (mount);
			root = g_mount_get_default_location (mount);
			mount_uri = g_file_get_uri (root);
			tooltip = g_file_get_parse_name (root);
			g_object_unref (root);
			name = g_mount_get_name (mount);
			add_place (sidebar->tree_data, NULL,
				   PLACES_MOUNTED_VOLUME,
				   SECTION_DEVICES,
				   name, icon, mount_uri,
				   NULL, volume, mount, 0, tooltip);
			g_object_unref (mount);
			g_object_unref (icon);
			g_free (name);
			g_free (tooltip);
			g_free (mount_uri);
		} else {
			/* see comment above in why we add an icon for an unmounted mountable volume */
			icon = g_volume_get_symbolic_icon (volume);
			name = g_volume_get_name (volume);
			add_place (sidebar->tree_data, NULL,
				   PLACES_MOUNTED_VOLUME,
				   SECTION_DEVICES,
				   name, icon, NULL,
				   NULL, volume, NULL, 0, name);
			g_object_unref (icon);
			g_free (name);
		}
		g_object_unref (volume);
	}
	g_list_free (volumes);

	/* add mounts that has no volume (/etc/mtab mounts, ftp, sftp,...) */
	mounts = g_volume_monitor_get_mounts (volume_monitor);

	for (l = mounts; l != NULL; l = l->next) {
		mount = l->data;
		if (g_mount_is_shadowed (mount)) {
			g_object_unref (mount);
			continue;
		}
		volume = g_mount_get_volume (mount);
		if (volume != NULL) {
		    	g_object_unref (volume);
			g_object_unref (mount);
			continue;
		}
		root = g_mount_get_default_location (mount);

		if (!g_file_is_native (root)) {
			network_mounts = g_list_prepend (network_mounts, mount);
			g_object_unref (root);
			continue;
		}

		icon = g_mount_get_symbolic_icon (mount);
		mount_uri = g_file_get_uri (root);
		name = g_mount_get_name (mount);
		tooltip = g_file_get_parse_name (root);
		add_place (sidebar->tree_data, NULL,
			   PLACES_MOUNTED_VOLUME,
			   SECTION_COMPUTER,
			   name, icon, mount_uri,
			   NULL, NULL, mount, 0, tooltip);
		g_object_unref (root);
		g_object_unref (mount);
		g_object_unref (icon);
		g_free (name);
		g_free (mount_uri);
		g_free (tooltip);
	}
	g_list_free (mounts);

	/* add bookmarks */
	bookmark_count = nautilus_bookmark_list_length (sidebar->bookmarks);

	for (index = 0; index < bookmark_count; ++index) {
		bookmark = nautilus_bookmark_list_item_at (sidebar->bookmarks, index);
		root = nautilus_bookmark_get_location (bookmark);

		if (!nautilus_bookmark_get_exists (bookmark) && g_file_is_native (root)) {
			g_object_unref (root);
			continue;
		}

		if (nautilus_bookmark_get_is_builtin (bookmark)) {
			g_object_unref (root);
			continue;
		}

		bookmark_name = nautilus_bookmark_get_name (bookmark);
		icon = nautilus_bookmark_get_symbolic_icon (bookmark);
		mount_uri = nautilus_bookmark_get_uri (bookmark);
		tooltip = g_file_get_parse_name (root);

		add_place (sidebar->tree_data, NULL,
			   PLACES_BOOKMARK,
			   SECTION_BOOKMARKS,
			   bookmark_name, icon, mount_uri,
			   NULL, NULL, NULL, index,
			   tooltip);
		g_object_unref (root);
		g_object_unref (icon);
		g_free (mount_uri);
		g_free (tooltip);
	}

	/* network */
	network_iter = add_expander (sidebar->tree_data, SECTION_NETWORK);

 	mount_uri = "network:///"; /* No need to strdup */
	icon = g_themed_icon_new (NAUTILUS_ICON_NETWORK);
	add_place (sidebar->tree_data, &network_iter,
		   PLACES_BUILT_IN,
		   SECTION_NETWORK,
		   _("Browse Network"), icon, mount_uri,
		   NULL, NULL, NULL, 0,
		   _("Browse the contents of the network"));
	g_object_unref (icon);

	icon = g_themed_icon_new (NAUTILUS_ICON_NETWORK_SERVER);
	add_place (sidebar->tree_data, &network_iter,
		   PLACES_CONNECT_SERVER,
		   SECTION_NETWORK,
		   _("Connect to Server"), icon, NULL,
		   NULL, NULL, NULL, 0,
		   _("Connect to a network server address"));
	g_object_unref (icon);

	network_volumes = g_list_reverse (network_volumes);
	for (l = network_volumes; l != NULL; l = l->next) {
		volume = l->data;
		mount = g_volume_get_mount (volume);

		if (mount != NULL) {
			network_mounts = g_list_prepend (network_mounts, mount);
			continue;
		} else {
			icon = g_volume_get_symbolic_icon (volume);
			name = g_volume_get_name (volume);
			tooltip = g_strdup_printf (_("Mount and open %s"), name);

			add_place (sidebar->tree_data, &network_iter,
				   PLACES_MOUNTED_VOLUME,
				   SECTION_NETWORK,
				   name, icon, NULL,
				   NULL, volume, NULL, 0, tooltip);
			g_object_unref (icon);
			g_free (name);
			g_free (tooltip);
		}
	}

	g_list_free_full (network_volumes, g_object_unref);

	network_mounts = g_list_reverse (network_mounts);
	for (l = network_mounts; l != NULL; l = l->next) {
		mount = l->data;
		root = g_mount_get_default_location (mount);
		icon = g_mount_get_symbolic_icon (mount);
		mount_uri = g_file_get_uri (root);
		name = g_mount_get_name (mount);
		tooltip = g_file_get_parse_name (root);
		add_place (sidebar->tree_data, &network_iter,
			   PLACES_MOUNTED_VOLUME,
			   SECTION_NETWORK,
			   name, icon, mount_uri,
			   NULL, NULL, mount, 0, tooltip);
		g_object_unref (root);
		g_object_unref (icon);
		g_free (name);
		g_free (mount_uri);
		g_free (tooltip);
	}

	g_list_free_full (network_mounts, g_object_unref);

	/* now add Trash to the secondary store */
	mount_uri = "trash:///"; /* No need to strdup */
	icon = nautilus_trash_monitor_get_icon ();
	add_place (sidebar->secondary_tree_data, NULL,
		   PLACES_BUILT_IN,
		   SECTION_COMPUTER,
		   _("Trash"), icon, mount_uri,
		   NULL, NULL, NULL, 0,
		   _("Open the trash"));
	g_object_unref (icon);

	/* restore selection */
	sidebar_update_restore_selection (sidebar, location, last_uri);

	g_free (location);
	g_free (last_uri);
}

static void
mount_added_callback (GVolumeMonitor *volume_monitor,
		      GMount *mount,
		      NautilusPlacesSidebar *sidebar)
{
	update_places (sidebar);
}

static void
mount_removed_callback (GVolumeMonitor *volume_monitor,
			GMount *mount,
			NautilusPlacesSidebar *sidebar)
{
	update_places (sidebar);
}

static void
mount_changed_callback (GVolumeMonitor *volume_monitor,
			GMount *mount,
			NautilusPlacesSidebar *sidebar)
{
	update_places (sidebar);
}

static void
volume_added_callback (GVolumeMonitor *volume_monitor,
		       GVolume *volume,
		       NautilusPlacesSidebar *sidebar)
{
	update_places (sidebar);
}

static void
volume_removed_callback (GVolumeMonitor *volume_monitor,
			 GVolume *volume,
			 NautilusPlacesSidebar *sidebar)
{
	update_places (sidebar);
}

static void
volume_changed_callback (GVolumeMonitor *volume_monitor,
			 GVolume *volume,
			 NautilusPlacesSidebar *sidebar)
{
	update_places (sidebar);
}

static void
drive_disconnected_callback (GVolumeMonitor *volume_monitor,
			     GDrive         *drive,
			     NautilusPlacesSidebar *sidebar)
{
	update_places (sidebar);
}

static void
drive_connected_callback (GVolumeMonitor *volume_monitor,
			  GDrive         *drive,
			  NautilusPlacesSidebar *sidebar)
{
	update_places (sidebar);
}

static void
drive_changed_callback (GVolumeMonitor *volume_monitor,
			GDrive         *drive,
			NautilusPlacesSidebar *sidebar)
{
	update_places (sidebar);
}

static gboolean
over_eject_button (SidebarTreeData *data,
		   gint x,
		   gint y,
		   GtkTreePath **path)
{
	GtkTreeViewColumn *column;
	int width, x_offset, hseparator;
	int eject_button_size;
	gboolean show_eject;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeView *tree_view;

	*path = NULL;
	tree_view = data->tree_view;
	model = gtk_tree_view_get_model (tree_view);
	show_eject = FALSE;

	if (gtk_tree_view_get_path_at_pos (tree_view,
					   x, y,
					   path, &column, NULL, NULL)) {

		gtk_tree_model_get_iter (model, &iter, *path);
		gtk_tree_model_get (model, &iter,
				    PLACES_SIDEBAR_COLUMN_EJECT, &show_eject,
				    -1);

		if (!show_eject) {
			gtk_tree_path_free (*path);
			*path = NULL;
			goto out;
		}


		gtk_widget_style_get (GTK_WIDGET (tree_view),
				      "horizontal-separator", &hseparator,
				      NULL);

		/* Reload cell attributes for this particular row */
		gtk_tree_view_column_cell_set_cell_data (column,
							 model, &iter, FALSE, FALSE);

		gtk_tree_view_column_cell_get_position (column,
							data->eject_icon_cell_renderer,
							&x_offset, &width);

		eject_button_size = nautilus_get_icon_size_for_stock_size (GTK_ICON_SIZE_MENU);

		/* This is kinda weird, but we have to do it to workaround gtk+ expanding
		 * the eject cell renderer (even thought we told it not to) and we then
		 * had to set it right-aligned */
		if (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_LTR)
			x_offset += width - hseparator - EJECT_BUTTON_XPAD - eject_button_size;
		else
			x_offset += EJECT_BUTTON_XPAD;

		if (x - x_offset >= 0 &&
		    x - x_offset <= eject_button_size) {
			show_eject = TRUE;
		} else {
			show_eject = FALSE;
		}
	}

 out:
	return show_eject;
}

static gboolean
clicked_eject_button (SidebarTreeData *data,
		      GtkTreePath **path)
{
	GdkEvent *event = gtk_get_current_event ();
	GdkEventButton *button_event = (GdkEventButton *) event;

	if ((event->type == GDK_BUTTON_PRESS || event->type == GDK_BUTTON_RELEASE) &&
	    over_eject_button (data, button_event->x, button_event->y, path)) {
		return TRUE;
	}

	return FALSE;
}

static void
update_current_uri_for_tree_data (SidebarTreeData *data)
{
	GtkTreeSelection *selection;
	GtkTreeIter 	 iter;
	gboolean 	 valid;
	char  		 *uri;

	selection = gtk_tree_view_get_selection (data->tree_view);
	gtk_tree_selection_unselect_all (selection);

	valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (data->store),
					       &iter);

	while (valid) {
		gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
				    PLACES_SIDEBAR_COLUMN_URI, &uri,
				    -1);

		if (uri != NULL) {
			if (strcmp (uri, data->sidebar->uri) == 0) {
				g_free (uri);
				gtk_tree_selection_select_iter (selection, &iter);
				break;
			}
			g_free (uri);
		}
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (data->store),
						  &iter);
	}
}

static void
update_current_uri (NautilusPlacesSidebar *sidebar)
{
	if (sidebar->uri == NULL) {
		return;
	}

	update_current_uri_for_tree_data (sidebar->tree_data);
	update_current_uri_for_tree_data (sidebar->secondary_tree_data);
}

static void
loading_uri_callback (NautilusWindow *window,
		      char *location,
		      NautilusPlacesSidebar *sidebar)
{
        if (g_strcmp0 (sidebar->uri, location) != 0) {
		g_free (sidebar->uri);
                sidebar->uri = g_strdup (location);

		update_current_uri (sidebar);
	}
}

/* Computes the appropriate row and position for dropping */
static gboolean
compute_drop_position (SidebarTreeData         *data,
		       int                      x,
		       int                      y,
		       GtkTreePath            **path,
		       GtkTreeViewDropPosition *pos)
{
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreeIter iter;
	PlaceType place_type;
	SectionType section_type;
	NautilusPlacesSidebar *sidebar;

	sidebar = data->sidebar;
	tree_view = data->tree_view;

	if (!gtk_tree_view_get_dest_row_at_pos (tree_view,
						x, y,
						path, pos)) {
		return FALSE;
	}

	model = gtk_tree_view_get_model (tree_view);

	gtk_tree_model_get_iter (model, &iter, *path);
	gtk_tree_model_get (model, &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
			    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
			    -1);

	if (section_type != SECTION_BOOKMARKS &&
	    place_type == PLACES_SEPARATOR) {
		/* never drop on separators, but special case the bookmarks separator,
		 * so we can drop bookmarks in between it and the first item when
		 * reordering.
		 */
		gtk_tree_path_free (*path);
		*path = NULL;
		
		return FALSE;
	}

	if (section_type != SECTION_BOOKMARKS &&
	    sidebar->drag_data_received &&
	    sidebar->drag_data_info == GTK_TREE_MODEL_ROW) {
		/* don't allow dropping bookmarks into non-bookmark areas */
		gtk_tree_path_free (*path);
		*path = NULL;

		return FALSE;
	}

	if (sidebar->drag_data_received &&
	    sidebar->drag_data_info == GTK_TREE_MODEL_ROW) {
		/* bookmark rows can only be reordered */
		*pos = GTK_TREE_VIEW_DROP_AFTER;
	} else {
		*pos = GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
	}

	return TRUE;
}

static gboolean
get_drag_data (GtkTreeView *tree_view,
	       GdkDragContext *context, 
	       unsigned int time)
{
	GdkAtom target;

	target = gtk_drag_dest_find_target (GTK_WIDGET (tree_view), 
					    context, 
					    NULL);

	if (target == GDK_NONE) {
		return FALSE;
	}

	gtk_drag_get_data (GTK_WIDGET (tree_view),
			   context, target, time);

	return TRUE;
}

static void
remove_switch_location_timer (NautilusPlacesSidebar *sidebar)
{
	if (sidebar->switch_location_timer != 0) {
		g_source_remove (sidebar->switch_location_timer);
		sidebar->switch_location_timer = 0;
	}
}

static void
free_drag_data (NautilusPlacesSidebar *sidebar)
{
	sidebar->drag_data_received = FALSE;

	if (sidebar->drag_list) {
		nautilus_drag_destroy_selection_list (sidebar->drag_list);
		sidebar->drag_list = NULL;
	}

	remove_switch_location_timer (sidebar);

	g_free (sidebar->target_uri);
	sidebar->target_uri = NULL;
}

static gboolean
switch_location_timer (gpointer user_data)
{
	NautilusPlacesSidebar *sidebar = user_data;
	GFile *location;
	NautilusWindowSlot *target_slot;

	sidebar->switch_location_timer = 0;

	target_slot = nautilus_window_get_active_slot (NAUTILUS_WINDOW (sidebar->window));

	location = g_file_new_for_uri (sidebar->target_uri);
	nautilus_window_slot_open_location (target_slot, location, 0);
	g_object_unref (location);

	return FALSE;
}

static void
check_switch_location_timer (NautilusPlacesSidebar *sidebar,
			     const char            *uri)
{
	GtkSettings *settings;
	guint timeout;

	if (g_strcmp0 (uri, sidebar->target_uri) == 0) {
		return;
	}
	remove_switch_location_timer (sidebar);

	settings = gtk_widget_get_settings (GTK_WIDGET (sidebar));
	g_object_get (settings, "gtk-timeout-expand", &timeout, NULL);

	g_free (sidebar->target_uri);
	sidebar->target_uri = NULL;

	if (uri != NULL) {
		sidebar->target_uri = g_strdup (uri);
		sidebar->switch_location_timer =
			gdk_threads_add_timeout (timeout,
						 switch_location_timer,
						 sidebar);
	}
}

static gboolean
drag_motion_callback (GtkTreeView *tree_view,
		      GdkDragContext *context,
		      int x,
		      int y,
		      unsigned int time,
		      SidebarTreeData *data)
{
	GtkTreePath *path;
	GtkTreeViewDropPosition pos;
	int action;
	GtkTreeIter iter;
	char *target_uri = NULL;
	gboolean res;
	NautilusPlacesSidebar *sidebar = data->sidebar;

	if (!sidebar->drag_data_received) {
		if (!get_drag_data (tree_view, context, time)) {
			return FALSE;
		}
	}

	path = NULL;
	res = compute_drop_position (data, x, y, &path, &pos);

	if (!res) {
		goto out;
	}

	if (pos == GTK_TREE_VIEW_DROP_AFTER ) {
		if (sidebar->drag_data_received &&
		    sidebar->drag_data_info == GTK_TREE_MODEL_ROW) {
			action = GDK_ACTION_MOVE;
		} else {
			action = 0;
		}
	} else {
		if (sidebar->drag_list == NULL) {
			action = 0;
		} else {
			gtk_tree_model_get_iter (GTK_TREE_MODEL (data->store),
						 &iter, path);
			gtk_tree_model_get (GTK_TREE_MODEL (data->store),
					    &iter,
					    PLACES_SIDEBAR_COLUMN_URI, &target_uri,
					    -1);
			nautilus_drag_default_drop_action_for_icons (context, target_uri,
								     sidebar->drag_list,
								     &action);
		}
	}

	if (action != 0) {
		gtk_tree_view_set_drag_dest_row (tree_view, path, pos);
	}

	if (path != NULL) {
		gtk_tree_path_free (path);
	}

 out:
	g_signal_stop_emission_by_name (tree_view, "drag-motion");

	if (action != 0) {
		check_switch_location_timer (sidebar, target_uri);
		gdk_drag_status (context, action, time);
	} else {
		remove_switch_location_timer (sidebar);
		gdk_drag_status (context, 0, time);
	}

	g_free (target_uri);

	return TRUE;
}

static void
drag_leave_callback (GtkTreeView *tree_view,
		     GdkDragContext *context,
		     unsigned int time,
		     SidebarTreeData *data)
{
	free_drag_data (data->sidebar);
	gtk_tree_view_set_drag_dest_row (tree_view, NULL, 0);
	g_signal_stop_emission_by_name (tree_view, "drag-leave");
}

static GList *
uri_list_from_selection (GList *selection)
{
	NautilusDragSelectionItem *item;
	GList *ret;
	GList *l;
	
	ret = NULL;
	for (l = selection; l != NULL; l = l->next) {
		item = l->data;
		ret = g_list_prepend (ret, item->uri);
	}
	
	return g_list_reverse (ret);
}

static GList*
build_selection_list (const char *data)
{
	NautilusDragSelectionItem *item;
	GList *result;
	char **uris;
	char *uri;
	int i;

	uris = g_uri_list_extract_uris (data);
	result = NULL;
	for (i = 0; uris[i]; i++) {
		uri = uris[i];
		item = nautilus_drag_selection_item_new ();
		item->uri = g_strdup (uri);
		item->file = nautilus_file_get_existing_by_uri (uri);
		item->got_icon_position = FALSE;
		result = g_list_prepend (result, item);
	}

	g_strfreev (uris);

	return g_list_reverse (result);
}

static gboolean
get_selected_iter (NautilusPlacesSidebar *sidebar,
		   GtkTreeIter *iter,
		   SidebarTreeData **data)
{
	GtkTreeSelection *selection;
	gboolean res;

	selection = gtk_tree_view_get_selection (sidebar->tree_data->tree_view);
	res = gtk_tree_selection_get_selected (selection, NULL, iter);

	if (!res) {
		selection = gtk_tree_view_get_selection (sidebar->secondary_tree_data->tree_view);
		res = gtk_tree_selection_get_selected (selection, NULL, iter);

		if (res && data) {
			*data = sidebar->secondary_tree_data;
		}
	} else {
		if (data) {
			*data = sidebar->tree_data;
		}
	}

	return res;
}

/* Reorders the selected bookmark to the specified position */
static void
reorder_bookmarks (NautilusPlacesSidebar *sidebar,
		   int new_position)
{
	GtkTreeIter iter;
	PlaceType type; 
	int old_position;
	SidebarTreeData *data;

	/* Get the selected path */
	if (!get_selected_iter (sidebar, &iter, &data)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
			    PLACES_SIDEBAR_COLUMN_INDEX, &old_position,
			    -1);

	if (type != PLACES_BOOKMARK ||
	    old_position < 0 ||
	    old_position >= nautilus_bookmark_list_length (data->sidebar->bookmarks)) {
		return;
	}

	nautilus_bookmark_list_move_item (data->sidebar->bookmarks, old_position,
					  new_position);
}

static void
drag_data_received_callback (GtkWidget *widget,
			     GdkDragContext *context,
			     int x,
			     int y,
			     GtkSelectionData *selection_data,
			     unsigned int info,
			     unsigned int time,
			     SidebarTreeData *data)
{
	GtkTreeView *tree_view;
	GtkTreePath *tree_path;
	GtkTreeViewDropPosition tree_pos;
	GtkTreeIter iter;
	int position;
	GtkTreeModel *model;
	char *drop_uri;
	GList *selection_list, *uris;
	PlaceType place_type;
	SectionType section_type;
	gboolean success;
	NautilusPlacesSidebar *sidebar;

	tree_view = GTK_TREE_VIEW (widget);
	sidebar = data->sidebar;

	if (!sidebar->drag_data_received) {
		if (gtk_selection_data_get_target (selection_data) != GDK_NONE &&
		    info == TEXT_URI_LIST) {
			sidebar->drag_list = build_selection_list ((const gchar *) gtk_selection_data_get_data (selection_data));
		} else {
			sidebar->drag_list = NULL;
		}
		sidebar->drag_data_received = TRUE;
		sidebar->drag_data_info = info;
	}

	g_signal_stop_emission_by_name (widget, "drag-data-received");

	if (!sidebar->drop_occured) {
		return;
	}

	/* Compute position */
	success = compute_drop_position (data, x, y, &tree_path, &tree_pos);
	if (!success) {
		goto out;
	}

	success = FALSE;

	if (tree_pos == GTK_TREE_VIEW_DROP_AFTER) {
		model = gtk_tree_view_get_model (tree_view);

		if (!gtk_tree_model_get_iter (model, &iter, tree_path)) {
			goto out;
		}

		gtk_tree_model_get (model, &iter,
				    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
    				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
				    PLACES_SIDEBAR_COLUMN_INDEX, &position,
				    -1);

		if (section_type != SECTION_BOOKMARKS) {
			goto out;
		}

		if (tree_pos == GTK_TREE_VIEW_DROP_AFTER && place_type != PLACES_SEPARATOR) {
			/* separator already has position 0 */
			position++;
		}

		switch (info) {
		case GTK_TREE_MODEL_ROW:
			reorder_bookmarks (sidebar, position);
			success = TRUE;
			break;
		default:
			g_assert_not_reached ();
			break;
		}
	} else {
		GdkDragAction real_action;

		/* file transfer requested */
		real_action = gdk_drag_context_get_selected_action (context);

		if (real_action == GDK_ACTION_ASK) {
			real_action =
				nautilus_drag_drop_action_ask (GTK_WIDGET (tree_view),
							       gdk_drag_context_get_actions (context));
		}

		if (real_action > 0) {
			model = gtk_tree_view_get_model (tree_view);

			gtk_tree_model_get_iter (model, &iter, tree_path);
			gtk_tree_model_get (model, &iter,
					    PLACES_SIDEBAR_COLUMN_URI, &drop_uri,
					    -1);

			switch (info) {
			case TEXT_URI_LIST:
				selection_list = build_selection_list ((const gchar *) gtk_selection_data_get_data (selection_data));
				g_assert (selection_list);
				uris = uri_list_from_selection (selection_list);
				g_assert (uris);
				nautilus_file_operations_copy_move (uris, NULL, drop_uri,
								    real_action, GTK_WIDGET (tree_view),
								    NULL, NULL);
				nautilus_drag_destroy_selection_list (selection_list);
				g_list_free (uris);
				success = TRUE;
				break;
			case GTK_TREE_MODEL_ROW:
				success = FALSE;
				break;
			default:
				g_assert_not_reached ();
				break;
			}

			g_free (drop_uri);
		}
	}

 out:
	sidebar->drop_occured = FALSE;
	free_drag_data (sidebar);
	gtk_drag_finish (context, success, FALSE, time);

	gtk_tree_path_free (tree_path);
}

static gboolean
drag_drop_callback (GtkTreeView *tree_view,
		    GdkDragContext *context,
		    int x,
		    int y,
		    unsigned int time,
		    SidebarTreeData *data)
{
	gboolean retval = FALSE;
	NautilusPlacesSidebar *sidebar = data->sidebar;

	sidebar->drop_occured = TRUE;
	retval = get_drag_data (tree_view, context, time);
	g_signal_stop_emission_by_name (tree_view, "drag-drop");
	return retval;
}

/* Callback used when the file list's popup menu is detached */
static void
bookmarks_popup_menu_detach_cb (GtkWidget *attach_widget,
				GtkMenu   *menu)
{
	NautilusPlacesSidebar *sidebar;
	
	sidebar = NAUTILUS_PLACES_SIDEBAR (attach_widget);
	g_assert (NAUTILUS_IS_PLACES_SIDEBAR (sidebar));
	
	sidebar->popup_menu = NULL;
	sidebar->popup_menu_add_shortcut_item = NULL;
	sidebar->popup_menu_remove_item = NULL;
	sidebar->popup_menu_rename_item = NULL;
	sidebar->popup_menu_separator_item = NULL;
	sidebar->popup_menu_mount_item = NULL;
	sidebar->popup_menu_unmount_item = NULL;
	sidebar->popup_menu_eject_item = NULL;
	sidebar->popup_menu_rescan_item = NULL;
	sidebar->popup_menu_start_item = NULL;
	sidebar->popup_menu_stop_item = NULL;
	sidebar->popup_menu_empty_trash_item = NULL;
	sidebar->popup_menu_properties_separator_item = NULL;
	sidebar->popup_menu_properties_item = NULL;
	sidebar->popup_menu_format_item = NULL;
}

static gboolean
check_have_gnome_disks (void)
{
	gchar *disks_path;
	gboolean res;

	disks_path = g_find_program_in_path ("gnome-disks");
	res = (disks_path != NULL);
	g_free (disks_path);

	return res;
}

static void
check_visibility (GMount           *mount,
		  GVolume          *volume,
		  GDrive           *drive,
		  gboolean         *show_mount,
		  gboolean         *show_unmount,
		  gboolean         *show_eject,
		  gboolean         *show_rescan,
		  gboolean         *show_start,
		  gboolean         *show_stop,
		  gboolean         *show_format)
{
	gchar *unix_device_id;

	*show_mount = FALSE;
	*show_rescan = FALSE;
	*show_start = FALSE;
	*show_stop = FALSE;
	*show_format = FALSE;

	check_unmount_and_eject (mount, volume, drive, show_unmount, show_eject);

	if (drive != NULL) {
		if (g_drive_is_media_removable (drive) &&
		    !g_drive_is_media_check_automatic (drive) && 
		    g_drive_can_poll_for_media (drive))
			*show_rescan = TRUE;

		*show_start = g_drive_can_start (drive) || g_drive_can_start_degraded (drive);
		*show_stop  = g_drive_can_stop (drive);

		if (*show_stop)
			*show_unmount = FALSE;
	}

	if (volume != NULL) {
		if (mount == NULL)
			*show_mount = g_volume_can_mount (volume);

		unix_device_id = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
		*show_format = (unix_device_id != NULL) && check_have_gnome_disks ();
		g_free (unix_device_id);
	}
}

static void
bookmarks_check_popup_sensitivity (NautilusPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	PlaceType type; 
	GDrive *drive = NULL;
	GVolume *volume = NULL;
	GMount *mount = NULL;
	GFile *location;
	NautilusDirectory *directory;
	gboolean show_mount;
	gboolean show_unmount;
	gboolean show_eject;
	gboolean show_rescan;
	gboolean show_start;
	gboolean show_stop;
	gboolean show_empty_trash;
	gboolean show_properties;
	gboolean show_format;
	char *uri = NULL;
	SidebarTreeData *data;
	
	type = PLACES_BUILT_IN;

	if (sidebar->popup_menu == NULL) {
		return;
	}

	if (get_selected_iter (sidebar, &iter, &data)) {
		gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
				    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
				    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
 				    PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
				    PLACES_SIDEBAR_COLUMN_URI, &uri,
				    -1);
	}

	gtk_widget_set_visible (sidebar->popup_menu_add_shortcut_item, (type == PLACES_MOUNTED_VOLUME));

	gtk_widget_set_sensitive (sidebar->popup_menu_remove_item, (type == PLACES_BOOKMARK));
	gtk_widget_set_sensitive (sidebar->popup_menu_rename_item, (type == PLACES_BOOKMARK || type == PLACES_XDG_DIR));
	gtk_widget_set_sensitive (sidebar->popup_menu_empty_trash_item, !nautilus_trash_monitor_is_empty ());

 	check_visibility (mount, volume, drive,
			  &show_mount, &show_unmount, &show_eject, &show_rescan, &show_start, &show_stop, &show_format);

	/* We actually want both eject and unmount since eject will unmount all volumes. 
	 * TODO: hide unmount if the drive only has a single mountable volume 
	 */

	show_empty_trash = (uri != NULL) &&
		(!strcmp (uri, "trash:///"));

	/* Only show properties for local mounts */
	show_properties = (mount != NULL);
	if (mount != NULL) {
		location = g_mount_get_default_location (mount);
		directory = nautilus_directory_get (location);

		show_properties = nautilus_directory_is_local (directory);

		nautilus_directory_unref (directory);
		g_object_unref (location);
	}

	gtk_widget_set_visible (sidebar->popup_menu_separator_item,
				show_mount || show_unmount || show_eject || show_empty_trash);
	gtk_widget_set_visible (sidebar->popup_menu_mount_item, show_mount);
	gtk_widget_set_visible (sidebar->popup_menu_unmount_item, show_unmount);
	gtk_widget_set_visible (sidebar->popup_menu_eject_item, show_eject);
	gtk_widget_set_visible (sidebar->popup_menu_rescan_item, show_rescan);
	gtk_widget_set_visible (sidebar->popup_menu_start_item, show_start);
	gtk_widget_set_visible (sidebar->popup_menu_stop_item, show_stop);
	gtk_widget_set_visible (sidebar->popup_menu_empty_trash_item, show_empty_trash);
	gtk_widget_set_visible (sidebar->popup_menu_properties_separator_item, show_properties);
	gtk_widget_set_visible (sidebar->popup_menu_properties_item, show_properties);
	gtk_widget_set_visible (sidebar->popup_menu_format_item, show_format);

	/* Adjust start/stop items to reflect the type of the drive */
	gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_start_item), _("_Start"));
	gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_stop_item), _("_Stop"));
	if ((show_start || show_stop) && drive != NULL) {
		switch (g_drive_get_start_stop_type (drive)) {
		case G_DRIVE_START_STOP_TYPE_SHUTDOWN:
			/* start() for type G_DRIVE_START_STOP_TYPE_SHUTDOWN is normally not used */
			gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_start_item), _("_Power On"));
			gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_stop_item), _("_Safely Remove Drive"));
			break;
		case G_DRIVE_START_STOP_TYPE_NETWORK:
			gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_start_item), _("_Connect Drive"));
			gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_stop_item), _("_Disconnect Drive"));
			break;
		case G_DRIVE_START_STOP_TYPE_MULTIDISK:
			gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_start_item), _("_Start Multi-disk Device"));
			gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_stop_item), _("_Stop Multi-disk Device"));
			break;
		case G_DRIVE_START_STOP_TYPE_PASSWORD:
			/* stop() for type G_DRIVE_START_STOP_TYPE_PASSWORD is normally not used */
			gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_start_item), _("_Unlock Drive"));
			gtk_menu_item_set_label (GTK_MENU_ITEM (sidebar->popup_menu_stop_item), _("_Lock Drive"));
			break;

		default:
		case G_DRIVE_START_STOP_TYPE_UNKNOWN:
			/* uses defaults set above */
			break;
		}
	}


	g_free (uri);
}

/* Callback used when the selection in the shortcuts tree changes */
static void
bookmarks_selection_changed_cb (GtkTreeSelection *selection,
				SidebarTreeData  *data)
{
	bookmarks_check_popup_sensitivity (data->sidebar);
}

static void
volume_mounted_cb (GVolume *volume,
		   gboolean success,
		   GObject *user_data)
{
	GMount *mount;
	NautilusPlacesSidebar *sidebar;
	GFile *location;

	sidebar = NAUTILUS_PLACES_SIDEBAR (user_data);

	sidebar->mounting = FALSE;

	mount = g_volume_get_mount (volume);
	if (mount != NULL) {
		location = g_mount_get_default_location (mount);

		if (sidebar->go_to_after_mount_slot != NULL) {
			if ((sidebar->go_to_after_mount_flags & NAUTILUS_WINDOW_OPEN_FLAG_NEW_WINDOW) == 0) {
				nautilus_window_slot_open_location (sidebar->go_to_after_mount_slot, location,
								    sidebar->go_to_after_mount_flags);
			} else {
				NautilusWindow *new, *cur;

				cur = NAUTILUS_WINDOW (sidebar->window);
				new = nautilus_application_create_window (NAUTILUS_APPLICATION (g_application_get_default ()),
									  gtk_window_get_screen (GTK_WINDOW (cur)));
				nautilus_window_go_to (new, location);
			}
		}

		g_object_unref (G_OBJECT (location));
		g_object_unref (G_OBJECT (mount));
	}

	if (sidebar->go_to_after_mount_slot) {
		g_object_remove_weak_pointer (G_OBJECT (sidebar->go_to_after_mount_slot),
					      (gpointer *) &sidebar->go_to_after_mount_slot);
		sidebar->go_to_after_mount_slot = NULL;
	}
}

static void
drive_start_from_bookmark_cb (GObject      *source_object,
			      GAsyncResult *res,
			      gpointer      user_data)
{
	GError *error;
	char *primary;
	char *name;

	error = NULL;
	if (!g_drive_poll_for_media_finish (G_DRIVE (source_object), res, &error)) {
		if (error->code != G_IO_ERROR_FAILED_HANDLED) {
			name = g_drive_get_name (G_DRIVE (source_object));
			primary = g_strdup_printf (_("Unable to start %s"), name);
			g_free (name);
			eel_show_error_dialog (primary,
					       error->message,
					       NULL);
			g_free (primary);
		}
		g_error_free (error);
	}
}

static void
open_selected_volume (SidebarTreeData *data,
		      GtkTreeIter *iter,
		      NautilusWindowOpenFlags flags)
{
	GDrive *drive;
	GVolume *volume;
	NautilusWindowSlot *slot;
	NautilusPlacesSidebar *sidebar;

	sidebar = data->sidebar;
	gtk_tree_model_get (GTK_TREE_MODEL (data->store), iter,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
			    -1);

	if (volume != NULL && !sidebar->mounting) {
		sidebar->mounting = TRUE;

		g_assert (sidebar->go_to_after_mount_slot == NULL);

		slot = nautilus_window_get_active_slot (sidebar->window);
		sidebar->go_to_after_mount_slot = slot;
		g_object_add_weak_pointer (G_OBJECT (sidebar->go_to_after_mount_slot),
					   (gpointer *) &sidebar->go_to_after_mount_slot);

		sidebar->go_to_after_mount_flags = flags;

		nautilus_file_operations_mount_volume_full (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))),
							    volume,
							    volume_mounted_cb,
							    G_OBJECT (sidebar));
	} else if (volume == NULL && drive != NULL &&
		   (g_drive_can_start (drive) || g_drive_can_start_degraded (drive))) {
		GMountOperation *mount_op;

		mount_op = gtk_mount_operation_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))));
		g_drive_start (drive, G_DRIVE_START_NONE, mount_op, NULL, drive_start_from_bookmark_cb, NULL);
		g_object_unref (mount_op);
	}

	if (drive != NULL)
		g_object_unref (drive);
	if (volume != NULL)
		g_object_unref (volume);
}

static void
open_selected_uri (NautilusPlacesSidebar *sidebar,
		   const gchar *uri,
		   NautilusWindowOpenFlags flags)
{
	NautilusWindowSlot *slot;
	GFile *location;

	DEBUG ("Activating bookmark %s", uri);

	location = g_file_new_for_uri (uri);

	/* Navigate to the clicked location */
	if ((flags & NAUTILUS_WINDOW_OPEN_FLAG_NEW_WINDOW) == 0) {
		slot = nautilus_window_get_active_slot (sidebar->window);
		nautilus_window_slot_open_location (slot, location, flags);
	} else {
		NautilusWindow *cur, *new;

		cur = NAUTILUS_WINDOW (sidebar->window);
		new = nautilus_application_create_window (NAUTILUS_APPLICATION (g_application_get_default ()),
							  gtk_window_get_screen (GTK_WINDOW (cur)));
		nautilus_window_go_to (new, location);
	}

	g_object_unref (location);
}

static void
open_connect_to_server (NautilusPlacesSidebar *sidebar)
{
	NautilusApplication *application = NAUTILUS_APPLICATION (g_application_get_default ());
	GtkWidget *dialog;

	dialog = nautilus_application_connect_server (application, sidebar->window);
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
}

static void
open_selected_bookmark (SidebarTreeData        *data,
			GtkTreeIter	       *iter,
			NautilusWindowOpenFlags flags)
{
	char *uri;
	PlaceType row_type;

	if (!iter) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (data->store), iter,
			    PLACES_SIDEBAR_COLUMN_URI, &uri,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &row_type,
			    -1);

	if (uri != NULL) {
		open_selected_uri (data->sidebar, uri, flags);
		g_free (uri);
	} else if (row_type == PLACES_CONNECT_SERVER) {
		open_connect_to_server (data->sidebar);
	} else {
		open_selected_volume (data, iter, flags);
	}
}

static gboolean
get_cursor_for_tree_data (SidebarTreeData *data,
			  GtkTreeIter *iter)
{
	GtkTreeModel *model;
	GtkTreePath *path = NULL;
	gboolean retval = FALSE;

	model = gtk_tree_view_get_model (data->tree_view);
	gtk_tree_view_get_cursor (data->tree_view, &path, NULL);

	if (path != NULL && gtk_tree_model_get_iter (model, iter, path)) {
		retval = TRUE;
	}

	gtk_tree_path_free (path);
	return retval;
}

static gboolean
get_cursor (NautilusPlacesSidebar *sidebar,
	    SidebarTreeData **data,
	    GtkTreeIter *iter)
{
	gboolean retval = FALSE;

	retval = get_cursor_for_tree_data (sidebar->tree_data, iter);

	if (!retval) {
		retval = get_cursor_for_tree_data (sidebar->secondary_tree_data, iter);
		if (retval) {
			*data = sidebar->secondary_tree_data;
		}
	} else {
		*data = sidebar->tree_data;
	}

	return retval;
}

static void
open_shortcut_from_menu (NautilusPlacesSidebar *sidebar,
			 NautilusWindowOpenFlags flags)
{
	SidebarTreeData *data;
	GtkTreeIter iter;

	if (!get_cursor (sidebar, &data, &iter)) {
		return;
	}

	open_selected_bookmark (data, &iter, flags);
}

static void
open_shortcut_cb (GtkMenuItem		*item,
		  NautilusPlacesSidebar	*sidebar)
{
	open_shortcut_from_menu (sidebar, 0);
}

static void
open_shortcut_in_new_window_cb (GtkMenuItem	      *item,
				NautilusPlacesSidebar *sidebar)
{
	open_shortcut_from_menu (sidebar, NAUTILUS_WINDOW_OPEN_FLAG_NEW_WINDOW);
}

static void
open_shortcut_in_new_tab_cb (GtkMenuItem	      *item,
			     NautilusPlacesSidebar *sidebar)
{
	open_shortcut_from_menu (sidebar, NAUTILUS_WINDOW_OPEN_FLAG_NEW_TAB);
}

/* Add bookmark for the selected item */
static void
add_bookmark (NautilusPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	char *uri;
	char *name;
	GFile *location;
	NautilusBookmark *bookmark;
	SidebarTreeData *data;

	if (get_selected_iter (sidebar, &iter, &data)) {
		gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
				    PLACES_SIDEBAR_COLUMN_URI, &uri,
				    PLACES_SIDEBAR_COLUMN_NAME, &name,
				    -1);

		if (uri == NULL) {
			return;
		}

		location = g_file_new_for_uri (uri);
		bookmark = nautilus_bookmark_new (location, name);
		nautilus_bookmark_list_append (sidebar->bookmarks, bookmark);

		g_object_unref (location);
		g_object_unref (bookmark);
		g_free (uri);
		g_free (name);
	}
}

static void
add_shortcut_cb (GtkMenuItem           *item,
		 NautilusPlacesSidebar *sidebar)
{
	add_bookmark (sidebar);
}

/* Rename the selected bookmark */
static void
rename_selected_bookmark (NautilusPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeViewColumn *column;
	GtkCellRenderer *cell;
	PlaceType type;
	SidebarTreeData *data;
	
	if (get_selected_iter (sidebar, &iter, &data)) {
		gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
				    -1);

		if (type != PLACES_BOOKMARK && type != PLACES_XDG_DIR) {
			return;
		}

		path = gtk_tree_model_get_path (GTK_TREE_MODEL (data->store), &iter);
		column = gtk_tree_view_get_column (GTK_TREE_VIEW (data->tree_view), 0);
		cell = data->normal_text_cell_renderer;
		g_object_set (cell, "editable", TRUE, NULL);
		gtk_tree_view_set_cursor_on_cell (GTK_TREE_VIEW (data->tree_view),
						  path, column, cell, TRUE);
		gtk_tree_path_free (path);
	}
}

static void
rename_shortcut_cb (GtkMenuItem           *item,
		    NautilusPlacesSidebar *sidebar)
{
	rename_selected_bookmark (sidebar);
}

/* Removes the selected bookmarks */
static void
remove_selected_bookmarks (NautilusPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	PlaceType type; 
	int index;
	SidebarTreeData *data;

	if (!get_selected_iter (sidebar, &iter, &data)) {
		return;
	}
	
	gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
			    -1);

	if (type != PLACES_BOOKMARK) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
			    PLACES_SIDEBAR_COLUMN_INDEX, &index,
			    -1);

	nautilus_bookmark_list_delete_item_at (sidebar->bookmarks, index);
}

static void
remove_shortcut_cb (GtkMenuItem           *item,
		    NautilusPlacesSidebar *sidebar)
{
	remove_selected_bookmarks (sidebar);
}

static void
mount_shortcut_cb (GtkMenuItem           *item,
		   NautilusPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GVolume *volume;
	SidebarTreeData *data;

	if (!get_selected_iter (sidebar, &iter, &data)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
			    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
			    -1);

	if (volume != NULL) {
		nautilus_file_operations_mount_volume (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))), volume);
		g_object_unref (volume);
	}
}

static void
unmount_done (gpointer data)
{
	NautilusWindow *window;

	window = data;
	g_object_unref (window);
}

static void
show_unmount_progress_cb (GMountOperation *op,
			  const gchar *message,
			  gint64 time_left,
			  gint64 bytes_left,
			  gpointer user_data)
{
	NautilusApplication *app = NAUTILUS_APPLICATION (g_application_get_default ());

	if (bytes_left == 0) {
		nautilus_application_notify_unmount_done (app, message);
	} else {
		nautilus_application_notify_unmount_show (app, message);
	}
}

static void
show_unmount_progress_aborted_cb (GMountOperation *op,
				  gpointer user_data)
{
	NautilusApplication *app = NAUTILUS_APPLICATION (g_application_get_default ());
	nautilus_application_notify_unmount_done (app, NULL);
}

static GMountOperation *
get_unmount_operation (NautilusPlacesSidebar *sidebar)
{
	GMountOperation *mount_op;

	mount_op = gtk_mount_operation_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))));
	g_signal_connect (mount_op, "show-unmount-progress",
			  G_CALLBACK (show_unmount_progress_cb), sidebar);
	g_signal_connect (mount_op, "aborted",
			  G_CALLBACK (show_unmount_progress_aborted_cb), sidebar);

	return mount_op;
}

static void
do_unmount (GMount *mount,
	    NautilusPlacesSidebar *sidebar)
{
	GMountOperation *mount_op;

	if (mount != NULL) {
		mount_op = get_unmount_operation (sidebar);
		nautilus_file_operations_unmount_mount_full (NULL, mount, mount_op, FALSE, TRUE,
							     unmount_done,
							     g_object_ref (sidebar->window));
		g_object_unref (mount_op);
	}
}

static void
do_unmount_selection (NautilusPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GMount *mount;
	SidebarTreeData *data;

	if (!get_selected_iter (sidebar, &iter, &data)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
			    PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
			    -1);

	if (mount != NULL) {
		do_unmount (mount, sidebar);
		g_object_unref (mount);
	}
}

static void
unmount_shortcut_cb (GtkMenuItem           *item,
		     NautilusPlacesSidebar *sidebar)
{
	do_unmount_selection (sidebar);
}

static void
drive_eject_cb (GObject *source_object,
		GAsyncResult *res,
		gpointer user_data)
{
	NautilusWindow *window;
	GError *error;
	char *primary;
	char *name;

	window = user_data;
	g_object_unref (window);

	error = NULL;
	if (!g_drive_eject_with_operation_finish (G_DRIVE (source_object), res, &error)) {
		if (error->code != G_IO_ERROR_FAILED_HANDLED) {
			name = g_drive_get_name (G_DRIVE (source_object));
			primary = g_strdup_printf (_("Unable to eject %s"), name);
			g_free (name);
			eel_show_error_dialog (primary,
					       error->message,
					       NULL);
			g_free (primary);
		}
		g_error_free (error);
	}
}

static void
volume_eject_cb (GObject *source_object,
		 GAsyncResult *res,
		 gpointer user_data)
{
	NautilusWindow *window;
	GError *error;
	char *primary;
	char *name;

	window = user_data;
	g_object_unref (window);

	error = NULL;
	if (!g_volume_eject_with_operation_finish (G_VOLUME (source_object), res, &error)) {
		if (error->code != G_IO_ERROR_FAILED_HANDLED) {
			name = g_volume_get_name (G_VOLUME (source_object));
			primary = g_strdup_printf (_("Unable to eject %s"), name);
			g_free (name);
			eel_show_error_dialog (primary,
					       error->message,
					       NULL);
			g_free (primary);
		}
		g_error_free (error);
	}
}

static void
mount_eject_cb (GObject *source_object,
		GAsyncResult *res,
		gpointer user_data)
{
	NautilusWindow *window;
	GError *error;
	char *primary;
	char *name;

	window = user_data;
	g_object_unref (window);

	error = NULL;
	if (!g_mount_eject_with_operation_finish (G_MOUNT (source_object), res, &error)) {
		if (error->code != G_IO_ERROR_FAILED_HANDLED) {
			name = g_mount_get_name (G_MOUNT (source_object));
			primary = g_strdup_printf (_("Unable to eject %s"), name);
			g_free (name);
			eel_show_error_dialog (primary,
					       error->message,
					       NULL);
			g_free (primary);
		}
		g_error_free (error);
	}
}

static void
do_eject (GMount *mount,
	  GVolume *volume,
	  GDrive *drive,
	  NautilusPlacesSidebar *sidebar)
{
	GMountOperation *mount_op = get_unmount_operation (sidebar);

	if (mount != NULL) {
		g_mount_eject_with_operation (mount, 0, mount_op, NULL, mount_eject_cb,
					      g_object_ref (sidebar->window));
	} else if (volume != NULL) {
		g_volume_eject_with_operation (volume, 0, mount_op, NULL, volume_eject_cb,
					       g_object_ref (sidebar->window));
	} else if (drive != NULL) {
		g_drive_eject_with_operation (drive, 0, mount_op, NULL, drive_eject_cb,
					      g_object_ref (sidebar->window));
	}

	g_object_unref (mount_op);
}

static void
eject_shortcut_cb (GtkMenuItem           *item,
		   NautilusPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GMount *mount;
	GVolume *volume;
	GDrive *drive;
	SidebarTreeData *data;

	if (!get_selected_iter (sidebar, &iter, &data)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
			    PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
			    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	do_eject (mount, volume, drive, sidebar);
}

static gboolean
eject_or_unmount_bookmark (SidebarTreeData *data,
			   GtkTreePath *path)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean can_unmount, can_eject;
	GMount *mount;
	GVolume *volume;
	GDrive *drive;
	gboolean ret;

	model = GTK_TREE_MODEL (data->store);

	if (!path) {
		return FALSE;
	}
	if (!gtk_tree_model_get_iter (model, &iter, path)) {
		return FALSE;
	}

	gtk_tree_model_get (model, &iter,
			    PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
			    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	ret = FALSE;

	check_unmount_and_eject (mount, volume, drive, &can_unmount, &can_eject);
	/* if we can eject, it has priority over unmount */
	if (can_eject) {
		do_eject (mount, volume, drive, data->sidebar);
		ret = TRUE;
	} else if (can_unmount) {
		do_unmount (mount, data->sidebar);
		ret = TRUE;
	}

	if (mount != NULL)
		g_object_unref (mount);
	if (volume != NULL)
		g_object_unref (volume);
	if (drive != NULL)
		g_object_unref (drive);

	return ret;
}

static gboolean
eject_or_unmount_selection (SidebarTreeData *data)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	gboolean ret;

	if (!get_selected_iter (data->sidebar, &iter, NULL)) {
		return FALSE;
	}

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (data->store), &iter);
	if (path == NULL) {
		return FALSE;
	}

	ret = eject_or_unmount_bookmark (data, path);

	gtk_tree_path_free (path);

	return ret;
}

static void
drive_poll_for_media_cb (GObject *source_object,
			 GAsyncResult *res,
			 gpointer user_data)
{
	GError *error;
	char *primary;
	char *name;

	error = NULL;
	if (!g_drive_poll_for_media_finish (G_DRIVE (source_object), res, &error)) {
		if (error->code != G_IO_ERROR_FAILED_HANDLED) {
			name = g_drive_get_name (G_DRIVE (source_object));
			primary = g_strdup_printf (_("Unable to poll %s for media changes"), name);
			g_free (name);
			eel_show_error_dialog (primary,
					       error->message,
					       NULL);
			g_free (primary);
		}
		g_error_free (error);
	}
}

static void
rescan_shortcut_cb (GtkMenuItem           *item,
		    NautilusPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GDrive  *drive;
	SidebarTreeData *data;

	if (!get_selected_iter (sidebar, &iter, &data)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	if (drive != NULL) {
		g_drive_poll_for_media (drive, NULL, drive_poll_for_media_cb, NULL);
	}
	g_object_unref (drive);
}

static void
drive_start_cb (GObject      *source_object,
		GAsyncResult *res,
		gpointer      user_data)
{
	GError *error;
	char *primary;
	char *name;

	error = NULL;
	if (!g_drive_poll_for_media_finish (G_DRIVE (source_object), res, &error)) {
		if (error->code != G_IO_ERROR_FAILED_HANDLED) {
			name = g_drive_get_name (G_DRIVE (source_object));
			primary = g_strdup_printf (_("Unable to start %s"), name);
			g_free (name);
			eel_show_error_dialog (primary,
					       error->message,
					       NULL);
			g_free (primary);
		}
		g_error_free (error);
	}
}

static void
start_shortcut_cb (GtkMenuItem           *item,
		   NautilusPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GDrive  *drive;
	SidebarTreeData *data;

	if (!get_selected_iter (sidebar, &iter, &data)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	if (drive != NULL) {
		GMountOperation *mount_op;

		mount_op = gtk_mount_operation_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))));

		g_drive_start (drive, G_DRIVE_START_NONE, mount_op, NULL, drive_start_cb, NULL);

		g_object_unref (mount_op);
	}
	g_object_unref (drive);
}

static void
drive_stop_cb (GObject *source_object,
	       GAsyncResult *res,
	       gpointer user_data)
{
	NautilusWindow *window;
	GError *error;
	char *primary;
	char *name;

	window = user_data;
	g_object_unref (window);

	error = NULL;
	if (!g_drive_poll_for_media_finish (G_DRIVE (source_object), res, &error)) {
		if (error->code != G_IO_ERROR_FAILED_HANDLED) {
			name = g_drive_get_name (G_DRIVE (source_object));
			primary = g_strdup_printf (_("Unable to stop %s"), name);
			g_free (name);
			eel_show_error_dialog (primary,
					       error->message,
					       NULL);
			g_free (primary);
		}
		g_error_free (error);
	}
}

static void
stop_shortcut_cb (GtkMenuItem           *item,
		  NautilusPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GDrive  *drive;
	SidebarTreeData *data;

	if (!get_selected_iter (sidebar, &iter, &data)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	if (drive != NULL) {
		GMountOperation *mount_op = get_unmount_operation (sidebar);
		g_drive_stop (drive, G_MOUNT_UNMOUNT_NONE, mount_op, NULL, drive_stop_cb,
			      g_object_ref (sidebar->window));
		g_object_unref (mount_op);
	}
	g_object_unref (drive);
}

static void
format_shortcut_cb (GtkMenuItem           *item,
		    NautilusPlacesSidebar *sidebar)
{
	GAppInfo *app_info;
	gchar *cmdline, *device_identifier, *xid_string;
	GVolume *volume;
	GtkTreeIter iter;
	gint xid;
	SidebarTreeData *data;

	if (!get_selected_iter (sidebar, &iter, &data)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
			    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
			    -1);

	if (!volume) {
		return;
	}

	device_identifier = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	xid = (gint) gdk_x11_window_get_xid (gtk_widget_get_window (GTK_WIDGET (sidebar->window)));
	xid_string = g_strdup_printf ("%d", xid);

	cmdline = g_strconcat ("gnome-disks ",
			       "--block-device ", device_identifier, " ",
			       "--format-device ",
			       "--xid ", xid_string,
			       NULL);
	app_info = g_app_info_create_from_commandline (cmdline, NULL, 0, NULL);
	g_app_info_launch (app_info, NULL, NULL, NULL);

	g_free (cmdline);
	g_free (device_identifier);
	g_free (xid_string);
	g_clear_object (&volume);
	g_clear_object (&app_info);
}

static void
empty_trash_cb (GtkMenuItem           *item,
		NautilusPlacesSidebar *sidebar)
{
	nautilus_file_operations_empty_trash (GTK_WIDGET (sidebar->window));
}

static gboolean
find_prev_or_next_row (SidebarTreeData *data,
		       GtkTreeIter *iter,
		       gboolean go_up)
{
	GtkTreeModel *model = GTK_TREE_MODEL (data->store);
	gboolean res;
	int place_type;

	if (go_up) {
		res = gtk_tree_model_iter_previous (model, iter);
	} else {
		res = gtk_tree_model_iter_next (model, iter);
	}

	if (res) {
		gtk_tree_model_get (model, iter,
				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
				    -1);
		if (place_type == PLACES_SEPARATOR) {
			if (go_up) {
				res = gtk_tree_model_iter_previous (model, iter);
			} else {
				res = gtk_tree_model_iter_next (model, iter);
			}
		}
	}

	return res;
}

static gboolean
find_prev_row (SidebarTreeData *data, GtkTreeIter *iter)
{
	return find_prev_or_next_row (data, iter, TRUE);
}

static gboolean
find_next_row (SidebarTreeData *data, GtkTreeIter *iter)
{
	return find_prev_or_next_row (data, iter, FALSE);
}

static void
properties_cb (GtkMenuItem           *item,
	       NautilusPlacesSidebar *sidebar)
{
	GtkTreeModel *model;
	GtkTreePath *path = NULL;
	GtkTreeIter iter;
	GList *list;
	NautilusFile *file;
	char *uri;
	SidebarTreeData *data;

	if (!get_cursor (sidebar, &data, &iter)) {
		return;
	}

	model = GTK_TREE_MODEL (data->store);
	gtk_tree_model_get (model, &iter, PLACES_SIDEBAR_COLUMN_URI, &uri, -1);

	if (uri != NULL) {

		file = nautilus_file_get_by_uri (uri);
		list = g_list_prepend (NULL, nautilus_file_ref (file));

		nautilus_properties_window_present (list, GTK_WIDGET (sidebar), NULL);

		nautilus_file_list_free (list);
		g_free (uri);
	}

	gtk_tree_path_free (path);
}

static gboolean
do_focus_for_tree_data (SidebarTreeData *data)
{
	GtkTreeStore *store = data->store;
	GtkTreePath *path;
	GtkTreeIter iter;
	gboolean res;

	gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter);
	res = find_next_row (data, &iter);
	if (res) {
		path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &iter);
		gtk_tree_view_set_cursor (data->tree_view, path, NULL, FALSE);
		gtk_tree_path_free (path);
	}

	return res;
}

static gboolean
nautilus_places_sidebar_focus (GtkWidget *widget,
			       GtkDirectionType direction)
{
	NautilusPlacesSidebar *sidebar = NAUTILUS_PLACES_SIDEBAR (widget);
	SidebarTreeData *data;
	gboolean res;
	GtkTreeIter iter;

	res = get_selected_iter (sidebar, &iter, &data);

	if (!res) {
		res = do_focus_for_tree_data (sidebar->tree_data);
	}

	if (!res) {
		res = do_focus_for_tree_data (sidebar->secondary_tree_data);
	}		

	return GTK_WIDGET_CLASS (nautilus_places_sidebar_parent_class)->focus (widget, direction);
}

/* Handler for GtkWidget::key-press-event on the shortcuts list */
static gboolean
bookmarks_key_press_event_cb (GtkWidget             *widget,
			      GdkEventKey           *event,
			      SidebarTreeData       *data)
{
	guint modifiers;
	GtkTreeIter selected_iter;
	GtkTreePath *path;
	NautilusPlacesSidebar *sidebar = data->sidebar;

	if (!get_selected_iter (sidebar, &selected_iter, NULL)) {
		return FALSE;
	}

	modifiers = gtk_accelerator_get_default_mod_mask ();

	if ((event->keyval == GDK_KEY_Return ||
	     event->keyval == GDK_KEY_KP_Enter ||
	     event->keyval == GDK_KEY_ISO_Enter ||
	     event->keyval == GDK_KEY_space)) {
		NautilusWindowOpenFlags flags = 0;

		if ((event->state & modifiers) == GDK_SHIFT_MASK) {
			flags = NAUTILUS_WINDOW_OPEN_FLAG_NEW_TAB;
		} else if ((event->state & modifiers) == GDK_CONTROL_MASK) {
			flags = NAUTILUS_WINDOW_OPEN_FLAG_NEW_WINDOW;
		}

		open_selected_bookmark (data, &selected_iter, flags);
		return TRUE;
	}

	if (event->keyval == GDK_KEY_Down &&
	    (event->state & modifiers) == GDK_MOD1_MASK) {
		return eject_or_unmount_selection (data);
	}

	if (event->keyval == GDK_KEY_Up) {
		if (find_prev_row (data, &selected_iter)) {
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (data->store), &selected_iter);
			gtk_tree_view_set_cursor (data->tree_view, path, NULL, FALSE);
			gtk_tree_path_free (path);
		}
		return TRUE;
	}

	if (event->keyval == GDK_KEY_Down) {
		if (find_next_row (data, &selected_iter)) {
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (data->store), &selected_iter);
			gtk_tree_view_set_cursor (data->tree_view, path, NULL, FALSE);
			gtk_tree_path_free (path);
		}
		return TRUE;
	}

	if ((event->keyval == GDK_KEY_Delete
	     || event->keyval == GDK_KEY_KP_Delete)
	    && (event->state & modifiers) == 0) {
		remove_selected_bookmarks (sidebar);
		return TRUE;
	}

	if ((event->keyval == GDK_KEY_F2)
	    && (event->state & modifiers) == 0) {
		rename_selected_bookmark (sidebar);
		return TRUE;
	}

	return FALSE;
}

/* Constructs the popup menu for the file list if needed */
static void
bookmarks_build_popup_menu (NautilusPlacesSidebar *sidebar)
{
	GtkWidget *item;
	
	if (sidebar->popup_menu) {
		return;
	}

	sidebar->popup_menu = gtk_menu_new ();
	gtk_menu_attach_to_widget (GTK_MENU (sidebar->popup_menu),
			           GTK_WIDGET (sidebar),
			           bookmarks_popup_menu_detach_cb);
	
	item = gtk_image_menu_item_new_with_mnemonic (_("_Open"));
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
				       gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_MENU));
	g_signal_connect (item, "activate",
			  G_CALLBACK (open_shortcut_cb), sidebar);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

	item = gtk_menu_item_new_with_mnemonic (_("Open in New _Tab"));
	sidebar->popup_menu_open_in_new_tab_item = item;
	g_signal_connect (item, "activate",
			  G_CALLBACK (open_shortcut_in_new_tab_cb), sidebar);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);
	gtk_widget_show (item);

	item = gtk_menu_item_new_with_mnemonic (_("Open in New _Window"));
	g_signal_connect (item, "activate",
			  G_CALLBACK (open_shortcut_in_new_window_cb), sidebar);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);
	gtk_widget_show (item);

	eel_gtk_menu_append_separator (GTK_MENU (sidebar->popup_menu));

	item = gtk_menu_item_new_with_mnemonic (_("_Add Bookmark"));
	sidebar->popup_menu_add_shortcut_item = item;
	g_signal_connect (item, "activate",
			  G_CALLBACK (add_shortcut_cb), sidebar);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

	item = gtk_image_menu_item_new_with_label (_("Remove"));
	sidebar->popup_menu_remove_item = item;
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
				       gtk_image_new_from_stock (GTK_STOCK_REMOVE, GTK_ICON_SIZE_MENU));
	g_signal_connect (item, "activate",
			  G_CALLBACK (remove_shortcut_cb), sidebar);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);
	
	item = gtk_menu_item_new_with_label (_("Rename…"));
	sidebar->popup_menu_rename_item = item;
	g_signal_connect (item, "activate",
			  G_CALLBACK (rename_shortcut_cb), sidebar);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);
	
	/* Mount/Unmount/Eject menu items */

	sidebar->popup_menu_separator_item =
		GTK_WIDGET (eel_gtk_menu_append_separator (GTK_MENU (sidebar->popup_menu)));

	item = gtk_menu_item_new_with_mnemonic (_("_Mount"));
	sidebar->popup_menu_mount_item = item;
	g_signal_connect (item, "activate",
			  G_CALLBACK (mount_shortcut_cb), sidebar);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

	item = gtk_menu_item_new_with_mnemonic (_("_Unmount"));
	sidebar->popup_menu_unmount_item = item;
	g_signal_connect (item, "activate",
			  G_CALLBACK (unmount_shortcut_cb), sidebar);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

	item = gtk_menu_item_new_with_mnemonic (_("_Eject"));
	sidebar->popup_menu_eject_item = item;
	g_signal_connect (item, "activate",
			  G_CALLBACK (eject_shortcut_cb), sidebar);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

	item = gtk_menu_item_new_with_mnemonic (_("_Detect Media"));
	sidebar->popup_menu_rescan_item = item;
	g_signal_connect (item, "activate",
			  G_CALLBACK (rescan_shortcut_cb), sidebar);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

	item = gtk_menu_item_new_with_mnemonic (_("_Start"));
	sidebar->popup_menu_start_item = item;
	g_signal_connect (item, "activate",
			  G_CALLBACK (start_shortcut_cb), sidebar);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

	item = gtk_menu_item_new_with_mnemonic (_("_Stop"));
	sidebar->popup_menu_stop_item = item;
	g_signal_connect (item, "activate",
			  G_CALLBACK (stop_shortcut_cb), sidebar);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

	item = gtk_menu_item_new_with_mnemonic (_("_Format…"));
	sidebar->popup_menu_format_item = item;
	g_signal_connect (item, "activate",
			  G_CALLBACK (format_shortcut_cb), sidebar);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

	/* Empty Trash menu item */

	item = gtk_menu_item_new_with_mnemonic (_("Empty _Trash"));
	sidebar->popup_menu_empty_trash_item = item;
	g_signal_connect (item, "activate",
			  G_CALLBACK (empty_trash_cb), sidebar);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

	/* Properties menu item */

	sidebar->popup_menu_properties_separator_item =
		GTK_WIDGET (eel_gtk_menu_append_separator (GTK_MENU (sidebar->popup_menu)));

	item = gtk_menu_item_new_with_mnemonic (_("_Properties"));
	sidebar->popup_menu_properties_item = item;
	g_signal_connect (item, "activate",
			  G_CALLBACK (properties_cb), sidebar);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), item);

	bookmarks_check_popup_sensitivity (sidebar);
}

static void
bookmarks_popup_menu (SidebarTreeData *data,
		      GdkEventButton  *event)
{
	bookmarks_build_popup_menu (data->sidebar);
	eel_pop_up_context_menu (GTK_MENU (data->sidebar->popup_menu),
				 event);
}

/* Callback used for the GtkWidget::popup-menu signal of the shortcuts list */
static gboolean
bookmarks_popup_menu_cb (GtkWidget *widget,
			 SidebarTreeData *data)
{
	bookmarks_popup_menu (data, NULL);
	return TRUE;
}

static void
bookmarks_row_activated_cb (GtkWidget *widget,
			    GtkTreePath *path,
			    GtkTreeViewColumn *column,
			    SidebarTreeData *data)
{
	GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
	GtkTreeIter iter;
	GtkTreePath *clicked_path = NULL;
	GtkTreeModel *model = gtk_tree_view_get_model (tree_view);

	if (!gtk_tree_model_get_iter (model, &iter, path)) {
		return;
	}

	if (!clicked_eject_button (data, &clicked_path)) {
		open_selected_bookmark (data, &iter, 0);
	} else {
		gtk_tree_path_free (clicked_path);
	}
}

static gboolean
bookmarks_button_release_event_cb (GtkWidget *widget,
				   GdkEventButton *event,
				   SidebarTreeData *data)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeView *tree_view;
	gboolean ret = FALSE;
	gboolean res;

	path = NULL;
	tree_view = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (tree_view);

	if (event->type != GDK_BUTTON_RELEASE) {
		return TRUE;
	}

	if (clicked_eject_button (data, &path)) {
		eject_or_unmount_bookmark (data, path);
		gtk_tree_path_free (path);

		return FALSE;
	}

	if (event->button == 1) {
		return FALSE;
	}

	if (event->window != gtk_tree_view_get_bin_window (tree_view)) {
		return FALSE;
	}

	res = gtk_tree_view_get_path_at_pos (tree_view, (int) event->x, (int) event->y,
					     &path, NULL, NULL, NULL);

	if (!res || path == NULL) {
		return FALSE;
	}

	res = gtk_tree_model_get_iter (model, &iter, path);
	if (!res) {
		gtk_tree_path_free (path);
		return FALSE;
	}

	if (event->button == 2) {
		NautilusWindowOpenFlags flags = 0;

		flags = (event->state & GDK_CONTROL_MASK) ?
			NAUTILUS_WINDOW_OPEN_FLAG_NEW_WINDOW :
			NAUTILUS_WINDOW_OPEN_FLAG_NEW_TAB;

		open_selected_bookmark (data, &iter, flags);
		ret = TRUE;
	} else if (event->button == 3) {
		PlaceType row_type;

		gtk_tree_model_get (model, &iter,
				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &row_type,
				    -1);

		if (row_type != PLACES_SEPARATOR &&
		    row_type != PLACES_CONNECT_SERVER) {
			bookmarks_popup_menu (data, event);
		}
	}

	gtk_tree_path_free (path);

	return ret;
}

static void
bookmarks_edited (GtkCellRenderer *cell,
		  gchar           *path_string,
		  gchar           *new_text,
		  SidebarTreeData *data)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	gchar *uri;
	GFile *location;
	NautilusBookmark *bookmark;
	PlaceType type;
	NautilusPlacesSidebar *sidebar = data->sidebar;

	g_object_set (cell, "editable", FALSE, NULL);
	
	path = gtk_tree_path_new_from_string (path_string);
	if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (data->store), &iter, path)) {
		goto out;
	}

	uri = NULL;
	gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
		            PLACES_SIDEBAR_COLUMN_URI, &uri,
		            PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
		            -1);
	if (!uri) {
		goto out;
	}

	location = g_file_new_for_uri (uri);
	bookmark = nautilus_bookmark_list_item_with_location (sidebar->bookmarks, location, NULL);

	if (bookmark != NULL) {
		nautilus_bookmark_set_custom_name (bookmark, new_text);
	} else if (type == PLACES_XDG_DIR) {
		/* In case we're renaming a built-in bookmark, and it's not in the
		 * list, add it with a custom name.
		 */
		bookmark = nautilus_bookmark_new (location, new_text);
		nautilus_bookmark_list_append (sidebar->bookmarks, bookmark);
		g_object_unref (bookmark);
  	}

	g_object_unref (location);

 out:
	g_free (uri);
	gtk_tree_path_free (path);
}

static void
bookmarks_editing_canceled (GtkCellRenderer *cell,
			    SidebarTreeData *data)
{
	g_object_set (cell, "editable", FALSE, NULL);
}

static void
trash_state_changed_cb (NautilusTrashMonitor *trash_monitor,
			gboolean             state,
			gpointer             data)
{
	NautilusPlacesSidebar *sidebar;

	sidebar = NAUTILUS_PLACES_SIDEBAR (data);

	/* The trash icon changed, update the sidebar */
	update_places (sidebar);

	bookmarks_check_popup_sensitivity (sidebar);
}

static gboolean
places_sidebar_selection_func (GtkTreeSelection *selection,
			       GtkTreeModel *model,
			       GtkTreePath *path,
			       gboolean path_currently_selected,
			       gpointer user_data)
{
	GtkTreeIter iter;
	PlaceType row_type;

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &row_type,
			    -1);

	if (row_type == PLACES_EXPANDER) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
places_sidebar_separator_func (GtkTreeModel *model,
			       GtkTreeIter  *iter,
			       gpointer      user_data)
{
	PlaceType type;

	gtk_tree_model_get (model, iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
			    -1);

	return (type == PLACES_SEPARATOR);
}

static gint
places_sidebar_sort_func (GtkTreeModel *model,
			  GtkTreeIter *iter_a,
			  GtkTreeIter *iter_b,
			  gpointer user_data)
{
	PlaceType place_type_a;
	gint retval = 0;

	gtk_tree_model_get (model, iter_a,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type_a,
			    -1);

	/* "Connect to Server" always ranks last... */
	if (place_type_a == PLACES_CONNECT_SERVER) {
		retval = 1;
	}

	return retval;
}

static void
expander_cell_data_func (GtkTreeViewColumn *column,
			 GtkCellRenderer *cell,
			 GtkTreeModel *model,
			 GtkTreeIter *iter,
			 gpointer user_data)
{
	PlaceType place_type;
	gboolean is_expander, is_expanded;
	GtkTreePath *path;
	SidebarTreeData *data = user_data;

	gtk_tree_model_get (model, iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
			    -1);

	is_expander = (place_type == PLACES_EXPANDER);
	g_object_set (cell, "visible", is_expander, NULL);

	if (!is_expander) {
		return;
	}

	path = gtk_tree_model_get_path (model, iter);
	is_expanded = gtk_tree_view_row_expanded (data->tree_view,
						  path);

	if (is_expanded) {
		g_object_set (cell,
			      "expander-style", GTK_EXPANDER_EXPANDED,
			      NULL);
	} else {
		g_object_set (cell,
			      "expander-style", GTK_EXPANDER_COLLAPSED,
			      NULL);
	}
}

static void
icon_cell_data_func (GtkTreeViewColumn *column,
		     GtkCellRenderer *cell,
		     GtkTreeModel *model,
		     GtkTreeIter *iter,
		     gpointer user_data)
{
	PlaceType place_type;

	gtk_tree_model_get (model, iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
			    -1);
	
	g_object_set (cell, "visible", (place_type != PLACES_EXPANDER), NULL);
}

static void
update_hostname (NautilusPlacesSidebar *sidebar)
{
	GVariant *variant;
	gsize len;
	const gchar *hostname;

	if (sidebar->hostnamed_proxy == NULL)
		return;

	variant = g_dbus_proxy_get_cached_property (sidebar->hostnamed_proxy,
						    "PrettyHostname");
	if (variant == NULL) {
		return;
	}

	hostname = g_variant_get_string (variant, &len);
	if (len > 0 &&
	    g_strcmp0 (sidebar->hostname, hostname) != 0) {
		g_free (sidebar->hostname);
		sidebar->hostname = g_strdup (hostname);
		update_places (sidebar);
	}

	g_variant_unref (variant);
}

static void
hostname_proxy_new_cb (GObject      *source_object,
		       GAsyncResult *res,
		       gpointer      user_data)
{
	NautilusPlacesSidebar *sidebar = user_data;
	GError *error = NULL;

	sidebar->hostnamed_proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	g_clear_object (&sidebar->hostnamed_cancellable);

	if (error != NULL) {
		g_debug ("Failed to create D-Bus proxy: %s", error->message);
		g_error_free (error);
		return;
	}

	g_signal_connect_swapped (sidebar->hostnamed_proxy,
				  "g-properties-changed",
				  G_CALLBACK (update_hostname),
				  sidebar);
	update_hostname (sidebar);
}

static void
sidebar_tree_view_data_free (SidebarTreeData *data)
{
	g_clear_object (&data->store);
	g_slice_free (SidebarTreeData, data);
}

static SidebarTreeData *
setup_tree_view (NautilusPlacesSidebar *sidebar)
{
	SidebarTreeData   *data;
	GtkTreeView       *tree_view;
	GtkTreeViewColumn *col;
	GtkCellRenderer   *cell;
	GtkTreeSelection  *selection;
	GIcon             *eject;
	GtkTreeStore      *store;

	data = g_slice_new0 (SidebarTreeData);
	data->sidebar = sidebar;

	tree_view = GTK_TREE_VIEW (gtk_tree_view_new ());
	gtk_tree_view_set_headers_visible (tree_view, FALSE);
	gtk_tree_view_set_show_expanders (tree_view, FALSE);
	gtk_tree_view_set_level_indentation (tree_view, TREE_VIEW_INDENT_XPAD);
	data->tree_view = tree_view;

	col = GTK_TREE_VIEW_COLUMN (gtk_tree_view_column_new ());

	/* initial padding */
	cell = gtk_cell_renderer_text_new ();
	g_object_set (cell,
		      "xpad", INITIAL_XPAD,
		      "ypad", INITIAL_YPAD,
		      NULL);
	gtk_tree_view_column_pack_start (col, cell, FALSE);

	/* icon renderer */
	cell = gtk_cell_renderer_pixbuf_new ();
	g_object_set (cell,
		      "follow-state", TRUE,
		      NULL);
	gtk_tree_view_column_pack_start (col, cell, FALSE);
	gtk_tree_view_column_set_attributes (col, cell,
					     "gicon", PLACES_SIDEBAR_COLUMN_GICON,
					     NULL);
	gtk_tree_view_column_set_cell_data_func (col, cell,
						 icon_cell_data_func,
						 data, NULL);

	/* eject text renderer */
	cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (col, cell, TRUE);
	gtk_tree_view_column_set_attributes (col, cell,
					     "text", PLACES_SIDEBAR_COLUMN_NAME,
					     "visible", PLACES_SIDEBAR_COLUMN_EJECT,
					     NULL);
	g_object_set (cell,
		      "ellipsize", PANGO_ELLIPSIZE_END,
		      "ellipsize-set", TRUE,
		      NULL);

	/* eject icon renderer */
	cell = gtk_cell_renderer_pixbuf_new ();
	data->eject_icon_cell_renderer = cell;
	eject = g_themed_icon_new_with_default_fallbacks ("media-eject-symbolic");
	g_object_set (cell,
		      "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
		      "stock-size", GTK_ICON_SIZE_MENU,
		      "xpad", EJECT_BUTTON_XPAD,
		      /* align right, because for some reason gtk+ expands
			 this even though we tell it not to. */
		      "xalign", 1.0,
		      "follow-state", TRUE,
		      "gicon", eject,
		      NULL);
	gtk_tree_view_column_pack_start (col, cell, FALSE);
	gtk_tree_view_column_set_attributes (col, cell,
					     "visible", PLACES_SIDEBAR_COLUMN_EJECT,
					     NULL);
	g_object_unref (eject);

	/* normal text renderer */
	cell = gtk_cell_renderer_text_new ();
	data->normal_text_cell_renderer = cell;
	gtk_tree_view_column_pack_start (col, cell, TRUE);
	g_object_set (G_OBJECT (cell), "editable", FALSE, NULL);
	gtk_tree_view_column_set_attributes (col, cell,
					     "text", PLACES_SIDEBAR_COLUMN_NAME,
					     "visible", PLACES_SIDEBAR_COLUMN_NO_EJECT,
					     "editable-set", PLACES_SIDEBAR_COLUMN_BOOKMARK,
					     NULL);
	g_object_set (cell,
		      "ellipsize", PANGO_ELLIPSIZE_END,
		      "ellipsize-set", TRUE,
		      NULL);

	g_signal_connect (cell, "edited", 
			  G_CALLBACK (bookmarks_edited), data);
	g_signal_connect (cell, "editing-canceled", 
			  G_CALLBACK (bookmarks_editing_canceled), data);

	/* expander cell renederer */
	cell = empathy_cell_renderer_expander_new ();
	g_object_set (cell,
		      "xalign", 0.0,
		      NULL);
	gtk_tree_view_column_pack_start (col, cell, FALSE);
	gtk_tree_view_column_set_cell_data_func (col, cell,
						 expander_cell_data_func,
						 data, NULL);

	/* this is required to align the eject buttons to the right */
	gtk_tree_view_column_set_max_width (GTK_TREE_VIEW_COLUMN (col), NAUTILUS_ICON_SIZE_SMALLER);
	gtk_tree_view_append_column (tree_view, col);

	store = nautilus_shortcuts_model_new (sidebar);
	data->store = store;

	gtk_tree_view_set_tooltip_column (tree_view, PLACES_SIDEBAR_COLUMN_TOOLTIP);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
					      PLACES_SIDEBAR_COLUMN_NAME,
					      GTK_SORT_ASCENDING);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store),
					 PLACES_SIDEBAR_COLUMN_NAME,
					 places_sidebar_sort_func,
					 data, NULL);
	gtk_tree_view_set_row_separator_func (tree_view,
					      places_sidebar_separator_func,
					      data, NULL);

	gtk_tree_view_set_model (tree_view, GTK_TREE_MODEL (store));
	gtk_tree_view_set_enable_search (tree_view, FALSE);

	gtk_tree_view_set_search_column (tree_view, PLACES_SIDEBAR_COLUMN_NAME);
	selection = gtk_tree_view_get_selection (tree_view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	gtk_tree_selection_set_select_function (selection,
						places_sidebar_selection_func,
						data, NULL);

	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (tree_view),
						GDK_BUTTON1_MASK,
						nautilus_shortcuts_source_targets,
						G_N_ELEMENTS (nautilus_shortcuts_source_targets),
						GDK_ACTION_MOVE);
	gtk_drag_dest_set (GTK_WIDGET (tree_view),
			   0,
			   nautilus_shortcuts_drop_targets, G_N_ELEMENTS (nautilus_shortcuts_drop_targets),
			   GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK);

	g_signal_connect (tree_view, "key-press-event",
			  G_CALLBACK (bookmarks_key_press_event_cb), data);

	g_signal_connect (tree_view, "drag-motion",
			  G_CALLBACK (drag_motion_callback), data);
	g_signal_connect (tree_view, "drag-leave",
			  G_CALLBACK (drag_leave_callback), data);
	g_signal_connect (tree_view, "drag-data-received",
			  G_CALLBACK (drag_data_received_callback), data);
	g_signal_connect (tree_view, "drag-drop",
			  G_CALLBACK (drag_drop_callback), data);

	g_signal_connect (selection, "changed",
			  G_CALLBACK (bookmarks_selection_changed_cb), data);
	g_signal_connect (tree_view, "popup-menu",
			  G_CALLBACK (bookmarks_popup_menu_cb), data);
	g_signal_connect (tree_view, "button-release-event",
			  G_CALLBACK (bookmarks_button_release_event_cb), data);
	g_signal_connect (tree_view, "row-activated",
			  G_CALLBACK (bookmarks_row_activated_cb), data);

	gtk_tree_view_set_activate_on_single_click (tree_view, TRUE);

	return data;
}

static void
nautilus_places_sidebar_init (NautilusPlacesSidebar *sidebar)
{
	GtkWidget         *scrolled_win;

	sidebar->volume_monitor = g_volume_monitor_get ();

	gtk_orientable_set_orientation (GTK_ORIENTABLE (sidebar), GTK_ORIENTATION_VERTICAL);

	scrolled_win = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_win),
					GTK_POLICY_NEVER,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_win), GTK_SHADOW_IN);
	gtk_style_context_set_junction_sides (gtk_widget_get_style_context (GTK_WIDGET (scrolled_win)),
					      GTK_JUNCTION_RIGHT | GTK_JUNCTION_LEFT);
	gtk_widget_set_vexpand (scrolled_win, TRUE);
	gtk_container_add (GTK_CONTAINER (sidebar), scrolled_win);

  	/* tree views */
	sidebar->tree_data = setup_tree_view (sidebar);
	gtk_widget_set_valign (GTK_WIDGET (sidebar->tree_data->tree_view), GTK_ALIGN_CENTER);
	gtk_container_add (GTK_CONTAINER (scrolled_win),
			   GTK_WIDGET (sidebar->tree_data->tree_view));

	sidebar->secondary_tree_data = setup_tree_view (sidebar);
	gtk_container_add (GTK_CONTAINER (sidebar),
			   GTK_WIDGET (sidebar->secondary_tree_data->tree_view));

	sidebar->hostname = g_strdup (_("Computer"));
	sidebar->hostnamed_cancellable = g_cancellable_new ();
	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
				  G_DBUS_PROXY_FLAGS_GET_INVALIDATED_PROPERTIES,
				  NULL,
				  "org.freedesktop.hostname1",
				  "/org/freedesktop/hostname1",
				  "org.freedesktop.hostname1",
				  sidebar->hostnamed_cancellable,
				  hostname_proxy_new_cb,
				  sidebar);

	g_signal_connect_object (nautilus_trash_monitor_get (),
				 "trash-state-changed",
				 G_CALLBACK (trash_state_changed_cb),
				 sidebar, 0);

	gtk_widget_show_all (GTK_WIDGET (sidebar));
}

static void
nautilus_places_sidebar_dispose (GObject *object)
{
	NautilusPlacesSidebar *sidebar;

	sidebar = NAUTILUS_PLACES_SIDEBAR (object);

	sidebar->window = NULL;
	g_clear_pointer (&sidebar->tree_data, sidebar_tree_view_data_free);
	g_clear_pointer (&sidebar->secondary_tree_data, sidebar_tree_view_data_free);

	g_free (sidebar->uri);
	sidebar->uri = NULL;

	free_drag_data (sidebar);

	if (sidebar->bookmarks_changed_id != 0) {
		g_signal_handler_disconnect (sidebar->bookmarks,
					     sidebar->bookmarks_changed_id);
		sidebar->bookmarks_changed_id = 0;
	}

	if (sidebar->go_to_after_mount_slot) {
		g_object_remove_weak_pointer (G_OBJECT (sidebar->go_to_after_mount_slot),
					      (gpointer *) &sidebar->go_to_after_mount_slot);
		sidebar->go_to_after_mount_slot = NULL;
	}

	g_signal_handlers_disconnect_by_func (nautilus_preferences,
					      bookmarks_popup_menu_detach_cb,
					      sidebar);

	if (sidebar->volume_monitor != NULL) {
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor, 
						      volume_added_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor, 
						      volume_removed_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor, 
						      volume_changed_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor, 
						      mount_added_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor, 
						      mount_removed_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor, 
						      mount_changed_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor, 
						      drive_disconnected_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor, 
						      drive_connected_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor, 
						      drive_changed_callback, sidebar);

		g_clear_object (&sidebar->volume_monitor);
	}

	if (sidebar->hostnamed_cancellable != NULL) {
		g_cancellable_cancel (sidebar->hostnamed_cancellable);
		g_clear_object (&sidebar->hostnamed_cancellable);
	}

	g_clear_object (&sidebar->hostnamed_proxy);
	g_free (sidebar->hostname);
	sidebar->hostname = NULL;

	G_OBJECT_CLASS (nautilus_places_sidebar_parent_class)->dispose (object);
}

static void
nautilus_places_sidebar_class_init (NautilusPlacesSidebarClass *class)
{
	G_OBJECT_CLASS (class)->dispose = nautilus_places_sidebar_dispose;

	GTK_WIDGET_CLASS (class)->style_set = nautilus_places_sidebar_style_set;
	GTK_WIDGET_CLASS (class)->focus = nautilus_places_sidebar_focus;
}

static void
nautilus_places_sidebar_set_parent_window (NautilusPlacesSidebar *sidebar,
					   NautilusWindow *window)
{
	NautilusWindowSlot *slot;
	NautilusApplication *app = NAUTILUS_APPLICATION (g_application_get_default ());

	sidebar->window = window;

	slot = nautilus_window_get_active_slot (window);

	sidebar->bookmarks = nautilus_application_get_bookmarks (app);
	sidebar->bookmarks_changed_id =
		g_signal_connect_swapped (sidebar->bookmarks, "changed",
					  G_CALLBACK (update_places),
					  sidebar);

	g_signal_connect_object (sidebar->volume_monitor, "volume-added",
				 G_CALLBACK (volume_added_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "volume-removed",
				 G_CALLBACK (volume_removed_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "volume-changed",
				 G_CALLBACK (volume_changed_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "mount-added",
				 G_CALLBACK (mount_added_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "mount-removed",
				 G_CALLBACK (mount_removed_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "mount-changed",
				 G_CALLBACK (mount_changed_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "drive-disconnected",
				 G_CALLBACK (drive_disconnected_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "drive-connected",
				 G_CALLBACK (drive_connected_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "drive-changed",
				 G_CALLBACK (drive_changed_callback), sidebar, 0);

	update_places (sidebar);

	g_signal_connect_object (window, "loading-uri",
				 G_CALLBACK (loading_uri_callback),
				 sidebar, 0);
	sidebar->uri = nautilus_window_slot_get_current_uri (slot);
	update_current_uri (sidebar);
}

static void
nautilus_places_sidebar_style_set (GtkWidget *widget,
				   GtkStyle  *previous_style)
{
	NautilusPlacesSidebar *sidebar;

	sidebar = NAUTILUS_PLACES_SIDEBAR (widget);

	update_places (sidebar);
}

GtkWidget *
nautilus_places_sidebar_new (NautilusWindow *window)
{
	NautilusPlacesSidebar *sidebar;
	
	sidebar = g_object_new (nautilus_places_sidebar_get_type (), NULL);
	nautilus_places_sidebar_set_parent_window (sidebar, window);

	return GTK_WIDGET (sidebar);
}


/* Drag and drop interfaces */

/* GtkTreeDragSource::row_draggable implementation for the shortcuts filter model */

static gboolean
nautilus_shortcuts_model_row_draggable (GtkTreeDragSource *drag_source,
					GtkTreePath       *path)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	SectionType section_type;

	model = GTK_TREE_MODEL (drag_source);

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
			    -1);

	if (section_type == SECTION_BOOKMARKS) {
		return TRUE;
	}

	return FALSE;
}

static void
_nautilus_shortcuts_model_class_init (NautilusShortcutsModelClass *klass)
{

}

static void
_nautilus_shortcuts_model_init (NautilusShortcutsModel *model)
{
	model->sidebar = NULL;
}

static void
_nautilus_shortcuts_model_drag_source_init (GtkTreeDragSourceIface *iface)
{
	iface->row_draggable = nautilus_shortcuts_model_row_draggable;
}

static GtkTreeStore *
nautilus_shortcuts_model_new (NautilusPlacesSidebar *sidebar)
{
	NautilusShortcutsModel *model;
	GType model_types[PLACES_SIDEBAR_COLUMN_COUNT] = {
		G_TYPE_INT, 
		G_TYPE_STRING,
		G_TYPE_DRIVE,
		G_TYPE_VOLUME,
		G_TYPE_MOUNT,
		G_TYPE_STRING,
		G_TYPE_ICON,
		G_TYPE_INT,
		G_TYPE_BOOLEAN,
		G_TYPE_BOOLEAN,
		G_TYPE_BOOLEAN,
		G_TYPE_STRING,
		G_TYPE_INT
	};

	model = g_object_new (_nautilus_shortcuts_model_get_type (), NULL);
	model->sidebar = sidebar;

	gtk_tree_store_set_column_types (GTK_TREE_STORE (model),
					 PLACES_SIDEBAR_COLUMN_COUNT,
					 model_types);

	return GTK_TREE_STORE (model);
}
