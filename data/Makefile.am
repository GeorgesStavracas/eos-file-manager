NULL=

xml_in_files = eos-file-manager.xml.in
xml_files = $(xml_in_files:.xml.in=.xml)
@INTLTOOL_XML_RULE@

desktopdir       = $(datadir)/applications
desktop_DATA     = 					\
	$(desktop_in_files:.desktop.in=.desktop)	\
	eos-file-manager-classic.desktop
desktop_in_files = 					\
	eos-file-manager.desktop.in				\
	eos-file-manager-autorun-software.desktop.in		\
	eos-file-manager-connect-server.desktop.in

autostartdir     = $(sysconfdir)/xdg/autostart
autostart_in_files = eos-file-manager-autostart.desktop.in
autostart_DATA = $(autostart_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

mimedir = $(datadir)/mime/packages
mime_DATA = $(xml_files)

servicedir = $(datadir)/dbus-1/services
service_DATA = $(service_in_files:.service.in=.service)
service_in_files = 				\
	org.freedesktop.FileManager1.service.in	\
	org.gnome.Nautilus.service.in		\
	org.gnome.Nautilus.SearchProvider.service.in

org.gnome.Nautilus.service: org.gnome.Nautilus.service.in Makefile
	$(AM_V_GEN) sed -e "s|\@bindir\@|$(bindir)|" $< > $@

org.freedesktop.FileManager1.service: org.freedesktop.FileManager1.service.in Makefile
	$(AM_V_GEN) sed -e "s|\@bindir\@|$(bindir)|" $< > $@

org.gnome.Nautilus.SearchProvider.service: org.gnome.Nautilus.SearchProvider.service.in Makefile
	$(AM_V_GEN) sed -e "s|\@bindir\@|$(bindir)|" $< > $@

searchproviderdir = $(datadir)/gnome-shell/search-providers
searchprovider_DATA = eos-file-manager-search-provider.ini

eosfilemanagerdatadir = $(datadir)/eos-file-manager

eosfilemanagerdata_DATA = \
	eos-file-manager-extras.placeholder 	\
	eos-file-manager-suggested.placeholder 	\
	$(NULL)

EXTRA_DIST = $(eosfilemanagerdata_DATA) \
	eos-file-manager.desktop	\
	eos-file-manager.desktop.in	\
	dbus-interfaces.xml		\
	freedesktop-dbus-interfaces.xml	\
	shell-search-provider-dbus-interfaces.xml \
	$(xml_in_files) 		\
	$(desktop_in_files)		\
	$(service_in_files)		\
	$(autostart_in_files)		\
	$(searchprovider_DATA)		\
	$(NULL)

CLEANFILES = $(xml_files)	\
	$(desktop_DATA)		\
	$(service_DATA)		\
	$(autostart_DATA)	\
	$(NULL)

install-data-hook:
if ENABLE_UPDATE_MIMEDB
	$(UPDATE_MIME_DATABASE) "$(DESTDIR)$(datadir)/mime"
endif

uninstall-hook:
if ENABLE_UPDATE_MIMEDB
	$(UPDATE_MIME_DATABASE) "$(DESTDIR)$(datadir)/mime"
endif

-include $(top_srcdir)/git.mk
