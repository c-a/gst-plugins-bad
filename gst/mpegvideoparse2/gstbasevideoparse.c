/* GStreamer
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#include "gstbasevideoparse.h"

#include <string.h>
#include <math.h>

GST_DEBUG_CATEGORY_STATIC (gst_basevideoparse_debug);
#define GST_CAT_DEFAULT gst_basevideoparse_debug

/* GstBaseVideoParse signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0
};

/* GObject */
static void gst_base_video_parse_base_init (gpointer g_class);
static void gst_base_video_parse_class_init (GstBaseVideoParseClass * klass);
static void gst_base_video_parse_finalize (GObject * object);
static void gst_base_video_parse_init (GstBaseVideoParse * parse,
    GstBaseVideoParseClass * klass);

/* GstElement */
static GstStateChangeReturn gst_base_video_parse_change_state (GstElement *
    element, GstStateChange transition);

/* GstPad */
static const GstQueryType *gst_base_video_parse_get_query_types (GstPad * pad);
static gboolean gst_base_video_parse_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_base_video_parse_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_base_video_parse_sink_event (GstPad * pad,
    GstEvent * event);
static GstFlowReturn gst_base_video_parse_chain (GstPad * pad, GstBuffer * buf);
static void gst_base_video_parse_free_frame (GstVideoFrame * frame);

/* Utility */
static void gst_base_video_parse_flush (GstBaseVideoParse * parse);
static gboolean
gst_base_video_parse_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_base_video_parse_start (GstBaseVideoParse * parse);
static gboolean gst_base_video_parse_stop (GstBaseVideoParse * parse);
static GstVideoFrame *gst_base_video_parse_new_frame (GstBaseVideoParse *
    parse);

static GstFlowReturn gst_base_video_parse_drain (GstBaseVideoParse * parse,
    gboolean at_eos);
static void gst_base_video_parse_add_to_frame (GstBaseVideoParse * parse,
    GstBuffer * buffer);

static GstFlowReturn
gst_base_video_parse_finish_frame (GstBaseVideoParse * parse);
static void gst_base_video_parse_send_pending_segs (GstBaseVideoParse * parse);
static void gst_base_video_parse_clear_pending_segs (GstBaseVideoParse * parse);

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_basevideoparse_debug, "basevideoparse", 0, \
    "Video Parse Base Class");

GST_BOILERPLATE_FULL (GstBaseVideoParse, gst_base_video_parse,
    GstElement, GST_TYPE_ELEMENT, _do_init);

/*
 * GObject functions
 */
static void
gst_base_video_parse_base_init (gpointer g_class)
{

}

static void
gst_base_video_parse_class_init (GstBaseVideoParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_base_video_parse_finalize;

  element_class->change_state = gst_base_video_parse_change_state;
}

static void
gst_base_video_parse_finalize (GObject * object)
{
  GstBaseVideoParse *parse;

  g_return_if_fail (GST_IS_BASE_VIDEO_PARSE (object));
  parse = GST_BASE_VIDEO_PARSE (object);

  if (parse->input_adapter) {
    g_object_unref (parse->input_adapter);
  }
  if (parse->output_adapter) {
    g_object_unref (parse->output_adapter);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_base_video_parse_init (GstBaseVideoParse * parse,
    GstBaseVideoParseClass * klass)
{
  GstPadTemplate *pad_template;
  GstPad *pad;

  GST_DEBUG ("gst_base_video_parse_init");

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  g_return_if_fail (pad_template != NULL);

  parse->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_element_add_pad (GST_ELEMENT (parse), parse->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);

  parse->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_element_add_pad (GST_ELEMENT (parse), parse->srcpad);

  /* SINK_PAD */
  pad = GST_BASE_VIDEO_PARSE_SINK_PAD (parse);

  gst_pad_set_chain_function (pad, gst_base_video_parse_chain);
  gst_pad_set_event_function (pad, gst_base_video_parse_sink_event);

  /* SRC_PAD */
  pad = GST_BASE_VIDEO_PARSE_SRC_PAD (parse);

  gst_pad_use_fixed_caps (pad);
  gst_pad_set_query_type_function (pad, gst_base_video_parse_get_query_types);
  gst_pad_set_query_function (pad, gst_base_video_parse_src_query);
  gst_pad_set_event_function (pad, gst_base_video_parse_src_event);

  parse->input_adapter = gst_adapter_new ();
  parse->output_adapter = gst_adapter_new ();
}

/*
 * Public functions
 */
GstVideoState *
gst_base_video_parse_get_state (GstBaseVideoParse * parse)
{
  return &parse->state;
}

void
gst_base_video_parse_set_state (GstBaseVideoParse * parse,
    GstVideoState * state)
{
  GST_DEBUG ("set_state");

  memcpy (&parse->state, state, sizeof (GstVideoState));

  /* FIXME set caps */

}

GstVideoFrame *
gst_base_video_parse_get_frame (GstBaseVideoParse * parse)
{
  g_return_val_if_fail (GST_IS_BASE_VIDEO_PARSE (parse), NULL);

  return parse->current_frame;
}

void
gst_base_video_parse_set_duration (GstBaseVideoParse * parse,
    GstFormat format, gint64 duration)
{
  g_return_if_fail (GST_IS_BASE_VIDEO_PARSE (parse));

  if (duration != parse->duration) {
    GstMessage *m;

    m = gst_message_new_duration (GST_OBJECT (parse), format, duration);
    gst_element_post_message (GST_ELEMENT (parse), m);
  }

  parse->duration = duration;
  parse->duration_fmt = format;
}

void
gst_base_video_parse_lost_sync (GstBaseVideoParse * parse)
{
  g_return_if_fail (GST_IS_BASE_VIDEO_PARSE (parse));

  GST_DEBUG ("lost_sync");

  if (gst_adapter_available (parse->input_adapter) >= 1) {
    gst_adapter_flush (parse->input_adapter, 1);
  }

  parse->have_sync = FALSE;
}

void
gst_base_video_parse_set_sync_point (GstBaseVideoParse * parse)
{
  GST_DEBUG ("set_sync_point");

  parse->current_frame->is_sync_point = TRUE;

  parse->distance_from_sync = 0;
}

GstFlowReturn
gst_base_video_parse_push (GstBaseVideoParse * parse, GstBuffer * buffer)
{
  GstBaseVideoParseClass *parse_class;

  parse_class = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);

  if (parse->caps == NULL) {
    gboolean ret;

    parse->caps = parse_class->get_caps (parse);

    ret = gst_pad_set_caps (GST_BASE_VIDEO_PARSE_SRC_PAD (parse), parse->caps);

    if (!ret) {
      GST_WARNING ("pad didn't accept caps");
      return GST_FLOW_ERROR;
    }

    gst_base_video_parse_send_pending_segs (parse);
  }
  gst_buffer_set_caps (buffer,
      GST_PAD_CAPS (GST_BASE_VIDEO_PARSE_SRC_PAD (parse)));

  GST_LOG ("pushing buffer of %u bytes ts=%" GST_TIME_FORMAT " dur=%"
      GST_TIME_FORMAT " off=%" GST_TIME_FORMAT " off_end=%" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (buffer), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
      GST_TIME_ARGS (GST_BUFFER_OFFSET (buffer)),
      GST_TIME_ARGS (GST_BUFFER_OFFSET_END (buffer)));

  if (parse->discont) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    parse->discont = FALSE;
  } else {
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
  }

  return gst_pad_push (GST_BASE_VIDEO_PARSE_SRC_PAD (parse), buffer);
}

/*
 * GstElement functions
 */
static GstStateChangeReturn
gst_base_video_parse_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseVideoParse *parse = GST_BASE_VIDEO_PARSE (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_base_video_parse_start (parse);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_base_video_parse_stop (parse);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

/*
 * GstPad functions
 */
static const GstQueryType *
gst_base_video_parse_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    0
  };

  return query_types;
}

static gboolean
gst_base_video_parse_src_query (GstPad * pad, GstQuery * query)
{
  GstBaseVideoParse *parse;
  gboolean res = FALSE;

  parse = GST_BASE_VIDEO_PARSE (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 time;
      gint64 value;

      /* see if upstream can handle it */
      res = gst_pad_query_default (pad, query);
      if (res)
        goto done;

      gst_query_parse_position (query, &format, NULL);

      if (format == GST_FORMAT_DEFAULT || format == GST_FORMAT_TIME) {
        time = gst_segment_to_stream_time (&parse->state.segment,
            GST_FORMAT_TIME, parse->state.segment.last_stop);
        GST_DEBUG ("query position %" GST_TIME_FORMAT, GST_TIME_ARGS (time));
        res = gst_base_video_parse_convert (pad, GST_FORMAT_TIME, time, &format,
            &value);
      }
      if (res)
        gst_query_set_position (query, format, value);

      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      gint64 duration;

      /* see if upstream can handle it */
      res = gst_pad_query_default (pad, query);
      if (res)
        goto done;

      gst_query_parse_duration (query, &format, NULL);

      if (GST_CLOCK_TIME_IS_VALID (parse->duration) &&
          format == parse->duration_fmt) {
        duration = parse->duration;
        res = TRUE;
      } else if (GST_CLOCK_TIME_IS_VALID (parse->duration))
        res = gst_base_video_parse_convert (pad,
            parse->duration_fmt, parse->duration, &format, &duration);

      if (res)
        gst_query_set_position (query, format, duration);

      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      /* see if upstream can handle it */
      res = gst_pad_query_default (pad, query);
      if (res)
        goto done;

      GST_WARNING ("query convert");

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res = gst_base_video_parse_convert (pad,
          src_fmt, src_val, &dest_fmt, &dest_val);
      if (res)
        gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);

      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
done:
  gst_object_unref (parse);

  return res;
}

static gboolean
gst_base_video_parse_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseVideoParse *parse;
  gboolean res = FALSE;

  parse = GST_BASE_VIDEO_PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstFormat format, tformat;
      gdouble rate;
      GstEvent *real_seek;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gint64 cur, stop;
      gint64 tcur, tstop;

      /* see if upstream can handle it */
      res = gst_pad_push_event (GST_BASE_VIDEO_PARSE_SINK_PAD (parse), event);
      if (res)
        goto done;

      gst_event_parse_seek (event, &rate, &format, &flags, &cur_type,
          &cur, &stop_type, &stop);

      tformat = GST_FORMAT_BYTES;
      res = gst_base_video_parse_convert (pad, format, cur, &tformat, &tcur);
      if (!res)
        goto convert_error;
      res = gst_base_video_parse_convert (pad, format, stop, &tformat, &tstop);
      if (!res)
        goto convert_error;

      real_seek = gst_event_new_seek (rate, GST_FORMAT_BYTES,
          flags, cur_type, tcur, stop_type, tstop);

      res =
          gst_pad_push_event (GST_BASE_VIDEO_PARSE_SINK_PAD (parse), real_seek);

      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
done:
  gst_object_unref (parse);
  return res;

convert_error:
  GST_DEBUG_OBJECT (parse, "could not convert format");
  goto done;
}

static gboolean
gst_base_video_parse_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseVideoParse *parse;
  gboolean res = FALSE;

  parse = GST_BASE_VIDEO_PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_base_video_parse_flush (parse);
      res = gst_pad_push_event (GST_BASE_VIDEO_PARSE_SRC_PAD (parse), event);
      break;
    case GST_EVENT_EOS:
    {
      GST_DEBUG ("EOS");

      gst_base_video_parse_flush (parse);

      res = gst_pad_push_event (GST_BASE_VIDEO_PARSE_SRC_PAD (parse), event);
      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      GstFormat format;
      gdouble rate;
      gint64 start, stop, time;

      gst_event_parse_new_segment (event, &update, &rate, &format, &start,
          &stop, &time);

      if (rate <= 0.0)
        goto newseg_wrong_rate;

      if (format != GST_FORMAT_TIME) {
        GstFormat tformat;

        tformat = GST_FORMAT_TIME;
        res = gst_base_video_parse_convert (pad, format, start, &tformat,
            &start);

        res = gst_base_video_parse_convert (pad, format, stop, &tformat, &stop);

        res = gst_base_video_parse_convert (pad, format, time, &tformat, &time);

        /* if we couldn't convert we output a default open-ended
         * TIME segment */
        if (!res) {
          /* set new values */
          start = 0;
          stop = GST_CLOCK_TIME_NONE;
          time = 0;
        }

        event = gst_event_new_new_segment (update, rate, tformat, start,
            stop, time);
      }

      GST_DEBUG ("newsegment %lld %lld", start, time);
      gst_segment_set_newsegment (&parse->state.segment, update,
          rate, GST_FORMAT_TIME, start, stop, time);

      if (parse->caps) {
        res = gst_pad_push_event (GST_BASE_VIDEO_PARSE_SRC_PAD (parse), event);
      } else {
        parse->pending_segs = g_slist_append (parse->pending_segs, event);
        res = TRUE;
      }
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
done:
  gst_object_unref (parse);
  return res;

newseg_wrong_rate:
  GST_DEBUG_OBJECT (parse, "negative rates not supported");
  goto done;
}

/*
 * Utility functions
 */
static void
gst_base_video_parse_flush (GstBaseVideoParse * parse)
{
  GstBaseVideoParseClass *klass = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);

  if (gst_adapter_available (parse->input_adapter)) {
    gst_base_video_parse_add_to_frame (parse,
        gst_adapter_take_buffer (parse->input_adapter,
            gst_adapter_available (parse->input_adapter)));
  }

  gst_base_video_parse_finish_frame (parse);

  parse->discont = TRUE;
  parse->have_sync = FALSE;
  parse->presentation_timestamp = GST_CLOCK_TIME_NONE;
  parse->next_offset = GST_BUFFER_OFFSET_NONE;

  if (klass->flush)
    klass->flush (parse);
}

static gint64
gst_base_video_parse_get_frame_number (GstBaseVideoParse * parse,
    GstClockTime timestamp)
{
  GstVideoFrame *frame = parse->current_frame;

  return gst_util_uint64_scale (frame->presentation_timestamp
      - parse->timestamp_offset, parse->state.fps_n,
      parse->state.fps_d * GST_SECOND);
}

static GstClockTime
gst_base_video_parse_get_timestamp (GstBaseVideoParse * parse,
    gint64 picture_number)
{
  if (picture_number < 0) {
    return parse->timestamp_offset -
        (gint64) gst_util_uint64_scale (-picture_number,
        parse->state.fps_d * GST_SECOND, parse->state.fps_n);
  } else {
    return parse->timestamp_offset +
        gst_util_uint64_scale (picture_number,
        parse->state.fps_d * GST_SECOND, parse->state.fps_n);
  }
}

static gboolean
gst_base_video_parse_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = FALSE;
  GstBaseVideoParse *parse;
  GstBaseVideoParseClass *klass;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  parse = GST_BASE_VIDEO_PARSE (gst_pad_get_parent (pad));
  klass = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);

  if (klass->convert)
    res =
        klass->convert (parse, src_format, src_value, *dest_format, dest_value);

  if (!res) {
    if (src_format == GST_FORMAT_DEFAULT && *dest_format == GST_FORMAT_TIME) {
      *dest_value = gst_base_video_parse_get_timestamp (parse, src_value);
      res = TRUE;
    } else if (src_format == GST_FORMAT_TIME
        && *dest_format == GST_FORMAT_DEFAULT) {
      *dest_value = gst_base_video_parse_get_frame_number (parse, src_value);
      res = TRUE;
    } else
      GST_WARNING ("unhandled conversion from %d to %d", src_format,
          *dest_format);
  }

  gst_object_unref (parse);

  return res;
}

static gboolean
gst_base_video_parse_start (GstBaseVideoParse * parse)
{
  GstBaseVideoParseClass *klass = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);
  gboolean res = TRUE;

  parse->eos = FALSE;
  parse->discont = TRUE;
  parse->have_sync = FALSE;

  parse->system_frame_number = 0;
  parse->presentation_timestamp = 0;
  parse->next_offset = GST_BUFFER_OFFSET_NONE;

  parse->reorder_depth = 1;

  parse->duration = GST_CLOCK_TIME_NONE;

  parse->caps = NULL;
  parse->pending_segs = NULL;

  gst_segment_init (&parse->state.segment, GST_FORMAT_TIME);

  parse->current_frame = gst_base_video_parse_new_frame (parse);

  if (klass->start)
    res = klass->start (parse);

  return res;
}

static gboolean
gst_base_video_parse_stop (GstBaseVideoParse * parse)
{
  GstBaseVideoParseClass *klass = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);
  gboolean res = TRUE;

  if (parse->caps)
    gst_caps_unref (parse->caps);

  gst_base_video_parse_clear_pending_segs (parse);

  gst_adapter_clear (parse->input_adapter);
  gst_adapter_clear (parse->output_adapter);

  if (parse->current_frame)
    gst_base_video_parse_free_frame (parse->current_frame);

  if (klass->stop)
    res = klass->stop (parse);

  return res;
}

static GstFlowReturn
gst_base_video_parse_drain (GstBaseVideoParse * parse, gboolean at_eos)
{
  GstBaseVideoParseClass *klass;
  GstFlowReturn ret;
  gint next;

  klass = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);

  if (!parse->have_sync) {
    gint n, m;

    GST_DEBUG ("no sync, scanning");

    n = gst_adapter_available (parse->input_adapter);
    m = klass->scan_for_sync (parse->input_adapter, at_eos, 0, n);

    gst_adapter_flush (parse->input_adapter, m);

    if (m < n) {
      GST_DEBUG ("found possible sync after %d bytes (of %d)", m, n);
      /* this is only "maybe" sync */
      parse->have_sync = TRUE;
    }

    if (!parse->have_sync) {
      return GST_FLOW_OK;
    }
  }

  ret = klass->scan_for_packet_end (parse, parse->input_adapter, &next);
  while (ret == GST_FLOW_OK) {
    GstBuffer *buffer;
    GstBaseVideoParseReturn p_ret;

    parse->input_buffer_timestamp =
        GST_BUFFER_TIMESTAMP (parse->input_adapter->buflist->data);
    buffer = gst_adapter_take_buffer (parse->input_adapter, next);
    p_ret = klass->parse_data (parse, buffer);

    switch (p_ret) {
      case GST_BASE_VIDEO_PARSE_ADD:
        gst_base_video_parse_add_to_frame (parse, buffer);
        break;
      case GST_BASE_VIDEO_PARSE_FINISH:
        gst_base_video_parse_finish_frame (parse);
        gst_base_video_parse_add_to_frame (parse, buffer);
        break;
      case GST_BASE_VIDEO_PARSE_DROP:
        gst_buffer_unref (buffer);
        break;
      default:
        GST_WARNING ("Invalid return from GstBaseVideoParse::parse_data");
        break;
    }

    ret = klass->scan_for_packet_end (parse, parse->input_adapter, &next);
  }

  if (ret == GST_BASE_VIDEO_PARSE_FLOW_NEED_DATA) {
    return GST_FLOW_OK;
  }
  return ret;
}

static GstFlowReturn
gst_base_video_parse_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseVideoParse *parse;
  GstBaseVideoParseClass *klass;
  gboolean discont = FALSE;

  GST_DEBUG ("chain with %d bytes", GST_BUFFER_SIZE (buf));

  parse = GST_BASE_VIDEO_PARSE (GST_PAD_PARENT (pad));
  klass = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);

  /* If we have an offset, and the incoming offset doesn't match, 
     or we have a discont, handle it first by flushing out data
     we have collected. */
  if (parse->next_offset != GST_BUFFER_OFFSET_NONE) {
    if (GST_BUFFER_OFFSET_IS_VALID (buf)) {
      if (parse->next_offset != GST_BUFFER_OFFSET (buf))
        discont = TRUE;
      parse->next_offset = GST_BUFFER_OFFSET (buf) + GST_BUFFER_SIZE (buf);
    } else {
      parse->next_offset = parse->next_offset + GST_BUFFER_SIZE (buf);
    }
  }

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT)))
    discont = TRUE;

  if (discont) {
    GST_DEBUG_OBJECT (parse, "received DISCONT buffer");
    gst_base_video_parse_flush (parse);
  }

  GST_DEBUG ("upstream timestamp: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
  gst_adapter_push (parse->input_adapter, buf);

  return gst_base_video_parse_drain (parse, FALSE);
}

static void
gst_base_video_parse_add_to_frame (GstBaseVideoParse * parse,
    GstBuffer * buffer)
{
  if (gst_adapter_available (parse->output_adapter) == 0)
    parse->upstream_timestamp = parse->input_buffer_timestamp;

  gst_adapter_push (parse->output_adapter, buffer);
}

static GstFlowReturn
gst_base_video_parse_finish_frame (GstBaseVideoParse * parse)
{
  GstVideoFrame *frame = parse->current_frame;
  GstClockTime upstream_timestamp;
  GstBuffer *buffer;
  GstBaseVideoParseClass *parse_class;
  GstFlowReturn ret;

  GST_DEBUG ("finish_frame");

  parse_class = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);

  if (gst_adapter_available (parse->output_adapter)) {
    upstream_timestamp = parse->upstream_timestamp;
    buffer =
        gst_adapter_take_buffer (parse->output_adapter,
        gst_adapter_available (parse->output_adapter));

    if (!GST_CLOCK_TIME_IS_VALID (frame->presentation_duration)) {
      frame->presentation_duration = gst_util_uint64_scale (GST_SECOND,
          parse->state.fps_d, parse->state.fps_n);
    }

    /* we prefer timestamps coming from upstream */
    if (GST_CLOCK_TIME_IS_VALID (upstream_timestamp)
        && upstream_timestamp != parse->presentation_timestamp) {
      GST_DEBUG ("Got upstream timestamp");
      frame->presentation_timestamp = parse->upstream_timestamp;
    }

    if (frame->is_sync_point) {
      if (GST_CLOCK_TIME_IS_VALID (frame->presentation_timestamp)
          && frame->presentation_frame_number != -1) {
        parse->timestamp_offset =
            frame->presentation_timestamp -
            gst_util_uint64_scale (frame->presentation_frame_number,
            parse->state.fps_d * GST_SECOND, parse->state.fps_n);

        parse->distance_from_sync = 0;
      }

      else
        GST_DEBUG ("subclass set frame as sync_point but didn't supply "
            "presentation_timestamp and presentation_frame_number");
    }

    /* calculate timestamp from frame number if we've got one */
    if (frame->presentation_timestamp == GST_CLOCK_TIME_NONE
        && frame->presentation_frame_number != -1) {

      frame->presentation_timestamp =
          gst_base_video_parse_get_timestamp (parse,
          frame->presentation_frame_number);
    }

    parse->presentation_timestamp = frame->presentation_timestamp;
    gst_segment_set_last_stop (&parse->state.segment,
        GST_FORMAT_TIME, parse->presentation_timestamp);

    frame->distance_from_sync = parse->distance_from_sync;
    parse->distance_from_sync++;

    frame->decode_timestamp =
        gst_base_video_parse_get_timestamp (parse, frame->decode_frame_number);

    GST_BUFFER_TIMESTAMP (buffer) = frame->presentation_timestamp;
    GST_BUFFER_DURATION (buffer) = frame->presentation_duration;
    if (frame->decode_frame_number < 0) {
      GST_BUFFER_OFFSET (buffer) = 0;
    } else {
      GST_BUFFER_OFFSET (buffer) = frame->decode_timestamp;
    }
    GST_BUFFER_OFFSET_END (buffer) = GST_CLOCK_TIME_NONE;

    if (frame->is_sync_point) {
      GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    } else {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    }

    frame->src_buffer = buffer;
    ret = parse_class->shape_output (parse, frame);
  }

  gst_base_video_parse_free_frame (parse->current_frame);

  /* create new frame */
  parse->current_frame = gst_base_video_parse_new_frame (parse);

  return ret;
}

static void
gst_base_video_parse_free_frame (GstVideoFrame * frame)
{
  if (frame->sink_buffer) {
    gst_buffer_unref (frame->sink_buffer);
  }
#if 0
  if (frame->src_buffer) {
    gst_buffer_unref (frame->src_buffer);
  }
#endif

  g_free (frame);
}

static GstVideoFrame *
gst_base_video_parse_new_frame (GstBaseVideoParse * parse)
{
  GstVideoFrame *frame;

  frame = g_malloc0 (sizeof (GstVideoFrame));

  frame->presentation_duration = GST_CLOCK_TIME_NONE;
  frame->presentation_timestamp = GST_CLOCK_TIME_NONE;
  frame->presentation_frame_number = -1;

  frame->system_frame_number = parse->system_frame_number;
  parse->system_frame_number++;

  frame->decode_frame_number = frame->system_frame_number -
      parse->reorder_depth;

  return frame;
}

static void
gst_base_video_parse_send_pending_segs (GstBaseVideoParse * parse)
{
  while (parse->pending_segs) {
    GstEvent *ev = parse->pending_segs->data;

    gst_pad_push_event (parse->srcpad, ev);

    parse->pending_segs =
        g_slist_delete_link (parse->pending_segs, parse->pending_segs);
  }
  parse->pending_segs = NULL;
}

static void
gst_base_video_parse_clear_pending_segs (GstBaseVideoParse * parse)
{
  while (parse->pending_segs) {
    GstEvent *ev = parse->pending_segs->data;
    gst_event_unref (ev);

    parse->pending_segs =
        g_slist_delete_link (parse->pending_segs, parse->pending_segs);
  }
}
