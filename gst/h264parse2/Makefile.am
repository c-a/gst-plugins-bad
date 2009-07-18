plugin_LTLIBRARIES = libgsth264parse2.la

libgsth264parse2_la_SOURCES = \
	gsth264parse2.c \
	gstbasevideoparse.c

noinst_HEADERS = \
	gsth264parse2.h \
	gstbasevideoparse.h

libgsth264parse2_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgsth264parse2_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgsth264parse2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsth264parse2_la_LIBTOOLFLAGS = --tag=disable-static