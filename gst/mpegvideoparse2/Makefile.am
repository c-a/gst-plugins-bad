plugin_LTLIBRARIES = libgstmpegvideoparse2.la

libgstmpegvideoparse2_la_SOURCES = gstbasevideoparse.c gstmpegvideoparse2.c mpegutil.c
libgstmpegvideoparse2_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegvideoparse2_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS) -lgstvideo-$(GST_MAJORMINOR)
libgstmpegvideoparse2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegvideoparse2_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstbasevideoparse.h gstmpegvideoparse2.h mpegutil.h
