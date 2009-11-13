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

GST_DEBUG_CATEGORY_STATIC (h264parse2_debug);
#define GST_CAT_DEFAULT h264parse2_debug

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
    GST_DEBUG_CATEGORY_INIT (h264parse2_debug, "h264parse2", 0, \
    "H264 parser");

GST_BOILERPLATE_FULL (GstH264Parse2, gst_h264_parse2, SatBaseVideoParse,
    GST_TYPE_BASE_VIDEO_PARSE, _do_init);

#define SYNC_CODE_SIZE 3

#define GST_H264_GOT_PRIMARY SAT_VIDEO_FRAME_FLAG_USER1

#define READ_UINT8(reader, val, nbits) { \
  if (!gst_bit_reader_get_bits_uint8 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint8, nbits: %d", nbits); \
    return FALSE; \
  } \
}

#define READ_UINT16(reader, val, nbits) { \
  if (!gst_bit_reader_get_bits_uint16 (reader, &val, nbits)) { \
  GST_WARNING ("failed to read uint16, nbits: %d", nbits); \
    return FALSE; \
  } \
}

#define SKIP(reader, nbits) { \
  if (!gst_bit_reader_skip (reader, nbits)) { \
  GST_WARNING ("failed to skip nbits: %d", nbits); \
    return FALSE; \
  } \
}

static gboolean
gst_h264_parse2_set_sink_caps (SatBaseVideoParse * parse, GstCaps * caps)
{
  GstH264Parse2 *h264parse;
  GstStructure *structure;
  const GValue *value;

  h264parse = GST_H264_PARSE2 (parse);

  structure = gst_caps_get_structure (caps, 0);
  /* packetized video has a codec_data */
  if ((value = gst_structure_get_value (structure, "codec_data"))) {
    GstBuffer *buf;
    GstBitReader reader;
    guint8 version;
    guint8 n_sps, n_pps;
    gint i;

    GST_DEBUG_OBJECT (h264parse, "have packetized h264");
    h264parse->packetized = TRUE;

    buf = gst_value_get_buffer (value);
    GST_MEMDUMP ("avcC:", GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

    /* parse the avcC data */
    if (GST_BUFFER_SIZE (buf) < 7) {
      GST_ERROR_OBJECT (h264parse, "avcC size %u < 7", GST_BUFFER_SIZE (buf));
      return FALSE;
    }

    gst_bit_reader_init_from_buffer (&reader, buf);

    READ_UINT8 (&reader, version, 8);
    if (version != 1)
      return FALSE;

    SKIP (&reader, 30);

    READ_UINT8 (&reader, h264parse->nal_length_size, 2);
    h264parse->nal_length_size += 1;
    GST_DEBUG_OBJECT (h264parse, "nal length %u", h264parse->nal_length_size);

    SKIP (&reader, 3);

    READ_UINT8 (&reader, n_sps, 5);
    for (i = 0; i < n_sps; i++) {
      guint16 sps_length;
      guint8 *data;

      READ_UINT16 (&reader, sps_length, 16);
      sps_length -= 1;
      SKIP (&reader, 8);

      data = GST_BUFFER_DATA (buf) + gst_bit_reader_get_pos (&reader) / 8;
      if (!gst_h264_parser_parse_sequence (h264parse->parser, data, sps_length))
        return FALSE;

      SKIP (&reader, sps_length * 8);
    }

    READ_UINT8 (&reader, n_pps, 8);
    for (i = 0; i < n_pps; i++) {
      guint16 pps_length;
      guint8 *data;

      READ_UINT16 (&reader, pps_length, 16);
      pps_length -= 1;
      SKIP (&reader, 8);

      data = GST_BUFFER_DATA (buf) + gst_bit_reader_get_pos (&reader) / 8;
      if (!gst_h264_parser_parse_picture (h264parse->parser, data, pps_length))
        return FALSE;

      SKIP (&reader, pps_length * 8);
    }

    h264parse->codec_data = buf;
  }

  return TRUE;
}

static GstCaps *
gst_h264_parse2_get_base_caps (SatBaseVideoParse * parse)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);
  GstCaps *caps;

  caps = gst_caps_new_simple ("video/x-h264", NULL);

  if (h264parse->codec_data) {
    GstStructure *structure;

    structure = gst_caps_get_structure (caps, 0);
    gst_structure_set (structure,
        "codec_data", GST_TYPE_BUFFER, h264parse->codec_data, NULL);
  }

  return caps;
}

static gboolean
gst_h264_parse2_convert (SatBaseVideoParse * parse, GstFormat src_format,
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
gst_h264_parse2_scan_for_sync (SatBaseVideoParse * parse, GstAdapter * adapter)
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

static SatBaseVideoParseScanResult
gst_h264_parse2_scan_for_packet_end (SatBaseVideoParse * parse,
    GstAdapter * adapter, guint * size)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);
  guint avail;

  avail = gst_adapter_available (adapter);
  if (avail < h264parse->nal_length_size)
    return SAT_BASE_VIDEO_PARSE_SCAN_RESULT_NEED_DATA;

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
      GST_DEBUG ("fixing invalid NALU size to %u", nal_length);
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

    GST_DEBUG ("start_code: %d", start_code);
    if (start_code == 0x000001)
      return SAT_BASE_VIDEO_PARSE_SCAN_RESULT_LOST_SYNC;

    n = gst_adapter_masked_scan_uint32 (adapter, 0xffffff00, 0x00000100,
        SYNC_CODE_SIZE, avail - SYNC_CODE_SIZE);
    if (n == -1)
      return SAT_BASE_VIDEO_PARSE_SCAN_RESULT_NEED_DATA;

    *size = n;
  }

  GST_DEBUG ("NAL size: %d", *size);

  return SAT_BASE_VIDEO_PARSE_SCAN_RESULT_OK;
}

static GstFlowReturn
gst_h264_parse2_parse_data (SatBaseVideoParse * parse, GstBuffer * buffer)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);
  GstBitReader reader;
  GstNalUnit nal_unit;
  guint8 forbidden_zero_bit;

  guint8 *data;
  guint size;
  gint i;

  SatVideoFrame *frame;

  GST_MEMDUMP ("data", GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));

  gst_bit_reader_init_from_buffer (&reader, buffer);

  /* skip nal_length or sync code */
  gst_bit_reader_skip (&reader, h264parse->nal_length_size * 8);

  if (!gst_bit_reader_get_bits_uint8 (&reader, &forbidden_zero_bit, 1))
    goto invalid_packet;
  if (forbidden_zero_bit != 0) {
    GST_WARNING ("forbidden_zero_bit != 0");
    return GST_FLOW_ERROR;
  }

  if (!gst_bit_reader_get_bits_uint16 (&reader, &nal_unit.ref_idc, 2))
    goto invalid_packet;
  GST_DEBUG ("nal_ref_idc: %u", nal_unit.ref_idc);

  /* read nal_unit_type */
  if (!gst_bit_reader_get_bits_uint16 (&reader, &nal_unit.type, 5))
    goto invalid_packet;

  GST_DEBUG ("nal_unit_type: %u", nal_unit.type);
  if (nal_unit.type == 14 || nal_unit.type == 20) {
    if (!gst_bit_reader_skip (&reader, 24))
      goto invalid_packet;
  }

  data = GST_BUFFER_DATA (buffer) + gst_bit_reader_get_pos (&reader) / 8;
  size = gst_bit_reader_get_remaining (&reader) / 8;

  i = size - 1;
  while (size >= 0 && data[i] == 0x00) {
    size--;
    i--;
  }

  SAT_BASE_VIDEO_PARSE_FRAME_LOCK (parse);
  frame = sat_base_video_parse_get_current_frame (parse);

  /* does this mark the beginning of a new access unit */
  if (nal_unit.type == GST_NAL_AU_DELIMITER)
    sat_base_video_parse_finish_frame (parse, &frame);

  if (sat_video_frame_flag_is_set (frame, GST_H264_GOT_PRIMARY)) {
    if (nal_unit.type == GST_NAL_SPS || nal_unit.type == GST_NAL_PPS ||
        nal_unit.type == GST_NAL_SEI ||
        (nal_unit.type >= 14 && nal_unit.type <= 18))
      sat_base_video_parse_finish_frame (parse, &frame);
  }

  if (nal_unit.type >= GST_NAL_SLICE && nal_unit.type <= GST_NAL_SLICE_IDR) {
    GstH264Slice slice;

    if (!gst_h264_parser_parse_slice_header (h264parse->parser, &slice, data,
            size, nal_unit))
      goto invalid_packet;

    if (slice.redundant_pic_cnt == 0) {
      if (sat_video_frame_flag_is_set (frame, GST_H264_GOT_PRIMARY)) {
        GstH264Slice *p_slice;
        guint8 pic_order_cnt_type, p_pic_order_cnt_type;

        p_slice = &h264parse->slice;
        pic_order_cnt_type = slice.picture->sequence->pic_order_cnt_type;
        p_pic_order_cnt_type = p_slice->picture->sequence->pic_order_cnt_type;

        if (slice.frame_num != p_slice->frame_num)
          sat_base_video_parse_finish_frame (parse, &frame);

        else if (slice.picture != p_slice->picture)
          sat_base_video_parse_finish_frame (parse, &frame);

        else if (slice.bottom_field_flag != p_slice->bottom_field_flag)
          sat_base_video_parse_finish_frame (parse, &frame);

        else if (nal_unit.ref_idc != p_slice->nal_unit.ref_idc &&
            (nal_unit.ref_idc == 0 || p_slice->nal_unit.ref_idc == 0))
          sat_base_video_parse_finish_frame (parse, &frame);

        else if ((pic_order_cnt_type == 0 && p_pic_order_cnt_type == 0) &&
            (slice.pic_order_cnt_lsb != p_slice->pic_order_cnt_lsb ||
                slice.delta_pic_order_cnt_bottom !=
                p_slice->delta_pic_order_cnt_bottom))
          sat_base_video_parse_finish_frame (parse, &frame);

        else if ((p_pic_order_cnt_type == 1 && p_pic_order_cnt_type == 1) &&
            (slice.delta_pic_order_cnt[0] != p_slice->delta_pic_order_cnt[0] ||
                slice.delta_pic_order_cnt[1] !=
                p_slice->delta_pic_order_cnt[1]))
          sat_base_video_parse_finish_frame (parse, &frame);
      }

      if (!sat_video_frame_flag_is_set (frame, GST_H264_GOT_PRIMARY)) {
        if (GST_H264_IS_I_SLICE (slice.type)
            || GST_H264_IS_SI_SLICE (slice.type))
          sat_video_frame_set_flag (frame, SAT_VIDEO_FRAME_FLAG_KEYFRAME);

        h264parse->slice = slice;
        sat_video_frame_set_flag (frame, GST_H264_GOT_PRIMARY);
      }
    }
  }

  if (nal_unit.type == GST_NAL_SPS) {
    if (!gst_h264_parser_parse_sequence (h264parse->parser, data, size))
      goto invalid_packet;
  }

  if (nal_unit.type == GST_NAL_PPS) {
    if (!gst_h264_parser_parse_picture (h264parse->parser, data, size))
      goto invalid_packet;
  }

  if (nal_unit.type == GST_NAL_SEI) {
    GstH264Sequence *seq;
    GstH264SEIMessage sei;

    if (sat_video_frame_flag_is_set (frame, GST_H264_GOT_PRIMARY))
      seq = h264parse->slice.picture->sequence;
    else
      seq = NULL;

    if (!gst_h264_parser_parse_sei_message (h264parse->parser, seq, &sei, data,
            size))
      goto invalid_packet;
  }

  sat_video_frame_add_buffer (frame, buffer);
  SAT_BASE_VIDEO_PARSE_FRAME_UNLOCK (parse);

  return GST_FLOW_OK;

invalid_packet:
  SAT_BASE_VIDEO_PARSE_FRAME_UNLOCK (parse);
  GST_WARNING ("Invalid packet size!");
  gst_buffer_unref (buffer);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h264_parse2_shape_output (SatBaseVideoParse * parse, SatVideoFrame * frame)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);
  GstClockTime timestamp;
  guint64 byte_offset;

  timestamp = sat_video_frame_get_timestamp (frame);

  if (sat_video_frame_flag_is_set (frame, SAT_VIDEO_FRAME_FLAG_EOS),
      GST_CLOCK_TIME_IS_VALID (timestamp)) {
    h264parse->final_duration = timestamp;
    sat_base_video_parse_set_duration (parse, GST_FORMAT_TIME,
        h264parse->final_duration);
  }

  byte_offset = sat_video_frame_get_upstream_offset (frame);

  if (GST_CLOCK_TIME_IS_VALID (timestamp) &&
      byte_offset != GST_BUFFER_OFFSET_NONE &&
      byte_offset > h264parse->byte_offset) {
    h264parse->byte_offset = byte_offset;
    h264parse->byterate =
        gst_util_uint64_scale (GST_SECOND, h264parse->byte_offset, timestamp);

    if (!GST_CLOCK_TIME_IS_VALID (h264parse->final_duration)) {
      GstFormat format;
      gint64 byte_duration;

      /* update duration */
      format = GST_FORMAT_BYTES;
      if (gst_pad_query_duration (SAT_BASE_VIDEO_PARSE_SRC_PAD (parse), &format,
              &byte_duration) && format == GST_FORMAT_BYTES) {
        gint64 duration;

        duration = gst_util_uint64_scale (GST_SECOND, byte_duration,
            h264parse->byterate);
        sat_base_video_parse_set_duration (parse, GST_FORMAT_TIME, duration);
      }
    }
  }

  return sat_base_video_parse_push (parse, frame);
}

static gboolean
gst_h264_parse2_start (SatBaseVideoParse * parse)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);

  h264parse->codec_data = NULL;
  h264parse->packetized = FALSE;
  h264parse->nal_length_size = SYNC_CODE_SIZE;
  h264parse->parser = g_object_new (GST_TYPE_H264_PARSER, NULL);

  h264parse->byterate = -1;
  h264parse->byte_offset = 0;

  h264parse->final_duration = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static gboolean
gst_h264_parse2_stop (SatBaseVideoParse * parse)
{
  GstH264Parse2 *h264parse = GST_H264_PARSE2 (parse);

  g_object_unref (h264parse->parser);

  return TRUE;
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
  SatBaseVideoParseClass *baseparse_class = SAT_BASE_VIDEO_PARSE_CLASS (klass);

  gobject_class->finalize = gst_h264_parse2_finalize;

  baseparse_class->start = GST_DEBUG_FUNCPTR (gst_h264_parse2_start);
  baseparse_class->stop = GST_DEBUG_FUNCPTR (gst_h264_parse2_stop);

  baseparse_class->scan_for_sync =
      GST_DEBUG_FUNCPTR (gst_h264_parse2_scan_for_sync);
  baseparse_class->scan_for_packet_end =
      GST_DEBUG_FUNCPTR (gst_h264_parse2_scan_for_packet_end);

  baseparse_class->parse_data = GST_DEBUG_FUNCPTR (gst_h264_parse2_parse_data);
  baseparse_class->shape_output =
      GST_DEBUG_FUNCPTR (gst_h264_parse2_shape_output);

  baseparse_class->set_sink_caps =
      GST_DEBUG_FUNCPTR (gst_h264_parse2_set_sink_caps);
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
