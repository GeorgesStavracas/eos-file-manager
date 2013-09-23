NULL=

INCLUDES =\
	-I$(top_srcdir) \
	$(BASE_CFLAGS) \
	$(COMMON_CFLAGS) \
	$(EOS_FILE_MANAGER_CFLAGS) \
	$(WARNING_CFLAGS) \
	-DVERSION="\"$(VERSION)\"" \
	-DEOS_FILE_MANAGER_DATADIR=\""$(datadir)/eos-file-manager"\" \
	-DGNOMELOCALEDIR=\""$(prefix)/${DATADIRNAME}/locale"\"	\
	$(NULL)

LDADD =\
	$(top_builddir)/libnautilus-private/libnautilus-private.la \
	$(BASE_LIBS) \
	$(COMMON_LIBS) \
	$(EOS_FILE_MANAGER_LIBS) \
	$(NULL)

noinst_PROGRAMS =\
	test-nautilus-search-engine \
	test-nautilus-directory-async \
	test-nautilus-copy \
	test-eel-editable-label	\
	$(NULL)

test_nautilus_copy_SOURCES = test-copy.c test.c

test_nautilus_search_engine_SOURCES = test-nautilus-search-engine.c 

test_nautilus_directory_async_SOURCES = test-nautilus-directory-async.c

EXTRA_DIST = \
	test.h \
	$(NULL)

-include $(top_srcdir)/git.mk
