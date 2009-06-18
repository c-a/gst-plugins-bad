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
static void gst_base_video_parse_set_index (GstElement * element,
    GstIndex * index);
static GstIndex *gst_base_video_parse_get_index (GstElement * element);
static GstStateChangeReturn gst_base_video_parse_change_state (GstElement *
    element, GstStateChange transition);

/* GstPad */
static const GstQueryType *gst_base_video_parse_get_query_types (GstPad * pad);
static gboolean gst_base_video_parse_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_base_video_parse_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_base_video_parse_sink_event (GstPad * pad,
    GstEvent * event);
static GstFlowReturn gst_base_video_parse_chain (GstPad * pad, GstBuffer * buf);

/* Utility */
static gboolean
gst_base_video_parse_index_find_offset (GstBaseVideoParse * parse,
    gint64 time, gint64 * offset);
static void gst_base_video_parse_flush (GstBaseVideoParse * parse);
static gboolean
gst_base_video_parse_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_base_video_parse_start (GstBaseVideoParse * parse);
static gboolean gst_base_video_parse_stop (GstBaseVideoParse * parse);
static GstBaseVideoParseFrame *gst_base_video_parse_new_frame (GstBaseVideoParse
    * parse);
static void gst_base_video_parse_free_frame (GstBaseVideoParseFrame * frame);

static GstFlowReturn gst_base_video_parse_drain (GstBaseVideoParse * parse,
    gboolean at_eos);

static GstClockTime
gst_base_video_parse_get_timestamp (GstBaseVideoParse * parse,
    gint64 picture_number);

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

  element_class->set_index = gst_base_video_parse_set_index;
  element_class->get_index = gst_base_video_parse_get_index;
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
  if (parse->index) {
    gst_object_unref (parse->index);
  }

  g_mutex_free (parse->parse_lock);

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

  parse->index = NULL;

  parse->parse_lock = g_mutex_new ();
}

/*
 * Public functions
 */
GstVideoState
gst_base_video_parse_get_state (GstBaseVideoParse * parse)
{
  return parse->state;
}

void
gst_base_video_parse_set_state (GstBaseVideoParse * parse, GstVideoState state)
{
  GstBaseVideoParseClass *klass;
  GstCaps *caps;

  g_return_if_fail (GST_IS_BASE_VIDEO_PARSE (parse));

  klass = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);

  GST_DEBUG ("set_state");
  parse->state = state;

  caps = klass->get_base_caps (parse);
  if (!caps) {
    GST_WARNING ("Subclass returned null caps");
    return;
  }

  gst_caps_set_simple (caps,
      "width", G_TYPE_INT, state.width,
      "height", G_TYPE_INT, state.height,
      "framerate", GST_TYPE_FRACTION, state.fps_n, state.fps_d,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, state.par_n, state.par_d,
      "interlaced", G_TYPE_BOOLEAN, state.interlaced, NULL);

  if (!gst_caps_is_equal (caps, GST_PAD_CAPS (parse->srcpad))) {
    if (!gst_pad_set_caps (parse->srcpad, caps))
      GST_WARNING ("Couldn't set caps: %" GST_PTR_FORMAT, caps);
  }

  gst_caps_unref (caps);
}

void
gst_base_video_parse_set_duration (GstBaseVideoParse * parse,
    GstFormat format, gint64 duration)
{
  g_return_if_fail (GST_IS_BASE_VIDEO_PARSE (parse));

  GST_DEBUG ("set_duration");

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
gst_base_video_parse_frame_add (GstBaseVideoParse * parse, GstBuffer * buffer)
{
  g_return_if_fail (GST_IS_BASE_VIDEO_PARSE (parse));

  if (gst_adapter_available (parse->output_adapter) == 0) {
    parse->current_frame->upstream_timestamp = parse->upstream_timestamp;
    parse->current_frame->byte_offset = parse->byte_offset;
  }

  gst_adapter_push (parse->output_adapter, buffer);
}

GstFlowReturn
gst_base_video_parse_frame_finish (GstBaseVideoParse * parse)
{
  GstBaseVideoParseFrame *frame;
  GstBuffer *buffer;
  GstBaseVideoParseClass *parse_class;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_BASE_VIDEO_PARSE (parse), GST_FLOW_OK);

  GST_DEBUG ("finish_frame");

  frame = parse->current_frame;
  parse_class = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);

  if (gst_adapter_available (parse->output_adapter)) {
    buffer =
        gst_adapter_take_buffer (parse->output_adapter,
        gst_adapter_available (parse->output_adapter));

    if (!GST_CLOCK_TIME_IS_VALID (frame->presentation_duration)) {
      frame->presentation_duration = gst_util_uint64_scale (GST_SECOND,
          parse->state.fps_d, parse->state.fps_n);
    }

    if (GST_CLOCK_TIME_IS_VALID (frame->upstream_timestamp))
      frame->presentation_timestamp = frame->upstream_timestamp;

    if (frame->is_sync_point) {
      parse->timestamp_offset =
          frame->presentation_timestamp -
          gst_util_uint64_scale (frame->presentation_frame_number,
          parse->state.fps_d * GST_SECOND, parse->state.fps_n);

      parse->distance_from_sync = 0;
    }

    /* calculate timestamp from frame number if we've got one */
    if (frame->presentation_timestamp == GST_CLOCK_TIME_NONE
        && frame->presentation_frame_number != -1) {

      frame->presentation_timestamp =
          gst_base_video_parse_get_timestamp (parse,
          frame->presentation_frame_number);
    }

    parse->presentation_timestamp = frame->presentation_timestamp;

    frame->distance_from_sync = parse->distance_from_sync;
    parse->distance_from_sync++;

    frame->decode_timestamp =
        gst_base_video_parse_get_timestamp (parse, frame->decode_frame_number);

    /* add index association */
    if (parse->index && frame->byte_offset != GST_CLOCK_TIME_NONE &&
        GST_CLOCK_TIME_IS_VALID (frame->presentation_timestamp)) {
      gst_index_add_association (parse->index, parse->index_id,
          frame->is_keyframe ? GST_ASSOCIATION_FLAG_KEY_UNIT :
          GST_ASSOCIATION_FLAG_NONE,
          GST_FORMAT_TIME, frame->presentation_timestamp,
          GST_FORMAT_BYTES, frame->byte_offset, NULL);
    }

    GST_BUFFER_TIMESTAMP (buffer) = frame->presentation_timestamp;
    GST_BUFFER_DURATION (buffer) = frame->presentation_duration;
    if (frame->decode_frame_number < 0) {
      GST_BUFFER_OFFSET (buffer) = 0;
    } else {
      GST_BUFFER_OFFSET (buffer) = frame->decode_timestamp;
    }
    GST_BUFFER_OFFSET_END (buffer) = GST_CLOCK_TIME_NONE;

    if (frame->is_keyframe) {
      GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    } else {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    }

    if (frame->is_discont) {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    } else {
      GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
    }

    frame->buffer = buffer;
    if (parse_class->shape_output)
      ret = parse_class->shape_output (parse, frame);
    else
      ret = gst_base_video_parse_push (parse, buffer);
  }

  gst_base_video_parse_free_frame (parse->current_frame);

  /* create new frame */
  parse->current_frame = gst_base_video_parse_new_frame (parse);

  return ret;
}

void
gst_base_video_parse_frame_set_timestamp (GstBaseVideoParse * parse,
    GstClockTime timestamp)
{
  g_return_if_fail (GST_IS_BASE_VIDEO_PARSE (parse));
  g_return_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp));

  parse->current_frame->presentation_timestamp = timestamp;
}

void
gst_base_video_parse_frame_set_frame_nr (GstBaseVideoParse * parse,
    guint64 frame_number)
{
  g_return_if_fail (GST_IS_BASE_VIDEO_PARSE (parse));
  g_return_if_fail (frame_number != -1);

  parse->current_frame->presentation_frame_number = frame_number;
}

void
gst_base_video_parse_frame_set_keyframe (GstBaseVideoParse * parse)
{
  g_return_if_fail (GST_IS_BASE_VIDEO_PARSE (parse));

  parse->current_frame->is_keyframe = TRUE;
}

void
gst_base_video_parse_set_sync_point (GstBaseVideoParse * parse)
{
  g_return_if_fail (GST_IS_BASE_VIDEO_PARSE (parse));

  parse->current_frame->is_sync_point = TRUE;

  /* if subclass hasn't explicitly set the timestamp and frame_number
   * we assume that they're both zero */
  if (parse->current_frame->presentation_timestamp == GST_CLOCK_TIME_NONE)
    parse->current_frame->presentation_timestamp = 0;
  if (parse->current_frame->presentation_frame_number == -1)
    parse->current_frame->presentation_frame_number = 0;

  parse->distance_from_sync = 0;
}

GstFlowReturn
gst_base_video_parse_push (GstBaseVideoParse * parse, GstBuffer * buffer)
{
  GstBaseVideoParseClass *parse_class;

  g_return_val_if_fail (GST_IS_BASE_VIDEO_PARSE (parse), GST_FLOW_OK);

  parse_class = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);

  GST_BASE_VIDEO_PARSE_LOCK (parse);

  gst_base_video_parse_send_pending_segs (parse);

  gst_segment_set_last_stop (&parse->state.segment,
      GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (buffer));

  if (parse->need_newsegment) {
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
      GstEvent *event;
      event =
          gst_event_new_new_segment (FALSE, parse->state.segment.rate,
          GST_FORMAT_TIME, parse->state.segment.last_stop,
          parse->state.segment.stop, parse->state.segment.last_stop);
      gst_pad_push_event (parse->srcpad, event);
      parse->need_newsegment = FALSE;
    } else {
      gst_buffer_unref (buffer);
      GST_BASE_VIDEO_PARSE_UNLOCK (parse);
      return GST_FLOW_OK;
    }
  }
  GST_BASE_VIDEO_PARSE_UNLOCK (parse);

  gst_buffer_set_caps (buffer,
      GST_PAD_CAPS (GST_BASE_VIDEO_PARSE_SRC_PAD (parse)));

  GST_LOG ("pushing buffer of %u bytes ts=%" GST_TIME_FORMAT " dur=%"
      GST_TIME_FORMAT " off=%" GST_TIME_FORMAT " off_end=%" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (buffer), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
      GST_TIME_ARGS (GST_BUFFER_OFFSET (buffer)),
      GST_TIME_ARGS (GST_BUFFER_OFFSET_END (buffer)));

  return gst_pad_push (GST_BASE_VIDEO_PARSE_SRC_PAD (parse), buffer);
}

/*
 * GstElement functions
 */
static void
gst_base_video_parse_set_index (GstElement * element, GstIndex * index)
{
  GstBaseVideoParse *parse = GST_BASE_VIDEO_PARSE (element);

  GST_OBJECT_LOCK (parse);
  if (parse->index)
    gst_object_unref (parse->index);
  parse->index = gst_object_ref (index);
  GST_OBJECT_UNLOCK (parse);

  gst_index_get_writer_id (index, GST_OBJECT (element), &parse->index_id);
}

static GstIndex *
gst_base_video_parse_get_index (GstElement * element)
{
  GstBaseVideoParse *parse = GST_BASE_VIDEO_PARSE (element);
  GstIndex *result = NULL;

  GST_OBJECT_LOCK (parse);
  if (parse->index)
    result = gst_object_ref (parse->index);
  GST_OBJECT_UNLOCK (parse);

  return result;
}

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

      if (res) {
        GST_DEBUG ("Duration: %" GST_TIME_FORMAT, GST_TIME_ARGS (duration));
        gst_query_set_duration (query, format, duration);
      }

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
  GstBaseVideoParse *parse = GST_BASE_VIDEO_PARSE (gst_pad_get_parent (pad));
  GstBaseVideoParseClass *klass = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);
  gboolean res;

  if (klass->src_event && (res == klass->src_event (parse, event)))
    goto done;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstFormat format;
      gdouble rate;
      GstEvent *real_seek;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      GstSegment seeksegment;
      gint64 offset;
      gboolean update;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type,
          &start, &stop_type, &stop);

      /* see if upstream can handle it */
      res = gst_pad_push_event (parse->sinkpad, event);
      if (res)
        goto done;

      if (format != GST_FORMAT_TIME) {
        res = FALSE;
        goto done;
      }

      /* Work on a copy until we are sure the seek succeeded. */
      memcpy (&seeksegment, &parse->state.segment, sizeof (GstSegment));
      gst_segment_set_seek (&seeksegment, rate, format, flags, start_type,
          start, stop_type, stop, &update);

      if (update) {
        if (!gst_base_video_parse_index_find_offset (parse, seeksegment.start,
                &offset)) {
          GstFormat tformat;

          tformat = GST_FORMAT_BYTES;
          res = gst_base_video_parse_convert (pad, format, seeksegment.start,
              &tformat, &offset);
          if (!res)
            goto convert_error;
        }

        real_seek = gst_event_new_seek (rate, GST_FORMAT_BYTES,
            flags, GST_SEEK_TYPE_SET, offset, GST_SEEK_TYPE_NONE, 0);

        res = gst_pad_push_event (parse->sinkpad, real_seek);
      } else
        res = TRUE;

      if (res) {
        GST_BASE_VIDEO_PARSE_LOCK (parse);
        memcpy (&parse->state.segment, &seeksegment, sizeof (GstSegment));
        parse->need_newsegment = TRUE;
        GST_BASE_VIDEO_PARSE_UNLOCK (parse);
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

convert_error:
  GST_DEBUG_OBJECT (parse, "could not convert format");
  goto done;
}

static gboolean
gst_base_video_parse_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseVideoParse *parse = GST_BASE_VIDEO_PARSE (gst_pad_get_parent (pad));
  GstBaseVideoParseClass *klass = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);
  gboolean res;

  if (klass->sink_event && (res = klass->sink_event (parse, event)))
    goto done;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_base_video_parse_flush (parse);
      res = gst_pad_push_event (parse->srcpad, event);
      break;
    case GST_EVENT_EOS:
    {
      GST_DEBUG ("EOS");

      parse->current_frame->is_eos = TRUE;
      gst_base_video_parse_flush (parse);

      if (parse->index)
        gst_index_commit (parse->index, parse->index_id);

      res = gst_pad_event_default (pad, event);
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

      if (format == GST_FORMAT_TIME) {
        gst_segment_set_newsegment (&parse->state.segment, update,
            rate, GST_FORMAT_TIME, start, stop, time);

        if (gst_pad_is_linked (parse->srcpad))
          res = gst_pad_push_event (parse->srcpad, event);
        else {
          parse->pending_segs = g_slist_append (parse->pending_segs, event);
          res = TRUE;
        }
      } else {
        GST_BASE_VIDEO_PARSE_LOCK (parse);
        parse->need_newsegment = TRUE;
        GST_BASE_VIDEO_PARSE_UNLOCK (parse);

        gst_event_unref (event);
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
  res = TRUE;
  goto done;
}

/*
 * Utility functions
 */

static gboolean
gst_base_video_parse_index_find_offset (GstBaseVideoParse * parse,
    gint64 time, gint64 * offset)
{
  gint64 bytes;
  gint64 found_time;
  gboolean res = FALSE;

  if (parse->index) {
    GstIndexEntry *entry;

    /* Let's check if we have an index entry for that seek time */
    entry = gst_index_get_assoc_entry (parse->index, parse->index_id,
        GST_INDEX_LOOKUP_BEFORE, GST_ASSOCIATION_FLAG_KEY_UNIT,
        GST_FORMAT_TIME, time);

    if (entry) {
      gst_index_entry_assoc_map (entry, GST_FORMAT_BYTES, &bytes);
      gst_index_entry_assoc_map (entry, GST_FORMAT_TIME, &found_time);

      GST_DEBUG_OBJECT (parse, "found index entry for %" GST_TIME_FORMAT
          " at %" GST_TIME_FORMAT ", seeking to %" G_GINT64_FORMAT,
          GST_TIME_ARGS (time), GST_TIME_ARGS (found_time), bytes);

      res = TRUE;
    } else {
      GST_DEBUG_OBJECT (parse, "no index entry found for %" GST_TIME_FORMAT,
          GST_TIME_ARGS (time));
    }
  }

  if (res)
    *offset = bytes;

  return res;
}

static void
gst_base_video_parse_flush (GstBaseVideoParse * parse)
{
  GstBaseVideoParseClass *klass = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);

  if (gst_adapter_available (parse->input_adapter)) {
    gst_base_video_parse_frame_add (parse,
        gst_adapter_take_buffer (parse->input_adapter,
            gst_adapter_available (parse->input_adapter)));
  }

  gst_base_video_parse_frame_finish (parse);
  parse->current_frame->is_discont = TRUE;

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
  GstBaseVideoParseFrame *frame = parse->current_frame;

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

static void
gst_base_video_parse_init_state (GstVideoState * state)
{
  state->width = 0;
  state->height = 0;

  state->fps_n = 0;
  state->fps_d = 1;

  state->par_n = 0;
  state->par_d = 1;

  state->interlaced = FALSE;
}

static gboolean
gst_base_video_parse_start (GstBaseVideoParse * parse)
{
  GstBaseVideoParseClass *klass = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);
  gboolean res = TRUE;

  gst_base_video_parse_init_state (&parse->state);

  parse->have_sync = FALSE;

  parse->system_frame_number = 0;
  parse->presentation_timestamp = 0;
  parse->next_offset = GST_BUFFER_OFFSET_NONE;

  parse->reorder_depth = 1;

  parse->duration = GST_CLOCK_TIME_NONE;

  parse->pending_segs = NULL;

  gst_segment_init (&parse->state.segment, GST_FORMAT_TIME);

  parse->current_frame = gst_base_video_parse_new_frame (parse);

  parse->need_newsegment = FALSE;

  if (klass->start)
    res = klass->start (parse);

  return res;
}

static gboolean
gst_base_video_parse_stop (GstBaseVideoParse * parse)
{
  GstBaseVideoParseClass *klass = GST_BASE_VIDEO_PARSE_GET_CLASS (parse);
  gboolean res = TRUE;

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

    parse->upstream_timestamp =
        GST_BUFFER_TIMESTAMP (parse->input_adapter->buflist->data);
    parse->byte_offset =
        GST_BUFFER_OFFSET (parse->input_adapter->buflist->data);
    buffer = gst_adapter_take_buffer (parse->input_adapter, next);
    ret = klass->parse_data (parse, buffer);
    if (ret != GST_FLOW_OK)
      break;

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
gst_base_video_parse_free_frame (GstBaseVideoParseFrame * frame)
{
  g_free (frame);
}

static GstBaseVideoParseFrame *
gst_base_video_parse_new_frame (GstBaseVideoParse * parse)
{
  GstBaseVideoParseFrame *frame;

  frame = g_malloc0 (sizeof (GstBaseVideoParseFrame));

  frame->upstream_timestamp = GST_CLOCK_TIME_NONE;
  frame->byte_offset = GST_BUFFER_OFFSET_NONE;

  frame->decode_timestamp = GST_CLOCK_TIME_NONE;

  frame->presentation_timestamp = GST_CLOCK_TIME_NONE;
  frame->presentation_duration = GST_CLOCK_TIME_NONE;
  frame->presentation_frame_number = -1;

  frame->system_frame_number = parse->system_frame_number;
  parse->system_frame_number++;

  frame->decode_frame_number = frame->system_frame_number -
      parse->reorder_depth;

  frame->is_sync_point = FALSE;
  frame->is_eos = FALSE;
  frame->is_keyframe = FALSE;
  frame->is_discont = FALSE;

  frame->buffer = NULL;

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
