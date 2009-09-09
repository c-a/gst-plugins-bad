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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include <math.h>

#include "gstnalreader.h"

#include "gsth264parser.h"

/* default scaling_lists according to Table 7-2 */
const guint8 default_4x4_intra[16] =
    { 6, 13, 13, 20, 20, 20, 28, 28, 28, 28, 32, 32,
  32, 37, 37, 42
};

const guint8 default_4x4_inter[16] =
    { 10, 14, 14, 20, 20, 20, 24, 24, 24, 24, 27, 27,
  27, 30, 30, 34
};

const guint8 default_8x8_intra[64] =
    { 6, 10, 10, 13, 11, 13, 16, 16, 16, 16, 18, 18,
  18, 18, 18, 23, 23, 23, 23, 23, 23, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27,
  27, 27, 27, 27, 27, 29, 29, 29, 29, 29, 29, 29, 31, 31, 31, 31, 31, 31, 33,
  33, 33, 33, 33, 36, 36, 36, 36, 38, 38, 38, 40, 40, 42
};

const guint8 default_8x8_inter[64] =
    { 9, 13, 13, 15, 13, 15, 17, 17, 17, 17, 19, 19,
  19, 19, 19, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 24, 24, 24,
  24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27, 27, 27, 28,
  28, 28, 28, 28, 30, 30, 30, 30, 32, 32, 32, 33, 33, 35
};


#define CHECK_ALLOWED(val, min, max) { \
  if (val < min || val > max) \
    goto error; \
}

#define READ_UINT8(reader, val, nbits) { \
  if (!gst_nal_reader_get_bits_uint8 (reader, &val, nbits)) \
    goto error; \
}

#define READ_UINT16(reader, val, nbits) { \
  if (!gst_nal_reader_get_bits_uint16 (reader, &val, nbits)) \
    goto error; \
}

#define READ_UINT32(reader, val, nbits) { \
  if (!gst_nal_reader_get_bits_uint32 (reader, &val, nbits)) \
    goto error; \
}

#define READ_UINT64(reader, val, nbits) { \
  if (!gst_nal_reader_get_bits_uint64 (reader, &val, nbits)) \
    goto error; \
}

#define READ_UE(reader, val) { \
  if (!gst_nal_reader_get_ue (reader, &val)) \
    goto error; \
}

#define READ_UE_ALLOWED(reader, val, min, max) { \
  guint32 tmp; \
  READ_UE (reader, tmp); \
  CHECK_ALLOWED (tmp, min, max); \
  val = tmp; \
}

#define READ_SE(reader, val) { \
  if (!gst_nal_reader_get_se (reader, &val)) \
    goto error; \
}

#define READ_SE_ALLOWED(reader, val, min, max) { \
  gint32 tmp; \
  READ_SE (reader, tmp); \
  CHECK_ALLOWED (tmp, min, max); \
  val = tmp; \
}

G_DEFINE_TYPE (GstH264Parser, gst_h264_parser, G_TYPE_OBJECT);

static void
gst_h264_sequence_free (void *data)
{
  g_slice_free (GstH264Sequence, data);
}

static gboolean
gst_h264_parser_parse_scaling_list (GstNalReader * reader,
    guint8 scaling_lists_4x4[6][16], guint8 scaling_lists_8x8[6][64],
    const guint8 fallback_4x4_inter[16], const guint8 fallback_4x4_intra[16],
    const guint8 fallback_8x8_inter[64], const guint8 fallback_8x8_intra[64],
    guint32 chroma_format_idc)
{
  gint i;
  guint8 seq_scaling_list_present_flag[12] = { 0, };

  for (i = 0; i < ((chroma_format_idc) != 3) ? 8 : 12; i++) {
    READ_UINT8 (reader, seq_scaling_list_present_flag[i], 1);
  }

  for (i = 0; i < 12; i++) {
    gboolean use_default = FALSE;

    if (seq_scaling_list_present_flag[i]) {
      guint8 *scaling_list;
      guint size;
      guint j;
      guint8 last_scale, next_scale;

      if (i <= 5) {
        scaling_list = scaling_lists_4x4[i];
        size = 16;
      } else {
        scaling_list = scaling_lists_8x8[i];
        size = 64;
      }

      last_scale = 8;
      next_scale = 8;
      for (j = 0; j < size; j++) {
        if (next_scale != 0) {
          gint32 delta_scale;

          READ_SE (reader, delta_scale);
          next_scale = (last_scale + delta_scale + 256) % 256;
          use_default = (j == 0 && next_scale == 0);
        }
        scaling_list[j] = (next_scale == 0) ? last_scale : next_scale;
        last_scale = scaling_list[j];
      }
    } else
      use_default = TRUE;

    if (use_default) {
      switch (i) {
        case 0:
          memcpy (scaling_lists_4x4[0], fallback_4x4_intra, 16);
          break;
        case 1:
          memcpy (scaling_lists_4x4[1], scaling_lists_4x4[0], 16);
          break;
        case 2:
          memcpy (scaling_lists_4x4[2], scaling_lists_4x4[1], 16);
          break;
        case 3:
          memcpy (scaling_lists_4x4[3], fallback_4x4_inter, 16);
          break;
        case 4:
          memcpy (scaling_lists_4x4[4], scaling_lists_4x4[3], 16);
          break;
        case 5:
          memcpy (scaling_lists_4x4[5], scaling_lists_4x4[4], 16);
          break;
        case 6:
          memcpy (scaling_lists_8x8[0], fallback_8x8_intra, 64);
          break;
        case 7:
          memcpy (scaling_lists_8x8[1], fallback_8x8_inter, 64);
          break;
        case 8:
          memcpy (scaling_lists_8x8[2], scaling_lists_8x8[0], 64);
          break;
        case 9:
          memcpy (scaling_lists_8x8[3], scaling_lists_8x8[1], 64);
          break;
        case 10:
          memcpy (scaling_lists_8x8[4], scaling_lists_8x8[2], 64);
          break;
        case 11:
          memcpy (scaling_lists_8x8[5], scaling_lists_8x8[3], 64);
          break;

        default:
          break;
      }
    }
  }

error:
  return FALSE;
}

GstH264Sequence *
gst_h264_parser_parse_sequence (GstH264Parser * parser, guint8 * data,
    guint size)
{
  GstNalReader reader = GST_NAL_READER_INIT (data, size);
  GstH264Sequence *seq;

  g_return_val_if_fail (GST_IS_H264_PARSER (parser), NULL);
  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (size > 0, NULL);

  seq = g_slice_new (GstH264Sequence);

  /* set default values for fields that might not be present in the bitstream
     and have valid defaults */
  seq->chroma_format_idc = 1;
  seq->separate_colour_plane_flag = 0;
  seq->bit_depth_luma_minus8 = 0;
  seq->bit_depth_chroma_minus8 = 0;
  memset (seq->scaling_lists_4x4, 16, 96);
  memset (seq->scaling_lists_8x8, 16, 384);

  READ_UINT8 (&reader, seq->profile_idc, 8);
  READ_UINT8 (&reader, seq->constraint_set0_flag, 1);
  READ_UINT8 (&reader, seq->constraint_set1_flag, 1);
  READ_UINT8 (&reader, seq->constraint_set2_flag, 1);
  READ_UINT8 (&reader, seq->constraint_set3_flag, 1);

  /* skip reserved_zero_4bits */
  if (!gst_nal_reader_skip (&reader, 4))
    goto error;

  READ_UINT8 (&reader, seq->level_idc, 8);

  READ_UE (&reader, seq->id);

  if (seq->profile_idc == 100 || seq->profile_idc == 110 ||
      seq->profile_idc == 122 || seq->profile_idc == 244 ||
      seq->profile_idc == 244 || seq->profile_idc == 44 ||
      seq->profile_idc == 83 || seq->profile_idc == 86) {
    READ_UE_ALLOWED (&reader, seq->chroma_format_idc, 0, 3);
    if (seq->chroma_format_idc == 3)
      READ_UINT8 (&reader, seq->separate_colour_plane_flag, 1);

    READ_UE (&reader, seq->bit_depth_luma_minus8);
    READ_UE (&reader, seq->bit_depth_chroma_minus8);
    READ_UINT8 (&reader, seq->qpprime_y_zero_transform_bypass_flag, 1);

    READ_UINT8 (&reader, seq->scaling_matrix_present_flag, 1);
    if (seq->scaling_matrix_present_flag) {
      if (!gst_h264_parser_parse_scaling_list (&reader,
              seq->scaling_lists_4x4, seq->scaling_lists_8x8,
              default_4x4_inter, default_4x4_intra,
              default_8x8_inter, default_8x8_intra, seq->chroma_format_idc))
        goto error;
    }
  }

  READ_UE_ALLOWED (&reader, seq->log2_max_frame_num_minus4, 0, 12);
  /* calculate MaxFrameNum */
  seq->MaxFrameNum = pow (2, seq->log2_max_frame_num_minus4 + 4);

  READ_UE (&reader, seq->pic_order_cnt_type);
  if (seq->pic_order_cnt_type == 0) {
    READ_UE (&reader, seq->log2_max_pic_order_cnt_lsb_minus4);
  } else if (seq->pic_order_cnt_type == 1) {
    guint i;

    READ_UINT8 (&reader, seq->delta_pic_order_always_zero_flag, 1);
    READ_SE (&reader, seq->offset_for_non_ref_pic);
    READ_SE (&reader, seq->offset_for_top_to_bottom_field);
    READ_UE (&reader, seq->num_ref_frames_in_pic_order_cnt_cycle);
    for (i = 0; i < seq->num_ref_frames_in_pic_order_cnt_cycle; i++)
      READ_SE (&reader, seq->offset_for_ref_frame[i]);
  }

  READ_UE (&reader, seq->num_ref_frames);
  READ_UINT8 (&reader, seq->gaps_in_frame_num_value_allowed_flag, 1);
  READ_UE (&reader, seq->pic_width_in_mbs_minus1);
  READ_UE (&reader, seq->pic_height_in_map_units_minus1);
  READ_UINT8 (&reader, seq->frame_mbs_only_flag, 1);

  g_hash_table_insert (parser->sequences, GUINT_TO_POINTER (seq->id), seq);

error:
  gst_h264_sequence_free (seq);
  return NULL;
}

static void
gst_h264_picture_free (void *data)
{
  GstH264Picture *pic = (GstH264Picture *) data;

  if (pic->slice_group_id)
    g_free (pic->slice_group_id);

  g_slice_free (GstH264Picture, data);
}

static gboolean
gst_h264_parser_more_data (GstNalReader * reader)
{
  guint remaining;

  remaining = gst_nal_reader_get_remaining (reader);
  if (remaining > 0 && remaining < 8) {
    guint8 rbsp_stop_one_bit;

    if (!gst_nal_reader_peek_bits_uint8 (reader, &rbsp_stop_one_bit, 1))
      return FALSE;

    if (rbsp_stop_one_bit == 1)
      return FALSE;
  }

  return TRUE;
}

GstH264Picture *
gst_h264_parser_parse_picture (GstH264Parser * parser, guint8 * data,
    guint size)
{
  GstNalReader reader = GST_NAL_READER_INIT (data, size);
  GstH264Picture *pic;
  guint32 seq_parameter_set_id;
  GstH264Sequence *seq;

  g_return_val_if_fail (GST_IS_H264_PARSER (parser), NULL);
  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (size > 0, NULL);

  pic = g_slice_new (GstH264Picture);

  /* set default values for fields that might not be present in the bitstream
     and have valid defaults */
  pic->slice_group_id = NULL;
  pic->transform_8x8_mode_flag = 0;

  READ_UE (&reader, pic->id);
  READ_UE (&reader, seq_parameter_set_id);
  seq =
      g_hash_table_lookup (parser->sequences,
      GINT_TO_POINTER (seq_parameter_set_id));
  if (!seq)
    goto error;
  pic->sequence = seq;

  READ_UINT8 (&reader, pic->entropy_coding_mode_flag, 1);
  READ_UINT8 (&reader, pic->pic_order_present_flag, 1);
  READ_UE_ALLOWED (&reader, pic->num_slice_groups_minus1, 0, 7);
  if (pic->num_slice_groups_minus1 > 0) {
    READ_UE (&reader, pic->slice_group_map_type);
    if (pic->slice_group_map_type == 0) {
      gint i;

      for (i = 0; i <= pic->num_slice_groups_minus1; i++)
        READ_UE (&reader, pic->run_length_minus1[i]);
    }
  } else if (pic->slice_group_map_type == 2) {
    gint i;

    for (i = 0; i <= pic->num_slice_groups_minus1; i++) {
      READ_UE (&reader, pic->top_left[i]);
      READ_UE (&reader, pic->bottom_right[i]);
    }
  } else if (pic->slice_group_map_type >= 3 && pic->slice_group_map_type <= 5) {
    READ_UINT8 (&reader, pic->slice_group_change_direction_flag, 1);
    READ_UE (&reader, pic->slice_group_change_rate_minus1);
  } else if (pic->slice_group_map_type == 6) {
    gint bits;
    gint i;

    READ_UE (&reader, pic->pic_size_in_map_units_minus1);
    bits = ceil (log2 (pic->num_slice_groups_minus1 + 1));

    pic->slice_group_id = g_new (guint8, pic->pic_size_in_map_units_minus1 + 1);
    for (i = 0; i <= pic->pic_size_in_map_units_minus1; i++)
      READ_UINT8 (&reader, pic->slice_group_id[i], bits);
  }

  READ_UE_ALLOWED (&reader, pic->num_ref_idx_l0_active_minus1, 0, 31);
  READ_UE_ALLOWED (&reader, pic->num_ref_idx_l1_active_minus1, 0, 31);
  READ_UINT8 (&reader, pic->weighted_pred_flag, 1);
  READ_UINT8 (&reader, pic->weighted_bipred_idc, 1);
  READ_SE_ALLOWED (&reader, pic->pic_init_qp_minus26, -26, 25);
  READ_SE_ALLOWED (&reader, pic->pic_init_qs_minus26, -26, 25);
  READ_SE_ALLOWED (&reader, pic->chroma_qp_index_offset, -12, 12);
  READ_UINT8 (&reader, pic->deblocking_filter_control_present_flag, 1);
  READ_UINT8 (&reader, pic->constrained_intra_pred_flag, 1);
  READ_UINT8 (&reader, pic->redundant_pic_cnt_present_flag, 1);

  if (!gst_h264_parser_more_data (&reader))
    return pic;

  READ_UINT8 (&reader, pic->transform_8x8_mode_flag, 1);

  READ_UINT8 (&reader, pic->scaling_matrix_present_flag, 1);
  if (pic->scaling_matrix_present_flag) {
    if (seq->scaling_matrix_present_flag) {
      if (!gst_h264_parser_parse_scaling_list (&reader,
              pic->scaling_lists_4x4, pic->scaling_lists_8x8,
              seq->scaling_lists_4x4[0], seq->scaling_lists_4x4[3],
              seq->scaling_lists_8x8[0], seq->scaling_lists_8x8[3],
              seq->chroma_format_idc))
        goto error;
    } else {
      if (!gst_h264_parser_parse_scaling_list (&reader,
              seq->scaling_lists_4x4, seq->scaling_lists_8x8,
              default_4x4_inter, default_4x4_intra,
              default_8x8_inter, default_8x8_intra, seq->chroma_format_idc))
        goto error;
    }
  }

  READ_SE_ALLOWED (&reader, pic->second_chroma_qp_index_offset, -12, 12);

error:
  gst_h264_picture_free (pic);
  return NULL;
}

static gboolean
gst_h264_slice_parse_pred_weight_table (GstH264Slice * slice,
    GstNalReader * reader,
    const GstH264Sequence * seq, const GstH264Picture * pic)
{
  GstH264PredWeightTable *p;
  gint i;

  p = &slice->pred_weight_table;

  READ_UE_ALLOWED (reader, p->luma_log2_weight_denom, 0, 7);
  /* set default values */
  memset (p->luma_weight_l0, pow (2, p->luma_log2_weight_denom), 32);
  memset (p->luma_offset_l0, 0, 32);

  if (seq->ChromaArrayType != 0) {
    READ_UE_ALLOWED (reader, p->chroma_log2_weight_denom, 0, 7);
    /* set default values */
    memset (p->chroma_weight_l0, pow (2, p->chroma_log2_weight_denom), 64);
    memset (p->chroma_offset_l0, 0, 64);
  }

  for (i = 0; i <= pic->num_ref_idx_l0_active_minus1; i++) {
    guint8 luma_weight_l0_flag;

    READ_UINT8 (reader, luma_weight_l0_flag, 1);
    if (luma_weight_l0_flag) {
      READ_SE_ALLOWED (reader, p->luma_weight_l0[i], -128, 127);
      READ_SE_ALLOWED (reader, p->luma_offset_l0[i], -128, 127);
    }
    if (seq->ChromaArrayType != 0) {
      guint8 chroma_weight_l0_flag;
      gint j;

      READ_UINT8 (reader, chroma_weight_l0_flag, 1);
      for (j = 0; j <= 2; j++) {
        READ_SE_ALLOWED (reader, p->chroma_weight_l0[i][j], -128, 127);
        READ_SE_ALLOWED (reader, p->chroma_offset_l0[i][j], -128, 127);
      }
    }
  }

  if (GST_H264_IS_B_SLICE (slice->type)) {
    for (i = 0; i <= pic->num_ref_idx_l1_active_minus1; i++) {
      guint8 luma_weight_l1_flag;

      READ_UINT8 (reader, luma_weight_l1_flag, 1);
      if (luma_weight_l1_flag) {
        READ_SE_ALLOWED (reader, p->luma_weight_l1[i], -128, 127);
        READ_SE_ALLOWED (reader, p->luma_offset_l1[i], -128, 127);
      }
      if (seq->ChromaArrayType != 0) {
        guint8 chroma_weight_l1_flag;
        gint j;

        READ_UINT8 (reader, chroma_weight_l1_flag, 1);
        for (j = 0; j <= 2; j++) {
          READ_SE_ALLOWED (reader, p->chroma_weight_l1[i][j], -128, 127);
          READ_SE_ALLOWED (reader, p->chroma_offset_l1[i][j], -128, 127);
        }
      }
    }
  }

error:
  return FALSE;
}

static gboolean
gst_h264_slice_parse_ref_pic_list_reordering (GstH264Slice * slice,
    GstNalReader * reader)
{
  if (!GST_H264_IS_I_SLICE (slice->type) && !GST_H264_IS_SI_SLICE (slice->type)) {
    guint8 ref_pic_list_reordering_flag_l0;
    guint8 reordering_of_pic_nums_idc;

    READ_UINT8 (reader, ref_pic_list_reordering_flag_l0, 1);
    if (ref_pic_list_reordering_flag_l0)
      do {
        READ_UE_ALLOWED (reader, reordering_of_pic_nums_idc, 0, 3);
        if (reordering_of_pic_nums_idc == 0 || reordering_of_pic_nums_idc == 1) {
          guint32 abs_diff_pic_num_minus1;

          READ_UE_ALLOWED (reader, abs_diff_pic_num_minus1, 0,
              slice->MaxPicNum - 1);
        } else if (reordering_of_pic_nums_idc == 2) {
          guint32 long_term_pic_num;

          READ_UE (reader, long_term_pic_num);
        }
      } while (reordering_of_pic_nums_idc != 3);
  }

  if (GST_H264_IS_B_SLICE (slice->type)) {
    guint8 ref_pic_list_reordering_flag_l1;
    guint8 reordering_of_pic_nums_idc;

    READ_UINT8 (reader, ref_pic_list_reordering_flag_l1, 1);
    if (ref_pic_list_reordering_flag_l1)
      do {
        READ_UE_ALLOWED (reader, reordering_of_pic_nums_idc, 0, 3);
        if (reordering_of_pic_nums_idc == 0 || reordering_of_pic_nums_idc == 1) {
          guint32 abs_diff_num_minus1;
          READ_UE (reader, abs_diff_num_minus1);
        } else if (reordering_of_pic_nums_idc == 3) {
          guint32 long_term_pic_num;

          READ_UE (reader, long_term_pic_num);
        }
      } while (reordering_of_pic_nums_idc != 3);
  }

error:
  return FALSE;
}

static gboolean
gst_h264_slice_parse_dec_ref_pic_marking (GstH264Slice * slice,
    GstNalReader * reader)
{
  GstH264DecRefPicMarking *m;

  m = &slice->dec_ref_pic_marking;

  if (slice->nal_unit.IdrPicFlag) {
    READ_UINT8 (reader, m->no_output_of_prior_pics_flag, 1);
    READ_UINT8 (reader, m->long_term_reference_flag, 1);
  } else {
    READ_UINT8 (reader, m->adaptive_ref_pic_marking_mode_flag, 1);
    if (m->adaptive_ref_pic_marking_mode_flag) {
      guint8 memory_management_control_operation;

      do {
        READ_UE_ALLOWED (reader, memory_management_control_operation, 0, 6);
        if (memory_management_control_operation == 1 ||
            memory_management_control_operation == 3) {
          guint32 difference_of_pic_nums_minus1;

          READ_UE (reader, difference_of_pic_nums_minus1);
        }
        if (memory_management_control_operation == 2) {
          guint32 long_term_pic_num;

          READ_UE (reader, long_term_pic_num);
        }
        if (memory_management_control_operation == 3 ||
            memory_management_control_operation == 6) {
          guint32 long_term_frame_idx;

          READ_UE (reader, long_term_frame_idx);
        }
        if (memory_management_control_operation == 4) {
          guint32 max_long_term_frame_idx_plus1;

          READ_UE (reader, max_long_term_frame_idx_plus1);
        }
      }
      while (memory_management_control_operation != 0);
    }
  }

  return TRUE;

error:
  return FALSE;
}

gboolean
gst_h264_parser_parse_slice_header (GstH264Parser * parser,
    GstH264Slice * slice, guint8 * data, guint size, GstNalUnit nal_unit)
{
  GstNalReader reader = GST_NAL_READER_INIT (data, size);
  guint32 pic_parameter_set_id;
  GstH264Picture *pic;
  GstH264Sequence *seq;

  g_return_val_if_fail (GST_IS_H264_PARSER (parser), FALSE);
  g_return_val_if_fail (slice != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size > 0, FALSE);

  memcpy (&slice->nal_unit, &nal_unit, sizeof (GstNalUnit));

  READ_UE (&reader, slice->first_mb_in_slice);
  READ_UE (&reader, slice->type);

  READ_UE (&reader, pic_parameter_set_id);
  pic = g_hash_table_lookup (parser->pictures,
      GINT_TO_POINTER (pic_parameter_set_id));
  if (!pic)
    goto error;
  slice->picture = pic;
  seq = pic->sequence;

  /* set default values for fields that might not be present in the bitstream
     and have valid defaults */
  slice->field_pic_flag = 0;
  slice->bottom_field_flag = 0;
  slice->delta_pic_order_cnt_bottom = 0;
  slice->delta_pic_order_cnt[0] = 0;
  slice->delta_pic_order_cnt[1] = 0;
  slice->redundant_pic_cnt = 0;
  slice->num_ref_idx_l0_active_minus1 = pic->num_ref_idx_l0_active_minus1;
  slice->num_ref_idx_l1_active_minus1 = pic->num_ref_idx_l1_active_minus1;

  if (seq->separate_colour_plane_flag)
    READ_UINT8 (&reader, slice->colour_plane_id, 2);

  READ_UINT16 (&reader, slice->frame_num, seq->log2_max_frame_num_minus4 + 4);

  if (!seq->frame_mbs_only_flag) {
    READ_UINT8 (&reader, slice->field_pic_flag, 1);
    if (slice->field_pic_flag)
      READ_UINT8 (&reader, slice->bottom_field_flag, 1);
  }

  /* calculate MaxPicNum */
  if (slice->field_pic_flag)
    slice->MaxPicNum = seq->MaxFrameNum;
  else
    slice->MaxPicNum = 2 * seq->MaxFrameNum;

  if (nal_unit.type == 5)
    READ_UE (&reader, slice->idr_pic_id);

  if (seq->pic_order_cnt_type == 0) {
    READ_UINT16 (&reader, slice->pic_order_cnt_lsb,
        seq->log2_max_pic_order_cnt_lsb_minus4 + 4);
    if (pic->pic_order_present_flag && !slice->field_pic_flag)
      READ_SE (&reader, slice->delta_pic_order_cnt_bottom);
  }

  if (seq->pic_order_cnt_type == 1 && !seq->delta_pic_order_always_zero_flag) {
    READ_SE (&reader, slice->delta_pic_order_cnt[0]);
    if (pic->pic_order_present_flag && !slice->field_pic_flag)
      READ_SE (&reader, slice->delta_pic_order_cnt[1]);
  }

  if (pic->redundant_pic_cnt_present_flag)
    READ_UE (&reader, slice->redundant_pic_cnt);

  if (GST_H264_IS_B_SLICE (slice->type))
    READ_UINT8 (&reader, slice->direct_spatial_mv_pred_flag, 1);

  if (GST_H264_IS_P_SLICE (slice->type) ||
      GST_H264_IS_SP_SLICE (slice->type) || GST_H264_IS_B_SLICE (slice->type)) {
    guint8 num_ref_idx_active_override_flag;

    READ_UINT8 (&reader, num_ref_idx_active_override_flag, 1);
    if (num_ref_idx_active_override_flag) {
      READ_UE (&reader, slice->num_ref_idx_l0_active_minus1);

      if (GST_H264_IS_B_SLICE (slice->type))
        READ_UE (&reader, slice->num_ref_idx_l1_active_minus1);
    }
  }

  if (!gst_h264_slice_parse_ref_pic_list_reordering (slice, &reader))
    return FALSE;

  if ((pic->weighted_pred_flag && (GST_H264_IS_P_SLICE (slice->type) ||
              GST_H264_IS_SP_SLICE (slice->type)))
      || (pic->weighted_bipred_idc == 1 && GST_H264_IS_B_SLICE (slice->type))) {
    if (!gst_h264_slice_parse_pred_weight_table (slice, &reader, seq, pic))
      return FALSE;
  }

  if (nal_unit.ref_idc != 0) {
    if (!gst_h264_slice_parse_dec_ref_pic_marking (slice, &reader))
      return FALSE;
  }

  return TRUE;

error:
  return FALSE;
}

#undef CHECK_ALLOWED
#undef READ_UINT8
#undef READ_UINT16
#undef READ_UINT32
#undef READ_UINT64
#undef READ_UE
#undef READ_UE_ALLOWED
#undef READ_SE
#undef READ_SE_ALLOWED

static void
gst_h264_parser_init (GstH264Parser * object)
{
  GstH264Parser *parser = GST_H264_PARSER (object);

  parser->sequences = g_hash_table_new_full (g_int_hash, g_int_equal, NULL,
      gst_h264_sequence_free);
  parser->pictures = g_hash_table_new_full (g_int_hash, g_int_equal, NULL,
      gst_h264_picture_free);
}

static void
gst_h264_parser_finalize (GObject * object)
{
  GstH264Parser *parser = GST_H264_PARSER (object);

  g_hash_table_destroy (parser->sequences);
  g_hash_table_destroy (parser->pictures);

  G_OBJECT_CLASS (gst_h264_parser_parent_class)->finalize (object);
}

static void
gst_h264_parser_class_init (GstH264ParserClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_h264_parser_finalize;
}

gboolean
gst_h264_check_slice_type (GstH264SliceType type, GstH264SliceType check_type)
{
  if ((type % 5) == check_type)
    return TRUE;

  return FALSE;
}
