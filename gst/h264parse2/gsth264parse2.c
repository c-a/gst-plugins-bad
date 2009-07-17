/* GStreamer
*
* Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>.
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Library General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Library General Public License for more details.
*
* You should have received a copy of the GNU Library General Public
* License along with this library; if not, write to the
* Free Software Foundation, Inc., 59 Temple Place - Suite 330,
* Boston, MA 02111-1307, USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/base/gstadapter.h>
#include <gst/base/gstbytereader.h>
#include <string.h>

#include "gsth264parse2.h"

GST_DEBUG_CATEGORY_STATIC (h264parse_debug);
#define GST_CAT_DEFAULT h264parse_debug

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, " "parsed = (boolean) true")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, " "parsed = (boolean) false")
    );

GST_BOILERPLATE (GstH264Parse2, gst_h264_parse2, GstBaseVideoParse,
    GST_TYPE_BASE_VIDEO_PARSE);

#define MVP2_HEADER_SIZE 4

static gboolean
gst_h264_parse2_convert (GstBaseVideoParse * parse, GstFormat src_format,
    gint64 src_value, GstFormat dest_format, gint64 * dest_value)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);

  if (h264parse->byterate == -1)
    return FALSE;

  if (src_format == GST_FORMAT_BYTES && dest_format == GST_FORMAT_TIME) {
    *dest_value = gst_util_uint64_scale (GST_SECOND, src_value,
        h264parse->byterate);
    return TRUE;
  } else if (src_format == GST_FORMAT_TIME && dest_format == GST_FORMAT_BYTES) {
    *dest_value = gst_util_uint64_scale (src_value, h264parse->byterate,
        GST_SECOND);
    return TRUE;
  }

  return FALSE;
}

static GstCaps *
gst_h264_parse2_get_base_caps (GstBaseVideoParse * parse)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);
  GstCaps *caps;

  caps = gst_caps_new_simple ("video/x-h264",
      "parsed", G_TYPE_BOOLEAN, TRUE,
      "codec_data", GST_TYPE_BUFFER, h264parse->seq_header_buffer, NULL);

  return caps;
}

static gint
gst_h264_parse2_scan_for_sync (GstAdapter * adapter, gboolean at_eos,
    gint offset, gint n)
{
  gint m;

  if (n < 4) {
    if (at_eos)
      return n;
    else
      return 0;
  }
  m = gst_adapter_masked_scan_uint32 (adapter, 0xffffff00, 0x00000100,
      offset, n);
  if (m == -1)
    return n - (MVP2_HEADER_SIZE - 1);

  return m;
}

static GstFlowReturn
gst_h264_parse2_scan_for_packet_end (GstBaseVideoParse * parse,
    GstAdapter * adapter, gint * size)
{
  return GST_FLOW_OK;
}

static void
gst_h264_parse2_set_state (GstH264Parse2 * h264parse)
{
  GstBaseVideoParse *parse = GST_BASE_VIDEO_PARSE (h264parse);
  GstVideoState state;

  state = gst_base_video_parse_get_state (parse);

  state.width = h264parse->width;
  state.height = h264parse->height;

  state.fps_n = h264parse->fps_n;
  state.fps_d = h264parse->fps_d;

  state.par_n = h264parse->par_n;
  state.par_d = h264parse->par_d;

  gst_base_video_parse_set_state (parse, state);
}

GstFlowReturn
gst_h264_parse2_finish_frame (GstH264Parse2 * h264parse)
{
  GstBaseVideoParse *parse = (GST_BASE_VIDEO_PARSE (h264parse));

  if (h264parse->prev_packet != -1)
    return gst_base_video_parse_frame_finish (parse);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_parse2_parse_data (GstBaseVideoParse * parse, GstBuffer * buffer)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_parse2_shape_output (GstBaseVideoParse * parse,
    GstBaseVideoParseFrame * frame)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);
  GstBuffer *buf;

  buf = gst_buffer_list_get (frame->buffer_list, 0, 0);

  if (frame->is_eos && GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    h264parse->final_duration = GST_BUFFER_TIMESTAMP (buf);
    gst_base_video_parse_set_duration (parse, GST_FORMAT_TIME,
        h264parse->final_duration);
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf) &&
      frame->byte_offset != GST_BUFFER_OFFSET_NONE &&
      frame->byte_offset > h264parse->byte_offset) {
    h264parse->byte_offset = frame->byte_offset;
    h264parse->byterate =
        gst_util_uint64_scale (GST_SECOND, h264parse->byte_offset,
        GST_BUFFER_TIMESTAMP (buf));

    if (!GST_CLOCK_TIME_IS_VALID (h264parse->final_duration)) {
      GstFormat format;
      gint64 byte_duration;

      /* update duration */
      format = GST_FORMAT_BYTES;
      if (gst_pad_query_duration (GST_BASE_VIDEO_PARSE_SRC_PAD (parse), &format,
              &byte_duration) && format == GST_FORMAT_BYTES) {
        gint64 duration;

        duration = gst_util_uint64_scale (GST_SECOND, byte_duration,
            h264parse->byterate);
        gst_base_video_parse_set_duration (parse, GST_FORMAT_TIME, duration);
      }
    }
  }

  return gst_base_video_parse_push (parse, frame->buffer_list);
}

static gboolean
gst_h264_parse2_start (GstBaseVideoParse * parse)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);

  h264parse->seq_header_buffer = NULL;

  h264parse->byterate = -1;
  h264parse->byte_offset = 0;

  h264parse->final_duration = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static gboolean
gst_h264_parse2_stop (GstBaseVideoParse * parse)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);

  if (h264parse->seq_header_buffer)
    gst_buffer_unref (h264parse->seq_header_buffer);

  return TRUE;
}

static void
gst_h264_parse2_flush (GstBaseVideoParse * parse)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);
}

static void
gst_h264_parse2_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details_simple (element_class, "H264 parser",
      "Codec/Parser/Video",
      "Parses H264", "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  GST_DEBUG_CATEGORY_INIT (h264parse_debug, "h264parse2", 0,
      "H264 parser element");
}

static void
gst_h264_parse2_init (GstH264Parse2 * h264parse, GstH264Parse2Class * klass)
{
}

static void
gst_h264_parse2_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_h264_parse2_class_init (GstH264Parse2Class * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseVideoParseClass *baseparse_class = GST_BASE_VIDEO_PARSE_CLASS (klass);

  gobject_class->finalize = gst_h264_parse2_finalize;

  baseparse_class->start = GST_DEBUG_FUNCPTR (gst_h264_parse2_start);
  baseparse_class->stop = GST_DEBUG_FUNCPTR (gst_h264_parse2_stop);
  baseparse_class->flush = GST_DEBUG_FUNCPTR (gst_h264_parse2_flush);

  baseparse_class->scan_for_sync =
      GST_DEBUG_FUNCPTR (gst_h264_parse2_scan_for_sync);
  baseparse_class->scan_for_packet_end =
      GST_DEBUG_FUNCPTR (gst_h264_parse2_scan_for_packet_end);

  baseparse_class->parse_data = GST_DEBUG_FUNCPTR (gst_h264_parse2_parse_data);
  baseparse_class->shape_output =
      GST_DEBUG_FUNCPTR (gst_h264_parse2_shape_output);

  baseparse_class->get_base_caps =
      GST_DEBUG_FUNCPTR (gst_h264_parse2_get_base_caps);

  baseparse_class->convert = GST_DEBUG_FUNCPTR (gst_h264_parse2_convert);
}

/**
* plugin_init:
* @plugin: GstPlugin
*
* Returns: TRUE on success.
*/
static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "h264parse2",
      GST_RANK_PRIMARY, GST_TYPE_H264_PARSE2);
}


GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "h264parse2",
    "H264 parser",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
