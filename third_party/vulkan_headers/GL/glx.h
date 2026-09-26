/*
 * Minimal GLX stub for the syntax check (no system Mesa GLX headers).
 */

#ifndef __glx_h_
#define __glx_h_

#ifdef __cplusplus
extern "C" {
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>

typedef struct __GLXcontextRec *GLXContext;
typedef struct __GLXFBConfigRec *GLXFBConfig;
typedef XID GLXPixmap;
typedef XID GLXDrawable;
typedef XID GLXWindow;

#define GLX_RGBA           4
#define GLX_DOUBLEBUFFER   5
#define GLX_RED_SIZE       8

#ifdef __cplusplus
}
#endif

#endif /* __glx_h_ */