/*
 * nautilus-option-menu-item.h - Menu item to show different options
 *
 * Copyright (C) 2013 Endless Mobile, Inc
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors: Cosimo Cecchi <cosimo@endlessm.com>
 *
 */

#ifndef __NAUTILUS_OPTION_MENU_ITEM_H__
#define __NAUTILUS_OPTION_MENU_ITEM_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_OPTION_MENU_ITEM            (nautilus_option_menu_item_get_type ())
#define NAUTILUS_OPTION_MENU_ITEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NAUTILUS_TYPE_OPTION_MENU_ITEM, NautilusOptionMenuItem))
#define NAUTILUS_OPTION_MENU_ITEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NAUTILUS_TYPE_OPTION_MENU_ITEM, NautilusOptionMenuItemClass))
#define NAUTILUS_IS_OPTION_MENU_ITEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NAUTILUS_TYPE_OPTION_MENU_ITEM))
#define NAUTILUS_IS_OPTION_MENU_ITEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NAUTILUS_TYPE_OPTION_MENU_ITEM))
#define NAUTILUS_OPTION_MENU_ITEM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NAUTILUS_TYPE_OPTION_MENU_ITEM, NautilusOptionMenuItemClass))


typedef struct _NautilusOptionMenuItem       NautilusOptionMenuItem;
typedef struct _NautilusOptionMenuItemPriv NautilusOptionMenuItemPriv;
typedef struct _NautilusOptionMenuItemClass  NautilusOptionMenuItemClass;

struct _NautilusOptionMenuItem
{
  GtkMenuItem parent;

  NautilusOptionMenuItemPriv *priv;
};

struct _NautilusOptionMenuItemClass
{
  GtkMenuItemClass parent_class;
};

GType	   nautilus_option_menu_item_get_type	   (void) G_GNUC_CONST;
GtkWidget* nautilus_option_menu_item_new	   (const gchar *label);

void nautilus_option_menu_item_add_option (NautilusOptionMenuItem *self,
                                           const gchar *id,
                                           const gchar *label,
                                           const gchar *icon_name);
gboolean nautilus_option_menu_item_remove_option (NautilusOptionMenuItem *self,
                                                  const gchar *id);

G_END_DECLS

#endif /* __NAUTILUS_OPTION_MENU_ITEM_H__ */
