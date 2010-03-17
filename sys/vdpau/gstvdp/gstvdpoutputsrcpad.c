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

#include <gst/cairo/gstcairo.h>
#include <cairo/cairo-xlib.h>

#include "gstvdputils.h"
#include "gstvdpvideobuffer.h"
#include "gstvdpoutputbufferpool.h"

#include "gstvdpoutputsrcpad.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_output_src_pad_debug);
#define GST_CAT_DEFAULT gst_vdp_output_src_pad_debug

enum
{
  PROP_0,
  PROP_DEVICE
};

typedef enum _GstVdpOutputSrcPadFormat GstVdpOutputSrcPadFormat;

enum _GstVdpOutputSrcPadFormat
{
  GST_VDP_OUTPUT_SRC_PAD_FORMAT_RGB,
  GST_VDP_OUTPUT_SRC_PAD_FORMAT_CAIRO,
  GST_VDP_OUTPUT_SRC_PAD_FORMAT_VDPAU
};

struct _GstVdpOutputSrcPad
{
  GstPad pad;

  GstCaps *caps;

  GstCaps *output_caps;
  GstVdpOutputSrcPadFormat output_format;
  VdpRGBAFormat rgba_format;
  gint width, height;

  GstVdpBufferPool *bpool;
  gboolean lock_caps;
  
  /* cairo output */
  Visual *visual;
  Drawable drawable;
  gint depth;

  /* properties */
  GstVdpDevice *device;
};

struct _GstVdpOutputSrcPadClass
{
  GstPadClass pad_class;
};

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_output_src_pad_debug, "vdpoutputsrcpad", 0, "GstVdpOutputSrcPad");

G_DEFINE_TYPE_WITH_CODE (GstVdpOutputSrcPad, gst_vdp_output_src_pad,
    GST_TYPE_PAD, DEBUG_INIT ());

static GstFlowReturn
gst_vdp_output_src_pad_download_to_cairo (GstVdpOutputSrcPad * vdp_pad,
    GstVdpOutputBuffer * output_buf, GstBuffer ** outbuf, GError ** error)
{
  GstVdpDevice *device;
  Pixmap pixmap;

  VdpStatus status;
  VdpPresentationQueueTarget target;
  VdpPresentationQueue queue;

  cairo_surface_t *cairo_surface, *output_surface;
  GstCairoFormat *cairo_format;
  cairo_t *cr;

  device = vdp_pad->device;

  pixmap = XCreatePixmap (device->display, vdp_pad->drawable,
      vdp_pad->width, vdp_pad->height, vdp_pad->depth);

  status = device->vdp_presentation_queue_target_create_x11 (device->device,
      pixmap, &target);
  if (status != VDP_STATUS_OK)
    goto presentation_target_error;

  status =
      device->vdp_presentation_queue_create (device->device, target, &queue);
  if (status != VDP_STATUS_OK)
    goto presentation_queue_error;

  status = device->vdp_presentation_queue_display (queue,
      output_buf->surface, 0, 0, 0);
  if (status != VDP_STATUS_OK)
    goto display_error;

  device->vdp_presentation_queue_destroy (queue);
  device->vdp_presentation_queue_target_destroy (target);

  cairo_surface = cairo_xlib_surface_create (device->display, (Drawable) pixmap,
      vdp_pad->visual, vdp_pad->width, vdp_pad->height);
  if (cairo_surface_status (cairo_surface) != CAIRO_STATUS_SUCCESS)
    goto surface_create_error;

  cairo_format = gst_cairo_format_new (GST_PAD_CAPS (vdp_pad));
  *outbuf = gst_cairo_buffer_new_similar (cairo_surface, cairo_format);

  output_surface = gst_cairo_create_surface (*outbuf, cairo_format);
  gst_cairo_format_free (cairo_format);

  cr = cairo_create (output_surface);
  cairo_set_source_surface (cr, cairo_surface, 0.0, 0.0);
  cairo_paint (cr);
  cairo_destroy (cr);

  cairo_surface_destroy (output_surface);
  cairo_surface_destroy (cairo_surface);

  XFreePixmap (device->display, pixmap);

  return GST_FLOW_OK;

presentation_target_error:
  XFreePixmap (device->display, pixmap);

  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_OPEN_WRITE,
      "Could not create presentation target, "
      "error returned from vdpau was: %s",
      device->vdp_get_error_string (status));
  return GST_FLOW_ERROR;

presentation_queue_error:
  XFreePixmap (device->display, pixmap);
  device->vdp_presentation_queue_target_destroy (target);

  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_OPEN_WRITE,
      "Could not create presentation queue, "
      "error returned from vdpau was: %s",
      device->vdp_get_error_string (status));
  return GST_FLOW_ERROR;

display_error:
  XFreePixmap (device->display, pixmap);
  device->vdp_presentation_queue_target_destroy (target);
  device->vdp_presentation_queue_destroy (queue);

  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_WRITE,
      "Could not display frame, "
      "error returned from vdpau was: %s",
      device->vdp_get_error_string (status));
  return GST_FLOW_ERROR;

surface_create_error:
  XFreePixmap (device->display, pixmap);

  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_OPEN_WRITE,
      "Could not create cairo surface, "
      "error returned from cairo was: %s",
      cairo_status_to_string (cairo_surface_status (cairo_surface)));
  cairo_surface_destroy (cairo_surface);

  return GST_FLOW_ERROR;

}

GstFlowReturn
gst_vdp_output_src_pad_push (GstVdpOutputSrcPad * vdp_pad,
    GstVdpOutputBuffer * output_buf, GError ** error)
{
  GstPad *pad;
  GstBuffer *outbuf;

  g_return_val_if_fail (GST_IS_VDP_OUTPUT_SRC_PAD (vdp_pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_VDP_OUTPUT_BUFFER (output_buf), GST_FLOW_ERROR);

  pad = (GstPad *) vdp_pad;

  if (G_UNLIKELY (!GST_PAD_CAPS (pad)))
    return GST_FLOW_NOT_NEGOTIATED;

  switch (vdp_pad->output_format) {
    case GST_VDP_OUTPUT_SRC_PAD_FORMAT_RGB:
    {
      GstFlowReturn ret;
      guint size;

      gst_vdp_output_buffer_calculate_size (output_buf, &size);

      vdp_pad->lock_caps = TRUE;
      ret = gst_pad_alloc_buffer (pad, 0, size, GST_PAD_CAPS (vdp_pad),
          &outbuf);
      vdp_pad->lock_caps = FALSE;

      if (ret != GST_FLOW_OK) {
        gst_buffer_unref (GST_BUFFER_CAST (output_buf));
        return ret;
      }

      if (!gst_vdp_output_buffer_download (output_buf, outbuf, error)) {
        gst_buffer_unref (GST_BUFFER_CAST (output_buf));
        gst_buffer_unref (outbuf);
        return GST_FLOW_ERROR;
      }

      gst_buffer_copy_metadata (outbuf, (const GstBuffer *) output_buf,
          GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);
      gst_buffer_unref (GST_BUFFER_CAST (output_buf));
      break;
    }
    case GST_VDP_OUTPUT_SRC_PAD_FORMAT_CAIRO:
    {
      GstFlowReturn ret;

      ret = gst_vdp_output_src_pad_download_to_cairo (vdp_pad, output_buf,
          &outbuf, error);
      gst_buffer_unref (GST_BUFFER_CAST (output_buf));
      if (ret != GST_FLOW_OK)
        return ret;

      break;
    }
    case GST_VDP_OUTPUT_SRC_PAD_FORMAT_VDPAU:
    {
      outbuf = GST_BUFFER_CAST (output_buf);
      break;
    }

    default:
      g_assert_not_reached ();
      break;
  }

  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (vdp_pad));

  return gst_pad_push (pad, outbuf);
}

static GstFlowReturn
gst_vdp_output_src_pad_create_buffer (GstVdpOutputSrcPad * vdp_pad,
    GstVdpOutputBuffer ** output_buf, GError ** error)
{
  GstFlowReturn ret;
  GstBuffer *neg_buf;

  /* negotiate */
  ret = gst_pad_alloc_buffer_and_set_caps (GST_PAD_CAST (vdp_pad),
      GST_BUFFER_OFFSET_NONE, 0, GST_PAD_CAPS (vdp_pad), &neg_buf);
  if (ret == GST_FLOW_OK)
    gst_buffer_unref (neg_buf);

  *output_buf =
      (GstVdpOutputBuffer *) gst_vdp_buffer_pool_get_buffer (vdp_pad->bpool,
      error);
  if (!*output_buf)
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vdp_output_src_pad_alloc_with_caps (GstVdpOutputSrcPad * vdp_pad,
    GstCaps * caps, GstVdpOutputBuffer ** output_buf, GError ** error)
{
  GstFlowReturn ret;

  ret = gst_pad_alloc_buffer_and_set_caps ((GstPad *) vdp_pad, 0, 0, caps,
      (GstBuffer **) output_buf);
  if (ret != GST_FLOW_OK)
    return ret;

  if (!GST_IS_VDP_OUTPUT_BUFFER (*output_buf))
    goto invalid_buf;

  return GST_FLOW_OK;

invalid_buf:
  gst_buffer_unref (GST_BUFFER (*output_buf));
  g_set_error (error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
      "Sink element returned buffer of wrong type");
  return GST_FLOW_ERROR;
}

GstFlowReturn
gst_vdp_output_src_pad_alloc_buffer (GstVdpOutputSrcPad * vdp_pad,
    GstVdpOutputBuffer ** output_buf, GError ** error)
{
  GstCaps *caps;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_VDP_OUTPUT_SRC_PAD (vdp_pad), GST_FLOW_ERROR);

  caps = GST_PAD_CAPS (vdp_pad);
  if (!caps)
    return GST_FLOW_NOT_NEGOTIATED;

  switch (vdp_pad->output_format) {
    case GST_VDP_OUTPUT_SRC_PAD_FORMAT_RGB:
    case GST_VDP_OUTPUT_SRC_PAD_FORMAT_CAIRO:
    {
      ret = gst_vdp_output_src_pad_create_buffer (vdp_pad, output_buf, error);
      if (ret != GST_FLOW_OK)
        return ret;

      break;
    }

    case GST_VDP_OUTPUT_SRC_PAD_FORMAT_VDPAU:
    {
      ret = gst_vdp_output_src_pad_alloc_with_caps (vdp_pad, caps, output_buf,
          error);
      if (ret != GST_FLOW_OK)
        return ret;

      break;
    }

    default:
      g_assert_not_reached ();
      break;
  }

  return GST_FLOW_OK;

}

static gboolean
gst_vdp_output_src_pad_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstVdpOutputSrcPad *vdp_pad = GST_VDP_OUTPUT_SRC_PAD (pad);

  if (!vdp_pad->lock_caps)
    return TRUE;

  return gst_caps_is_equal_fixed (caps, GST_PAD_CAPS (pad));
}

static gboolean
gst_vdp_output_src_pad_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVdpOutputSrcPad *vdp_pad = GST_VDP_OUTPUT_SRC_PAD (pad);
  const GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &vdp_pad->width))
    return FALSE;
  if (!gst_structure_get_int (structure, "height", &vdp_pad->height))
    return FALSE;

  if (gst_structure_has_name (structure, "video/x-raw-rgb")) {
    if (!gst_vdp_caps_to_rgba_format (caps, &vdp_pad->rgba_format))
      return FALSE;

    /* create buffer pool if we dont't have one */
    if (!vdp_pad->bpool)
      vdp_pad->bpool = gst_vdp_output_buffer_pool_new (vdp_pad->device);

    if (vdp_pad->output_caps)
      gst_caps_unref (vdp_pad->output_caps);

    vdp_pad->output_caps = gst_caps_new_simple ("video/x-vdpau-output",
        "rgba-format", G_TYPE_INT, vdp_pad->rgba_format,
        "width", G_TYPE_INT, vdp_pad->width, "height", G_TYPE_INT,
        vdp_pad->height, NULL);
    gst_vdp_buffer_pool_set_caps (vdp_pad->bpool, vdp_pad->output_caps);

    vdp_pad->output_format = GST_VDP_OUTPUT_SRC_PAD_FORMAT_RGB;
  } else if (gst_structure_has_name (structure, "video/x-cairo")) {
    vdp_pad->output_format = GST_VDP_OUTPUT_SRC_PAD_FORMAT_CAIRO;
    vdp_pad->rgba_format = VDP_RGBA_FORMAT_R10G10B10A2;
  } else if (gst_structure_has_name (structure, "video/x-vdpau-output")) {
    if (!gst_structure_get_int (structure, "rgba-format",
            (gint *) & vdp_pad->rgba_format))
      return FALSE;

    /* don't need the buffer pool */
    if (vdp_pad->bpool) {
      gst_object_unref (vdp_pad->bpool);
      vdp_pad->bpool = NULL;
    }

    vdp_pad->output_format = GST_VDP_OUTPUT_SRC_PAD_FORMAT_VDPAU;
  } else
    return FALSE;

  return TRUE;
}

static GstCaps *
gst_vdp_output_src_pad_getcaps (GstPad * pad)
{
  GstVdpOutputSrcPad *vdp_pad = (GstVdpOutputSrcPad *) pad;

  const GstCaps *templ_caps;

  if (vdp_pad->caps)
    return gst_caps_ref (vdp_pad->caps);

  else if ((templ_caps = gst_pad_get_pad_template_caps (pad)))
    return gst_caps_copy (templ_caps);

  return NULL;
}

static gboolean
gst_vdp_output_src_pad_activate_push (GstPad * pad, gboolean active)
{
  GstVdpOutputSrcPad *vdp_pad = GST_VDP_OUTPUT_SRC_PAD (pad);

  if (!active) {
    if (vdp_pad->caps)
      gst_caps_unref (vdp_pad->caps);
    vdp_pad->caps = NULL;

    if (vdp_pad->output_caps)
      gst_caps_unref (vdp_pad->output_caps);
    vdp_pad->output_caps = NULL;

    if (vdp_pad->bpool)
      g_object_unref (vdp_pad->bpool);
    vdp_pad->bpool = NULL;

    if (vdp_pad->device)
      g_object_unref (vdp_pad->device);
    vdp_pad->device = NULL;
  }

  return TRUE;
}

GstVdpOutputSrcPad *
gst_vdp_output_src_pad_new (GstPadTemplate * templ, const gchar * name)
{
  return g_object_new (GST_TYPE_VDP_OUTPUT_SRC_PAD, "name", name,
      "template", templ, "direction", GST_PAD_SRC, NULL);
}

static void
gst_vdp_output_src_pad_set_device (GstVdpOutputSrcPad * vdp_pad,
    GstVdpDevice * device)
{
  gint screen_number;
  GstCaps *caps;
  const GstCaps *templ_caps;

  if (vdp_pad->device)
    g_object_unref (vdp_pad->device);
  vdp_pad->device = device;

  screen_number = DefaultScreen (device->display);
  vdp_pad->visual = DefaultVisual (device->display, screen_number);
  vdp_pad->drawable = (Drawable) DefaultRootWindow (device->display);
  vdp_pad->depth = DefaultDepth (device->display, screen_number);
  
  /* update caps */
  if (vdp_pad->caps)
    gst_caps_unref (vdp_pad->caps);

  caps = gst_vdp_video_buffer_get_allowed_caps (device);

  if ((templ_caps = gst_pad_get_pad_template_caps (GST_PAD (vdp_pad)))) {
    vdp_pad->caps = gst_caps_intersect (caps, templ_caps);
    gst_caps_unref (caps);
  } else
    vdp_pad->caps = caps;
}

static void
gst_vdp_output_src_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVdpOutputSrcPad *vdp_pad = (GstVdpOutputSrcPad *) object;

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_object (value, vdp_pad->device);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_output_src_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpOutputSrcPad *vdp_pad = (GstVdpOutputSrcPad *) object;

  switch (prop_id) {
    case PROP_DEVICE:
      gst_vdp_output_src_pad_set_device (vdp_pad, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_output_src_pad_init (GstVdpOutputSrcPad * vdp_pad)
{
  GstPad *pad = GST_PAD (vdp_pad);

  vdp_pad->caps = NULL;
  vdp_pad->output_caps = NULL;
  vdp_pad->bpool = NULL;
  vdp_pad->device = NULL;

  vdp_pad->lock_caps = FALSE;

  gst_pad_set_getcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_vdp_output_src_pad_getcaps));
  gst_pad_set_setcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_vdp_output_src_pad_setcaps));
  gst_pad_set_acceptcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_vdp_output_src_pad_acceptcaps));

  gst_pad_set_activatepush_function (pad,
      GST_DEBUG_FUNCPTR (gst_vdp_output_src_pad_activate_push));
}

static void
gst_vdp_output_src_pad_class_init (GstVdpOutputSrcPadClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = gst_vdp_output_src_pad_get_property;
  object_class->set_property = gst_vdp_output_src_pad_set_property;

  /**
   * GstVdpVideoSrcPad:device:
   *
   * The #GstVdpDevice this pool is bound to.
   */
  g_object_class_install_property
      (object_class,
      PROP_DEVICE,
      g_param_spec_object ("device",
          "Device",
          "The GstVdpDevice the pad should use",
          GST_TYPE_VDP_DEVICE, G_PARAM_READWRITE));
}
