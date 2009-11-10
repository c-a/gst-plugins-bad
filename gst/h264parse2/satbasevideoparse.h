/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
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

#ifndef _SAT_BASE_VIDEO_PARSE_H_
#define _SAT_BASE_VIDEO_PARSE_H_

#define GST_USE_UNSTABLE_API

#include <gst/video/gstbasevideoutils.h>

#include "satvideoframe.h"

G_BEGIN_DECLS

#define GST_TYPE_BASE_VIDEO_PARSE           (sat_base_video_parse_get_type())
#define SAT_BASE_VIDEO_PARSE(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_VIDEO_PARSE,SatBaseVideoParse))
#define SAT_BASE_VIDEO_PARSE_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_VIDEO_PARSE,SatBaseVideoParseClass))
#define SAT_BASE_VIDEO_PARSE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_BASE_VIDEO_PARSE,SatBaseVideoParseClass))
#define SAT_IS_BASE_VIDEO_PARSE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_VIDEO_PARSE))
#define SAT_IS_BASE_VIDEO_PARSE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE((klass),SAT_TYPE_BASE_VIDEO_PARSE))

/**
   * SAT_BASE_VIDEO_PARSE_SINK_NAME:
   *
   * The name of the templates for the sink pad.
   */
#define SAT_BASE_VIDEO_PARSE_SINK_NAME    "sink"
/**
   * SAT_BASE_VIDEO_PARSE_SRC_NAME:
   *
   * The name of the templates for the source pad.
   */
#define SAT_BASE_VIDEO_PARSE_SRC_NAME     "src"

/**
 * SAT_BASE_VIDEO_PARSE_SRC_PAD:
 * @obj: base video codec instance
 *
 * Gives the pointer to the source #GstPad object of the element.
 */
#define SAT_BASE_VIDEO_PARSE_SRC_PAD(obj)         (((SatBaseVideoParse *) (obj))->srcpad)

/**
 * SAT_BASE_VIDEO_PARSE_SINK_PAD:
 * @obj: base video codec instance
 *
 * Gives the pointer to the sink #GstPad object of the element.
 */
#define SAT_BASE_VIDEO_PARSE_SINK_PAD(obj)        (((SatBaseVideoParse *) (obj))->sinkpad)

/**
   * SAT_BASE_VIDEO_PARSE_FLOW_NEED_DATA:
   *
   */
#define SAT_BASE_VIDEO_PARSE_FLOW_NEED_DATA GST_FLOW_CUSTOM_SUCCESS

/**
 * SAT_BASE_VIDEO_PARSE_LOCK
 * @obj base video parse instance 
 *
 * Obtain a lock to protect the parse function from concurrent access.
 */
#define SAT_BASE_VIDEO_PARSE_LOCK(obj) g_mutex_lock (((SatBaseVideoParse *) (obj))->parse_lock)

/**
 * SAT_BASE_VIDEO_PARSE_UNLOCK
 * @obj base video parse instance 
 *
 * Release the lock that protects the parse function from concurrent access.
 */
#define SAT_BASE_VIDEO_PARSE_UNLOCK(obj) g_mutex_unlock (((SatBaseVideoParse *) (obj))->parse_lock)

/**
 * SAT_BASE_VIDEO_PARSE_FRAME_LOCK
 * @obj base video parse instance 
 *
 * Obtain a lock to protect the frame from concurrent access.
 */
#define SAT_BASE_VIDEO_PARSE_FRAME_LOCK(obj) g_mutex_lock (((SatBaseVideoParse *) (obj))->frame_lock)

/**
 * SAT_BASE_VIDEO_PARSE_FRAME_UNLOCK
 * @obj base video parse instance 
 *
 * Release the lock that protects the frame from concurrent access.
 */
#define SAT_BASE_VIDEO_PARSE_FRAME_UNLOCK(obj) g_mutex_unlock (((SatBaseVideoParse *) (obj))->frame_lock)

typedef enum _SatBaseVideoParseScanResult SatBaseVideoParseScanResult;

enum _SatBaseVideoParseScanResult
{
  SAT_BASE_VIDEO_PARSE_SCAN_RESULT_OK,
  SAT_BASE_VIDEO_PARSE_SCAN_RESULT_LOST_SYNC,
  SAT_BASE_VIDEO_PARSE_SCAN_RESULT_NEED_DATA
};

typedef struct _SatBaseVideoParseFrame SatBaseVideoParseFrame;

typedef struct _SatBaseVideoParse SatBaseVideoParse;
typedef struct _SatBaseVideoParseClass SatBaseVideoParseClass;

struct _SatBaseVideoParse
{
  GstElement element;

  /*< private >*/
  GstPad *sinkpad;
  GstPad *srcpad;

  GSList *pending_segs;
  
  GstAdapter *input_adapter;

  GstVideoState state;

  GstIndex *index;
  gint index_id;
  
  gint reorder_depth;
  gint64 timestamp_offset;

  gboolean have_sync;

  SatVideoFrame *frame;
  GMutex *frame_lock;
  gint distance_from_sync;

  guint64 next_offset;

  GstClockTime upstream_timestamp;
  guint64 byte_offset;

  GstClockTime duration;
  GstFormat duration_fmt;

  gboolean need_newsegment;
  GstClockTime seek_timestamp;
  GMutex *parse_lock;
};

/**
 * SatBaseVideoParseClass:
 * @start:               Optional.
 *                       Called when the element starts processing.
 *                       Allows opening external resources.
 * @stop:                Optional.
 *                       Called when the element stops processing.
 *                       Allows closing external resources.
 * @flush                Optional.
 *                       Called when the elements state should be flushed
 * @scan_for_sync:       Called when the element needs to find the start of a packet.
 *                       Should return the size of the data before the firs
 *                       packet.
 * @scan_for_packet_end: Should determine the size of the packet. Should also
 *                       detect if we've lost sync.
 * @parse_data:          Parse the detected packet to determine what should be
 *                       done with it.
 * @shape_output:        Optional.
 *                       Determine what should be done with the current packet,
 *                       e.g. push it, drop it, cache for reverse playback etc.
 * @set_sink_caps        allows the subclass to be notified of the actual caps set.
 * @get_base_caps        Should return the base caps that should be set on the src pad
 * @convert:             Optional.
 *                       Convert between formats.
 * @sink_event:          Optional.
 *                       Event handler on the sink pad. This function should return
 *                       TRUE if the event was handled and can be dropped.
 * @src_event:           Optional.
 *                       Event handler on the source pad. Should return TRUE
 *                       if the event was handled and can be dropped.
 *
 * Subclasses can override any of the available virtual methods or not, as
 * needed. At minimum @scan_for_sync, @scan_for_packet_end, @parse_data and
 * @get_base_caps needs to be overridden.
 */
struct _SatBaseVideoParseClass
{
  GstElementClass element_class;

  gboolean      (*start)               (SatBaseVideoParse *parse);
  gboolean      (*stop)                (SatBaseVideoParse *parse);

  void          (*flush)               (SatBaseVideoParse *parse);

  gint          (*scan_for_sync)       (SatBaseVideoParse *parse,
                                        GstAdapter *adapter);
  
  SatBaseVideoParseScanResult (*scan_for_packet_end) (SatBaseVideoParse *parse,
                                                      GstAdapter *adapter,
                                                      guint *size);
  
  GstFlowReturn (*parse_data)          (SatBaseVideoParse *parse,
                                        GstBuffer *buffer);

  GstFlowReturn (*shape_output)        (SatBaseVideoParse *parse,
                                        SatVideoFrame *frame);

  gboolean      (*set_sink_caps)       (SatBaseVideoParse *parse,
                                        GstCaps *caps);
  GstCaps      *(*get_base_caps)       (SatBaseVideoParse *parse);

  gboolean      (*convert)             (SatBaseVideoParse *parse,
                                        GstFormat src_format,
                                        gint64 src_value,
                                        GstFormat dest_format,
                                        gint64 * dest_value);

  gboolean      (*sink_event)          (SatBaseVideoParse *parse,
                                        GstEvent *event);

  gboolean      (*src_event)           (SatBaseVideoParse *parse,
                                        GstEvent *event);
};

GType sat_base_video_parse_get_type (void);

GstVideoState  sat_base_video_parse_get_state         (SatBaseVideoParse *parse);
void           sat_base_video_parse_set_state         (SatBaseVideoParse *parse,
                                                       GstVideoState state);

void           sat_base_video_parse_set_duration      (SatBaseVideoParse *parse, 
                                                       GstFormat format,
                                                       gint64 duration);

void           sat_base_video_parse_lost_sync         (SatBaseVideoParse *parse);

SatVideoFrame *sat_base_video_parse_get_current_frame (SatBaseVideoParse *parse);
GstFlowReturn  sat_base_video_parse_finish_frame      (SatBaseVideoParse *parse);

GstFlowReturn  sat_base_video_parse_push              (SatBaseVideoParse *parse,
                                                       SatVideoFrame *frame);

G_END_DECLS

#endif

