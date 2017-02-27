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

#define DEBUG_IMAGE_DATA 0

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
void glVDPAUCloseVideoLayerCedar(int hLayer, int dispFd);
vdpauSurfaceCedar glVDPAURegisterVideoSurfaceCedar (const void *vdpSurface);
vdpauSurfaceCedar glVDPAURegisterOutputSurfaceCedar (const void *vdpSurface);
int glVDPAUIsSurfaceCedar (vdpauSurfaceCedar surface);
void glVDPAUUnregisterSurfaceCedar (vdpauSurfaceCedar surface);
void glVDPAUMapSurfacesCedar(GLsizei numSurfaces, const vdpauSurfaceCedar *surfaces);
void glVDPAUUnmapSurfacesCedar(GLsizei numSurfaces, const vdpauSurfaceCedar *surfaces);

void glVDPAUInitCedar(const void *vdpDevice, const void *getProcAddress,
                   void (*_Log)(int loglevel, const char *format, ...))
{
   Log = _Log;

  cedarv_disp_init();
}

void glVDPAUFiniCedar()
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
   return VDP_STATUS_OK;
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

#if DEBUG_IMGAE_DATA == 1
static void writeBuffers(void* dataY, size_t szDataY, void* dataU, size_t szDataU, int h, int w);
#endif

VdpStatus glVDPAUGetVideoFrameConfig(vdpauSurfaceCedar surface, int *srcFormat, void** addrY, void** addrU, void** addrV, int *height, int * width)
{
  surface_display_ctx_t *nv  = handle_get(surface);
  if(! nv)
  {
    return VDP_STATUS_INVALID_HANDLE;
  }

  video_surface_ctx_t *vs = handle_get(nv->surface);
  if(! vs)
  {
    handle_release(surface);
    return VDP_STATUS_INVALID_HANDLE;
  }

  *srcFormat = vs->source_format;
  *addrY = (void*)cedarv_virt2phys(vs->dataY);
  *addrU = (void*)cedarv_virt2phys(vs->dataU);
  if( cedarv_isValid(vs->dataV))
    *addrV = (void*)cedarv_virt2phys(vs->dataV);
  else
    *addrV = NULL;

  *height = vs->height;
  *width = vs->width;

#if DEBUG_IMAGE_DATA == 1
  static int first=1;
  if(first)
  {
     writeBuffers(cedarv_getPointer(vs->dataY),
                  cedarv_getSize(vs->dataY),
                  cedarv_getPointer(vs->dataU),
                  cedarv_getSize(vs->dataU),
                  *height,
                  *width);
     first = 0;
  }
#endif

  handle_release(nv->surface);
  handle_release(surface);
  return VDP_STATUS_OK;
}

#if DEBUG_IMAGE_DATA == 1
static void writeBuffers(void* dataY, size_t szDataY, void* dataU, size_t szDataU, int h, int w)
{
  FILE *file = fopen("/tmp/dataY.bin", "wb");
  fwrite(dataY, szDataY, 1, file);
  fclose(file);
  file = fopen("/tmp/dataU.bin", "wb");
  fwrite(dataU, szDataU, 1, file);
  fclose(file);
  file = fopen("/tmp/dataSz.txt", "w");
  fprintf(file, "width=%d height=%d\n", w, h);
  fclose(file);
}
#endif
