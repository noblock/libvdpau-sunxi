/*
 * Copyright (c) 2015-2016 Jens Kuske <jenskuske@gmail.com>
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

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "vdpau_private.h"
#include "sunxi_disp.h"
#include <stdio.h>
#include "kernel-headers/sunxi_disp_ioctl.h"

struct sunxi_disp0_private
{
	struct sunxi_disp pub;

        int layer;
        int fd;
        int screen_height;
        int screen_width;
  /*-*/
        int fd_disp;
        int g2d_fd;
        int osd_enabled;
        int fb_layer_id;
        int fb_fd;
        int fb_id;
};

static void sunxi_disp0_close(struct sunxi_disp *sunxi_disp);
static int sunxi_disp0_set_video_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
static void sunxi_disp0_close_video_layer(struct sunxi_disp *sunxi_disp);
static int sunxi_disp0_set_osd_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
static void sunxi_disp0_close_osd_layer(struct sunxi_disp *sunxi_disp);

struct sunxi_disp *sunxi_disp0_open(void)
{
  struct sunxi_disp0_private *disp = calloc(1, sizeof(*disp));
  uint32_t tmp[4];

  if (disp) {
    char *env_vdpau_osd = getenv("VDPAU_OSD");

    disp->fb_id = 0;

    if (env_vdpau_osd && strncmp(env_vdpau_osd, "1", 1) == 0)
      {
		disp->g2d_fd = open("/dev/g2d", O_RDWR);
		if (disp->g2d_fd != -1)
			disp->osd_enabled = 1;
		else
			VDPAU_DBG("Failed to open /dev/g2d! OSD disabled.");
	}


    disp->fd = open("/dev/disp", O_RDWR);
    if (disp->fd == -1)
    {
        return NULL;
    }
    fprintf(stderr, "%s: %d\n", __func__, __LINE__);
    disp->fb_fd = open("/dev/fb0", O_RDWR);
    if (disp->fb_fd == -1)
    {
        close(disp->fd);
        return NULL;
    }

    fprintf(stderr, "%s: %d\n", __func__, __LINE__);
    int ver = SUNXI_DISP_VERSION;
    if (ioctl(disp->fd, DISP_CMD_VERSION, &ver) < 0)
    {
      fprintf(stderr, "%s: %d - ver < 0\n", __func__, __LINE__);
        close(disp->fd);
        close(disp->fb_fd);
        return NULL;
    }

    fprintf(stderr, "%s: %d\n", __func__, __LINE__);
    if (ioctl(disp->fb_fd, FBIOGET_LAYER_HDL_0, &disp->fb_layer_id))
    {
        close(disp->fd);
        close(disp->fb_fd);
        return NULL;
    }

    fprintf(stderr, "%s: %d\n", __func__, __LINE__);
    uint32_t args[4]; 
    int i;
    for (i = 0x65; i <= 0x67; i++)
    {
    //release possibly lost allocated layers
       args[0] = disp->fb_id;
       args[1] = i;
       args[2] = 0;
       args[3] = 0;
       ioctl(disp->fd, DISP_CMD_LAYER_RELEASE, &args[0]);
    }

    args[1] = DISP_LAYER_WORK_MODE_SCALER;
    disp->layer = ioctl(disp->fd, DISP_CMD_LAYER_REQUEST, args);
    if (disp->layer == 0)
    {
            close(disp->fd);
            close(disp->fb_fd);
            return NULL;
    }

    //XSetWindowBackground(disp->display, drawable, 0x000102);

    __disp_colorkey_t ck;
#if 1
    ck.ck_max.red = ck.ck_min.red = 0x0;
    ck.ck_max.green = ck.ck_min.green = 0x1;
    ck.ck_max.blue = ck.ck_min.blue = 0x2;
    ck.ck_max.alpha = ck.ck_min.alpha = 0xff;
#endif
    ck.red_match_rule = 2;
    ck.green_match_rule = 2;
    ck.blue_match_rule = 2;

    args[0] = disp->fb_id;
    args[1] = (unsigned long)(&ck);
    ioctl(disp->fd, DISP_CMD_SET_COLORKEY, args);

    tmp[0] = disp->fb_id;
    int ret;
    ret = ioctl(disp->fd, DISP_CMD_SCN_GET_WIDTH, tmp);
    disp->screen_width = ret;

    ret = ioctl(disp->fd, DISP_CMD_SCN_GET_HEIGHT, tmp);
    disp->screen_height = ret;

#if 1
    __disp_layer_info_t layer_info;
    tmp[0] = disp->fb_id;
    tmp[1] = disp->fb_layer_id;
    tmp[2] = (unsigned long) (&layer_info);
    tmp[3] = 0;
    if (ioctl(disp->fd, DISP_CMD_LAYER_GET_PARA, tmp) < 0)
    {
            printf("layer get para failed\n");
    }
    layer_info.alpha_en = 1;
    layer_info.alpha_val = 255;

    if (ioctl(disp->fd, DISP_CMD_LAYER_SET_PARA, tmp) < 0)
    {
            printf("layer get para failed\n");
    }
#endif

#if 0
    /* Enable color key for the overlay layer */
    tmp[0] = disp->fb_id;
    tmp[1] = disp->layer;
    if (ioctl(disp->fd, DISP_CMD_LAYER_CK_ON, &tmp) < 0)
    {
            printf("layer ck on failed\n");
    }
#endif
    args[0] = disp->fb_id;
    args[1] = (unsigned long)(&ck);
    args[2] = 0;
    args[3] = 0;
    ioctl(disp->fd, DISP_CMD_SET_BKCOLOR, args);
    
    tmp[0] = disp->fb_id;
    tmp[1] = disp->layer;
    if (ioctl(disp->fd, DISP_CMD_LAYER_TOP, &tmp) < 0)
    {
        printf("layer bottom 2 failed\n");
    }
#if 0
    // but should be 1 when layering is fixed again.
    /* Set the overlay layer below the screen layer */
    tmp[0] = disp->fb_id;
    tmp[1] = disp->fb_layer_id;
    if (ioctl(disp->fd, DISP_CMD_LAYER_TOP, &tmp) < 0)
    {
        printf("layer bottom 1 failed\n");
    }
#endif

#if 1
    /* Disable color key and enable global alpha for the screen layer */
    tmp[0] = disp->fb_id;
    tmp[1] = disp->fb_layer_id;
    if (ioctl(disp->fd, DISP_CMD_LAYER_CK_OFF, &tmp) < 0)
    {
            printf("layer ck off failed\n");
    }
    tmp[0] = disp->fb_id;
    tmp[1] = disp->fb_layer_id;
    tmp[2] = 0xFF;
    if (ioctl(disp->fd,DISP_CMD_LAYER_SET_ALPHA_VALUE,(void*)tmp) < 0)
    {
            printf("set alpha value failed\n");
    }

    tmp[0] = disp->fb_id;
    tmp[1] = disp->fb_layer_id;
    if (ioctl(disp->fd, DISP_CMD_LAYER_ALPHA_ON, &tmp) < 0)
    {
            printf("alpha on failed\n");
    }
#if 0
    error = ioctl(disp->fd_disp, DISP_CMD_LAYER_BOTTOM, args);
    if(error < 0)
    {
            printf("layer top failed\n");
    }

    if (ioctl(disp->fd, DISP_CMD_VIDEO_START, args) < 0)
    {
            printf("video start failed\n");
    }
#endif
#endif

    disp->pub.close = sunxi_disp0_close;
    disp->pub.set_video_layer = sunxi_disp0_set_video_layer;
    disp->pub.close_video_layer = sunxi_disp0_close_video_layer;
    disp->pub.set_osd_layer = sunxi_disp0_set_osd_layer;
    disp->pub.close_osd_layer = sunxi_disp0_close_osd_layer;
    fprintf(stderr, "%s:%d\n", __func__, __LINE__);
    return (struct sunxi_disp *)disp;
  }

  return NULL;
}

static void sunxi_disp0_close(struct sunxi_disp *sunxi_disp)
{
	struct sunxi_disp0_private *disp = (struct sunxi_disp0_private *)sunxi_disp;
	uint32_t args[4] = { 0, disp->layer, 0, 0 };

	ioctl(disp->fd, DISP_CMD_LAYER_CLOSE, args);
	ioctl(disp->fd, DISP_CMD_LAYER_RELEASE, args);

	close(disp->fd);

	free(sunxi_disp);
}

static int sunxi_disp0_set_video_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface)
{
	struct sunxi_disp0_private *disp = (struct sunxi_disp0_private *)sunxi_disp;
	int error;
	//XTranslateCoordinates(q->device->display, disp->drawable, RootWindow(q->device->display, q->device->screen), 0, 0, &x, &y, &c);
	//XClearWindow(q->device->display, disp->drawable);

	__disp_layer_info_t layer_info;
	memset(&layer_info, 0, sizeof(layer_info));
	layer_info.pipe = 1;
#if 1
        layer_info.alpha_en = 1;
        layer_info.alpha_val = 0xff;
#endif
	layer_info.mode = DISP_LAYER_WORK_MODE_SCALER;
	layer_info.fb.format = DISP_FORMAT_YUV420;
	layer_info.fb.seq = DISP_SEQ_UVUV;
	switch (surface->vs->source_format) {
	case VDP_YCBCR_FORMAT_YUYV:
		layer_info.fb.mode = DISP_MOD_INTERLEAVED;
		layer_info.fb.format = DISP_FORMAT_YUV422;
		layer_info.fb.seq = DISP_SEQ_YUYV;
		break;
	case VDP_YCBCR_FORMAT_UYVY:
		layer_info.fb.mode = DISP_MOD_INTERLEAVED;
		layer_info.fb.format = DISP_FORMAT_YUV422;
		layer_info.fb.seq = DISP_SEQ_UYVY;
		break;
	case VDP_YCBCR_FORMAT_NV12:
		layer_info.fb.mode = DISP_MOD_NON_MB_UV_COMBINED;
		break;
	case VDP_YCBCR_FORMAT_YV12:
		layer_info.fb.mode = DISP_MOD_NON_MB_PLANAR;
		break;
	default:
	case INTERNAL_YCBCR_FORMAT:
		layer_info.fb.mode = DISP_MOD_MB_UV_COMBINED;
		break;
	}
	
	layer_info.fb.br_swap = 0;
	//recalc data to cpu kernel addresses (+ 0x40000000)
	layer_info.fb.addr[0] = cedarv_virt2phys(surface->vs->dataY) + 0x40000000;
	layer_info.fb.addr[1] = cedarv_virt2phys(surface->vs->dataU)/* + surface->vs->plane_size*/ + 0x40000000;
	if( cedarv_isValid(surface->vs->dataV))
	  layer_info.fb.addr[2] = cedarv_virt2phys(surface->vs->dataV)/* + surface->vs->plane_size + surface->vs->plane_size / 4*/ + 0x40000000;

	layer_info.fb.cs_mode = DISP_BT709;
	layer_info.fb.size.width = surface->vs->width; //disp->screen_width;
	layer_info.fb.size.height = surface->vs->width; //disp->screen_height;
#if 0
       layer_info.src_win.x = 0;
       layer_info.src_win.y = 0;
       layer_info.src_win.width = surface->vs->width;
       layer_info.src_win.height = surface->vs->height;
       layer_info.scn_win.x = 0; //x + surface->video_x;
       layer_info.scn_win.y = 0; //y + surface->video_y;
#endif
	layer_info.src_win.x = surface->video_src_rect.x0;
	layer_info.src_win.y = surface->video_src_rect.y0;
	layer_info.src_win.width = surface->video_src_rect.x1 - surface->video_src_rect.x0;
	layer_info.src_win.height = surface->video_src_rect.y1 - surface->video_src_rect.y0;
	layer_info.scn_win.x = x + surface->video_dst_rect.x0;
	layer_info.scn_win.y = y + surface->video_dst_rect.y0;
	layer_info.scn_win.width = surface->video_dst_rect.x1 - surface->video_dst_rect.x0;
	layer_info.scn_win.height = surface->video_dst_rect.y1 - surface->video_dst_rect.y0;
	layer_info.ck_enable = 1;

	if (layer_info.scn_win.y < 0)
	{
		int cutoff = -(layer_info.scn_win.y);
		layer_info.src_win.y += cutoff;
		layer_info.src_win.height -= cutoff;
		layer_info.scn_win.y = 0;
		layer_info.scn_win.height -= cutoff;
	}


	uint32_t args[4] = { 0, disp->layer, (unsigned long)(&layer_info), 0 };
	error = ioctl(disp->fd, DISP_CMD_LAYER_SET_PARA, args);
	if(error < 0)
	{
		printf("set para failed\n");
	}

#if 1
	error = ioctl(disp->fd, DISP_CMD_LAYER_OPEN, args);
	if(error < 0)
	{
		printf("layer open failed, fd=%d, errno=%d\n", disp->fd, errno);
	}
	// Note: might be more reliable (but slower and problematic when there
	// are driver issues and the GET functions return wrong values) to query the
	// old values instead of relying on our internal csc_change.
	// Since the driver calculates a matrix out of these values after each
	// set doing this unconditionally is costly.
#endif
	if (surface->csc_change) {
		ioctl(disp->fd, DISP_CMD_LAYER_ENHANCE_OFF, args);
		args[2] = 0xff * surface->brightness + 0x20;
		ioctl(disp->fd, DISP_CMD_LAYER_SET_BRIGHT, args);
		args[2] = 0x20 * surface->contrast;
		ioctl(disp->fd, DISP_CMD_LAYER_SET_CONTRAST, args);
		args[2] = 0x20 * surface->saturation;
		ioctl(disp->fd, DISP_CMD_LAYER_SET_SATURATION, args);
		// hue scale is randomly chosen, no idea how it maps exactly
		args[2] = (32 / 3.14) * surface->hue + 0x20;
		ioctl(disp->fd, DISP_CMD_LAYER_SET_HUE, args);
		ioctl(disp->fd, DISP_CMD_LAYER_ENHANCE_ON, args);
		surface->csc_change = 0;
	}


	return 0;
}

static void sunxi_disp0_close_video_layer(struct sunxi_disp *sunxi_disp)
{
  //struct sunxi_disp0_private *disp = (struct sunxi_disp0_private *)sunxi_disp;
}

static int sunxi_disp0_set_osd_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface)
{
  //struct sunxi_disp0_private *disp = (struct sunxi_disp0_private *)sunxi_disp;


	/*	switch (surface->rgba.format)
	{
	case VDP_RGBA_FORMAT_R8G8B8A8:
		disp->osd_config.info.fb.format = DISP_FORMAT_ABGR_8888;
		break;
	case VDP_RGBA_FORMAT_B8G8R8A8:
	default:
		disp->osd_config.info.fb.format = DISP_FORMAT_ARGB_8888;
		break;
	}

	disp->osd_config.info.fb.addr[0] = cedarv_virt2phys(surface->rgba.data);
	disp->osd_config.info.fb.size[0].width = surface->rgba.width;
	disp->osd_config.info.fb.size[0].height = surface->rgba.height;
	disp->osd_config.info.fb.align[0] = 1;
	disp->osd_config.info.fb.crop.x = (unsigned long long)(src.x) << 32;
	disp->osd_config.info.fb.crop.y = (unsigned long long)(src.y) << 32;
	disp->osd_config.info.fb.crop.width = (unsigned long long)(src.width) << 32;
	disp->osd_config.info.fb.crop.height = (unsigned long long)(src.height) << 32;
	disp->osd_config.info.screen_win = scn;
	disp->osd_config.enable = 1;

	if (ioctl(disp->fd, DISP_LAYER_SET_CONFIG, args))
	return -EINVAL;*/

	return 0;
}

static void sunxi_disp0_close_osd_layer(struct sunxi_disp *sunxi_disp)
{
  //struct sunxi_disp0_private *disp = (struct sunxi_disp0_private *)sunxi_disp;
}
