include $(top_srcdir)/common/check.mak

plugin_LTLIBRARIES = libgsth264parse2.la

libgsth264parse2_la_SOURCES = \
	gsth264parse2.c \
	satvideoframe.c \
	satbasevideoparse.c \
	gsth264parser.c

libgsth264parse2_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgsth264parse2_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
    libgstnalreader-@GST_MAJORMINOR@.la
libgsth264parse2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsth264parse2_la_LIBTOOLFLAGS = --tag=disable-static

lib_LTLIBRARIES = libgstnalreader-@GST_MAJORMINOR@.la

libgstnalreader_@GST_MAJORMINOR@_la_SOURCES = gstnalreader.c

libgstnalreader_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS)
libgstnalreader_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS)
libgstnalreader_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_ALL_LDFLAGS)
libgstnalreader_@GST_MAJORMINOR@_la_LIBTOOLFLAGS = --tag=disable-static

check_PROGRAMS = \
    nalreadertest

TESTS = $(check_PROGRAMS)

nalreadertest_CFLAGS = $(GST_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS)
nalreadertest_LDADD = $(GST_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS) \
    libgstnalreader-@GST_MAJORMINOR@.la

noinst_HEADERS = \
	gsth264parse2.h \
	satvideoframe.h \
	satbasevideoparse.h \
  gsth264parser.h
