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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
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

#include "mpegutil.h"

#include "gstmpegvideoparse2.h"

GST_DEBUG_CATEGORY_STATIC (mpegparse_debug);
#define GST_CAT_DEFAULT mpegparse_debug

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [ 1, 2 ], "
        "parsed = (boolean) true, "
        "systemstream = (boolean) false, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "pixel-aspect-ratio = (fraction) [ 0/1, MAX ], "
        "framerate = (fraction) [ 0/1, MAX ]")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [ 1, 2 ], "
        "parsed = (boolean) false, " "systemstream = (boolean) false")
    );

GST_BOILERPLATE (GstMpegVideoParse2, gst_mvp2, GstBaseVideoParse,
    GST_TYPE_BASE_VIDEO_PARSE);

#define MVP2_HEADER_SIZE 4

static GstCaps *
gst_mvp2_get_caps (GstBaseVideoParse * parse)
{
  GstMpegVideoParse2 *mpegparse = GST_MPEG_VIDEO_PARSE2 (parse);
  GstVideoState state;
  GstCaps *caps;

  state = gst_base_video_parse_get_state (parse);
  caps = gst_caps_new_simple ("video/mpeg",
      "systemstream", G_TYPE_BOOLEAN, FALSE,
      "parsed", G_TYPE_BOOLEAN, TRUE,
      "mpegversion", G_TYPE_INT, mpegparse->version,
      "width", G_TYPE_INT, state.width,
      "height", G_TYPE_INT, state.height,
      "framerate", GST_TYPE_FRACTION, state.fps_n, state.fps_d,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, state.par_n, state.par_d,
      "interlaced", G_TYPE_BOOLEAN, state.interlaced,
      "codec_data", GST_TYPE_BUFFER, mpegparse->seq_header_buffer, NULL);

  return caps;
}

static gint
gst_mvp2_scan_for_sync (GstAdapter * adapter, gboolean at_eos,
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
gst_mvp2_scan_for_packet_end (GstBaseVideoParse * parse, GstAdapter * adapter,
    gint * size)
{
  gint n, next;

  guint8 header[MVP2_HEADER_SIZE];

  if (gst_adapter_available (adapter) < 4)
    return GST_BASE_VIDEO_PARSE_FLOW_NEED_DATA;

  gst_adapter_copy (adapter, header, 0, MVP2_HEADER_SIZE);

  if (!(header[0] == 0 && header[1] == 0 && header[2] == 1)) {
    gst_base_video_parse_lost_sync (parse);
    return GST_BASE_VIDEO_PARSE_FLOW_NEED_DATA;
  }

  /* calculate packet size */
  n = gst_adapter_available (parse->input_adapter) - MVP2_HEADER_SIZE;
  next = gst_adapter_masked_scan_uint32 (adapter, 0xffffff00, 0x00000100,
      MVP2_HEADER_SIZE - 1, n);
  if (next == -1)
    return GST_BASE_VIDEO_PARSE_FLOW_NEED_DATA;

  *size = next;

  return GST_FLOW_OK;
}

static gboolean
gst_mvp2_handle_sequence_extension (GstMpegVideoParse2 * mpegparse,
    GstBuffer * buffer)
{
  GstBaseVideoParse *parse = GST_BASE_VIDEO_PARSE (mpegparse);
  MPEGSeqExtHdr hdr;
  GstVideoState state;
  GstBuffer *new_buffer;

  if (!mpeg_util_parse_sequence_extension (&hdr, buffer))
    return FALSE;

  state = gst_base_video_parse_get_state (parse);

  state.fps_n *= (hdr.fps_n_ext + 1);
  state.fps_d *= (hdr.fps_d_ext + 1);

  state.width += (hdr.horiz_size_ext << 12);
  state.height += (hdr.vert_size_ext << 12);

  state.interlaced = !hdr.progressive;
  gst_base_video_parse_set_state (parse, state);

  new_buffer = gst_buffer_merge (mpegparse->seq_header_buffer, buffer);
  gst_buffer_unref (mpegparse->seq_header_buffer);
  mpegparse->seq_header_buffer = new_buffer;

  mpegparse->version = 2;

  return TRUE;
}

static gboolean
gst_mvp2_handle_sequence (GstMpegVideoParse2 * mpegparse, GstBuffer * buffer)
{
  GstBaseVideoParse *parse = GST_BASE_VIDEO_PARSE (mpegparse);
  MPEGSeqHdr hdr;
  GstVideoState state;

  if (!mpeg_util_parse_sequence_hdr (&hdr, buffer))
    return FALSE;

  state = gst_base_video_parse_get_state (parse);

  state.width = hdr.width;
  state.height = hdr.height;

  state.fps_n = hdr.fps_n;
  state.fps_d = hdr.fps_d;

  state.par_n = hdr.par_w;
  state.par_d = hdr.par_h;

  gst_base_video_parse_set_state (parse, state);

  gst_buffer_replace (&mpegparse->seq_header_buffer, buffer);

  gst_base_video_parse_set_sync_point (parse);

  mpegparse->state = GST_MVP2_STATE_NEED_DATA;

  return TRUE;
}

static gboolean
gst_mvp2_handle_gop (GstMpegVideoParse2 * mpegparse, GstBuffer * buffer)
{
  GstBaseVideoParse *parse = GST_BASE_VIDEO_PARSE (mpegparse);
  MPEGGop gop;
  GstVideoState state;
  GstClockTime time;

  if (!mpeg_util_parse_gop (&gop, buffer))
    return FALSE;

  state = gst_base_video_parse_get_state (parse);

  time = GST_SECOND * (gop.hour * 3600 + gop.minute * 60 + gop.second);

  GST_DEBUG ("gop timestamp: %" GST_TIME_FORMAT, GST_TIME_ARGS (time));

  mpegparse->gop_start =
      gst_util_uint64_scale (time, state.fps_n,
      state.fps_d * GST_SECOND) + gop.frame;

  GST_DEBUG ("gop frame_nr: %" G_GUINT64_FORMAT, mpegparse->gop_start);

  return TRUE;
}

static gboolean
gst_mvp2_handle_picture (GstMpegVideoParse2 * mpegparse, GstBuffer * buffer)
{
  GstBaseVideoParse *parse = GST_BASE_VIDEO_PARSE (mpegparse);
  MPEGPictureHdr hdr;
  GstVideoFrame *frame;

  if (!mpeg_util_parse_picture_hdr (&hdr, buffer))
    return FALSE;

  if (mpegparse->gop_start != -1) {
    frame = gst_base_video_parse_get_frame (parse);
    frame->presentation_frame_number = mpegparse->gop_start + hdr.tsn;
  }

  return TRUE;
}

static GstBaseVideoParseReturn
gst_mvp2_parse_data (GstBaseVideoParse * parse, GstBuffer * buffer)
{
  GstMpegVideoParse2 *mpegparse = GST_MPEG_VIDEO_PARSE2 (parse);
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (buffer);

  guint8 start_code;
  GstBaseVideoParseReturn ret = GST_BASE_VIDEO_PARSE_ADD;

  if (!gst_byte_reader_skip (&reader, 3))
    goto invalid_packet;

  if (!gst_byte_reader_get_uint8 (&reader, &start_code))
    goto invalid_packet;

  GST_DEBUG_OBJECT (mpegparse, "start_code: %d", start_code);

  if (mpegparse->state == GST_MVP2_STATE_NEED_SEQUENCE) {
    if (start_code != MPEG_PACKET_SEQUENCE) {
      GST_DEBUG_OBJECT (mpegparse, "Drop data since we haven't found a "
          "MPEG_PACKET_SEQUENCE yet");
      goto invalid_packet;
    }

    /* use the first sequence as a sync point */
    else {
      GstVideoFrame *frame;

      frame = gst_base_video_parse_get_frame (parse);
      frame->presentation_timestamp = 0;
      frame->presentation_frame_number = 0;
      gst_base_video_parse_set_sync_point (parse);
    }
  }

  switch (start_code) {
    case MPEG_PACKET_SEQUENCE:
    {
      GST_DEBUG_OBJECT (mpegparse, "MPEG_PACKET_SEQUENCE");

      if (mpegparse->state != GST_MVP2_STATE_NEED_SEQUENCE)
        ret = GST_BASE_VIDEO_PARSE_FINISH;

      if (!gst_mvp2_handle_sequence (mpegparse, buffer))
        goto invalid_packet;
      break;
    }
    case MPEG_PACKET_GOP:
    {
      GST_DEBUG_OBJECT (mpegparse, "MPEG_PACKET_GOP");

      if (mpegparse->prev_packet != MPEG_PACKET_SEQUENCE)
        ret = GST_BASE_VIDEO_PARSE_FINISH;

      if (!gst_mvp2_handle_gop (mpegparse, buffer))
        goto invalid_packet;

      break;
    }
    case MPEG_PACKET_PICTURE:
    {
      GST_DEBUG_OBJECT (mpegparse, "MPEG_PACKET_PICTURE");

      if (mpegparse->prev_packet != MPEG_PACKET_SEQUENCE &&
          mpegparse->prev_packet != MPEG_PACKET_GOP)
        ret = GST_BASE_VIDEO_PARSE_FINISH;

      if (!gst_mvp2_handle_picture (mpegparse, buffer))
        goto invalid_packet;

      break;
    }
    case MPEG_PACKET_EXTENSION:
    {
      guint8 ext_code;

      GST_DEBUG_OBJECT (mpegparse, "MPEG_PACKET_EXTENSION");

      if (!gst_byte_reader_get_uint8 (&reader, &ext_code))
        goto invalid_packet;

      ext_code = ((ext_code >> 4) & 0x0f);
      switch (ext_code) {
        case MPEG_PACKET_EXT_SEQUENCE:
        {
          GST_DEBUG_OBJECT (mpegparse, "MPEG_PACKET_SEQUENCE_EXTENSION");
          if (!gst_mvp2_handle_sequence_extension (mpegparse, buffer))
            goto invalid_packet;

          /* so that we don't finish the frame if we get a MPEG_PACKET_PICTURE
           * or MPEG_PACKET_GOP after this */
          start_code = MPEG_PACKET_SEQUENCE;
          break;
        }
        default:
          break;
      }
    }
  }

  mpegparse->prev_packet = start_code;
  return ret;

invalid_packet:
  return GST_BASE_VIDEO_PARSE_DROP;
}

static GstFlowReturn
gst_mvp2_shape_output (GstBaseVideoParse * parse, GstVideoFrame * frame)
{
  GST_DEBUG ("frame_nr: %d", frame->presentation_frame_number);

  return gst_base_video_parse_push (parse, frame->src_buffer);
}

static gboolean
gst_mvp2_start (GstBaseVideoParse * parse)
{
  GstMpegVideoParse2 *mpegparse = GST_MPEG_VIDEO_PARSE2 (parse);

  mpegparse->state = GST_MVP2_STATE_NEED_SEQUENCE;
  mpegparse->prev_packet = 0;
  mpegparse->gop_start = 0;

  mpegparse->version = 1;
  mpegparse->seq_header_buffer = NULL;

  return TRUE;
}

static gboolean
gst_mvp2_stop (GstBaseVideoParse * parse)
{
  GstMpegVideoParse2 *mpegparse = GST_MPEG_VIDEO_PARSE2 (parse);

  if (mpegparse->seq_header_buffer)
    gst_buffer_unref (mpegparse->seq_header_buffer);

  return TRUE;
}

static void
gst_mvp2_flush (GstBaseVideoParse * parse)
{
  GstMpegVideoParse2 *mpegparse = GST_MPEG_VIDEO_PARSE2 (parse);

  mpegparse->prev_packet = 0;
  mpegparse->gop_start = -1;
}

static void
gst_mvp2_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details_simple (element_class, "Mpeg[12] video parser",
      "Codec/Parser/Video",
      "Parses mpeg[12] video",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  GST_DEBUG_CATEGORY_INIT (mpegparse_debug, "mpegvideoparse2", 0,
      "Mpeg[12] parser element");
}

static void
gst_mvp2_init (GstMpegVideoParse2 * mpegparse, GstMpegVideoParse2Class * klass)
{
  mpegparse->seq_header_buffer = NULL;
}

static void
gst_mvp2_finalize (GObject * object)
{
  GstMpegVideoParse2 *mpegparse = GST_MPEG_VIDEO_PARSE2 (object);

  if (mpegparse->seq_header_buffer)
    gst_buffer_unref (mpegparse->seq_header_buffer);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mvp2_class_init (GstMpegVideoParse2Class * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseVideoParseClass *baseparse_class = GST_BASE_VIDEO_PARSE_CLASS (klass);

  gobject_class->finalize = gst_mvp2_finalize;

  baseparse_class->start = GST_DEBUG_FUNCPTR (gst_mvp2_start);
  baseparse_class->stop = GST_DEBUG_FUNCPTR (gst_mvp2_stop);
  baseparse_class->flush = GST_DEBUG_FUNCPTR (gst_mvp2_flush);

  baseparse_class->scan_for_sync = GST_DEBUG_FUNCPTR (gst_mvp2_scan_for_sync);
  baseparse_class->scan_for_packet_end =
      GST_DEBUG_FUNCPTR (gst_mvp2_scan_for_packet_end);

  baseparse_class->parse_data = GST_DEBUG_FUNCPTR (gst_mvp2_parse_data);
  baseparse_class->shape_output = GST_DEBUG_FUNCPTR (gst_mvp2_shape_output);

  baseparse_class->get_caps = GST_DEBUG_FUNCPTR (gst_mvp2_get_caps);
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
  return gst_element_register (plugin, "mpegvideoparse2",
      GST_RANK_PRIMARY, GST_TYPE_MPEG_VIDEO_PARSE2);
}


GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpegvideoparse2",
    "Mpeg[12] video parser",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
