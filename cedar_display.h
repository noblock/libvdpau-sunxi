#ifndef _CEDAR_DISPLAY_H_
#define _CEDAR_DISPLAY_H_

#include "vdpau_private.h"

typedef uint32_t vdpauSurfaceCedar;

typedef struct surface_display_ctx_struct
{
  enum HandleType       surfaceType;
  VdpVideoSurface 	    surface;
  enum VdpauNVState	    vdpNvState;
  uint32_t              target;
} surface_display_ctx_t;

typedef struct display_ctx_struct
{
  uint32_t		fdDisp;
  uint32_t		fdFb;
  uint32_t		fbLayerId;
  uint32_t		videoLayerId;
} display_ctx_struct_t;

typedef struct cdRect
{
  uint32_t       x;
  uint32_t       y;
  uint32_t       width;
  uint32_t       height;
} cdRect_t;

struct videoFrameConfig
{
  uint16_t  width;
  uint16_t  height;
  void      *addr[3];
  uint8_t   align[3];
  uint8_t   srcFormat;
};

#define CS_MODE_BT709   1
#define CS_MODE_BT601   2
#define CS_MODE_YCC     3
#define CS_MODE_XVYCC   4

#endif
