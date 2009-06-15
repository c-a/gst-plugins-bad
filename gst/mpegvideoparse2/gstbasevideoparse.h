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

#ifndef _GST_BASE_VIDEO_PARSE_H_
#define _GST_BASE_VIDEO_PARSE_H_

#include <gst/video/gstbasevideoutils.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_VIDEO_PARSE           (gst_base_video_parse_get_type())
#define GST_BASE_VIDEO_PARSE(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_VIDEO_PARSE,GstBaseVideoParse))
#define GST_BASE_VIDEO_PARSE_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_VIDEO_PARSE,GstBaseVideoParseClass))
#define GST_BASE_VIDEO_PARSE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_BASE_VIDEO_PARSE,GstBaseVideoParseClass))
#define GST_IS_BASE_VIDEO_PARSE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_VIDEO_PARSE))
#define GST_IS_BASE_VIDEO_PARSE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_VIDEO_PARSE))

/**
   * GST_BASE_VIDEO_PARSE_SINK_NAME:
   *
   * The name of the templates for the sink pad.
   */
#define GST_BASE_VIDEO_PARSE_SINK_NAME    "sink"
/**
   * GST_BASE_VIDEO_PARSE_SRC_NAME:
   *
   * The name of the templates for the source pad.
   */
#define GST_BASE_VIDEO_PARSE_SRC_NAME     "src"

/**
 * GST_BASE_VIDEO_PARSE_SRC_PAD:
 * @obj: base video codec instance
 *
 * Gives the pointer to the source #GstPad object of the element.
 */
#define GST_BASE_VIDEO_PARSE_SRC_PAD(obj)         (((GstBaseVideoParse *) (obj))->srcpad)

/**
 * GST_BASE_VIDEO_PARSE_SINK_PAD:
 * @obj: base video codec instance
 *
 * Gives the pointer to the sink #GstPad object of the element.
 */
#define GST_BASE_VIDEO_PARSE_SINK_PAD(obj)        (((GstBaseVideoParse *) (obj))->sinkpad)

/**
   * GST_BASE_VIDEO_PARSE_FLOW_NEED_DATA:
   *
   */
#define GST_BASE_VIDEO_PARSE_FLOW_NEED_DATA GST_FLOW_CUSTOM_SUCCESS

/**
 * GST_BASE_VIDEO_PARSE_LOCK
 * @obj base video parse instance 
 *
 * Obtain a lock to protect the parse function from concurrent access.
 */
#define GST_BASE_VIDEO_PARSE_LOCK(obj) g_mutex_lock (((GstBaseVideoParse *) (obj))->parse_lock)

/**
 * GST_BASE_VIDEO_PARSE_UNLOCK
 * @obj base video parse instance 
 *
 * Release the lock that protects the parse function from concurrent access.
 */
#define GST_BASE_VIDEO_PARSE_UNLOCK(obj) g_mutex_unlock (((GstBaseVideoParse *) (obj))->parse_lock)

typedef struct _GstBaseVideoParse GstBaseVideoParse;
typedef struct _GstBaseVideoParseClass GstBaseVideoParseClass;

struct _GstBaseVideoParse
{
  GstElement element;

  /*< private >*/
  GstPad *sinkpad;
  GstPad *srcpad;

  GstCaps *caps;
  GSList *pending_segs;
  
  GstAdapter *input_adapter;
  GstAdapter *output_adapter;

  GstVideoState state;
  
  gint reorder_depth;
  gint64 timestamp_offset;

  gboolean have_sync;
  gboolean discont;
  gboolean eos;

  GstVideoFrame *current_frame;
  gint distance_from_sync;

  guint64 presentation_timestamp;
  guint64 system_frame_number;
  guint64 next_offset;

  GstClockTime upstream_timestamp;

  GstClockTime duration;
  GstFormat duration_fmt;

  gboolean need_newsegment;
  GMutex *parse_lock;
};

/**
 * GstBaseVideoParseClass:
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
 *                       Determine what should be done with the current package,
 *                       e.g. push it, drop it, cache for reverse playback etc.
 * @get_caps             Should return the caps that should be set on the src pad
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
 * @get_caps needs to be overridden.
 */
struct _GstBaseVideoParseClass
{
  GstElementClass element_class;

  gboolean      (*start)               (GstBaseVideoParse *parse);
  gboolean      (*stop)                (GstBaseVideoParse *parse);

  void          (*flush)               (GstBaseVideoParse *parse);

  gint          (*scan_for_sync)       (GstAdapter *adapter, gboolean at_eos,
                                        gint offset, gint n);
  
  GstFlowReturn (*scan_for_packet_end) (GstBaseVideoParse *parse,
                                        GstAdapter *adapter,
                                        gint *size);
  
  GstFlowReturn (*parse_data)          (GstBaseVideoParse *parse,
                                        GstBuffer *buffer);

  GstFlowReturn (*shape_output)        (GstBaseVideoParse *parse,
                                        GstVideoFrame *frame);
  
  GstCaps      *(*get_caps)            (GstBaseVideoParse *parse);

  gboolean      (*convert)             (GstBaseVideoParse *parse,
                                        GstFormat src_format,
                                        gint64 src_value,
                                        GstFormat dest_format,
                                        gint64 * dest_value);

  gboolean      (*sink_event)          (GstBaseVideoParse *parse,
                                        GstEvent *event);

  gboolean      (*src_event)           (GstBaseVideoParse *parse,
                                        GstEvent *event);
};

GType gst_base_video_parse_get_type (void);

GstVideoState  gst_base_video_parse_get_state      (GstBaseVideoParse *parse);
void           gst_base_video_parse_set_state      (GstBaseVideoParse *parse,
                                                    GstVideoState state);

void           gst_base_video_parse_set_duration   (GstBaseVideoParse *parse, 
                                                    GstFormat format,
                                                    gint64 duration);

void           gst_base_video_parse_lost_sync      (GstBaseVideoParse *parse);

GstVideoFrame *gst_base_video_parse_get_frame      (GstBaseVideoParse *base_video_parse);
void           gst_base_video_parse_add_to_frame   (GstBaseVideoParse *base_video_parse,
                                                    GstBuffer *buffer);
GstFlowReturn  gst_base_video_parse_finish_frame   (GstBaseVideoParse *base_video_parse);

void           gst_base_video_parse_set_sync_point (GstBaseVideoParse *base_video_parse);
GstFlowReturn  gst_base_video_parse_push           (GstBaseVideoParse *base_video_parse,
                                                    GstBuffer *buffer);

G_END_DECLS

#endif

