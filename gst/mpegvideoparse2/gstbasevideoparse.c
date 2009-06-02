/* Schrodinger
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
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

static void gst_base_video_parse_finalize (GObject * object);
static const GstQueryType *gst_base_video_parse_get_query_types (GstPad * pad);
static gboolean gst_base_video_parse_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_base_video_parse_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_base_video_parse_sink_event (GstPad * pad,
    GstEvent * event);
static GstStateChangeReturn gst_base_video_parse_change_state (GstElement *
    element, GstStateChange transition);
static GstFlowReturn gst_base_video_parse_chain (GstPad * pad, GstBuffer * buf);
static void gst_base_video_parse_free_frame (GstVideoFrame * frame);
static GstVideoFrame *gst_base_video_parse_new_frame (GstBaseVideoParse *
    base_video_parse);
static GstFlowReturn
gst_base_video_parse_drain (GstBaseVideoParse * base_video_parse,
    gboolean at_eos);


#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_basevideoparse_debug, "basevideoparse", 0, \
    "Video Parse Base Class");

GST_BOILERPLATE_FULL (GstBaseVideoParse, gst_base_video_parse,
    GstElement, GST_TYPE_ELEMENT, _do_init);

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
gst_base_video_parse_init (GstBaseVideoParse * base_video_parse,
    GstBaseVideoParseClass * klass)
{
  GstPadTemplate *pad_template;
  GstPad *pad;

  GST_DEBUG ("gst_base_video_parse_init");

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  g_return_if_fail (pad_template != NULL);

  base_video_parse->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_element_add_pad (GST_ELEMENT (base_video_parse),
      base_video_parse->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);

  base_video_parse->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_element_add_pad (GST_ELEMENT (base_video_parse),
      base_video_parse->srcpad);


  pad = GST_BASE_VIDEO_PARSE_SINK_PAD (base_video_parse);

  gst_pad_set_chain_function (pad, gst_base_video_parse_chain);
  gst_pad_set_event_function (pad, gst_base_video_parse_sink_event);

  pad = GST_BASE_VIDEO_PARSE_SRC_PAD (base_video_parse);

  gst_pad_set_query_type_function (pad, gst_base_video_parse_get_query_types);
  gst_pad_set_query_function (pad, gst_base_video_parse_src_query);
  gst_pad_set_event_function (pad, gst_base_video_parse_src_event);

  base_video_parse->input_adapter = gst_adapter_new ();
  base_video_parse->output_adapter = gst_adapter_new ();

  base_video_parse->reorder_depth = 1;

  base_video_parse->current_frame =
      gst_base_video_parse_new_frame (base_video_parse);
}

static void
gst_base_video_parse_reset (GstBaseVideoParse * base_video_parse)
{
  GST_DEBUG ("reset");

  base_video_parse->discont = TRUE;
  base_video_parse->have_sync = FALSE;
  base_video_parse->eos = FALSE;

  base_video_parse->system_frame_number = 0;
  base_video_parse->presentation_timestamp = 0;

  if (base_video_parse->caps) {
    gst_caps_unref (base_video_parse->caps);
    base_video_parse->caps = NULL;
  }

  gst_segment_init (&base_video_parse->state.segment, GST_FORMAT_TIME);
  gst_adapter_clear (base_video_parse->input_adapter);
  gst_adapter_clear (base_video_parse->output_adapter);

  if (base_video_parse->current_frame) {
    gst_base_video_parse_free_frame (base_video_parse->current_frame);
  }
  base_video_parse->current_frame =
      gst_base_video_parse_new_frame (base_video_parse);

}

static void
gst_base_video_parse_finalize (GObject * object)
{
  GstBaseVideoParse *base_video_parse;

  g_return_if_fail (GST_IS_BASE_VIDEO_PARSE (object));
  base_video_parse = GST_BASE_VIDEO_PARSE (object);

  if (base_video_parse->input_adapter) {
    g_object_unref (base_video_parse->input_adapter);
  }
  if (base_video_parse->output_adapter) {
    g_object_unref (base_video_parse->output_adapter);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

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

static gint64
gst_base_video_parse_get_frame_number (GstBaseVideoParse * base_video_parse,
    GstClockTime timestamp)
{
  GstVideoFrame *frame = base_video_parse->current_frame;

  return gst_util_uint64_scale (frame->presentation_timestamp
      - base_video_parse->timestamp_offset, base_video_parse->state.fps_n,
      base_video_parse->state.fps_d * GST_SECOND);
}

static GstClockTime
gst_base_video_parse_get_timestamp (GstBaseVideoParse * base_video_parse,
    gint64 picture_number)
{
  if (picture_number < 0) {
    return base_video_parse->timestamp_offset -
        (gint64) gst_util_uint64_scale (-picture_number,
        base_video_parse->state.fps_d * GST_SECOND,
        base_video_parse->state.fps_n);
  } else {
    return base_video_parse->timestamp_offset +
        gst_util_uint64_scale (picture_number,
        base_video_parse->state.fps_d * GST_SECOND,
        base_video_parse->state.fps_n);
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
gst_base_video_parse_src_query (GstPad * pad, GstQuery * query)
{
  GstBaseVideoParse *base_parse;
  gboolean res = FALSE;

  base_parse = GST_BASE_VIDEO_PARSE (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 time;
      gint64 value;

      /* see if upstream can handle it */
      gst_query_ref (query);
      res = gst_pad_query (GST_BASE_VIDEO_PARSE_SINK_PAD (base_parse), query);
      if (res)
        goto done;

      gst_query_parse_position (query, &format, NULL);

      if (format == GST_FORMAT_DEFAULT || format == GST_FORMAT_TIME) {
        time = gst_segment_to_stream_time (&base_parse->state.segment,
            GST_FORMAT_TIME, base_parse->state.segment.last_stop);
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
      gint64 value;

      /* see if upstream can handle it */
      gst_query_ref (query);
      res = gst_pad_query (GST_BASE_VIDEO_PARSE_SINK_PAD (base_parse), query);
      if (res)
        goto done;

      gst_query_parse_duration (query, &format, NULL);

#if 0
      if (format == GST_FORMAT_DEFAULT || format == GST_FORMAT_TIME)
        res = gst_base_video_parse_get_duration (parse, format, &value);
#endif
      res = FALSE;
      if (res)
        gst_query_set_position (query, format, value);

      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      /* see if upstream can handle it */
      gst_query_ref (query);
      res = gst_pad_query (GST_BASE_VIDEO_PARSE_SINK_PAD (base_parse), query);
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
      gst_pad_query_default (pad, query);
      break;
  }
done:
  gst_object_unref (base_parse);

  return res;
}

static gboolean
gst_base_video_parse_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseVideoParse *base_video_parse;
  gboolean res = FALSE;

  base_video_parse = GST_BASE_VIDEO_PARSE (gst_pad_get_parent (pad));

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
      gst_event_ref (event);
      res =
          gst_pad_push_event (GST_BASE_VIDEO_PARSE_SINK_PAD (base_video_parse),
          event);
      if (res) {
        gst_event_unref (event);
        goto done;
      }

      gst_event_parse_seek (event, &rate, &format, &flags, &cur_type,
          &cur, &stop_type, &stop);
      gst_event_unref (event);

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
          gst_pad_push_event (GST_BASE_VIDEO_PARSE_SINK_PAD (base_video_parse),
          real_seek);

      break;
    }
#if 0
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      GST_OBJECT_LOCK (base_video_parse);
      base_video_parse->proportion = proportion;
      base_video_parse->earliest_time = timestamp + diff;
      GST_OBJECT_UNLOCK (base_video_parse);

      GST_DEBUG_OBJECT (base_video_parse,
          "got QoS %" GST_TIME_FORMAT ", %" G_GINT64_FORMAT,
          GST_TIME_ARGS (timestamp), diff);

      res = gst_pad_push_event (base_video_parse->sinkpad, event);
      break;
    }
#endif
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
done:
  gst_object_unref (base_video_parse);
  return res;

convert_error:
  GST_DEBUG_OBJECT (base_video_parse, "could not convert format");
  goto done;
}

static gboolean
gst_base_video_parse_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseVideoParse *base_video_parse;
  gboolean res = FALSE;

  base_video_parse = GST_BASE_VIDEO_PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_base_video_parse_reset (base_video_parse);
      res =
          gst_pad_push_event (GST_BASE_VIDEO_PARSE_SRC_PAD (base_video_parse),
          event);
      break;
    case GST_EVENT_EOS:
    {
      GST_DEBUG ("EOS");

      if (gst_adapter_available (base_video_parse->input_adapter)) {
        gst_base_video_parse_add_to_frame (base_video_parse,
            gst_adapter_take_buffer (base_video_parse->input_adapter,
                gst_adapter_available (base_video_parse->input_adapter)));
        gst_base_video_parse_finish_frame (base_video_parse);
      }

      res =
          gst_pad_push_event (GST_BASE_VIDEO_PARSE_SRC_PAD (base_video_parse),
          event);
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

        gst_event_unref (event);
        event = gst_event_new_new_segment (update, rate, tformat, start,
            stop, time);
      }

      GST_DEBUG ("newsegment %lld %lld", start, time);
      gst_segment_set_newsegment (&base_video_parse->state.segment, update,
          rate, GST_FORMAT_TIME, start, stop, time);

      res =
          gst_pad_push_event (GST_BASE_VIDEO_PARSE_SRC_PAD (base_video_parse),
          event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
done:
  gst_object_unref (base_video_parse);
  return res;

newseg_wrong_rate:
  GST_DEBUG_OBJECT (base_video_parse, "negative rates not supported");
  gst_event_unref (event);
  goto done;
}

static GstStateChangeReturn
gst_base_video_parse_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseVideoParse *base_parse = GST_BASE_VIDEO_PARSE (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_base_video_parse_reset (base_parse);
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
      gst_base_video_parse_reset (base_parse);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static GstFlowReturn
gst_base_video_parse_drain (GstBaseVideoParse * base_video_parse,
    gboolean at_eos)
{
  GstBaseVideoParseClass *klass;
  GstFlowReturn ret;
  gint next;

  klass = GST_BASE_VIDEO_PARSE_GET_CLASS (base_video_parse);

  if (!base_video_parse->have_sync) {
    gint n, m;

    GST_DEBUG ("no sync, scanning");

    n = gst_adapter_available (base_video_parse->input_adapter);
    m = klass->scan_for_sync (base_video_parse->input_adapter, at_eos, 0, n);

    gst_adapter_flush (base_video_parse->input_adapter, m);

    if (m < n) {
      GST_DEBUG ("found possible sync after %d bytes (of %d)", m, n);
      /* this is only "maybe" sync */
      base_video_parse->have_sync = TRUE;
    }

    if (!base_video_parse->have_sync) {
      return GST_FLOW_OK;
    }
  }

  /* FIXME check klass->parse_data */

  ret = klass->scan_for_packet_end (base_video_parse,
      base_video_parse->input_adapter, &next);
  while (ret == GST_FLOW_OK) {
    GstBuffer *buffer;

    base_video_parse->input_buffer_timestamp =
        GST_BUFFER_TIMESTAMP (base_video_parse->input_adapter->buflist->data);
    buffer = gst_adapter_take_buffer (base_video_parse->input_adapter, next);
    ret = klass->parse_data (base_video_parse, buffer);
    if (ret != GST_FLOW_OK)
      break;

    ret = klass->scan_for_packet_end (base_video_parse,
        base_video_parse->input_adapter, &next);
  }

  if (ret == GST_BASE_VIDEO_PARSE_FLOW_NEED_DATA) {
    return GST_FLOW_OK;
  }
  return ret;
}

static GstFlowReturn
gst_base_video_parse_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseVideoParse *base_video_parse;
  GstBaseVideoParseClass *klass;

  GST_DEBUG ("chain with %d bytes", GST_BUFFER_SIZE (buf));

  base_video_parse = GST_BASE_VIDEO_PARSE (GST_PAD_PARENT (pad));
  klass = GST_BASE_VIDEO_PARSE_GET_CLASS (base_video_parse);

  if (!base_video_parse->started) {
    klass->start (base_video_parse);
    base_video_parse->started = TRUE;
  }

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (base_video_parse, "received DISCONT buffer");
    gst_base_video_parse_reset (base_video_parse);
    base_video_parse->discont = TRUE;
    base_video_parse->have_sync = FALSE;
  }

  GST_DEBUG ("upstream timestamp: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
  gst_adapter_push (base_video_parse->input_adapter, buf);

  return gst_base_video_parse_drain (base_video_parse, FALSE);
}

GstVideoState *
gst_base_video_parse_get_state (GstBaseVideoParse * base_video_parse)
{
  return &base_video_parse->state;
}

void
gst_base_video_parse_set_state (GstBaseVideoParse * base_video_parse,
    GstVideoState * state)
{
  GST_DEBUG ("set_state");

  memcpy (&base_video_parse->state, state, sizeof (GstVideoState));

  /* FIXME set caps */

}

void
gst_base_video_parse_lost_sync (GstBaseVideoParse * base_video_parse)
{
  g_return_if_fail (GST_IS_BASE_VIDEO_PARSE (base_video_parse));

  GST_DEBUG ("lost_sync");

  if (gst_adapter_available (base_video_parse->input_adapter) >= 1) {
    gst_adapter_flush (base_video_parse->input_adapter, 1);
  }

  base_video_parse->have_sync = FALSE;
}

GstVideoFrame *
gst_base_video_parse_get_frame (GstBaseVideoParse * base_video_parse)
{
  g_return_val_if_fail (GST_IS_BASE_VIDEO_PARSE (base_video_parse), NULL);

  return base_video_parse->current_frame;
}

void
gst_base_video_parse_add_to_frame (GstBaseVideoParse * base_video_parse,
    GstBuffer * buffer)
{
  if (gst_adapter_available (base_video_parse->output_adapter) == 0)
    base_video_parse->upstream_timestamp =
        base_video_parse->input_buffer_timestamp;

  gst_adapter_push (base_video_parse->output_adapter, buffer);
}

GstFlowReturn
gst_base_video_parse_finish_frame (GstBaseVideoParse * base_video_parse)
{
  GstVideoFrame *frame = base_video_parse->current_frame;
  GstClockTime upstream_timestamp;
  GstBuffer *buffer;
  GstBaseVideoParseClass *base_video_parse_class;
  GstFlowReturn ret;

  GST_DEBUG ("finish_frame");

  base_video_parse_class = GST_BASE_VIDEO_PARSE_GET_CLASS (base_video_parse);

  upstream_timestamp = base_video_parse->upstream_timestamp;
  buffer =
      gst_adapter_take_buffer (base_video_parse->output_adapter,
      gst_adapter_available (base_video_parse->output_adapter));

  /* set duration */
  frame->presentation_duration = gst_util_uint64_scale (GST_SECOND,
      base_video_parse->state.fps_d, base_video_parse->state.fps_n);

  /* we prefer timestamps coming from upstream */
  if (GST_CLOCK_TIME_IS_VALID (upstream_timestamp)
      && upstream_timestamp != base_video_parse->presentation_timestamp) {
    GST_DEBUG ("Got upstream timestamp");
    frame->presentation_timestamp = base_video_parse->upstream_timestamp;
  }

  if (frame->is_sync_point) {
    if (GST_CLOCK_TIME_IS_VALID (frame->presentation_timestamp)
        && frame->presentation_frame_number != -1) {
      base_video_parse->timestamp_offset =
          frame->presentation_timestamp -
          gst_util_uint64_scale (frame->presentation_frame_number,
          base_video_parse->state.fps_d * GST_SECOND,
          base_video_parse->state.fps_n);

      base_video_parse->distance_from_sync = 0;
    }

    else
      GST_DEBUG ("subclass set frame as sync_point but didn't supply "
          "presentation_timestamp and presentation_frame_number");
  }

  /* calculate timestamp from frame number if we've got one */
  if (frame->presentation_timestamp == GST_CLOCK_TIME_NONE
      && frame->presentation_frame_number != -1) {

    frame->presentation_timestamp =
        gst_base_video_parse_get_timestamp (base_video_parse,
        frame->presentation_frame_number);
  }

  base_video_parse->presentation_timestamp = frame->presentation_timestamp;
  gst_segment_set_last_stop (&base_video_parse->state.segment, GST_FORMAT_TIME,
      base_video_parse->presentation_timestamp);

  frame->distance_from_sync = base_video_parse->distance_from_sync;
  base_video_parse->distance_from_sync++;

  frame->decode_timestamp =
      gst_base_video_parse_get_timestamp (base_video_parse,
      frame->decode_frame_number);

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
  ret = base_video_parse_class->shape_output (base_video_parse, frame);

  gst_base_video_parse_free_frame (base_video_parse->current_frame);

  /* create new frame */
  base_video_parse->current_frame =
      gst_base_video_parse_new_frame (base_video_parse);

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
gst_base_video_parse_new_frame (GstBaseVideoParse * base_video_parse)
{
  GstVideoFrame *frame;

  frame = g_malloc0 (sizeof (GstVideoFrame));

  frame->presentation_timestamp = GST_CLOCK_TIME_NONE;
  frame->presentation_frame_number = -1;

  frame->system_frame_number = base_video_parse->system_frame_number;
  base_video_parse->system_frame_number++;

  frame->decode_frame_number = frame->system_frame_number -
      base_video_parse->reorder_depth;

  return frame;
}

void
gst_base_video_parse_set_sync_point (GstBaseVideoParse * base_video_parse)
{
  GST_DEBUG ("set_sync_point");

  base_video_parse->current_frame->is_sync_point = TRUE;

  base_video_parse->distance_from_sync = 0;
}

GstFlowReturn
gst_base_video_parse_push (GstBaseVideoParse * base_video_parse,
    GstBuffer * buffer)
{
  GstBaseVideoParseClass *base_video_parse_class;

  base_video_parse_class = GST_BASE_VIDEO_PARSE_GET_CLASS (base_video_parse);

  if (base_video_parse->caps == NULL) {
    gboolean ret;

    base_video_parse->caps =
        base_video_parse_class->get_caps (base_video_parse);

    ret = gst_pad_set_caps (GST_BASE_VIDEO_PARSE_SRC_PAD (base_video_parse),
        base_video_parse->caps);

    if (!ret) {
      GST_WARNING ("pad didn't accept caps");
      return GST_FLOW_ERROR;
    }
  }
  gst_buffer_set_caps (buffer,
      GST_PAD_CAPS (GST_BASE_VIDEO_PARSE_SRC_PAD (base_video_parse)));

  GST_LOG ("pushing buffer of %u bytes ts=%" GST_TIME_FORMAT " dur=%"
      GST_TIME_FORMAT " off=%" GST_TIME_FORMAT " off_end=%" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (buffer), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
      GST_TIME_ARGS (GST_BUFFER_OFFSET (buffer)),
      GST_TIME_ARGS (GST_BUFFER_OFFSET_END (buffer)));

  if (base_video_parse->discont) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    base_video_parse->discont = FALSE;
  } else {
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
  }

  return gst_pad_push (GST_BASE_VIDEO_PARSE_SRC_PAD (base_video_parse), buffer);
}
