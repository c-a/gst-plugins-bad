/* GStreamer
 *
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>

#include "gstnalreader.h"

#ifndef fail_unless_equals_int64
#define fail_unless_equals_int64(a, b)					\
G_STMT_START {								\
  gint64 first = a;							\
  gint64 second = b;							\
  fail_unless(first == second,						\
    "'" #a "' (%" G_GINT64_FORMAT ") is not equal to '" #b"' (%"	\
    G_GINT64_FORMAT ")", first, second);				\
} G_STMT_END;
#endif


GST_START_TEST (test_initialization)
{
  guint8 data[] = { 0x01, 0x02, 0x03, 0x04 };
  GstBuffer *buffer = gst_buffer_new ();
  GstNalReader reader = GST_NAL_READER_INIT (data, 4);
  GstNalReader *reader2;
  guint8 x;

  GST_BUFFER_DATA (buffer) = data;
  GST_BUFFER_SIZE (buffer) = 4;

  fail_unless (gst_nal_reader_get_bits_uint8 (&reader, &x, 8));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_nal_reader_get_bits_uint8 (&reader, &x, 8));
  fail_unless_equals_int (x, 0x02);

  memset (&reader, 0, sizeof (GstNalReader));

  gst_nal_reader_init (&reader, data, 4);
  fail_unless (gst_nal_reader_get_bits_uint8 (&reader, &x, 8));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_nal_reader_get_bits_uint8 (&reader, &x, 8));
  fail_unless_equals_int (x, 0x02);

  gst_nal_reader_init_from_buffer (&reader, buffer);
  fail_unless (gst_nal_reader_get_bits_uint8 (&reader, &x, 8));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_nal_reader_get_bits_uint8 (&reader, &x, 8));
  fail_unless_equals_int (x, 0x02);

  reader2 = gst_nal_reader_new (data, 4);
  fail_unless (gst_nal_reader_get_bits_uint8 (reader2, &x, 8));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_nal_reader_get_bits_uint8 (reader2, &x, 8));
  fail_unless_equals_int (x, 0x02);
  gst_nal_reader_free (reader2);

  reader2 = gst_nal_reader_new_from_buffer (buffer);
  fail_unless (gst_nal_reader_get_bits_uint8 (reader2, &x, 8));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_nal_reader_get_bits_uint8 (reader2, &x, 8));
  fail_unless_equals_int (x, 0x02);
  gst_nal_reader_free (reader2);

  gst_buffer_unref (buffer);
}

GST_END_TEST;

#define GET_CHECK(reader, dest, bits, nbits, val) { \
  fail_unless (gst_nal_reader_get_bits_uint##bits (reader, &dest, nbits)); \
  fail_unless_equals_uint64 (dest, val); \
}

#define PEEK_CHECK(reader, dest, bits, nbits, val) { \
  fail_unless (gst_nal_reader_peek_bits_uint##bits (reader, &dest, nbits)); \
  fail_unless_equals_uint64 (dest, val); \
}

#define GET_CHECK_FAIL(reader, dest, bits, nbits) { \
  fail_if (gst_nal_reader_get_bits_uint##bits (reader, &dest, nbits)); \
}

#define PEEK_CHECK_FAIL(reader, dest, bits, nbits) { \
  fail_if (gst_nal_reader_peek_bits_uint##bits (reader, &dest, nbits)); \
}

#define GET_CHECK_UE(reader, val) { \
  guint32 dest; \
  fail_unless (gst_nal_reader_get_ue (reader, &dest)); \
  fail_unless_equals_uint64 (dest, val); \
}

GST_START_TEST (test_get_bits)
{
  guint8 data[] = { 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef,
    0xfe, 0xdc, 0xba, 0x09, 0x87, 0x65, 0x43, 0x21
  };
  GstNalReader reader = GST_NAL_READER_INIT (data, 16);
  guint8 a;
  guint16 b;
  guint32 c;
  guint64 d;

  /* 8 bit */
  GET_CHECK (&reader, a, 8, 8, 0x12);
  GET_CHECK (&reader, a, 8, 4, 0x03);
  GET_CHECK (&reader, a, 8, 4, 0x04);
  GET_CHECK (&reader, a, 8, 3, 0x02);
  GET_CHECK (&reader, a, 8, 1, 0x01);
  GET_CHECK (&reader, a, 8, 2, 0x01);
  GET_CHECK (&reader, a, 8, 2, 0x02);

  PEEK_CHECK (&reader, a, 8, 8, 0x78);
  PEEK_CHECK (&reader, a, 8, 8, 0x78);
  fail_unless (gst_nal_reader_skip (&reader, 8));

  PEEK_CHECK (&reader, a, 8, 8, 0x90);
  GET_CHECK (&reader, a, 8, 1, 0x01);
  GET_CHECK (&reader, a, 8, 1, 0x00);
  GET_CHECK (&reader, a, 8, 1, 0x00);
  GET_CHECK (&reader, a, 8, 1, 0x01);
  fail_unless (gst_nal_reader_skip (&reader, 4));

  fail_unless (gst_nal_reader_skip (&reader, 10 * 8));
  GET_CHECK (&reader, a, 8, 8, 0x21);
  GET_CHECK_FAIL (&reader, a, 8, 1);
  PEEK_CHECK_FAIL (&reader, a, 8, 1);

  /* 16 bit */
  gst_nal_reader_init (&reader, data, 16);

  GET_CHECK (&reader, b, 16, 16, 0x1234);
  PEEK_CHECK (&reader, b, 16, 13, 0x0acf);
  GET_CHECK (&reader, b, 16, 8, 0x56);
  GET_CHECK (&reader, b, 16, 4, 0x07);
  GET_CHECK (&reader, b, 16, 2, 0x02);
  GET_CHECK (&reader, b, 16, 2, 0x00);
  PEEK_CHECK (&reader, b, 16, 8, 0x90);
  fail_unless (gst_nal_reader_skip (&reader, 11 * 8));
  GET_CHECK (&reader, b, 16, 8, 0x21);
  GET_CHECK_FAIL (&reader, b, 16, 16);
  PEEK_CHECK_FAIL (&reader, b, 16, 16);

  /* 32 bit */
  gst_nal_reader_init (&reader, data, 16);

  GET_CHECK (&reader, c, 32, 32, 0x12345678);
  GET_CHECK (&reader, c, 32, 24, 0x90abcd);
  GET_CHECK (&reader, c, 32, 16, 0xeffe);
  GET_CHECK (&reader, c, 32, 8, 0xdc);
  GET_CHECK (&reader, c, 32, 4, 0x0b);
  GET_CHECK (&reader, c, 32, 2, 0x02);
  GET_CHECK (&reader, c, 32, 2, 0x02);
  PEEK_CHECK (&reader, c, 32, 8, 0x09);
  fail_unless (gst_nal_reader_skip (&reader, 3 * 8));
  GET_CHECK (&reader, c, 32, 15, 0x2190);
  GET_CHECK (&reader, c, 32, 1, 0x1);
  GET_CHECK_FAIL (&reader, c, 32, 1);

  /* 64 bit */
  gst_nal_reader_init (&reader, data, 16);

  GET_CHECK (&reader, d, 64, 64, G_GINT64_CONSTANT (0x1234567890abcdef));
  GET_CHECK (&reader, d, 64, 7, 0xfe >> 1);
  GET_CHECK (&reader, d, 64, 1, 0x00);
  GET_CHECK (&reader, d, 64, 24, 0xdcba09);
  GET_CHECK (&reader, d, 64, 32, 0x87654321);
  GET_CHECK_FAIL (&reader, d, 64, 32);
}

GST_END_TEST;

GST_START_TEST (test_emulation_prevention_skipping)
{
  guint8 data[] = { 0x00, 0x00, 0x03, 0xff };

  GstNalReader reader = GST_NAL_READER_INIT (data, 4);
  guint8 byte;
  guint32 word;

  /* read one byte at a time */
  GET_CHECK (&reader, byte, 8, 8, 0x00);
  GET_CHECK (&reader, byte, 8, 8, 0x00);
  GET_CHECK (&reader, byte, 8, 8, 0xff);
  GET_CHECK_FAIL (&reader, byte, 8, 8);

  /* test reading the whole at once */
  gst_nal_reader_init (&reader, data, 4);
  GET_CHECK (&reader, word, 32, 24, 0x0000ff);

  /* test reading more than what's really there */
  gst_nal_reader_init (&reader, data, 4);
  GET_CHECK_FAIL (&reader, word, 32, 32);

  /* test skipping */
  gst_nal_reader_init (&reader, data, 4);
  fail_unless (gst_nal_reader_skip (&reader, 16));
  GET_CHECK (&reader, byte, 8, 8, 0xff);

  gst_nal_reader_init (&reader, data, 4);
  fail_unless (gst_nal_reader_skip (&reader, 24));
  GET_CHECK_FAIL (&reader, byte, 8, 1);
}

GST_END_TEST;

GST_START_TEST (test_golomb)
{
  guint8 data[] = { 0x00, 0x00, 0x03, 0xff, 0xff, 0xa5 };

  GstNalReader reader = GST_NAL_READER_INIT (data, 6);
  guint8 byte;

  GET_CHECK_UE (&reader, (0xffffa5 >> 7) - 1);
  GET_CHECK (&reader, byte, 8, 7, 0x25);
  GET_CHECK_FAIL (&reader, byte, 8, 1);
}

GST_END_TEST;

#undef GET_CHECK
#undef PEEK_CHECK
#undef GET_CHECK_FAIL
#undef PEEK_CHECK_FAIL
#undef GET_CHECK_UE

static Suite *
gst_nal_reader_suite (void)
{
  Suite *s = suite_create ("GstNalReader");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_initialization);
  tcase_add_test (tc_chain, test_get_bits);
  tcase_add_test (tc_chain, test_emulation_prevention_skipping);
  tcase_add_test (tc_chain, test_golomb);

  return s;
}


GST_CHECK_MAIN (gst_nal_reader);
