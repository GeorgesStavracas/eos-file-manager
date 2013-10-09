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
  GHashTable *actions;

  GtkWidget *hbox;
  GtkWidget *label_widget;
  GtkWidget *options_box;
};

static void nautilus_option_menu_item_activatable_interface_init (GtkActivatableIface *iface);
static void update_label_sensitivity_for_actions (NautilusOptionMenuItem *self);

G_DEFINE_TYPE_WITH_CODE (NautilusOptionMenuItem, nautilus_option_menu_item, GTK_TYPE_MENU_ITEM,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_ACTIVATABLE,
                                                nautilus_option_menu_item_activatable_interface_init))

static void
nautilus_option_menu_item_finalize (GObject *object)
{
  NautilusOptionMenuItem *self = NAUTILUS_OPTION_MENU_ITEM (object);

  g_clear_pointer (&self->priv->actions, g_hash_table_destroy);
  g_clear_pointer (&self->priv->options, g_hash_table_destroy);

  G_OBJECT_CLASS (nautilus_option_menu_item_parent_class)->finalize (object);
}

static void
nautilus_option_menu_item_set_label (GtkMenuItem *menu_item,
                                     const gchar *label)
{
  NautilusOptionMenuItem *self = NAUTILUS_OPTION_MENU_ITEM (menu_item);

  if (label != NULL)
    {
      gtk_widget_show (self->priv->label_widget);
      gtk_label_set_label (GTK_LABEL (self->priv->label_widget), label);
    }
  else
    {
      gtk_label_set_label (GTK_LABEL (self->priv->label_widget), "");
      gtk_widget_hide (self->priv->label_widget);
    }

  g_object_notify (G_OBJECT (self), "label");
}

static const gchar *
nautilus_option_menu_item_get_label_impl (GtkMenuItem *menu_item)
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
  mclass->get_label = nautilus_option_menu_item_get_label_impl;
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
  gtk_widget_set_hexpand (self->priv->label_widget, TRUE);
  gtk_misc_set_alignment (GTK_MISC (self->priv->label_widget), 0.0, 0.5);
  gtk_label_set_mnemonic_widget (GTK_LABEL (self->priv->label_widget),
                                 GTK_WIDGET (self));
  gtk_label_set_use_underline (GTK_LABEL (self->priv->label_widget), TRUE);
  gtk_container_add (GTK_CONTAINER (self->priv->hbox), self->priv->label_widget);

  self->priv->options_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_halign (self->priv->options_box, GTK_ALIGN_END);
  gtk_style_context_add_class (gtk_widget_get_style_context (self->priv->options_box),
                               GTK_STYLE_CLASS_LINKED);
  gtk_container_add (GTK_CONTAINER (self->priv->hbox), self->priv->options_box);

  gtk_widget_show_all (self->priv->hbox);

  self->priv->options = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, NULL);
  self->priv->actions = g_hash_table_new (NULL, NULL);
}

static void
update_label_for_action (NautilusOptionMenuItem *self,
                         GtkAction *action)
{
  const gchar *label;

  label = gtk_action_get_label (action);
  gtk_menu_item_set_label (GTK_MENU_ITEM (self), label);
}

static void
nautilus_option_menu_item_update (GtkActivatable *activatable,
                                  GtkAction *action,
                                  const gchar *property_name)
{
  NautilusOptionMenuItem *self = NAUTILUS_OPTION_MENU_ITEM (activatable);

  if (g_strcmp0 (property_name, "visible") == 0)
    gtk_widget_set_visible (GTK_WIDGET (self), gtk_action_is_visible (action));
  else if (g_strcmp0 (property_name, "sensitive") == 0)
    {
      gtk_widget_set_sensitive (GTK_WIDGET (self), gtk_action_is_sensitive (action));
      update_label_sensitivity_for_actions (self);
    }
  else if (gtk_activatable_get_use_action_appearance (activatable) &&
           g_strcmp0 (property_name, "label") == 0)
    update_label_for_action (self, action);
}

static void
nautilus_option_menu_item_sync_action_properties (GtkActivatable *activatable,
                                                  GtkAction *action)
{
  NautilusOptionMenuItem *self = NAUTILUS_OPTION_MENU_ITEM (activatable);
  gboolean use_action_appearance;

  use_action_appearance = gtk_activatable_get_use_action_appearance (activatable);

  if (!use_action_appearance || !action)
    gtk_label_set_mnemonic_widget (GTK_LABEL (self->priv->label_widget),
                                   NULL);

  if (!action)
    return;

  gtk_widget_set_visible (GTK_WIDGET (self), gtk_action_is_visible (action));
  gtk_widget_set_sensitive (GTK_WIDGET (self), gtk_action_is_sensitive (action));
  update_label_sensitivity_for_actions (self);

  if (use_action_appearance)
    {
      gtk_label_set_use_underline (GTK_LABEL (self->priv->label_widget), TRUE);
      update_label_for_action (self, action);
    }
}

static void
nautilus_option_menu_item_activatable_interface_init (GtkActivatableIface *iface)
{
  iface->update = nautilus_option_menu_item_update;
  iface->sync_action_properties = nautilus_option_menu_item_sync_action_properties;
}

static void
update_label_sensitivity_for_actions (NautilusOptionMenuItem *self)
{
  gint num_options = g_hash_table_size (self->priv->options);
  gint num_actions = g_hash_table_size (self->priv->actions);
  gint num_sensitive_actions;
  GtkAction *action;
  GHashTableIter iter;

  if (num_options > num_actions)
    {
      gtk_widget_set_sensitive (self->priv->label_widget, TRUE);
      return;
    }

  num_sensitive_actions = 0;
  g_hash_table_iter_init (&iter, self->priv->actions);
  while (g_hash_table_iter_next (&iter, (gpointer *) &action, NULL))
    {
      if (gtk_action_is_sensitive (action))
        num_sensitive_actions++;
    }

  gtk_widget_set_sensitive (self->priv->label_widget,
                            (num_sensitive_actions > 0));
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
      w = gtk_label_new_with_mnemonic (label);
      gtk_label_set_mnemonic_widget (GTK_LABEL (w), option);
      gtk_container_add (GTK_CONTAINER (child), w);
    }

  g_object_set_data_full (G_OBJECT (option),
                          "option-id", g_strdup (id),
                          (GDestroyNotify) g_free);

  return option;
}

static GtkWidget *
insert_menu_item_option (NautilusOptionMenuItem *self,
                         const gchar *id,
                         const gchar *label,
                         const gchar *icon_name)
{
  GtkWidget *option;

  option = create_menu_item_option (id, label, icon_name);
  g_hash_table_insert (self->priv->options, g_strdup (id), option);
  gtk_container_add (GTK_CONTAINER (self->priv->options_box), option);
  gtk_widget_show_all (option);

  g_signal_connect (option, "clicked",
                    G_CALLBACK (on_option_clicked), self);

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
  g_return_if_fail (NAUTILUS_IS_OPTION_MENU_ITEM (self));
  g_return_if_fail (id != NULL);

  insert_menu_item_option (self, id, label, icon_name);
  update_label_sensitivity_for_actions (self);
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

  update_label_sensitivity_for_actions (self);

  return TRUE;
}

void
nautilus_option_menu_item_add_action (NautilusOptionMenuItem *self,
                                      GtkAction *action)
{
  GtkWidget *option;
  const gchar *id;
  const gchar *label;

  g_return_if_fail (NAUTILUS_IS_OPTION_MENU_ITEM (self));
  g_return_if_fail (GTK_IS_ACTION (action));

  id = gtk_action_get_name (action);
  label = gtk_action_get_label (action);

  option = insert_menu_item_option (self, id, label, NULL);
  gtk_activatable_set_related_action (GTK_ACTIVATABLE (option), action);

  g_hash_table_add (self->priv->actions, action);

  g_signal_connect_swapped (action, "notify::sensitive",
                            G_CALLBACK (update_label_sensitivity_for_actions), self);
  update_label_sensitivity_for_actions (self);
}

gboolean
nautilus_option_menu_item_remove_action (NautilusOptionMenuItem *self,
                                         GtkAction *action)
{
  gboolean retval;

  g_return_val_if_fail (NAUTILUS_IS_OPTION_MENU_ITEM (self), FALSE);
  g_return_val_if_fail (GTK_IS_ACTION (action), FALSE);

  retval = nautilus_option_menu_item_remove_option (self, gtk_action_get_name (action));
  g_hash_table_remove (self->priv->actions, action);

  g_signal_handlers_disconnect_by_func (action, update_label_sensitivity_for_actions, self);
  update_label_sensitivity_for_actions (self);

  return retval;
}

GtkWidget *
nautilus_option_menu_item_get_label (NautilusOptionMenuItem *self)
{
  g_return_val_if_fail (NAUTILUS_IS_OPTION_MENU_ITEM (self), NULL);
  return self->priv->label_widget;
}

G_DEFINE_TYPE (NautilusOptionMenuAction, nautilus_option_menu_action, GTK_TYPE_ACTION)

static void
nautilus_option_menu_action_class_init (NautilusOptionMenuActionClass *klass)
{
  GtkActionClass *aclass = GTK_ACTION_CLASS (klass);
  aclass->menu_item_type = NAUTILUS_TYPE_OPTION_MENU_ITEM;
}

static void
nautilus_option_menu_action_init (NautilusOptionMenuAction *self)
{
  /* do nothing */
}

GtkAction *
nautilus_option_menu_action_new (const gchar *name,
                                 const gchar *label,
                                 const gchar *tooltip,
                                 const gchar *stock_id)
{
  g_return_val_if_fail (name != NULL, NULL);

  return g_object_new (NAUTILUS_TYPE_OPTION_MENU_ACTION,
                       "name", name,
		       "label", label,
		       "tooltip", tooltip,
		       "stock-id", stock_id,
		       NULL);
}
