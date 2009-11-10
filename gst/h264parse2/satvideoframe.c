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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "satvideoframe.h"

GST_DEBUG_CATEGORY_STATIC (sat_video_frame_debug);
#define GST_CAT_DEFAULT sat_video_frame_debug

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (sat_video_frame_debug, "satvideoframe", 0, "Video Frame");

struct _SatVideoFrame
{
  GstMiniObject mini_object;

  GstBufferList *buffer_list;
  GstBufferListIterator *it;

  GstCaps *caps;
  guint flags;

  GstClockTime upstream_timestamp;
  GstClockTime upstream_duration;
  guint64 upstream_offset;

  GstClockTime timestamp;
  GstClockTime duration;
  guint64 frame_nr;
  guint64 frame_nr_end;

  guint64 distance_from_sync;
};

struct _SatVideoFrameClass
{
  GstMiniObjectClass mini_object_class;
};

void
sat_video_frame_unset_flag (SatVideoFrame * frame, SatVideoFrameFlag flag)
{
  g_return_if_fail (SAT_IS_VIDEO_FRAME (frame));

  frame->flags &= ~flag;
}

void
sat_video_frame_set_flag (SatVideoFrame * frame, SatVideoFrameFlag flag)
{
  g_return_if_fail (SAT_IS_VIDEO_FRAME (frame));

  frame->flags |= flag;
}

gboolean
sat_video_frame_flag_is_set (SatVideoFrame * frame, SatVideoFrameFlag flag)
{
  g_return_val_if_fail (SAT_IS_VIDEO_FRAME (frame), FALSE);

  return frame->flags & flag;
}

void
sat_video_frame_set_caps (SatVideoFrame * frame, GstCaps * caps)
{
  g_return_if_fail (SAT_IS_VIDEO_FRAME (frame));
  g_return_if_fail (GST_IS_CAPS (caps));

  if (frame->caps)
    gst_caps_unref (frame->caps);

  frame->caps = gst_caps_ref (caps);
}

gboolean
sat_video_frame_empty (SatVideoFrame * frame)
{
  g_return_val_if_fail (SAT_IS_VIDEO_FRAME (frame), TRUE);

  return frame->it ? FALSE : TRUE;
}

void
sat_video_frame_add_buffer (SatVideoFrame * frame, GstBuffer * buf)
{
  g_return_if_fail (SAT_IS_VIDEO_FRAME (frame));
  g_return_if_fail (GST_IS_BUFFER (buf));

  if (!frame->it) {
    frame->it = gst_buffer_list_iterate (frame->buffer_list);
    gst_buffer_list_iterator_add_group (frame->it);

    frame->upstream_timestamp = GST_BUFFER_TIMESTAMP (buf);
    frame->upstream_duration = GST_BUFFER_DURATION (buf);
    frame->upstream_offset = GST_BUFFER_OFFSET (buf);
  }

  gst_buffer_list_iterator_add (frame->it, buf);
}

void
sat_video_frame_set_timestamp (SatVideoFrame * frame, GstClockTime timestamp)
{
  g_return_if_fail (SAT_IS_VIDEO_FRAME (frame));

  frame->timestamp = timestamp;
}

GstClockTime
sat_video_frame_get_timestamp (SatVideoFrame * frame)
{
  g_return_val_if_fail (SAT_IS_VIDEO_FRAME (frame), GST_CLOCK_TIME_NONE);

  return frame->timestamp;
}

void
sat_video_frame_set_duration (SatVideoFrame * frame, GstClockTime duration)
{
  g_return_if_fail (SAT_IS_VIDEO_FRAME (frame));

  frame->duration = duration;
}

GstClockTime
sat_video_frame_get_duration (SatVideoFrame * frame)
{
  g_return_val_if_fail (SAT_IS_VIDEO_FRAME (frame), GST_CLOCK_TIME_NONE);

  return frame->duration;
}

void
sat_video_frame_set_frame_nr (SatVideoFrame * frame, guint64 frame_nr,
    guint64 total_frames)
{
  g_return_if_fail (SAT_IS_VIDEO_FRAME (frame));

  frame->frame_nr = frame_nr;
  if (total_frames != -1)
    frame->frame_nr_end = total_frames - frame_nr;
}

guint64
sat_video_frame_get_frame_nr (SatVideoFrame * frame)
{
  g_return_val_if_fail (SAT_IS_VIDEO_FRAME (frame), GST_CLOCK_TIME_NONE);

  return frame->frame_nr;
}

GstClockTime
sat_video_frame_get_upstream_timestamp (SatVideoFrame * frame)
{
  g_return_val_if_fail (SAT_IS_VIDEO_FRAME (frame), GST_CLOCK_TIME_NONE);

  return frame->upstream_timestamp;
}

GstClockTime
sat_video_frame_get_upstream_duration (SatVideoFrame * frame)
{
  g_return_val_if_fail (SAT_IS_VIDEO_FRAME (frame), GST_CLOCK_TIME_NONE);

  return frame->upstream_duration;
}

guint64
sat_video_frame_get_upstream_offset (SatVideoFrame * frame)
{
  g_return_val_if_fail (SAT_IS_VIDEO_FRAME (frame), GST_CLOCK_TIME_NONE);

  return frame->upstream_offset;
}

void
sat_video_frame_set_distance_from_sync (SatVideoFrame * frame,
    guint64 distance_from_sync)
{
  g_return_if_fail (SAT_IS_VIDEO_FRAME (frame));

  frame->distance_from_sync = distance_from_sync;
}

guint64
sat_video_frame_get_distance_from_sync (SatVideoFrame * frame)
{
  g_return_val_if_fail (SAT_IS_VIDEO_FRAME (frame), -1);

  return frame->distance_from_sync;
}

static void
sat_video_frame_apply_metadata (SatVideoFrame * frame)
{
  GstBufferListIterator *it;

  it = gst_buffer_list_iterate (frame->buffer_list);
  while (gst_buffer_list_iterator_next_group (it)) {
    gboolean first = TRUE;
    GstBuffer *buf;

    while ((buf = gst_buffer_list_iterator_next (it))) {
      if (first) {
        if (sat_video_frame_flag_is_set (frame, SAT_VIDEO_FRAME_FLAG_DISCONT))
          GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
        else
          GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DISCONT);

        if (sat_video_frame_flag_is_set (frame, SAT_VIDEO_FRAME_FLAG_GAP))
          GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_GAP);
        else
          GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_GAP);

        first = FALSE;
      }

      if (sat_video_frame_flag_is_set (frame, SAT_VIDEO_FRAME_FLAG_PREROLL))
        GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_PREROLL);
      else
        GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_PREROLL);

      if (sat_video_frame_flag_is_set (frame, SAT_VIDEO_FRAME_FLAG_KEYFRAME))
        GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
      else
        GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

      GST_BUFFER_TIMESTAMP (buf) = frame->timestamp;
      GST_BUFFER_DURATION (buf) = frame->duration;
      GST_BUFFER_OFFSET (buf) = frame->frame_nr;
      GST_BUFFER_OFFSET_END (buf) = frame->frame_nr_end;

      if (frame->caps)
        gst_buffer_set_caps (buf, frame->caps);
    }
  }
  gst_buffer_list_iterator_free (it);
}

GstBufferList *
sat_video_frame_get_buffer_list (SatVideoFrame * frame)
{
  g_return_val_if_fail (SAT_IS_VIDEO_FRAME (frame), NULL);

  sat_video_frame_apply_metadata (frame);

  return frame->buffer_list;
}

SatVideoFrame *
sat_video_frame_new ()
{
  SatVideoFrame *frame;

  frame = (SatVideoFrame *) gst_mini_object_new (SAT_TYPE_VIDEO_FRAME);

  return frame;
}

static GObjectClass *sat_video_frame_parent_class;

static void
sat_video_frame_finalize (SatVideoFrame * frame)
{
  if (frame->it)
    gst_buffer_list_iterator_free (frame->it);

  if (frame->caps)
    gst_caps_unref (frame->caps);

  gst_buffer_list_unref (frame->buffer_list);

  GST_MINI_OBJECT_CLASS (sat_video_frame_parent_class)->finalize
      (GST_MINI_OBJECT (frame));
}

static void
sat_video_frame_init (SatVideoFrame * frame, gpointer g_class)
{
  frame->buffer_list = gst_buffer_list_new ();
  frame->it = NULL;
  frame->caps = NULL;

  frame->flags = 0x00;

  frame->upstream_timestamp = GST_CLOCK_TIME_NONE;
  frame->upstream_duration = GST_CLOCK_TIME_NONE;
  frame->upstream_offset = GST_BUFFER_OFFSET_NONE;

  frame->timestamp = GST_CLOCK_TIME_NONE;
  frame->duration = GST_CLOCK_TIME_NONE;
  frame->frame_nr = -1;
  frame->frame_nr_end = -1;

  frame->distance_from_sync = -1;
}

static void
sat_video_frame_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  sat_video_frame_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      sat_video_frame_finalize;
}


GType
sat_video_frame_get_type (void)
{
  static GType _sat_video_frame_type = 0;

  if (G_UNLIKELY (_sat_video_frame_type == 0)) {
    static const GTypeInfo info = {
      sizeof (SatVideoFrameClass),
      NULL,
      NULL,
      sat_video_frame_class_init,
      NULL,
      NULL,
      sizeof (SatVideoFrame),
      0,
      (GInstanceInitFunc) sat_video_frame_init,
      NULL
    };
    _sat_video_frame_type = g_type_register_static (GST_TYPE_MINI_OBJECT,
        "SatVideoFrame", &info, 0);

    DEBUG_INIT ();
  }
  return _sat_video_frame_type;
}
