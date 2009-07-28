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
#include <gst/base/gstbitreader.h>
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

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (h264parse_debug, "h264parse", 0, \
    "H264 parser");

GST_BOILERPLATE_FULL (GstH264Parse2, gst_h264_parse2, GstBaseVideoParse,
    GST_TYPE_BASE_VIDEO_PARSE, _do_init);

#define SYNC_CODE_SIZE 3

static gboolean
gst_h264_parse2_set_sink_caps (GstBaseVideoParse * parse, GstCaps * caps)
{
  GstH264Parse2 *h264parse;
  GstStructure *structure;
  const GValue *value;

  h264parse = GST_H264_PARSE2 (parse);

  structure = gst_caps_get_structure (caps, 0);
  /* packetized video has a codec_data */
  if ((value = gst_structure_get_value (structure, "codec_data"))) {
    GstBuffer *buf;
    guint8 *data;
    guint size;

    GST_DEBUG_OBJECT (h264parse, "have packetized h264");
    h264parse->packetized = TRUE;

    buf = gst_value_get_buffer (value);
    data = GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf);

    /* parse the avcC data */
    if (size < 7) {
      GST_ERROR_OBJECT (h264parse, "avcC size %u < 7", size);
      return FALSE;
    }
    /* parse the version, this must be 1 */
    if (data[0] != 1) {
      GST_ERROR_OBJECT (h264parse, "wrong avcC version");
      return FALSE;
    }

    /* 6 bits reserved | 2 bits lengthSizeMinusOne */
    /* this is the number of bytes in front of the NAL units to mark their
     * length */
    h264parse->nal_length_size = (data[4] & 0x03) + 1;
    GST_DEBUG_OBJECT (h264parse, "nal length %u", h264parse->nal_length_size);
  }

  return TRUE;
}

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

static gint
gst_h264_parse2_scan_for_sync (GstBaseVideoParse * parse, GstAdapter * adapter)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);
  gint m;

  if (h264parse->packetized)
    return 0;

  m = gst_adapter_masked_scan_uint32 (adapter, 0xffffff00, 0x00000100,
      0, gst_adapter_available (adapter));
  if (m == -1)
    return gst_adapter_available (adapter) - SYNC_CODE_SIZE;

  return m;
}

static GstBaseVideoParseScanResult
gst_h264_parse2_scan_for_packet_end (GstBaseVideoParse * parse,
    GstAdapter * adapter, guint * size)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);
  guint avail;

  avail = gst_adapter_available (adapter);
  if (avail < h264parse->nal_length_size)
    return GST_BASE_VIDEO_PARSE_SCAN_RESULT_NEED_DATA;

  if (h264parse->packetized) {
    guint8 *data;
    gint i;
    guint32 nal_length;

    data = g_slice_alloc (h264parse->nal_length_size);
    gst_adapter_copy (adapter, data, 0, h264parse->nal_length_size);
    for (i = 0; i < h264parse->nal_length_size; i++)
      nal_length = (nal_length << 8) | data[i];

    g_slice_free1 (h264parse->nal_length_size, data);

    nal_length += h264parse->nal_length_size;

    /* check for invalid NALU sizes, assume the size if the available bytes
     * when something is fishy */
    if (nal_length <= 1 || nal_length > avail) {
      nal_length = avail - h264parse->nal_length_size;
      GST_DEBUG_OBJECT (h264parse, "fixing invalid NALU size to %u",
          nal_length);
    }

    *size = nal_length;
  }

  else {
    guint8 *data;
    guint32 start_code;
    guint n;

    data = g_slice_alloc (SYNC_CODE_SIZE);
    gst_adapter_copy (adapter, data, 0, SYNC_CODE_SIZE);
    start_code = ((data[0] << 16) && (data[1] << 8) && data[2]);
    g_slice_free1 (SYNC_CODE_SIZE, data);

    if (start_code != 0x000001)
      return GST_BASE_VIDEO_PARSE_SCAN_RESULT_LOST_SYNC;

    n = gst_adapter_masked_scan_uint32 (adapter, 0xffffff00, 0x00000100,
        SYNC_CODE_SIZE, avail - SYNC_CODE_SIZE);
    if (n == -1)
      return GST_BASE_VIDEO_PARSE_SCAN_RESULT_NEED_DATA;

    *size = n;
  }

  return GST_BASE_VIDEO_PARSE_SCAN_RESULT_OK;
}

static GstFlowReturn
gst_h264_parse2_parse_data (GstBaseVideoParse * parse, GstBuffer * buffer)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);
  GstBitReader reader;
  guint16 forbidden_zero_bit;
  guint16 nal_ref_idc;
  guint16 nal_unit_type;

  gst_bit_reader_init_from_buffer (&reader, buffer);

  /* skip nal_length or sync code */
  gst_bit_reader_skip (&reader, h264parse->nal_length_size * 8);

  if (!gst_bit_reader_get_bits_uint16 (&reader, &forbidden_zero_bit, 1))
    goto invalid_packet;
  if (forbidden_zero_bit != 0) {
    GST_WARNING ("forbidden_zero_bit != 0");
    return GST_FLOW_ERROR;
  }

  if (!gst_bit_reader_get_bits_uint16 (&reader, &nal_ref_idc, 2))
    goto invalid_packet;
  GST_DEBUG ("nal_ref_idc: %u", nal_ref_idc);

  /* read nal_unit_type */
  if (!gst_bit_reader_get_bits_uint16 (&reader, &nal_unit_type, 5))
    goto invalid_packet;

  GST_DEBUG ("nal_unit_type: %u", nal_unit_type);

  return GST_FLOW_OK;

invalid_packet:
  GST_WARNING ("Invalid packet size!");

  return GST_FLOW_ERROR;
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

  h264parse->packetized = FALSE;
  h264parse->nal_length_size = SYNC_CODE_SIZE;

  h264parse->byterate = -1;
  h264parse->byte_offset = 0;

  h264parse->final_duration = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static gboolean
gst_h264_parse2_stop (GstBaseVideoParse * parse)
{
  return TRUE;
}

static void
gst_h264_parse2_flush (GstBaseVideoParse * parse)
{
#if 0
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);
#endif
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

  baseparse_class->set_sink_caps =
      GST_DEBUG_FUNCPTR (gst_h264_parse2_set_sink_caps);

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
