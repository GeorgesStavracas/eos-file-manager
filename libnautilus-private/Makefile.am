include $(top_srcdir)/Makefile.shared

noinst_LTLIBRARIES=libnautilus-private.la

INCLUDES = 						\
	-I$(top_srcdir) 				\
	-I$(top_builddir) 				\
	$(BASE_CFLAGS)					\
	$(COMMON_CFLAGS)				\
	$(EOS_FILE_MANAGER_CFLAGS)			\
	$(WARNING_CFLAGS)				\
	$(DISABLE_DEPRECATED)				\
	$(TRACKER_CFLAGS)				\
	-DDATADIR=\""$(datadir)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DEOS_FILE_MANAGER_DATADIR=\""$(datadir)/eos-file-manager"\" 	\
	-DNAUTILUS_EXTENSIONDIR=\""$(libdir)/eos-file-manager/extensions-3.0"\" \
	$(NULL)

dbus_built_sources = nautilus-generated.c nautilus-generated.h
$(dbus_built_sources) : Makefile.am $(top_srcdir)/data/dbus-interfaces.xml
	gdbus-codegen									\
		--interface-prefix org.gnome.Nautilus.					\
		--c-namespace NautilusDBus						\
		--c-generate-object-manager						\
		--generate-c-code nautilus-generated					\
		$(top_srcdir)/data/dbus-interfaces.xml					\
		$(NULL)

BUILT_SOURCES = \
	$(dbus_built_sources) \
	$(NULL)

if ENABLE_TRACKER
nautilus_tracker_engine_sources = \
	nautilus-search-engine-tracker.c \
	nautilus-search-engine-tracker.h
endif

libnautilus_private_la_LDFLAGS =	\
	-no-undefined			\
	$(NULL)

libnautilus_private_la_LIBADD =		\
	$(SELINUX_LIBS)			\
	$(TRACKER_LIBS)			\
	$(top_builddir)/eel/libeel-2.la \
	$(top_builddir)/libnautilus-extension/libnautilus-extension.la \
	$(BASE_LIBS)			\
	$(COMMON_LIBS)			\
	$(EOS_FILE_MANAGER_LIBS)		\
	-lm				\
	$(NULL)

libnautilus_private_la_SOURCES = \
	nautilus-bookmark.c \
	nautilus-bookmark.h \
	nautilus-canvas-container.c \
	nautilus-canvas-container.h \
	nautilus-canvas-dnd.c \
	nautilus-canvas-dnd.h \
	nautilus-canvas-item.c \
	nautilus-canvas-item.h \
	nautilus-canvas-private.h \
	nautilus-clipboard-monitor.c \
	nautilus-clipboard-monitor.h \
	nautilus-clipboard.c \
	nautilus-clipboard.h \
	nautilus-column-chooser.c \
	nautilus-column-chooser.h \
	nautilus-column-utilities.c \
	nautilus-column-utilities.h \
	nautilus-dbus-manager.c \
	nautilus-dbus-manager.h \
	nautilus-debug.c \
	nautilus-debug.h \
	nautilus-default-file-icon.c \
	nautilus-default-file-icon.h \
	nautilus-desktop-directory-file.c \
	nautilus-desktop-directory-file.h \
	nautilus-desktop-directory.c \
	nautilus-desktop-directory.h \
	nautilus-desktop-icon-file.c \
	nautilus-desktop-icon-file.h \
	nautilus-desktop-link-monitor.c \
	nautilus-desktop-link-monitor.h \
	nautilus-desktop-link.c \
	nautilus-desktop-link.h \
	nautilus-desktop-metadata.c \
	nautilus-desktop-metadata.h \
	nautilus-directory-async.c \
	nautilus-directory-notify.h \
	nautilus-directory-private.h \
	nautilus-directory.c \
	nautilus-directory.h \
	nautilus-dnd.c \
	nautilus-dnd.h \
	nautilus-entry.c \
	nautilus-entry.h \
	nautilus-file-attributes.h \
	nautilus-file-changes-queue.c \
	nautilus-file-changes-queue.h \
	nautilus-file-conflict-dialog.c \
	nautilus-file-conflict-dialog.h \
	nautilus-file-dnd.c \
	nautilus-file-dnd.h \
	nautilus-file-operations.c \
	nautilus-file-operations.h \
	nautilus-file-private.h \
	nautilus-file-queue.c \
	nautilus-file-queue.h \
	nautilus-file-utilities.c \
	nautilus-file-utilities.h \
	nautilus-file.c \
	nautilus-file.h \
	nautilus-generated.c \
	nautilus-generated.h \
	nautilus-global-preferences.c \
	nautilus-global-preferences.h \
	nautilus-icon-info.c \
	nautilus-icon-info.h \
	nautilus-icon-names.h \
	nautilus-lib-self-check-functions.c \
	nautilus-lib-self-check-functions.h \
	nautilus-link.c \
	nautilus-link.h \
	nautilus-metadata.h \
	nautilus-metadata.c \
	nautilus-mime-application-chooser.c \
	nautilus-mime-application-chooser.h \
	nautilus-module.c \
	nautilus-module.h \
	nautilus-monitor.c \
	nautilus-monitor.h \
	nautilus-profile.c \
	nautilus-profile.h \
	nautilus-progress-info.c \
	nautilus-progress-info.h \
	nautilus-progress-info-manager.c \
	nautilus-progress-info-manager.h \
	nautilus-program-choosing.c \
	nautilus-program-choosing.h \
	nautilus-recent.c \
	nautilus-recent.h \
	nautilus-search-directory.c \
	nautilus-search-directory.h \
	nautilus-search-directory-file.c \
	nautilus-search-directory-file.h \
	nautilus-search-provider.c \
	nautilus-search-provider.h \
	nautilus-search-engine.c \
	nautilus-search-engine.h \
	nautilus-search-engine-model.c \
	nautilus-search-engine-model.h \
	nautilus-search-engine-simple.c \
	nautilus-search-engine-simple.h \
	nautilus-search-hit.c \
	nautilus-search-hit.h \
	nautilus-selection-canvas-item.c \
	nautilus-selection-canvas-item.h \
	nautilus-signaller.h \
	nautilus-signaller.c \
	nautilus-query.c \
	nautilus-query.h \
	nautilus-thumbnails.c \
	nautilus-thumbnails.h \
	nautilus-trash-monitor.c \
	nautilus-trash-monitor.h \
	nautilus-tree-view-drag-dest.c \
	nautilus-tree-view-drag-dest.h \
	nautilus-ui-utilities.c \
	nautilus-ui-utilities.h \
	nautilus-vfs-directory.c \
	nautilus-vfs-directory.h \
	nautilus-vfs-file.c \
	nautilus-vfs-file.h \
	nautilus-file-undo-operations.c \
	nautilus-file-undo-operations.h \
	nautilus-file-undo-manager.c \
	nautilus-file-undo-manager.h \
	$(nautilus_tracker_engine_sources) \
	$(NULL)

nodist_libnautilus_private_la_SOURCES =\
	$(BUILT_SOURCES) \
	$(NULL)

gsettingsschema_in_files = org.gnome.nautilus.gschema.xml.in
gsettings_SCHEMAS = $(gsettingsschema_in_files:.xml.in=.xml)
.PRECIOUS: $(gsettings_SCHEMAS)

convertdir = $(datadir)/GConf/gsettings
convert_DATA = nautilus.convert

@INTLTOOL_XML_NOMERGE_RULE@

@GSETTINGS_RULES@

EXTRA_DIST =				\
	nautilus.convert		\
	$(gsettingsschema_in_files)     \
	$(TRACKER_SOURCES)		\
	$(NULL)

CLEANFILES = \
	$(BUILT_SOURCES)				\
	$(gsettings_SCHEMAS)				\
	$(NULL)

dist-hook:
	cd $(distdir); rm -f $(CLEANFILES)

-include $(top_srcdir)/git.mk
