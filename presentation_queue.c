/*
 * Copyright (c) 2013 Jens Kuske <jenskuske@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef DEF_RENDERX11
#define DRI2 1
#endif /*DEF_RENDERX11*/
#include "vdpau_private.h"
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include "kernel-headers/sunxi_disp_ioctl.h"
#include "ve.h"
#include <errno.h>
#include <stdio.h>
#include "sunxi_disp.h"

uint64_t get_time(void)
{
	struct timespec tp;

	if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1)
		return 0;

	return (uint64_t)tp.tv_sec * 1000000000ULL + (uint64_t)tp.tv_nsec;
}

VdpStatus vdp_presentation_queue_target_create_x11(VdpDevice device, Drawable drawable, VdpPresentationQueueTarget *target)
{
    if (!target /* || !drawable */)
        return VDP_STATUS_INVALID_POINTER;

    device_ctx_t *dev = handle_get(device);
    if (!dev)
        return VDP_STATUS_INVALID_HANDLE;

    fprintf(stderr, "%s: %d\n", __func__, __LINE__);
    queue_target_ctx_t *qt = handle_create(sizeof(*qt), target, htype_presentation_target);
    if (!qt)
    {
        handle_release(device);
        return VDP_STATUS_RESOURCES;
    }

    fprintf(stderr, "%s: %d\n", __func__, __LINE__);
    qt->drawable = drawable;
#ifndef DEF_LEGACYDISP
#ifdef DEF_RENDERX11 
    qt->disp = sunxi_dispx11_open(dev->display, qt->drawable);
#endif /*DEF_RENDERX11*/
#else /*DEF_LEGACYDISP*/
    qt->disp = sunxi_disp2_open();
    if (!qt->disp) qt->disp = sunxi_disp0_open();
#endif /*DEF_LEGACYDISP*/
    if (!qt->disp) {
        handle_release(device);
        handle_destroy(*target);
        return VDP_STATUS_ERROR;
    }
    printf("vdpau presentation target queue=%d created\n", *target);

    handle_release(device);
    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_target_destroy(VdpPresentationQueueTarget presentation_queue_target)
{
	queue_target_ctx_t *qt = handle_get(presentation_queue_target);
	if (!qt)
		return VDP_STATUS_INVALID_HANDLE;

        qt->disp->close(qt->disp);

        handle_release(presentation_queue_target);
	handle_destroy(presentation_queue_target);

        printf("vdpau presentation target queue=%d destroyed\n", presentation_queue_target);
        return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_create(VdpDevice device, VdpPresentationQueueTarget presentation_queue_target, VdpPresentationQueue *presentation_queue)
{
	if (!presentation_queue)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	queue_target_ctx_t *qt = handle_get(presentation_queue_target);
	if (!qt)
        {
                handle_release(device);
		return VDP_STATUS_INVALID_HANDLE;
        }

	queue_ctx_t *q = handle_create(sizeof(*q), presentation_queue, htype_presentation);
	if (!q)
        {
                handle_release(device);
                handle_release(presentation_queue_target);
		return VDP_STATUS_RESOURCES;
        }

	//keep refcnt from handle_get increased, decrease when destroying the presentation queue
        q->target = qt;
	q->device = dev;
        q->target_hdl = presentation_queue_target;
        q->device_hdl = device;
        
        printf("vdpau presentation queue=%d created\n", *presentation_queue);

        return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_destroy(VdpPresentationQueue presentation_queue)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

        handle_release(q->target_hdl);
        handle_release(q->device_hdl);
        handle_release(presentation_queue);
	handle_destroy(presentation_queue);

        printf("vdpau presentation queue=%d destroyed\n", presentation_queue);
        return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_set_background_color(VdpPresentationQueue presentation_queue, VdpColor *const background_color)
{
	if (!background_color)
		return VDP_STATUS_INVALID_POINTER;

	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	q->background.red = background_color->red;
	q->background.green = background_color->green;
	q->background.blue = background_color->blue;
	q->background.alpha = background_color->alpha;

	handle_release(presentation_queue);
        return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_get_background_color(VdpPresentationQueue presentation_queue, VdpColor *const background_color)
{
	if (!background_color)
		return VDP_STATUS_INVALID_POINTER;

	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	background_color->red = q->background.red;
	background_color->green = q->background.green;
	background_color->blue = q->background.blue;
	background_color->alpha = q->background.alpha;

        handle_release(presentation_queue);
	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_get_time(VdpPresentationQueue presentation_queue, VdpTime *current_time)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	*current_time = get_time();
        handle_release(presentation_queue);
	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_display(VdpPresentationQueue presentation_queue, VdpOutputSurface surface, uint32_t clip_width, uint32_t clip_height, VdpTime earliest_presentation_time)
{
	int x=0,y=0;
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	output_surface_ctx_t *os = handle_get(surface);
	if (!os)
        {
                handle_release(presentation_queue);
		return VDP_STATUS_INVALID_HANDLE;
        }

	if (!(os->vs))
	{
		printf("trying to display empty surface\n");
		VDPAU_DBG("trying to display empty surface");
                handle_release(presentation_queue);
                handle_release(surface);
		return VDP_STATUS_OK;
	}

	if (earliest_presentation_time != 0)
		VDPAU_DBG_ONCE("Presentation time not supported");

	Window c;
	XTranslateCoordinates(q->device->display, q->target->drawable, RootWindow(q->device->display, q->device->screen), 0, 0, &x, &y, &c);
	//XClearWindow(q->device->display, q->target->drawable);
	fprintf(stderr, "%s: %d - %d %d - %d %d\n", __func__, __LINE__, x, y, clip_width, clip_height);

	if (os->vs)
		q->target->disp->set_video_layer(q->target->disp, x, y, clip_width, clip_height, os);
	else
		q->target->disp->close_video_layer(q->target->disp);

#if 0
	if (q->device->osd_enabled) {
	  if (os->rgba.flags & RGBA_FLAG_NEEDS_CLEAR)
	    rgba_clear(&os->rgba);

	  if (os->rgba.flags & RGBA_FLAG_DIRTY)
	    {
	      rgba_flush(&os->rgba);

	      q->target->disp->set_osd_layer(q->target->disp, x, y, clip_width, clip_height, os);
	    }
	  else
	    {
	      q->target->disp->close_osd_layer(q->target->disp);
	    }
	}
#endif /*0*/


	//printf("%s: p_q=%d,o_s=%d\n", __FUNCTION__, presentation_queue, surface);

	//Window c;
        handle_release(presentation_queue);
        handle_release(surface);
	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_block_until_surface_idle(VdpPresentationQueue presentation_queue, VdpOutputSurface surface, VdpTime *first_presentation_time)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	output_surface_ctx_t *out = handle_get(surface);
	if (!out)
        {
                handle_release(presentation_queue);
		return VDP_STATUS_INVALID_HANDLE;
        }

	*first_presentation_time = get_time();

        handle_release(presentation_queue);
        handle_release(surface);
	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_query_surface_status(VdpPresentationQueue presentation_queue, VdpOutputSurface surface, VdpPresentationQueueStatus *status, VdpTime *first_presentation_time)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	output_surface_ctx_t *out = handle_get(surface);
	if (!out)
        {
                handle_release(presentation_queue);
		return VDP_STATUS_INVALID_HANDLE;
        }

	*status = VDP_PRESENTATION_QUEUE_STATUS_VISIBLE;
	*first_presentation_time = get_time();

        handle_release(presentation_queue);
        handle_release(surface);
	return VDP_STATUS_OK;
}
