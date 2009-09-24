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

#ifndef __GST_H264_PARSE2_H__
#define __GST_H264_PARSE2_H__

#include <gst/gst.h>

#include "satbasevideoparse.h"

#include "gsth264parser.h"

G_BEGIN_DECLS

#define GST_TYPE_H264_PARSE2		        (gst_h264_parse2_get_type())
#define GST_H264_PARSE2(obj)		        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H264_PARSE2,GstH264Parse2))
#define GST_H264_PARSE2_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H264_PARSE2,GstH264Parse2Class))
#define GST_H264_PARSE2_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_H264_PARSE2,GstH264Parse2Class))
#define GST_IS_H264_PARSE2(obj)	        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H264_PARSE2))
#define GST_IS_H264_PARSE2_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H264_PARSE2))
#define GST_H264_PARSE2_CAST(obj)	      ((GstH264Parse2 *)(obj))

typedef struct _GstH264Parse2 GstH264Parse2;
typedef struct _GstH264Parse2Class GstH264Parse2Class;


struct _GstH264Parse2 {
  SatBaseVideoParse parent;

  GstBuffer *codec_data;
  gboolean packetized;
  guint nal_length_size;
  GstH264Parser *parser;
  
  guint64 byte_offset;
  guint64 byterate;
  GstClockTime final_duration;
};

struct _GstH264Parse2Class {
  SatBaseVideoParseClass parent_class;
};

GType gst_h264_parse2_get_type (void);

G_END_DECLS

#endif /* __GST_H264_PARSE2_H__ */
