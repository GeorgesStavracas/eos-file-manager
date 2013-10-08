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

#include "config.h"

#include "nautilus-option-menu-item.h"

#define MENU_ITEM_SPACING 18
#define MENU_ITEM_OPTION_SPACING 6

enum {
  OPTION_ACTIVATED,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0, };

struct _NautilusOptionMenuItemPriv {
  GHashTable *options;

  GtkWidget *hbox;
  GtkWidget *label_widget;
  GtkWidget *options_box;
};

G_DEFINE_TYPE (NautilusOptionMenuItem, nautilus_option_menu_item, GTK_TYPE_MENU_ITEM)

static void
nautilus_option_menu_item_finalize (GObject *object)
{
  NautilusOptionMenuItem *self = NAUTILUS_OPTION_MENU_ITEM (object);

  g_clear_pointer (&self->priv->options, g_hash_table_destroy);

  G_OBJECT_CLASS (nautilus_option_menu_item_parent_class)->finalize (object);
}

static void
nautilus_option_menu_item_set_label (GtkMenuItem *menu_item,
                                     const gchar *label)
{
  NautilusOptionMenuItem *self = NAUTILUS_OPTION_MENU_ITEM (menu_item);

  gtk_label_set_label (GTK_LABEL (self->priv->label_widget), label ? label : "");
  g_object_notify (G_OBJECT (self), "label");
}

static const gchar *
nautilus_option_menu_item_get_label (GtkMenuItem *menu_item)
{
  NautilusOptionMenuItem *self = NAUTILUS_OPTION_MENU_ITEM (menu_item);

  return gtk_label_get_label (GTK_LABEL (self->priv->label_widget));
}

static void
nautilus_option_menu_item_select (GtkMenuItem *menu_item)
{
  /* do nothing, buttons have their own prelighting */
}

static void
nautilus_option_menu_item_deselect (GtkMenuItem *menu_item)
{
  /* do nothing, buttons have their own prelighting */
}

static void
nautilus_option_menu_item_map (GtkWidget *widget)
{
  NautilusOptionMenuItem *self = NAUTILUS_OPTION_MENU_ITEM (widget);
  GtkWidget *button;
  GdkWindow *event_window;
  GList *buttons, *l;

  GTK_WIDGET_CLASS (nautilus_option_menu_item_parent_class)->map (widget);

  buttons = g_hash_table_get_values (self->priv->options);
  for (l = buttons; l != NULL; l = l->next)
    {
      button = l->data;

      /* this is a tad evil, but we can't let GtkMenuItem's event_window
       * steal events from our buttons.
       */
      event_window = gtk_button_get_event_window (GTK_BUTTON (button));
      gdk_window_raise (event_window);
    }

  g_list_free (buttons);
}

static void
nautilus_option_menu_item_class_init (NautilusOptionMenuItemClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GtkMenuItemClass *mclass = GTK_MENU_ITEM_CLASS (klass);
  GtkWidgetClass *wclass = GTK_WIDGET_CLASS (klass);

  oclass->finalize = nautilus_option_menu_item_finalize;

  mclass->set_label = nautilus_option_menu_item_set_label;
  mclass->get_label = nautilus_option_menu_item_get_label;
  mclass->select = nautilus_option_menu_item_select;
  mclass->deselect = nautilus_option_menu_item_deselect;

  wclass->map = nautilus_option_menu_item_map;

  signals[OPTION_ACTIVATED] =
    g_signal_new ("option-activated",
                  NAUTILUS_TYPE_OPTION_MENU_ITEM,
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  g_type_class_add_private (klass, sizeof (NautilusOptionMenuItemPriv));
}

static void
nautilus_option_menu_item_init (NautilusOptionMenuItem *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, NAUTILUS_TYPE_OPTION_MENU_ITEM,
                                            NautilusOptionMenuItemPriv);

  self->priv->hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, MENU_ITEM_SPACING);
  gtk_container_add (GTK_CONTAINER (self), self->priv->hbox);

  self->priv->label_widget = gtk_label_new (NULL);
  gtk_container_add (GTK_CONTAINER (self->priv->hbox), self->priv->label_widget);

  self->priv->options_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_style_context_add_class (gtk_widget_get_style_context (self->priv->options_box),
                               GTK_STYLE_CLASS_LINKED);
  gtk_widget_set_halign (self->priv->options_box, GTK_ALIGN_END);
  gtk_container_add (GTK_CONTAINER (self->priv->hbox), self->priv->options_box);

  gtk_widget_show_all (self->priv->hbox);

  self->priv->options = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, NULL);
}

static void
on_option_clicked (GtkWidget *option,
                   gpointer user_data)
{
  NautilusOptionMenuItem *self = user_data;
  const gchar *id;
  GQuark id_quark;

  id = g_object_get_data (G_OBJECT (option), "option-id");
  id_quark = g_quark_from_string (id);

  g_signal_emit (self, signals[OPTION_ACTIVATED], id_quark);
}

static GtkWidget *
create_menu_item_option (const gchar *id,
                         const gchar *label,
                         const gchar *icon_name)
{
  GtkWidget *option, *child, *w;

  option = gtk_button_new ();
  child = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, MENU_ITEM_OPTION_SPACING);
  gtk_container_add (GTK_CONTAINER (option), child);

  if (icon_name != NULL)
    {
      w = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
      gtk_container_add (GTK_CONTAINER (child), w);
    }

  if (label != NULL)
    {
      w = gtk_label_new (label);
      gtk_container_add (GTK_CONTAINER (child), w);
    }

  g_object_set_data_full (G_OBJECT (option),
                          "option-id", g_strdup (id),
                          (GDestroyNotify) g_free);

  return option;
}

GtkWidget *
nautilus_option_menu_item_new (const gchar *label)
{
  return g_object_new (NAUTILUS_TYPE_OPTION_MENU_ITEM,
                       "label", label,
                       NULL);
}

void
nautilus_option_menu_item_add_option (NautilusOptionMenuItem *self,
                                      const gchar *id,
                                      const gchar *label,
                                      const gchar *icon_name)
{
  GtkWidget *option;

  g_return_if_fail (NAUTILUS_IS_OPTION_MENU_ITEM (self));
  g_return_if_fail (id != NULL);

  option = create_menu_item_option (id, label, icon_name);
  g_hash_table_insert (self->priv->options, g_strdup (id), option);
  gtk_container_add (GTK_CONTAINER (self->priv->options_box), option);
  gtk_widget_show_all (option);

  g_signal_connect (option, "clicked",
                    G_CALLBACK (on_option_clicked), self);
}

gboolean
nautilus_option_menu_item_remove_option (NautilusOptionMenuItem *self,
                                         const gchar *id)
{
  GtkWidget *option;

  g_return_val_if_fail (NAUTILUS_IS_OPTION_MENU_ITEM (self), FALSE);
  g_return_val_if_fail (id != NULL, FALSE);

  option = g_hash_table_lookup (self->priv->options, id);
  if (option == NULL)
    return FALSE;

  g_hash_table_remove (self->priv->options, id);
  gtk_widget_destroy (option);
  return TRUE;
}
