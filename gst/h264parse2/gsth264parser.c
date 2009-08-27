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

#define READ_SE(reader, val) { \
  if (!gst_nal_reader_get_se (reader, &val)) \
    goto error; \
}


G_DEFINE_TYPE (GstH264Parser, gst_h264_parser, G_TYPE_OBJECT);

static void
gst_h264_parser_default_4x4_scaling_list (guint8 scaling_lists_4x4[6][16],
    guint index)
{
  switch (index) {
    case 0:
      memcpy (scaling_lists_4x4[0], default_4x4_intra, 16);
      break;
    case 1:
      memcpy (scaling_lists_4x4[1], scaling_lists_4x4[0], 16);
      break;
    case 2:
      memcpy (scaling_lists_4x4[2], scaling_lists_4x4[1], 16);
      break;
    case 3:
      memcpy (scaling_lists_4x4[3], default_4x4_inter, 16);
      break;
    case 4:
      memcpy (scaling_lists_4x4[4], scaling_lists_4x4[3], 16);
      break;
    case 5:
      memcpy (scaling_lists_4x4[5], scaling_lists_4x4[4], 16);
      break;

    default:
      break;
  }
}

static void
gst_h264_parser_default_8x8_scaling_list (guint8 scaling_lists_8x8[6][64],
    guint index)
{
  switch (index) {
    case 1:
      memcpy (scaling_lists_8x8[0], default_8x8_intra, 64);
      break;
    case 2:
      memcpy (scaling_lists_8x8[1], default_8x8_inter, 64);
      break;
    case 3:
      memcpy (scaling_lists_8x8[2], scaling_lists_8x8[0], 64);
      break;
    case 4:
      memcpy (scaling_lists_8x8[3], scaling_lists_8x8[1], 64);
      break;
    case 5:
      memcpy (scaling_lists_8x8[4], scaling_lists_8x8[2], 64);
      break;
    case 6:
      memcpy (scaling_lists_8x8[5], scaling_lists_8x8[3], 64);
      break;

    default:
      break;
  }
}

static gboolean
gst_h264_parser_parse_scaling_list (GstNalReader * reader,
    guint8 scaling_lists_4x4[6][16], guint8 scaling_lists_8x8[6][64],
    guint32 chroma_format_idc)
{
  guint8 seq_scaling_matrix_flag;

  READ_UINT8 (reader, seq_scaling_matrix_flag, 1);
  if (seq_scaling_matrix_flag) {
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
        if (i < 6)
          gst_h264_parser_default_4x4_scaling_list (scaling_lists_4x4, i);
        else
          gst_h264_parser_default_8x8_scaling_list (scaling_lists_8x8, i - 6);
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

  g_return_val_if_fail (GST_H264_IS_PARSER (parser), NULL);
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
    READ_UE (&reader, seq->chroma_format_idc);
    if (seq->chroma_format_idc == 3)
      READ_UINT8 (&reader, seq->separate_colour_plane_flag, 1);

    READ_UE (&reader, seq->bit_depth_luma_minus8);
    READ_UE (&reader, seq->bit_depth_chroma_minus8);
    READ_UINT8 (&reader, seq->qpprime_y_zero_transform_bypass_flag, 1);

    if (!gst_h264_parser_parse_scaling_list (&reader, seq->scaling_lists_4x4,
            seq->scaling_lists_8x8, seq->chroma_format_idc))
      goto error;
  }

  READ_UE (&reader, seq->log2_max_frame_num_minus4);
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
  g_slice_free (GstH264Sequence, seq);
  return NULL;
}

GstH264Picture *
gst_h264_parser_parse_picture (GstH264Parser * parser, guint8 * data,
    guint size)
{
  GstNalReader reader = GST_NAL_READER_INIT (data, size);
  GstH264Picture *pic;
  guint32 seq_parameter_set_id;
  GstH264Sequence *seq;

  g_return_val_if_fail (GST_H264_IS_PARSER (parser), NULL);
  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (size > 0, NULL);

  pic = g_slice_new (GstH264Picture);

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
  READ_UE (&reader, pic->num_slice_groups_minus1);
  if (pic->num_slice_groups_minus1 > 0) {
  }

error:
  g_slice_free (GstH264Picture, pic);
  return NULL;
}

gboolean
gst_h264_parser_parse_slice_header (GstH264Parser * parser,
    GstH264Slice * slice, guint8 * data, guint size, guint32 nal_unit_type)
{
  GstNalReader reader = GST_NAL_READER_INIT (data, size);
  guint32 pic_parameter_set_id;
  GstH264Picture *pic;
  GstH264Sequence *seq;

  g_return_val_if_fail (GST_H264_IS_PARSER (parser), FALSE);
  g_return_val_if_fail (slice != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size > 0, FALSE);

  READ_UE (&reader, slice->first_mb_in_slice);
  READ_UE (&reader, slice->slice_type);

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

  if (nal_unit_type == 5)
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

  if (slice->slice_type == GST_H264_B_SLICE)
    READ_UINT8 (&reader, slice->direct_spatial_mv_pred_flag, 1);

  if (slice->slice_type == GST_H264_P_SLICE ||
      slice->slice_type == GST_H264_SP_SLICE ||
      slice->slice_type == GST_H264_B_SLICE) {
    guint8 num_ref_idx_active_override_flag;

    READ_UINT8 (&reader, num_ref_idx_active_override_flag, 1);
    if (num_ref_idx_active_override_flag) {
      READ_UE (&reader, slice->num_ref_idx_l0_active_minus1);

      if (slice->slice_type == GST_H264_B_SLICE)
        READ_UE (&reader, slice->num_ref_idx_l1_active_minus1);
    }
  }

  return TRUE;

error:
  return FALSE;
}

#undef READ_UINT8
#undef READ_UINT16
#undef READ_UINT32
#undef READ_UINT64
#undef READ_UE
#undef READ_SE

static void
gst_h264_sequence_free (void *data)
{
  g_slice_free (GstH264Sequence, data);
}

static void
gst_h264_picture_free (void *data)
{
  g_slice_free (GstH264Picture, data);
}

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
