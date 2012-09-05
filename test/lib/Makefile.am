ACLOCAL_AMFLAGS = -I ../../m4 -Wnone

if HAVE_CHECK
TESTS = check_liboml2 check_libocomm check_server
check_PROGRAMS = check_liboml2 check_libocomm check_server

check_liboml2_SOURCES = \
	check_util.c \
	check_util.h \
	check_liboml2.c \
	check_liboml2_util.c \
	check_liboml2_api.c \
	check_liboml2_filters.c \
	check_liboml2_parser.c \
	check_liboml2_bswap.c \
	check_liboml2_marshal.c \
	check_liboml2_mbuf.c \
	check_liboml2_cbuf.c \
	check_liboml2_writers.c \
	check_liboml2_suites.h \
	$(top_srcdir)/lib/client/oml2/omlc.h \
	$(top_srcdir)/lib/client/oml2/oml_filter.h

check_libocomm_SOURCES = \
			 check_libocomm.c \
			 check_libocomm_mt_queue.c \
			 check_libocomm_queue.c \
			 check_libocomm_suites.h \
			 $(top_srcdir)/lib/ocomm/ocomm/mt_queue.h \
			 $(top_srcdir)/lib/ocomm/ocomm/o_eventloop.h \
			 $(top_srcdir)/lib/ocomm/ocomm/o_log.h \
			 $(top_srcdir)/lib/ocomm/ocomm/o_socket_group.h \
			 $(top_srcdir)/lib/ocomm/ocomm/o_socket.h \
			 $(top_srcdir)/lib/ocomm/ocomm/queue.h

check_server_SOURCES = \
	check_util.c \
	check_util.h \
	check_server.c \
	check_server_suites.h \
	check_server_headers.c

check_liboml2_CFLAGS = $(CHECK_CFLAGS)
check_libocomm_CFLAGS = $(CHECK_CFLAGS)
check_server_CFLAGS = $(CHECK_CFLAGS)

check_liboml2_LDADD = $(CHECK_LIBS) $(XML2_LIBS) \
	$(top_builddir)/lib/client/liboml2.la \
	$(top_builddir)/lib/ocomm/libocomm.la

check_libocomm_LDADD = $(CHECK_LIBS) \
	$(top_builddir)/lib/ocomm/libocomm.la

check_server_LDADD = $(CHECK_LIBS) \
	$(top_builddir)/lib/shared/libshared.la \
	$(top_builddir)/lib/ocomm/libocomm.la

endif

AM_CPPFLAGS = \
	-I  $(top_srcdir)/lib/client \
	-I  $(top_srcdir)/lib/ocomm \
	-I  $(top_srcdir)/lib/shared \
	-I $(top_srcdir)/server

BUILT_SOURCES = \
	stddev_0.c \
	stddev_1.c

CLEANFILES = \
	stddev_0.c \
	stddev_1.c \
	check_oml2.log \
	check_ocomm.log \
	test_fw_create_buffered

STDDEV = $(srcdir)/stddev.py

dist_check_SCRIPTS=$(STDDEV)

stddev_0.c stddev_1.c:	Makefile
	$(STDDEV)
