#ifndef __gl_h_
#define __gl_h_

/*
 * Minimal OpenGL API stub for the syntax-check harness (system has no
 * Mesa GL headers installed). Only the symbols used by this codebase.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef void           GLvoid;
typedef signed char    GLbyte;
typedef short          GLshort;
typedef int            GLint;
typedef unsigned char  GLubyte;
typedef unsigned short GLushort;
typedef unsigned int   GLuint;
typedef int            GLsizei;
typedef float          GLfloat;
typedef float          GLclampf;
typedef double         GLdouble;
typedef double         GLclampd;

#define GL_TEXTURE_2D                0x0DE1
#define GL_TEXTURE_MIN_FILTER        0x2801
#define GL_TEXTURE_MAG_FILTER        0x2800
#define GL_LINEAR                    0x2601
#define GL_UNPACK_ROW_LENGTH         0x0CF2
#define GL_RGB                       0x1907
#define GL_RGBA                      0x1908
#define GL_LINEAR_MIPMAP_LINEAR      0x2703
#define GL_NEAREST                   0x2600
#define GL_FRAMEBUFFER               0x8D40
#define GL_COLOR_ATTACHMENT0         0x8CE0

#ifdef __cplusplus
}
#endif

#endif /* _gl_h_ */