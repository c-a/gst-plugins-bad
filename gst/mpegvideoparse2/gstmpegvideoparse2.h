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

#ifndef __GST_MPEG_VIDEO_PARSE2_H__
#define __GST_MPEG_VIDEO_PARSE2_H__

#include <gst/gst.h>
#include "gstbasevideoparse.h"

G_BEGIN_DECLS

#define GST_TYPE_MPEG_VIDEO_PARSE2		            (gst_mvp2_get_type())
#define GST_MPEG_VIDEO_PARSE2(obj)		            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEG_VIDEO_PARSE2,GstMpegVideoParse2))
#define GST_MPEG_VIDEO_PARSE2_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEG_VIDEO_PARSE2,GstMpegVideoParse2Class))
#define GST_MPEG_VIDEO_PARSE2_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_MPEG_VIDEO_PARSE2,GstMpegVideoParse2Class))
#define GST_IS_MPEG_VIDEO_PARSE2(obj)	            (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEG_VIDEO_PARSE2))
#define GST_IS_MPEG_VIDEO_PARSE2_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEG_VIDEO_PARSE2))
#define GST_MPEG_VIDEO_PARSE2_CAST(obj)	          ((GstMpegVideoParse2 *)(obj))

typedef struct _GstMpegVideoParse2 GstMpegVideoParse2;
typedef struct _GstMpegVideoParse2Class GstMpegVideoParse2Class;


typedef enum {
  GST_MVP2_STATE_NEED_SEQUENCE,
  GST_MVP2_STATE_NEED_DATA
} GstMVP2State;

struct _GstMpegVideoParse2 {
  GstBaseVideoParse parent;

  GstMVP2State state;
  gint prev_packet;

  gboolean sequence;
  
  gint width, height;
  gint fps_n, fps_d;
  gint par_n, par_d;
  gboolean interlaced;
  gint version;
  GstBuffer *seq_header_buffer;
  
  guint64 gop_start;
  
  guint64 byte_offset;
  guint64 byterate;

  GstClockTime final_duration;
};

struct _GstMpegVideoParse2Class {
  GstBaseVideoParseClass parent_class;
};

GType gst_mpeg_video_parse2_get_type (void);

G_END_DECLS

#endif /* __GST_MPEG_VIDEO_PARSE2_H__ */
