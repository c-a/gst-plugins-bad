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
    gboolean at_eos, gint * size)
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
  GstVideoState *state;
  GstBuffer *new_buffer;

  if (!mpeg_util_parse_sequence_extension (&hdr, buffer))
    return FALSE;

  state = gst_base_video_parse_get_state (parse);

  state->fps_n *= (hdr.fps_n_ext + 1);
  state->fps_d *= (hdr.fps_d_ext + 1);

  state->width += (hdr.horiz_size_ext << 12);
  state->height += (hdr.vert_size_ext << 12);

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
  GstVideoState *state;

  if (!mpeg_util_parse_sequence_hdr (&hdr, buffer))
    return FALSE;

  state = gst_base_video_parse_get_state (parse);

  state->width = hdr.width;
  state->height = hdr.height;

  state->fps_n = hdr.fps_n;
  state->fps_d = hdr.fps_d;

  state->par_n = hdr.par_w;
  state->par_d = hdr.par_h;

  gst_base_video_parse_set_state (parse, state);

  if (mpegparse->seq_header_buffer)
    gst_buffer_unref (mpegparse->seq_header_buffer);
  mpegparse->seq_header_buffer = gst_buffer_ref (buffer);

  gst_base_video_parse_set_sync_point (parse);

  mpegparse->state = GST_MVP2_STATE_NEED_DATA;

  return TRUE;
}

GstFlowReturn
gst_mvp2_finish_frame (GstMpegVideoParse2 * mpegparse)
{
  GstBaseVideoParse *parse = (GST_BASE_VIDEO_PARSE (mpegparse));

  parse->current_frame->presentation_frame_number = mpegparse->frame_nr;
  mpegparse->frame_nr++;

  mpegparse->prev_packet = 0;
  return gst_base_video_parse_finish_frame (parse);
}

static GstFlowReturn
gst_mvp2_parse_data (GstBaseVideoParse * parse, GstBuffer * buffer)
{
  GstMpegVideoParse2 *mpegparse = GST_MPEG_VIDEO_PARSE2 (parse);
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (buffer);

  guint8 start_code;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (mpegparse, "buffer_size: %d", GST_BUFFER_SIZE (buffer));

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
  }

  switch (start_code) {
    case MPEG_PACKET_SEQUENCE:
    {
      GST_DEBUG_OBJECT (mpegparse, "MPEG_PACKET_SEQUENCE");

      if (mpegparse->state != GST_MVP2_STATE_NEED_SEQUENCE)
        gst_base_video_parse_finish_frame (parse);

      if (!gst_mvp2_handle_sequence (mpegparse, buffer))
        goto invalid_packet;
      break;
    }
    case MPEG_PACKET_GOP:
    {
      GST_DEBUG_OBJECT (mpegparse, "MPEG_PACKET_GOP");

      if (mpegparse->prev_packet != MPEG_PACKET_SEQUENCE)
        ret = gst_mvp2_finish_frame (mpegparse);
      break;
    }
    case MPEG_PACKET_PICTURE:
    {
      GST_DEBUG_OBJECT (mpegparse, "MPEG_PACKET_PICTURE");

      if (mpegparse->prev_packet != MPEG_PACKET_SEQUENCE &&
          mpegparse->prev_packet != MPEG_PACKET_GOP)
        ret = gst_mvp2_finish_frame (mpegparse);

#if 0
      if (!gst_mvp2_handle_picture (mpegparse, buffer))
        goto invalid_packet;
#endif
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
          if (!gst_mvp2_handle_sequence_extension (mpegparse, buffer))
            goto invalid_packet;
          break;
        }
        default:
          break;
      }
    }
  }

  mpegparse->prev_packet = start_code;
  gst_base_video_parse_add_to_frame (parse, buffer);

  return ret;

invalid_packet:
  gst_buffer_unref (buffer);
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mvp2_shape_output (GstBaseVideoParse * parse, GstVideoFrame * frame)
{
  return gst_base_video_parse_push (parse, frame->src_buffer);
}

static GstCaps *
gst_mvp2_get_caps (GstBaseVideoParse * parse)
{
  GstMpegVideoParse2 *mpegparse = GST_MPEG_VIDEO_PARSE2 (parse);
  GstVideoState *state;
  GstCaps *caps;

  state = gst_base_video_parse_get_state (parse);
  caps = gst_caps_new_simple ("video/mpeg",
      "systemstream", G_TYPE_BOOLEAN, FALSE,
      "parsed", G_TYPE_BOOLEAN, TRUE,
      "mpegversion", G_TYPE_INT, mpegparse->version,
      "width", G_TYPE_INT, state->width,
      "height", G_TYPE_INT, state->height,
      "framerate", GST_TYPE_FRACTION, state->fps_n, state->fps_d,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, state->par_n, state->par_d,
      "codec_data", GST_TYPE_BUFFER, mpegparse->seq_header_buffer, NULL);

  return caps;
}

static gboolean
gst_mvp2_start (GstBaseVideoParse * parse)
{
  GstMpegVideoParse2 *mpegparse = GST_MPEG_VIDEO_PARSE2 (parse);

  mpegparse->state = GST_MVP2_STATE_NEED_SEQUENCE;
  mpegparse->prev_packet = 0;
  mpegparse->frame_nr = 0;

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

#if 0
static gboolean
gst_mvp2_find_start_code (GstByteReader * reader, guint8 * start_code)
{
  guint32 sync_code = 0xffffffff;
  guint8 val;

  while (gst_byte_reader_get_uint8 (reader, &val)) {
    sync_code <<= 8;
    sync_code |= val;

    if (sync_code != 0x00000001)
      continue;

    /* get stream id */
    if (!gst_byte_reader_get_uint8 (reader, start_code)) {
      gst_byte_reader_set_pos (reader, reader->byte - 4);
      return FALSE;
    }

    return TRUE;
  }

  gst_byte_reader_set_pos (reader, reader->byte - 2);

  return FALSE;
}

static gboolean
gst_mvp2_check_valid_frame (GstBaseParse * parse, GstBuffer * buffer,
    guint * framesize, gint * skipsize)
{
  GstMpegVideoParse2 *mpegparse = GST_MPEG_VIDEO_PARSE2 (parse);
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (buffer);

  guint8 start_code = 0;

  if (mpegparse->state == GST_MVP2_STATE_NEED_END) {
    gst_byte_reader_set_pos (&reader, mpegparse->pos);
  }

  while (gst_mvp2_find_start_code (&reader, &start_code)) {
    switch (mpegparse->state) {
      case GST_MVP2_STATE_NEED_SEQUENCE:
      {
        if (start_code == MPEG_PACKET_SEQUENCE) {
          guint pos;

          pos = gst_byte_reader_get_pos (&reader);
          if (pos > 4) {
            GST_DEBUG_OBJECT (mpegparse, "Drop data since we haven't found a"
                "MPEG_PACKET_SEQUENCE yet");
            *skipsize = pos - 4;
            return FALSE;
          }

          mpegparse->prev_packet = start_code;
          mpegparse->state = GST_MVP2_STATE_NEED_END;
        }
        break;
      }

      case GST_MVP2_STATE_NEED_START:
      {
        if (start_code == MPEG_PACKET_SEQUENCE || start_code == MPEG_PACKET_GOP
            || start_code == MPEG_PACKET_PICTURE) {
          guint pos;

          pos = gst_byte_reader_get_pos (&reader);
          if (pos > 4) {
            GST_DEBUG_OBJECT (mpegparse, "Drop data since we haven't found"
                "a packet start yet");
            *skipsize = pos - 4;
            return FALSE;
          }

          mpegparse->prev_packet = start_code;
          mpegparse->state = GST_MVP2_STATE_NEED_END;
        }
        break;
      }

      case GST_MVP2_STATE_NEED_END:
      {
        if ((start_code == MPEG_PACKET_SEQUENCE) ||
            (start_code == MPEG_PACKET_GOP
                && mpegparse->prev_packet != MPEG_PACKET_SEQUENCE)
            || (start_code == MPEG_PACKET_PICTURE
                && mpegparse->prev_packet != MPEG_PACKET_SEQUENCE
                && mpegparse->prev_packet != MPEG_PACKET_GOP)) {
          mpegparse->state = GST_MVP2_STATE_NEED_START;
          *framesize = gst_byte_reader_get_pos (&reader) - 4;
          return TRUE;
        } else
          mpegparse->prev_packet = start_code;
      }
        break;
    }
  }

  /* we have found the start of the packet but not the end
   * therefore we increase the frame_size */
  if (mpegparse->state == GST_MVP2_STATE_NEED_END) {
    GST_DEBUG ("Increasing frame_size");
    mpegparse->pos = gst_byte_reader_get_pos (&reader);
    gst_base_parse_set_min_frame_size (parse, mpegparse->min_frame_size +=
        4096);
    *skipsize = 0;

    return FALSE;
  } else {
    *skipsize = gst_byte_reader_get_pos (&reader);
    return FALSE;
  }
}

static gboolean
gst_mvp2_handle_sequence (GstMpegVideoParse2 * mpegparse, const guint8 * start,
    guint8 size)
{
  MPEGSeqHdr seq;
  GstBuffer *buffer;
  GstCaps *caps;

  mpeg_util_parse_sequence_hdr (&seq, (guint8 *) start,
      (guint8 *) start + size);

  mpegparse->fps_n = seq.fps_n;
  mpegparse->fps_d = seq.fps_d;

  buffer = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (buffer), start, size);

  caps = gst_caps_new_simple ("video/mpeg",
      "systemstream", G_TYPE_BOOLEAN, FALSE,
      "parsed", G_TYPE_BOOLEAN, TRUE,
      "mpegversion", G_TYPE_INT, seq.mpeg_version,
      "width", G_TYPE_INT, seq.width,
      "height", G_TYPE_INT, seq.height,
      "framerate", GST_TYPE_FRACTION, seq.fps_n, seq.fps_d,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, seq.par_w, seq.par_h,
      "codec_data", GST_TYPE_BUFFER, buffer, NULL);

  GST_DEBUG ("New mpegvideoparse caps: %" GST_PTR_FORMAT, caps);
  if (!gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (GST_BASE_PARSE (mpegparse)),
          caps)) {
    gst_caps_unref (caps);
    return FALSE;
  }

  gst_caps_unref (caps);

  return TRUE;
}

static GstFlowReturn
gst_mvp2_parse_frame (GstBaseParse * parse, GstBuffer * buffer)
{
  GstMpegVideoParse2 *mpegparse = GST_MPEG_VIDEO_PARSE2 (parse);
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (buffer);
  gboolean ret;
  guint8 start_code;

  /* reset min_frame_size */
  /* FIXME: use VBV size */
  mpegparse->min_frame_size = 4096;
  gst_base_parse_set_min_frame_size (parse, mpegparse->min_frame_size);

  if (!gst_mvp2_find_start_code (&reader, &start_code))
    return GST_FLOW_ERROR;

  do {
    guint8 next_start_code;
    const guint8 *start, *end;
    guint size;

    /* calculate start and end position of packet */
    start = reader.data + reader.byte - 4;
    if ((ret = gst_mvp2_find_start_code (&reader, &next_start_code)))
      end = reader.data + reader.byte - 4;
    else
      end = reader.data + reader.size;

    size = end - start;

    switch (start_code) {
      case MPEG_PACKET_SEQUENCE:
      {
        guint pos;

        /* there may be a MPEG_PACKET_EXT_SEQUENCE after */
        pos = reader.byte;

        if (gst_mvp2_find_start_code (&reader, NULL))
          end = reader.data + reader.byte - 4;

        else
          end = reader.data + reader.size;

        size = end - start;

        /* set back the bytereader */
        gst_byte_reader_set_pos (&reader, pos);

        gst_mvp2_handle_sequence (mpegparse, start, size);
        break;
      }
      case MPEG_PACKET_PICTURE:
      {
        GST_DEBUG_OBJECT (mpegparse, "Handling MPEG_PACKET_PICTURE");
        //gst_mvp2_handle_picture (mpegparse, buffer, start, size);
        break;
      }
    }

    start_code = next_start_code;
  }
  while (ret);

  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale (GST_SECOND, mpegparse->fps_d, mpegparse->fps_n);

  /* 
   * We do some naive timestamping of nontimestamped buffers by simply
   * taking the previous timestamp and increment it by the buffer duration.
   */
  if (GST_BUFFER_TIMESTAMP (buffer) == GST_CLOCK_TIME_NONE)
    GST_BUFFER_TIMESTAMP (buffer) =
        mpegparse->time + GST_BUFFER_DURATION (buffer);

  mpegparse->time = GST_BUFFER_TIMESTAMP (buffer);

  GST_DEBUG ("Pushing buffer, timestamp: %" GST_TIME_FORMAT
      ", duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  return GST_FLOW_OK;
}

#endif

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
