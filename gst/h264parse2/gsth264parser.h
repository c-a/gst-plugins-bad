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

#ifndef _GST_H264_PARSER_H_
#define _GST_H264_PARSER_H_

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum
{
  GST_NAL_UNKNOWN = 0,
  GST_NAL_SLICE = 1,
  GST_NAL_SLICE_DPA = 2,
  GST_NAL_SLICE_DPB = 3,
  GST_NAL_SLICE_DPC = 4,
  GST_NAL_SLICE_IDR = 5,
  GST_NAL_SEI = 6,
  GST_NAL_SPS = 7,
  GST_NAL_PPS = 8,
  GST_NAL_AU_DELIMITER = 9,
  GST_NAL_SEQ_END = 10,
  GST_NAL_STREAM_END = 11,
  GST_NAL_FILTER_DATA = 12
} GstNalUnitType;

typedef enum
{
  GST_H264_P_SLICE,
  GST_H264_B_SLICE,
  GST_H264_I_SLICE,
  GST_H264_SP_SLICE,
  GST_H264_SI_SLICE,
  GST_H264_S_P_SLICE,
  GST_H264_S_B_SLICE,
  GST_H264_S_I_SLICE,
  GST_H264_S_SP_SLICE,
  GST_H264_S_SI_SLICE
} GstH264SliceType;

typedef struct _GstH264Sequence GstH264Sequence;
typedef struct _GstH264Picture GstH264Picture;
typedef struct _GstH264Slice GstH264Slice;

struct _GstH264Sequence
{
  guint32 id;

  guint8 profile_idc;
  guint8 constraint_set0_flag;
  guint8 constraint_set1_flag;
  guint8 constraint_set2_flag;
  guint8 constraint_set3_flag;
  guint8 level_idc;

  guint32 chroma_format_idc;
  guint8 separate_colour_plane_flag;
  guint32 bit_depth_luma_minus8;
  guint32 bit_depth_chroma_minus8;
  guint8 qpprime_y_zero_transform_bypass_flag;

  guint8 scaling_matrix_present_flag;
  guint8 scaling_lists_4x4[6][16];
  guint8 scaling_lists_8x8[6][64];

  guint32 log2_max_frame_num_minus4;
  guint32 pic_order_cnt_type;

  /* if pic_order_cnt_type == 0 */
  guint32 log2_max_pic_order_cnt_lsb_minus4;

  /* else if pic_order_cnt_type == 1 */
  guint8 delta_pic_order_always_zero_flag;
  gint32 offset_for_non_ref_pic;
  gint32 offset_for_top_to_bottom_field;
  guint32 num_ref_frames_in_pic_order_cnt_cycle;
  gint32 offset_for_ref_frame[255];

  guint32 num_ref_frames;
  guint8 gaps_in_frame_num_value_allowed_flag;
  guint32 pic_width_in_mbs_minus1;
  guint32 pic_height_in_map_units_minus1;
  guint8 frame_mbs_only_flag;
};

struct _GstH264Picture
{
  guint32 id;

  GstH264Sequence *sequence;

  guint8 entropy_coding_mode_flag;
  guint8 pic_order_present_flag;

  guint32 num_slice_groups_minus1;

  /* if num_slice_groups_minus1 > 0 */
  guint32 slice_group_map_type;
  /* and if slice_group_map_type == 0 */
  guint32 run_length_minus1[8];
  /* or if slice_group_map_type == 2 */
  guint32 top_left[8];
  guint32 bottom_right[8];
  /* or if slice_group_map_type == (3, 4, 5) */
  guint8 slice_group_change_direction_flag;
  guint32 slice_group_change_rate_minus1;
  /* or if slice_group_map_type == 6 */
  guint32 pic_size_in_map_units_minus1;
  guint8 *slice_group_id;

  guint8 num_ref_idx_l0_active_minus1;
  guint8 num_ref_idx_l1_active_minus1;
  guint8 weighted_pred_flag;
  guint8 weighted_bipred_idc;
  gint8 pic_init_qp_minus26;
  gint8 pic_init_qs_minus26;
  gint8 chroma_qp_index_offset;
  guint8 deblocking_filter_control_present_flag;
  guint8 constrained_intra_pred_flag;
  guint8 redundant_pic_cnt_present_flag;

  guint8 transform_8x8_mode_flag;

  guint8 scaling_matrix_present_flag;
  /* if scaling_matrix_present_flag == 1 */
  guint8 scaling_lists_4x4[6][16];
  guint8 scaling_lists_8x8[6][64];

  guint8 second_chroma_qp_index_offset;
};

struct _GstH264Slice
{ 
  guint32 first_mb_in_slice;
  guint32 slice_type;
  
  GstH264Picture *picture;

  /* if seq->separate_colour_plane_flag */
  guint8 colour_plane_id;

  guint16 frame_num;

  guint8 field_pic_flag;
  guint8 bottom_field_flag;

  /* if nal_unit_type == 5 */
  guint32 idr_pic_id;

  /* if seq->pic_order_cnt_type == 0 */
  guint16 pic_order_cnt_lsb;
  /* and if (pic->pic_order_present_flag && !slice->field_pic_flag) */
  gint32 delta_pic_order_cnt_bottom;

  gint32 delta_pic_order_cnt[2];
  guint32 redundant_pic_cnt;

  /* if slice_type == B_SLICE */
  guint8 direct_spatial_mv_pred_flag;

  guint32 num_ref_idx_l0_active_minus1;
  guint32 num_ref_idx_l1_active_minus1;
};

#define GST_TYPE_H264_PARSER             (gst_h264_parser_get_type ())
#define GST_H264_PARSER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_H264_PARSER, GstH264Parser))
#define GST_H264_PARSER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_H264_PARSER, GstH264ParserClass))
#define GST_IS_H264_PARSER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_H264_PARSER))
#define GST_IS_H264_PARSER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_H264_PARSER))
#define GST_H264_PARSER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_H264_PARSER, GstH264ParserClass))

typedef struct _GstH264ParserClass GstH264ParserClass;
typedef struct _GstH264Parser GstH264Parser;

struct _GstH264ParserClass
{
  GObjectClass parent_class;
};

struct _GstH264Parser
{
  GObject parent_instance;

  GHashTable *sequences;
  GHashTable *pictures;
};
		
GType gst_h264_parser_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* _GST_H264_PARSER_H_ */
