/* 
 * GStreamer
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

#ifndef _SAT_VIDEO_FRAME_H_
#define _SAT_VIDEO_FRAME_H_

#include <gst/gst.h>

typedef struct _SatVideoFrame SatVideoFrame;
typedef struct _SatVideoFrameClass SatVideoFrameClass;

#define SAT_TYPE_VIDEO_FRAME (sat_video_frame_get_type())
#define SAT_IS_VIDEO_FRAME(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SAT_TYPE_VIDEO_FRAME))
#define SAT_VIDEO_FRAME(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SAT_TYPE_VIDEO_FRAME, SatVideoFrame))

typedef enum
{
  SAT_VIDEO_FRAME_FLAG_PREROLL = (GST_MINI_OBJECT_FLAG_LAST << 0),
  SAT_VIDEO_FRAME_FLAG_DISCONT = (GST_MINI_OBJECT_FLAG_LAST << 1),
  SAT_VIDEO_FRAME_FLAG_GAP = (GST_MINI_OBJECT_FLAG_LAST << 2),
  SAT_VIDEO_FRAME_FLAG_KEYFRAME = (GST_MINI_OBJECT_FLAG_LAST << 3),
  SAT_VIDEO_FRAME_FLAG_SYNC_POINT = (GST_MINI_OBJECT_FLAG_LAST << 4),
  SAT_VIDEO_FRAME_FLAG_EOS = (GST_MINI_OBJECT_FLAG_LAST << 5),
  SAT_VIDEO_FRAME_FLAG_USER1 = (GST_MINI_OBJECT_FLAG_LAST << 6),
  SAT_VIDEO_FRAME_FLAG_USER2 = (GST_MINI_OBJECT_FLAG_LAST << 7),
  SAT_VIDEO_FRAME_FLAG_USER3 = (GST_MINI_OBJECT_FLAG_LAST << 8),
  SAT_VIDEO_FRAME_FLAG_LAST = (GST_MINI_OBJECT_FLAG_LAST << 9)
} SatVideoFrameFlag;

void sat_video_frame_unset_flag (SatVideoFrame *frame, SatVideoFrameFlag flag);
void sat_video_frame_set_flag (SatVideoFrame *frame, SatVideoFrameFlag flag);
gboolean sat_video_frame_flag_is_set (SatVideoFrame *frame, SatVideoFrameFlag flag);

gboolean sat_video_frame_empty (SatVideoFrame *frame);
void sat_video_frame_add_buffer (SatVideoFrame *frame, GstBuffer *buf);
void sat_video_frame_set_caps (SatVideoFrame *frame, GstCaps *caps);

void sat_video_frame_set_timestamp (SatVideoFrame *frame, GstClockTime timestamp);
GstClockTime sat_video_frame_get_timestamp (SatVideoFrame *frame);

void sat_video_frame_set_duration (SatVideoFrame *frame, GstClockTime duration);
GstClockTime sat_video_frame_get_duration (SatVideoFrame *frame);

void sat_video_frame_set_frame_nr (SatVideoFrame *frame, guint64 frame_nr,
    guint64 total_frames);
guint64 sat_video_frame_get_frame_nr (SatVideoFrame *frame);

GstClockTime sat_video_frame_get_upstream_timestamp (SatVideoFrame *frame);
GstClockTime sat_video_frame_get_upstream_duration (SatVideoFrame *frame);
guint64 sat_video_frame_get_upstream_offset (SatVideoFrame *frame);

void sat_video_frame_set_distance_from_sync (SatVideoFrame *frame, guint64 distance_from_sync);
guint64 sat_video_frame_get_distance_from_sync (SatVideoFrame *frame);

GstBufferList *sat_video_frame_get_buffer_list (SatVideoFrame *frame);

SatVideoFrame *sat_video_frame_new ();

GType sat_video_frame_get_type (void);

#endif