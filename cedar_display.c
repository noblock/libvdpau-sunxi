/*
 * Copyright (c) 2015 Martin Ostertag <martin.ostertag@gmx.de>
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

#include "vdpau_private.h"
#include "ve.h"
#include <vdpau/vdpau_x11.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <assert.h>
#include "cedar_display.h"
#include "ve.h"
#include "veisp.h"
#include <stdlib.h>

#include <sys/ioctl.h>
#include "sunxi_disp_ioctl.h"
#include <errno.h>

static void (*Log)(int loglevel, const char *format, ...);

enum col_plane
{
  y_plane,
  u_plane,
  v_plane,
  uv_plane
};

void glVDPAUUnmapSurfacesCedar(GLsizei numSurfaces, const vdpauSurfaceCedar *surfaces);
void glVDPAUInitCedar(const void *vdpDevice, const void *getProcAddress, void (*_Log)(int loglevel, const char *format, ...));
void glVDPAUFiniCedar(void);
vdpauSurfaceCedar glVDPAURegisterVideoSurfaceCedar (const void *vdpSurface);
vdpauSurfaceCedar glVDPAURegisterOutputSurfaceCedar (const void *vdpSurface);
int glVDPAUIsSurfaceCedar (vdpauSurfaceCedar surface);
void glVDPAUUnregisterSurfaceCedar (vdpauSurfaceCedar surface);
void glVDPAUMapSurfacesCedar(GLsizei numSurfaces, const vdpauSurfaceCedar *surfaces);
void glVDPAUUnmapSurfacesCedar(GLsizei numSurfaces, const vdpauSurfaceCedar *surfaces);
VdpStatus glVDPAUPresentSurface(vdpauSurfaceCedar surface, int hLayer, int dispFd, cdRect_t srcRect, cdRect_t dstRect, cdRect_t fbRect);

#if 0
VdpStatus vdp_device_opengles_nv_open(EGLDisplay _eglDisplay, VdpGetProcAddress **get_proc_address)
{
  eglDisplay = _eglDisplay;
}
#endif

void glVDPAUInitCedar(const void *vdpDevice, const void *getProcAddress,
                   void (*_Log)(int loglevel, const char *format, ...))
{
   Log = _Log;

  cedarv_disp_init();
}

void glVDPAUFiniCedar(void)
{
   cedarv_disp_close();
}

vdpauSurfaceCedar glVDPAURegisterVideoSurfaceCedar (const void *vdpSurface)
{
   vdpauSurfaceCedar surfaceNV;

   enum HandleType type = handle_get_type((VdpHandle)vdpSurface);
   assert(type == htype_video);

   video_surface_ctx_t *vs = (video_surface_ctx_t *)handle_get((VdpHandle)vdpSurface);
   assert(vs);

   assert(vs->chroma_type == VDP_CHROMA_TYPE_420);

   surface_display_ctx_t *nv = handle_create(sizeof(*nv), &surfaceNV, htype_display_vdpau);
   assert(nv);

   assert(vs->vdpNvState == VdpauNVState_Unregistered);

   vs->vdpNvState = VdpauNVState_Registered;

   nv->surface 		= (uint32_t)vdpSurface;
   nv->vdpNvState 	= VdpauNVState_Registered;

   nv->surfaceType = type;
 
   return surfaceNV;
}

vdpauSurfaceCedar glVDPAURegisterOutputSurfaceCedar (const void *vdpSurface)
{

  vdpauSurfaceCedar surfaceNV;

  enum HandleType type = handle_get_type((VdpHandle)vdpSurface);
  assert(type == htype_output);

  output_surface_ctx_t *vs = (output_surface_ctx_t *)handle_get((VdpHandle)vdpSurface);
  assert(vs);

  surface_display_ctx_t *nv = handle_create(sizeof(*nv), &surfaceNV, htype_display_vdpau);
  assert(nv);

  assert(vs->vdpNvState == VdpauNVState_Unregistered);

  vs->vdpNvState = VdpauNVState_Registered;

  nv->surface 		= (uint32_t)vdpSurface;
  nv->vdpNvState 	= VdpauNVState_Registered;

  nv->surfaceType = type;
 
  return surfaceNV;
}

int glVDPAUIsSurfaceCedar (vdpauSurfaceCedar surface)
{
}

void glVDPAUUnregisterSurfaceCedar (vdpauSurfaceCedar surface)
{
   surface_display_ctx_t *nv  = handle_get(surface);
   assert(nv);
   
   video_surface_ctx_t *vs = handle_get(nv->surface);
   assert(vs);
   if(vs->vdpNvState == VdpauNVState_Mapped)
   {
      vdpauSurfaceCedar surf[] = {surface};
      glVDPAUUnmapSurfacesCedar(1, surf);
   }

   vs->vdpNvState = VdpauNVState_Unregistered;
   if(nv->surface)
   {
      handle_release(nv->surface);
      handle_destroy(nv->surface);
      nv->surface = 0;
   }

   handle_release(surface);
   handle_destroy(surface); 
}

void glVDPAUGetSurfaceivCedar(vdpauSurfaceCedar surface, uint32_t pname, GLsizei bufSize,
			 GLsizei *length, int *values)
{
}

void glVDPAUSurfaceAccessCedar(vdpauSurfaceCedar surface, uint32_t access)
{
}

static void mapVideoTextures(GLsizei numSurfaces, const vdpauSurfaceCedar *surfaces)
{
  int j;
  for(j = 0; j < numSurfaces; j++)
  {
    surface_display_ctx_t *nv = handle_get(surfaces[j]);
    assert(nv);

    video_surface_ctx_t *vs = handle_get(nv->surface);
    assert(vs);

    vs->vdpNvState = VdpauNVState_Mapped;
    handle_release(nv->surface);
    nv->vdpNvState = VdpauNVState_Mapped;
    handle_release(surfaces[j]);
  }
}

static void mapOutputTextures(GLsizei numSurfaces, const vdpauSurfaceCedar *surfaces)
{
  int j;
  for(j = 0; j < numSurfaces; j++)
  {
    surface_display_ctx_t *nv = handle_get(surfaces[j]);
    assert(nv);

    output_surface_ctx_t *vs = handle_get(nv->surface);
    assert(vs);

    vs->vdpNvState = VdpauNVState_Mapped;
    handle_release(nv->surface);
    nv->vdpNvState = VdpauNVState_Mapped;
    handle_release(surfaces[j]);
  }
}

void glVDPAUMapSurfacesCedar(GLsizei numSurfaces, const vdpauSurfaceCedar *surfaces)
{
  surface_display_ctx_t *nv  = handle_get(surfaces[0]);
  enum HandleType surfaceType = nv->surfaceType;
  handle_release(surfaces[0]);
  
  if(surfaceType == htype_video)
    mapVideoTextures(numSurfaces, surfaces);
  else if(surfaceType == htype_output)
    mapOutputTextures(numSurfaces, surfaces);
}

void glVDPAUUnmapSurfacesCedar(GLsizei numSurfaces, const vdpauSurfaceCedar *surfaces)
{
  int j;
  
  for(j = 0; j < numSurfaces; j++)
  {
    surface_display_ctx_t *nv  = handle_get(surfaces[j]);
    assert(nv);
    
    video_surface_ctx_t *vs = handle_get(nv->surface);
    assert(vs);
    vs->vdpNvState = VdpauNVState_Registered;
    handle_release(nv->surface);
    nv->vdpNvState = VdpauNVState_Registered;
    handle_release(surfaces[j]);
  }
}

VdpStatus glVDPAUPresentSurfaceCedar(vdpauSurfaceCedar surface, int hLayer, int dispFd, cdRect_t srcRect, cdRect_t dstRect, cdRect_t fbRect)
{
  int error;
  surface_display_ctx_t *nv  = handle_get(surface);
  assert(nv);
   
  video_surface_ctx_t *vs = handle_get(nv->surface);
  assert(vs);

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
  switch (vs->source_format) {
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
  layer_info.fb.addr[0] = cedarv_virt2phys(vs->dataY) + 0x40000000;
  layer_info.fb.addr[1] = cedarv_virt2phys(vs->dataU) + 0x40000000;
  if( cedarv_isValid(vs->dataV))
    layer_info.fb.addr[2] = cedarv_virt2phys(vs->dataV) + 0x40000000;

  layer_info.fb.cs_mode = DISP_BT709;
  layer_info.fb.size.width = fbRect.width;
  layer_info.fb.size.height = fbRect.height;
  layer_info.src_win.x = 0; //srcRect.x;
  layer_info.src_win.y = 0; //srcRect.y;
  layer_info.src_win.width = vs->width; //srcRect.width;
  layer_info.src_win.height = vs->height; //srcRect.height;
  layer_info.scn_win.x = dstRect.x;
  layer_info.scn_win.y = dstRect.y;
  layer_info.scn_win.width = dstRect.width;
  layer_info.scn_win.height = dstRect.height;
  //layer_info.ck_enable = 1;

  if (layer_info.scn_win.y < 0)
  {
    int cutoff = -(layer_info.scn_win.y);
    layer_info.src_win.y += cutoff;
    layer_info.src_win.height -= cutoff;
    layer_info.scn_win.y = 0;
    layer_info.scn_win.height -= cutoff;
  }


  uint32_t args[4] = { 
    0, 
    hLayer, 
    (unsigned long)(&layer_info), 
    0 
  };
  error = ioctl(dispFd, DISP_CMD_LAYER_SET_PARA, args);
  if(error < 0)
  {
    printf("set para failed\n");
  }

  static int layer_opened = 0;
  if(layer_opened == 0)
  {
    layer_opened = 1;
#if 1
    error = ioctl(dispFd, DISP_CMD_LAYER_OPEN, args);
    if(error < 0)
    {
      printf("layer open failed, fd=%d, errno=%d\n", dispFd, errno);
    }
      // Note: might be more reliable (but slower and problematic when there
      // are driver issues and the GET functions return wrong values) to query the
      // old values instead of relying on our internal csc_change.
      // Since the driver calculates a matrix out of these values after each
      // set doing this unconditionally is costly.
#endif
  args[2] = 0;
    error = ioctl(dispFd, DISP_CMD_VIDEO_START, args);
    if(error < 0)
    {
      printf("video start failed, fd=%d, errno=%d\n", dispFd, errno);
    }
  }
  handle_release(nv->surface);
  handle_release(surface);
}
