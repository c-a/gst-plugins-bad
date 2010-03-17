/* GStreamer
 * Copyright (C) 2007 Jan Schmidt <thaytan@mad.scientist.com>
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

#include <gst/base/gstbitreader.h>
#include <string.h>

#include "mpegutil.h"

/* default intra quant matrix, in zig-zag order */
static const guint8 default_intra_quantizer_matrix[64] = {
  8,
  16, 16,
  19, 16, 19,
  22, 22, 22, 22,
  22, 22, 26, 24, 26,
  27, 27, 27, 26, 26, 26,
  26, 27, 27, 27, 29, 29, 29,
  34, 34, 34, 29, 29, 29, 27, 27,
  29, 29, 32, 32, 34, 34, 37,
  38, 37, 35, 35, 34, 35,
  38, 38, 40, 40, 40,
  48, 48, 46, 46,
  56, 56, 58,
  69, 69,
  83
};

guint8 mpeg2_scan[64] = {
  /* Zig-Zag scan pattern */
  0, 1, 8, 16, 9, 2, 3, 10,
  17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

guint8 bits[] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };

guint32
read_bits (guint8 * buf, gint start_bit, gint n_bits)
{
  gint i;
  guint32 ret = 0x00;

  buf += start_bit / 8;
  start_bit %= 8;

  for (i = 0; i < n_bits; i++) {
    guint32 tmp;

    tmp = ((*buf & bits[start_bit]) >> (7 - start_bit));
    ret = (ret | (tmp << (n_bits - i - 1)));
    if (++start_bit == 8) {
      buf += 1;
      start_bit = 0;
    }
  }

  return ret;
}

guint8 *
mpeg_util_find_start_code (guint32 * sync_word, guint8 * cur, guint8 * end)
{
  guint32 code;

  if (G_UNLIKELY (cur == NULL))
    return NULL;

  code = *sync_word;

  while (cur < end) {
    code <<= 8;

    if (code == 0x00000100) {
      /* Reset the sync word accumulator */
      *sync_word = 0xffffffff;
      return cur;
    }

    /* Add the next available byte to the collected sync word */
    code |= *cur++;
  }

  *sync_word = code;
  return NULL;
}

static void
set_fps_from_code (MPEGSeqHdr * hdr, guint8 fps_code)
{
  const gint framerates[][2] = {
    {30, 1}, {24000, 1001}, {24, 1}, {25, 1},
    {30000, 1001}, {30, 1}, {50, 1}, {60000, 1001},
    {60, 1}, {30, 1}
  };

  if (fps_code < 10) {
    hdr->fps_n = framerates[fps_code][0];
    hdr->fps_d = framerates[fps_code][1];
  } else {
    /* Force a valid framerate */
    hdr->fps_n = 30000;
    hdr->fps_d = 1001;
  }
}

/* Set the Pixel Aspect Ratio in our hdr from a DAR code in the data */
static void
set_par_from_dar (MPEGSeqHdr * hdr, guint8 asr_code)
{
  /* Pixel_width = DAR_width * display_vertical_size */
  /* Pixel_height = DAR_height * display_horizontal_size */
  switch (asr_code) {
    case 0x02:                 /* 3:4 DAR = 4:3 pixels */
      hdr->par_w = 4 * hdr->height;
      hdr->par_h = 3 * hdr->width;
      break;
    case 0x03:                 /* 9:16 DAR */
      hdr->par_w = 16 * hdr->height;
      hdr->par_h = 9 * hdr->width;
      break;
    case 0x04:                 /* 1:2.21 DAR */
      hdr->par_w = 221 * hdr->height;
      hdr->par_h = 100 * hdr->width;
      break;
    case 0x01:                 /* Square pixels */
    default:
      hdr->par_w = hdr->par_h = 1;
      break;
  }
}

gboolean
mpeg_util_parse_sequence_extension (MPEGSeqExtHdr * hdr, GstBuffer * buffer)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buffer);;

  /* skip sync word */
  if (!gst_bit_reader_skip (&reader, 8 * 4))
    return FALSE;

  /* skip extension code */
  if (!gst_bit_reader_skip (&reader, 4))
    return FALSE;

  /* skip profile and level escape bit */
  if (!gst_bit_reader_skip (&reader, 1))
    return FALSE;

  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->profile, 3))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->level, 4))
    return FALSE;

  /* skip progressive_sequence and chroma_format */
  if (!gst_bit_reader_skip (&reader, 3))
    return FALSE;

  /* resolution extension */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->horiz_size_ext, 2))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->vert_size_ext, 2))
    return FALSE;

  /* skip to framerate extension */
  if (!gst_bit_reader_skip (&reader, 22))
    return FALSE;

  /* framerate extension */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->fps_n_ext, 2))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint8 (&reader, &hdr->fps_d_ext, 2))
    return FALSE;

  return TRUE;
}

gboolean
mpeg_util_parse_sequence_hdr (MPEGSeqHdr * hdr, GstBuffer * buffer)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buffer);
  guint8 dar_idx, par_idx;
  guint8 load_intra_flag, load_non_intra_flag;

  /* skip sync word */
  if (!gst_bit_reader_skip (&reader, 8 * 4))
    return FALSE;

  /* resolution */
  if (!gst_bit_reader_get_bits_uint16 (&reader, &hdr->width, 12))
    return FALSE;
  if (!gst_bit_reader_get_bits_uint16 (&reader, &hdr->height, 12))
    return FALSE;

  /* aspect ratio */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &dar_idx, 4))
    return FALSE;
  set_par_from_dar (hdr, dar_idx);

  /* framerate */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &par_idx, 4))
    return FALSE;
  set_fps_from_code (hdr, par_idx);

  /* bitrate */
  if (!gst_bit_reader_get_bits_uint32 (&reader, &hdr->bitrate, 18))
    return FALSE;

  if (!gst_bit_reader_skip (&reader, 1))
    return FALSE;

  /* VBV buffer size */
  if (!gst_bit_reader_get_bits_uint16 (&reader, &hdr->vbv_buffer, 10))
    return FALSE;

  /* constrained parameters flag */
  if (!gst_bit_reader_get_bits_uint8 (&reader,
          &hdr->constrained_parameters_flag, 1))
    return FALSE;

  /* intra quantizer matrix */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &load_intra_flag, 1))
    return FALSE;
  if (load_intra_flag) {
    gint i;
    for (i = 0; i < 64; i++) {
      if (!gst_bit_reader_get_bits_uint8 (&reader,
              &hdr->intra_quantizer_matrix[mpeg2_scan[i]], 8))
        return FALSE;
    }
  } else
    memcpy (hdr->intra_quantizer_matrix, default_intra_quantizer_matrix, 64);

  /* non intra quantizer matrix */
  if (!gst_bit_reader_get_bits_uint8 (&reader, &load_non_intra_flag, 1))
    return FALSE;
  if (load_non_intra_flag) {
    gint i;
    for (i = 0; i < 64; i++) {
      if (!gst_bit_reader_get_bits_uint8 (&reader,
              &hdr->non_intra_quantizer_matrix[mpeg2_scan[i]], 8))
        return FALSE;
    }
  } else
    memset (hdr->non_intra_quantizer_matrix, 16, 64);

  return TRUE;
}

gboolean
mpeg_util_parse_picture_hdr (MPEGPictureHdr * hdr, guint8 * data, guint8 * end)
{
  guint32 code;

  if (G_UNLIKELY ((end - data) < 8))
    return FALSE;               /* Packet too small */

  code = GST_READ_UINT32_BE (data);
  if (G_UNLIKELY (code != (0x00000100 | MPEG_PACKET_PICTURE)))
    return FALSE;

  /* Skip the sync word */
  data += 4;

  hdr->pic_type = (data[1] >> 3) & 0x07;
  if (hdr->pic_type == 0 || hdr->pic_type > 4)
    return FALSE;               /* Corrupted picture packet */

  if (hdr->pic_type == P_FRAME || hdr->pic_type == B_FRAME) {
    if (G_UNLIKELY ((end - data) < 5))
      return FALSE;             /* packet too small */

    hdr->full_pel_forward_vector = read_bits (data + 3, 5, 1);
    hdr->f_code[0][0] = hdr->f_code[0][1] = read_bits (data + 3, 6, 3);

    if (hdr->pic_type == B_FRAME) {
      hdr->full_pel_backward_vector = read_bits (data + 4, 1, 1);
      hdr->f_code[1][0] = hdr->f_code[1][1] = read_bits (data + 4, 2, 3);
    } else
      hdr->full_pel_backward_vector = 0;
  } else {
    hdr->full_pel_forward_vector = 0;
    hdr->full_pel_backward_vector = 0;
  }

  return TRUE;
}

gboolean
mpeg_util_parse_picture_coding_extension (MPEGPictureExt * ext, guint8 * data,
    guint8 * end)
{
  guint32 code;

  if (G_UNLIKELY ((end - data) < 9))
    return FALSE;               /* Packet too small */

  code = GST_READ_UINT32_BE (data);

  if (G_UNLIKELY (G_UNLIKELY (code != (0x00000100 | MPEG_PACKET_EXTENSION))))
    return FALSE;

  /* Skip the sync word */
  data += 4;

  ext->f_code[0][0] = read_bits (data, 4, 4);
  ext->f_code[0][1] = read_bits (data + 1, 0, 4);
  ext->f_code[1][0] = read_bits (data + 1, 4, 4);
  ext->f_code[1][1] = read_bits (data + 2, 0, 4);

  ext->intra_dc_precision = read_bits (data + 2, 4, 2);
  ext->picture_structure = read_bits (data + 2, 6, 2);
  ext->top_field_first = read_bits (data + 3, 0, 1);
  ext->frame_pred_frame_dct = read_bits (data + 3, 1, 1);
  ext->concealment_motion_vectors = read_bits (data + 3, 2, 1);
  ext->q_scale_type = read_bits (data + 3, 3, 1);
  ext->intra_vlc_format = read_bits (data + 3, 4, 1);
  ext->alternate_scan = read_bits (data + 3, 5, 1);

  return TRUE;
}

gboolean
mpeg_util_parse_picture_gop (MPEGPictureGOP * gop, guint8 * data, guint8 * end)
{
  guint32 code;

  if (G_UNLIKELY ((end - data) < 8))
    return FALSE;               /* Packet too small */

  code = GST_READ_UINT32_BE (data);

  if (G_UNLIKELY (G_UNLIKELY (code != (0x00000100 | MPEG_PACKET_GOP))))
    return FALSE;

  /* Skip the sync word */
  data += 4;

  gop->drop_frame_flag = read_bits (data, 0, 1);

  gop->hour = read_bits (data, 1, 5);
  gop->minute = read_bits (data, 6, 6);
  gop->second = read_bits (data + 1, 4, 6);
  gop->frame = read_bits (data + 2, 3, 6);

  gop->closed_gop = read_bits (data + 3, 1, 1);
  gop->broken_gop = read_bits (data + 3, 2, 1);

  return TRUE;
}

gboolean
mpeg_util_parse_quant_matrix (MPEGQuantMatrix * qm, guint8 * data, guint8 * end)
{
  guint32 code;
  gboolean load_intra_flag, load_non_intra_flag;
  gint i;

  if (G_UNLIKELY ((end - data) < 5))
    return FALSE;               /* Packet too small */

  code = GST_READ_UINT32_BE (data);

  if (G_UNLIKELY (G_UNLIKELY (code != (0x00000100 | MPEG_PACKET_EXTENSION))))
    return FALSE;

  /* Skip the sync word */
  data += 4;

  load_intra_flag = read_bits (data, 4, 1);
  if (load_intra_flag) {
    if (G_UNLIKELY ((end - data) < 64))
      return FALSE;
    for (i = 0; i < 64; i++)
      qm->intra_quantizer_matrix[mpeg2_scan[i]] = read_bits (data + i, 5, 8);

    data += 64;
  } else
    memcpy (qm->intra_quantizer_matrix, default_intra_quantizer_matrix, 64);

  load_non_intra_flag = read_bits (data, 5 + load_intra_flag, 1);
  if (load_non_intra_flag) {
    if (G_UNLIKELY ((end - data) < 64))
      return FALSE;
    for (i = 0; i < 64; i++)
      qm->non_intra_quantizer_matrix[mpeg2_scan[i]] =
          read_bits (data + i, 6 + load_intra_flag, 8);
  } else
    memset (qm->non_intra_quantizer_matrix, 16, 64);

  return TRUE;
}
