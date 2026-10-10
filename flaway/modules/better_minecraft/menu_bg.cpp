#include "menu_bg.h"
#include "menu_assets.h"
#include "../../utils/rlog.h"

#include <jni.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <dlfcn.h>
#include <unistd.h>

#include "../../gui/data/stb_image.h"

// GL function pointer types
typedef void   (*PFN_GENTEX)(int, unsigned*);
typedef void   (*PFN_BINDTEX)(unsigned, unsigned);
typedef void   (*PFN_TEXPARAM)(unsigned, unsigned, int);
typedef void   (*PFN_TEXIMAGE)(unsigned, int, int, int, int, int, unsigned, unsigned, const void*);
typedef void   (*PFN_DELTEX)(int, const unsigned*);
typedef void   (*PFN_PIXELSTORE)(unsigned, int);
typedef void   (*PFN_GETINTEGERV)(unsigned, int*);
typedef unsigned (*PFN_GETERROR)(void);
typedef void   (*PFN_BINDBUF)(unsigned, unsigned);
typedef unsigned (*PFN_CREATEPROGRAM)();
typedef void   (*PFN_DELETEPROGRAM)(unsigned);
typedef unsigned (*PFN_CREATESHADER)(unsigned);
typedef void   (*PFN_DELETESHADER)(unsigned);
typedef void   (*PFN_SHADERSOURCE)(unsigned, int, const char**, const int*);
typedef void   (*PFN_COMPILESHADER)(unsigned);
typedef void   (*PFN_GETSHADERIV)(unsigned, unsigned, int*);
typedef void   (*PFN_GETSHADERINFOLOG)(unsigned, int, int*, char*);
typedef void   (*PFN_ATTACHSHADER)(unsigned, unsigned);
typedef void   (*PFN_LINKPROGRAM)(unsigned);
typedef void   (*PFN_GETPROGRAMIV)(unsigned, unsigned, int*);
typedef void   (*PFN_GETPROGRAMINFOLOG)(unsigned, int, int*, char*);
typedef void   (*PFN_USEPROGRAM)(unsigned);
typedef int    (*PFN_GETUNIFORMLOCATION)(unsigned, const char*);
typedef void   (*PFN_UNIFORM1I)(unsigned, int);
typedef void   (*PFN_UNIFORM1F)(unsigned, float);
typedef void   (*PFN_UNIFORM2F)(unsigned, float, float);
typedef void   (*PFN_UNIFORM4F)(unsigned, float, float, float, float);
typedef void   (*PFN_ACTIVETEXTURE)(unsigned);
typedef void   (*PFN_GENVERTEXARRAYS)(int, unsigned*);
typedef void   (*PFN_BINDVERTEXARRAY)(unsigned);
typedef void   (*PFN_GENBUFFERS)(int, unsigned*);
typedef void   (*PFN_DELETEBUFFERS)(int, const unsigned*);
typedef void   (*PFN_BUFFERDATA)(unsigned, long long, const void*, unsigned);
typedef void   (*PFN_ENABLEVERTEXATTRIBARRAY)(unsigned);
typedef void   (*PFN_VERTEXATTRIBPOINTER)(unsigned, int, unsigned, unsigned char, int, const void*);
typedef void   (*PFN_DRAWARRAYS)(unsigned, int, int);
typedef void   (*PFN_ENABLE)(unsigned);
typedef void   (*PFN_DISABLE)(unsigned);
typedef void   (*PFN_BLENDFUNC)(unsigned, unsigned);
typedef void   (*PFN_VIEWPORT)(int, int, int, int);
typedef int    (*PFN_GETATTRIBLOCATION)(unsigned, const char*);
typedef void   (*PFN_COLORMASK)(unsigned char, unsigned char, unsigned char, unsigned char);
typedef void   (*PFN_DEPTHMASK)(unsigned char);
typedef void   (*PFN_BLENDEQUATION)(unsigned);

namespace flaway {
namespace modules {
namespace better_minecraft {
namespace menu_bg {
namespace {

// GL constants
constexpr unsigned GL_TEXTURE_2D = 0x0DE1;
constexpr unsigned GL_RGBA = 0x1908;
constexpr unsigned GL_RGBA8 = 0x8058;
constexpr unsigned GL_UNSIGNED_BYTE = 0x1401;
constexpr unsigned GL_LINEAR = 0x2601;
constexpr unsigned GL_CLAMP_TO_EDGE = 0x812F;
constexpr unsigned GL_REPEAT = 0x2901;
constexpr unsigned GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr unsigned GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr unsigned GL_TEXTURE_WRAP_S = 0x2802;
constexpr unsigned GL_TEXTURE_WRAP_T = 0x2803;
constexpr unsigned GL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr unsigned GL_UNPACK_ROW_LENGTH = 0x0CF2;
constexpr unsigned GL_UNPACK_SKIP_ROWS = 0x0CF3;
constexpr unsigned GL_UNPACK_SKIP_PIXELS = 0x0CF4;
constexpr unsigned GL_TEXTURE_BINDING_2D = 0x806A;
constexpr unsigned GL_PIXEL_UNPACK_BUFFER = 0x88EB;
constexpr unsigned GL_PIXEL_UNPACK_BUFFER_BINDING = 0x88EC;
constexpr unsigned GL_ARRAY_BUFFER = 0x8892;
constexpr unsigned GL_ARRAY_BUFFER_BINDING = 0x8894;
constexpr unsigned GL_STATIC_DRAW = 0x88E4;
constexpr unsigned GL_DYNAMIC_DRAW = 0x88E8;
constexpr unsigned GL_FLOAT = 0x1406;
constexpr unsigned GL_VERTEX_ARRAY_BINDING = 0x85B5;
constexpr unsigned GL_ACTIVE_TEXTURE = 0x84E0;
constexpr unsigned GL_TEXTURE0 = 0x84C0;
constexpr unsigned GL_FRAGMENT_SHADER = 0x8B30;
constexpr unsigned GL_VERTEX_SHADER = 0x8B31;
constexpr unsigned GL_BLEND = 0x0BE2;
constexpr unsigned GL_SRC_ALPHA = 0x0302;
constexpr unsigned GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr unsigned GL_ONE = 1;
constexpr unsigned GL_TRIANGLES = 0x0004;
constexpr unsigned GL_PROGRAM = 0x82E2;
constexpr unsigned GL_CURRENT_PROGRAM = 0x8B8D;
constexpr unsigned GL_DEPTH_TEST = 0x0B71;
constexpr unsigned GL_DEPTH_WRITEMASK = 0x0B72;
constexpr unsigned GL_SCISSOR_TEST = 0x0C11;
constexpr unsigned GL_CULL_FACE = 0x0B44;
constexpr unsigned GL_STENCIL_TEST = 0x0B90;
constexpr unsigned GL_COLOR_WRITEMASK = 0x0C23;
constexpr unsigned GL_VIEWPORT_ENUM = 0x0BA2;
constexpr unsigned GL_FUNC_ADD = 0x8006;

// GL function pointers
PFN_GENTEX pGenTextures = nullptr;
PFN_BINDTEX pBindTexture = nullptr;
PFN_TEXPARAM pTexParameteri = nullptr;
PFN_TEXIMAGE pTexImage2D = nullptr;
PFN_DELTEX pDeleteTextures = nullptr;
PFN_PIXELSTORE pPixelStorei = nullptr;
PFN_GETINTEGERV pGetIntegerv = nullptr;
PFN_GETERROR pGetError = nullptr;
PFN_BINDBUF pBindBuffer = nullptr;
PFN_CREATEPROGRAM pCreateProgram = nullptr;
PFN_DELETEPROGRAM pDeleteProgram = nullptr;
PFN_CREATESHADER pCreateShader = nullptr;
PFN_DELETESHADER pDeleteShader = nullptr;
PFN_SHADERSOURCE pShaderSource = nullptr;
PFN_COMPILESHADER pCompileShader = nullptr;
PFN_GETSHADERIV pGetShaderiv = nullptr;
PFN_GETSHADERINFOLOG pGetShaderInfoLog = nullptr;
PFN_ATTACHSHADER pAttachShader = nullptr;
PFN_LINKPROGRAM pLinkProgram = nullptr;
PFN_GETPROGRAMIV pGetProgramiv = nullptr;
PFN_GETPROGRAMINFOLOG pGetProgramInfoLog = nullptr;
PFN_USEPROGRAM pUseProgram = nullptr;
PFN_GETUNIFORMLOCATION pGetUniformLocation = nullptr;
PFN_UNIFORM1I pUniform1i = nullptr;
PFN_UNIFORM1F pUniform1f = nullptr;
PFN_UNIFORM2F pUniform2f = nullptr;
PFN_UNIFORM4F pUniform4f = nullptr;
PFN_ACTIVETEXTURE pActiveTexture = nullptr;
PFN_GENVERTEXARRAYS pGenVertexArrays = nullptr;
PFN_BINDVERTEXARRAY pBindVertexArray = nullptr;
PFN_GENBUFFERS pGenBuffers = nullptr;
PFN_DELETEBUFFERS pDeleteBuffers = nullptr;
PFN_BUFFERDATA pBufferData = nullptr;
PFN_ENABLEVERTEXATTRIBARRAY pEnableVertexAttribArray = nullptr;
PFN_VERTEXATTRIBPOINTER pVertexAttribPointer = nullptr;
PFN_DRAWARRAYS pDrawArrays = nullptr;
PFN_ENABLE pEnable = nullptr;
PFN_DISABLE pDisable = nullptr;
PFN_BLENDFUNC pBlendFunc = nullptr;
PFN_VIEWPORT pViewport = nullptr;
PFN_GETATTRIBLOCATION pGetAttribLocation = nullptr;
PFN_COLORMASK pColorMask = nullptr;
PFN_DEPTHMASK pDepthMask = nullptr;
PFN_BLENDEQUATION pBlendEquation = nullptr;

bool gl_loaded = false;
bool gl_ok = false;

bool load_gl() {
    if (gl_loaded) return gl_ok;
    gl_loaded = true;
    pGenTextures = (PFN_GENTEX)dlsym(RTLD_DEFAULT, "glGenTextures");
    pBindTexture = (PFN_BINDTEX)dlsym(RTLD_DEFAULT, "glBindTexture");
    pTexParameteri = (PFN_TEXPARAM)dlsym(RTLD_DEFAULT, "glTexParameteri");
    pTexImage2D = (PFN_TEXIMAGE)dlsym(RTLD_DEFAULT, "glTexImage2D");
    pDeleteTextures = (PFN_DELTEX)dlsym(RTLD_DEFAULT, "glDeleteTextures");
    pPixelStorei = (PFN_PIXELSTORE)dlsym(RTLD_DEFAULT, "glPixelStorei");
    pGetIntegerv = (PFN_GETINTEGERV)dlsym(RTLD_DEFAULT, "glGetIntegerv");
    pGetError = (PFN_GETERROR)dlsym(RTLD_DEFAULT, "glGetError");
    pBindBuffer = (PFN_BINDBUF)dlsym(RTLD_DEFAULT, "glBindBuffer");
    pCreateProgram = (PFN_CREATEPROGRAM)dlsym(RTLD_DEFAULT, "glCreateProgram");
    pDeleteProgram = (PFN_DELETEPROGRAM)dlsym(RTLD_DEFAULT, "glDeleteProgram");
    pCreateShader = (PFN_CREATESHADER)dlsym(RTLD_DEFAULT, "glCreateShader");
    pDeleteShader = (PFN_DELETESHADER)dlsym(RTLD_DEFAULT, "glDeleteShader");
    pShaderSource = (PFN_SHADERSOURCE)dlsym(RTLD_DEFAULT, "glShaderSource");
    pCompileShader = (PFN_COMPILESHADER)dlsym(RTLD_DEFAULT, "glCompileShader");
    pGetShaderiv = (PFN_GETSHADERIV)dlsym(RTLD_DEFAULT, "glGetShaderiv");
    pGetShaderInfoLog = (PFN_GETSHADERINFOLOG)dlsym(RTLD_DEFAULT, "glGetShaderInfoLog");
    pAttachShader = (PFN_ATTACHSHADER)dlsym(RTLD_DEFAULT, "glAttachShader");
    pLinkProgram = (PFN_LINKPROGRAM)dlsym(RTLD_DEFAULT, "glLinkProgram");
    pGetProgramiv = (PFN_GETPROGRAMIV)dlsym(RTLD_DEFAULT, "glGetProgramiv");
    pGetProgramInfoLog = (PFN_GETPROGRAMINFOLOG)dlsym(RTLD_DEFAULT, "glGetProgramInfoLog");
    pUseProgram = (PFN_USEPROGRAM)dlsym(RTLD_DEFAULT, "glUseProgram");
    pGetUniformLocation = (PFN_GETUNIFORMLOCATION)dlsym(RTLD_DEFAULT, "glGetUniformLocation");
    pUniform1i = (PFN_UNIFORM1I)dlsym(RTLD_DEFAULT, "glUniform1i");
    pUniform1f = (PFN_UNIFORM1F)dlsym(RTLD_DEFAULT, "glUniform1f");
    pUniform2f = (PFN_UNIFORM2F)dlsym(RTLD_DEFAULT, "glUniform2f");
    pUniform4f = (PFN_UNIFORM4F)dlsym(RTLD_DEFAULT, "glUniform4f");
    pActiveTexture = (PFN_ACTIVETEXTURE)dlsym(RTLD_DEFAULT, "glActiveTexture");
    pGenVertexArrays = (PFN_GENVERTEXARRAYS)dlsym(RTLD_DEFAULT, "glGenVertexArrays");
    pBindVertexArray = (PFN_BINDVERTEXARRAY)dlsym(RTLD_DEFAULT, "glBindVertexArray");
    pGenBuffers = (PFN_GENBUFFERS)dlsym(RTLD_DEFAULT, "glGenBuffers");
    pDeleteBuffers = (PFN_DELETEBUFFERS)dlsym(RTLD_DEFAULT, "glDeleteBuffers");
    pBufferData = (PFN_BUFFERDATA)dlsym(RTLD_DEFAULT, "glBufferData");
    pEnableVertexAttribArray = (PFN_ENABLEVERTEXATTRIBARRAY)dlsym(RTLD_DEFAULT, "glEnableVertexAttribArray");
    pVertexAttribPointer = (PFN_VERTEXATTRIBPOINTER)dlsym(RTLD_DEFAULT, "glVertexAttribPointer");
    pDrawArrays = (PFN_DRAWARRAYS)dlsym(RTLD_DEFAULT, "glDrawArrays");
    pEnable = (PFN_ENABLE)dlsym(RTLD_DEFAULT, "glEnable");
    pDisable = (PFN_DISABLE)dlsym(RTLD_DEFAULT, "glDisable");
    pBlendFunc = (PFN_BLENDFUNC)dlsym(RTLD_DEFAULT, "glBlendFunc");
    pViewport = (PFN_VIEWPORT)dlsym(RTLD_DEFAULT, "glViewport");
    pGetAttribLocation = (PFN_GETATTRIBLOCATION)dlsym(RTLD_DEFAULT, "glGetAttribLocation");
    pColorMask = (PFN_COLORMASK)dlsym(RTLD_DEFAULT, "glColorMask");
    pDepthMask = (PFN_DEPTHMASK)dlsym(RTLD_DEFAULT, "glDepthMask");
    pBlendEquation = (PFN_BLENDEQUATION)dlsym(RTLD_DEFAULT, "glBlendEquation");

    gl_ok = pGenTextures && pBindTexture && pTexParameteri && pTexImage2D &&
            pDeleteTextures && pPixelStorei && pGetIntegerv && pGetError &&
            pCreateProgram && pDeleteProgram && pCreateShader && pDeleteShader &&
            pShaderSource && pCompileShader && pGetShaderiv && pAttachShader &&
            pLinkProgram && pGetProgramiv && pUseProgram && pGetUniformLocation &&
            pUniform1i && pUniform2f && pActiveTexture && pGenVertexArrays &&
            pBindVertexArray && pGenBuffers && pBufferData &&
            pEnableVertexAttribArray && pVertexAttribPointer && pDrawArrays &&
            pEnable && pDisable && pBlendFunc && pViewport &&
            pGetAttribLocation && pUniform1f && pGetShaderInfoLog &&
            pGetProgramInfoLog && pUniform4f;
    if (!gl_ok) rlog::logf("menu_bg: GL resolve failed");
    return gl_ok;
}

int get_int(unsigned pname) {
    int v = 0;
    if (pGetIntegerv) pGetIntegerv(pname, &v);
    return v;
}

void drain_gl_errors() {
    if (pGetError) while (pGetError() != 0) {}
}

// --- shaders ---
// Background: aspect-fill textured quad.
// aPos = pixel coords (0..w, 0..h), aTexCoord = (0..1, 0..1).
static const char* k_bg_vs = R"(
#version 150
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vTexCoord;
uniform vec2 uResolution;
void main() {
    vTexCoord = aTexCoord;
    vec2 ndc = (aPos / uResolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

static const char* k_bg_fs = R"(
#version 150
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform float uBlack;
void main() {
    vec4 col = texture(uTexture, vTexCoord);
    col.rgb = mix(col.rgb, vec3(0.0), uBlack);
    FragColor = col;
}
)";

// Stars: textured quad with scrolling UV + additive bloom.
static const char* k_star_vs = R"(
#version 150
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vTexCoord;
uniform vec2 uResolution;
void main() {
    vTexCoord = aTexCoord;
    vec2 ndc = (aPos / uResolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

static const char* k_star_fs = R"(
#version 150
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform float uTime;
uniform float uAlpha;
void main() {
    // Scroll UV downward for falling effect.
    vec2 uv = vTexCoord;
    uv.y += uTime * 0.03;
    uv.y = fract(uv.y);
    vec4 col = texture(uTexture, uv);
    // Bloom: boost brightness, keep additive.
    col.rgb *= 1.5;
    col.a *= uAlpha;
    FragColor = col;
}
)";

// VAO/VBO
unsigned s_bg_program = 0;
unsigned s_star_program = 0;
int s_bg_loc_pos = 0, s_bg_loc_uv = 1;
int s_star_loc_pos = 0, s_star_loc_uv = 1;
unsigned s_quad_vao = 0;
unsigned s_quad_vbo = 0;

// Textures
unsigned s_menu_tex = 0;
unsigned s_stars_tex = 0;
unsigned s_dark_tex = 0;
unsigned s_logo_tex = 0;
unsigned s_edition_tex = 0;
int s_menu_w = 0, s_menu_h = 0;
int s_stars_w = 0, s_stars_h = 0;
bool s_textures_loaded = false;
float s_anim_time = 0.0f;

// GIF frames
constexpr int k_max_gif_frames = 128;
unsigned s_gif_frames[k_max_gif_frames] = {};
int s_gif_delays_ms[k_max_gif_frames] = {};
int s_gif_frame_count = 0;
int s_gif_w = 0, s_gif_h = 0;
double s_gif_clock_ms = 0.0;
long long s_gif_last_us = 0;

long long now_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Screen class refs
void* g_title_class = nullptr;
void* g_progress_class = nullptr;

// GL state save/restore
struct GlState {
    unsigned active_texture;
    int binding_2d;
    int array_buffer;
    int vao;
    int program;
    int unpack_alignment;
    int unpack_row_length;
    int unpack_skip_rows;
    int unpack_skip_pixels;
    int unpack_buffer;
    bool depth_test;
    bool scissor_test;
    bool cull_face;
    bool stencil_test;
    bool depth_mask;
    unsigned char color_mask[4];
};

GlState save_state() {
    GlState s{};
    s.active_texture = (unsigned)get_int(GL_ACTIVE_TEXTURE);
    if (!s.active_texture) s.active_texture = GL_TEXTURE0;
    if (pActiveTexture) pActiveTexture(GL_TEXTURE0);
    s.binding_2d = get_int(GL_TEXTURE_BINDING_2D);
    if (pActiveTexture) pActiveTexture(s.active_texture);
    s.array_buffer = get_int(GL_ARRAY_BUFFER_BINDING);
    s.vao = get_int(GL_VERTEX_ARRAY_BINDING);
    s.program = get_int(GL_CURRENT_PROGRAM);
    s.unpack_alignment = get_int(GL_UNPACK_ALIGNMENT);
    s.unpack_row_length = get_int(GL_UNPACK_ROW_LENGTH);
    s.unpack_skip_rows = get_int(GL_UNPACK_SKIP_ROWS);
    s.unpack_skip_pixels = get_int(GL_UNPACK_SKIP_PIXELS);
    s.unpack_buffer = get_int(GL_PIXEL_UNPACK_BUFFER_BINDING);
    s.depth_test = get_int(GL_DEPTH_TEST) != 0;
    s.scissor_test = get_int(GL_SCISSOR_TEST) != 0;
    s.cull_face = get_int(GL_CULL_FACE) != 0;
    s.stencil_test = get_int(GL_STENCIL_TEST) != 0;
    s.depth_mask = get_int(GL_DEPTH_WRITEMASK) != 0;
    s.color_mask[0] = s.color_mask[1] = s.color_mask[2] = s.color_mask[3] = 1;
    if (pGetIntegerv) pGetIntegerv(GL_COLOR_WRITEMASK, (int*)s.color_mask);
    drain_gl_errors();
    return s;
}

void restore_state(const GlState& s) {
    // Restore shader program FIRST so subsequent calls use MC's program.
    if (pUseProgram) pUseProgram((unsigned)s.program);
    if (pActiveTexture) pActiveTexture(GL_TEXTURE0);
    if (pBindTexture) pBindTexture(GL_TEXTURE_2D, (unsigned)s.binding_2d);
    if (pActiveTexture) pActiveTexture(s.active_texture);
    if (pBindBuffer) pBindBuffer(GL_ARRAY_BUFFER, (unsigned)s.array_buffer);
    if (pBindVertexArray) pBindVertexArray((unsigned)s.vao);
    if (pPixelStorei) {
        pPixelStorei(GL_UNPACK_ALIGNMENT, s.unpack_alignment);
        pPixelStorei(GL_UNPACK_ROW_LENGTH, s.unpack_row_length);
        pPixelStorei(GL_UNPACK_SKIP_ROWS, s.unpack_skip_rows);
        pPixelStorei(GL_UNPACK_SKIP_PIXELS, s.unpack_skip_pixels);
    }
    if (s.unpack_buffer && pBindBuffer)
        pBindBuffer(GL_PIXEL_UNPACK_BUFFER, (unsigned)s.unpack_buffer);
    if (s.depth_test) pEnable(GL_DEPTH_TEST); else pDisable(GL_DEPTH_TEST);
    if (s.scissor_test) pEnable(GL_SCISSOR_TEST); else pDisable(GL_SCISSOR_TEST);
    if (s.cull_face) pEnable(GL_CULL_FACE); else pDisable(GL_CULL_FACE);
    if (s.stencil_test) pEnable(GL_STENCIL_TEST); else pDisable(GL_STENCIL_TEST);
    if (pDepthMask) pDepthMask(s.depth_mask ? 1 : 0);
    if (pColorMask) pColorMask(s.color_mask[0], s.color_mask[1],
        s.color_mask[2], s.color_mask[3]);
    drain_gl_errors();
}

// Reset the fixed-function state our fullscreen quads need. MC's panorama
// renderer leaves culling/depth/colorMask in an arbitrary state, which can
// silently discard our quads.
void prep_draw(int w, int h) {
    pDisable(GL_DEPTH_TEST);
    pDisable(GL_SCISSOR_TEST);
    pDisable(GL_CULL_FACE);
    pDisable(GL_STENCIL_TEST);
    if (pDepthMask) pDepthMask(1);
    if (pColorMask) pColorMask(1, 1, 1, 1);
    if (pViewport) pViewport(0, 0, w, h);
    pEnable(GL_BLEND);
    pBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (pBlendEquation) pBlendEquation(GL_FUNC_ADD); // panorama may leave a custom equation
}

// --- texture upload ---
unsigned upload_rgba(const unsigned char* pixels, int w, int h, bool repeat) {
    if (!pixels || w <= 0 || h <= 0 || !load_gl()) return 0;
    int saved[6] = {0, 0, 0, 4, 0, 0};
    pGetIntegerv(GL_UNPACK_ROW_LENGTH, &saved[0]);
    pGetIntegerv(GL_UNPACK_SKIP_PIXELS, &saved[1]);
    pGetIntegerv(GL_UNPACK_SKIP_ROWS, &saved[2]);
    pGetIntegerv(GL_UNPACK_ALIGNMENT, &saved[3]);
    pGetIntegerv(GL_TEXTURE_BINDING_2D, &saved[4]);
    pGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &saved[5]);
    drain_gl_errors();
    unsigned tex = 0;
    pGenTextures(1, &tex);
    if (!tex) return 0;
    pBindTexture(GL_TEXTURE_2D, tex);
    pPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    pPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    pPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    pPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (int)GL_LINEAR);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (int)GL_LINEAR);
    unsigned wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (int)wrap);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (int)wrap);
    if (saved[5] && pBindBuffer) pBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    pTexImage2D(GL_TEXTURE_2D, 0, (int)GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (saved[5] && pBindBuffer) pBindBuffer(GL_PIXEL_UNPACK_BUFFER, (unsigned)saved[5]);
    pPixelStorei(GL_UNPACK_ROW_LENGTH, saved[0]);
    pPixelStorei(GL_UNPACK_SKIP_PIXELS, saved[1]);
    pPixelStorei(GL_UNPACK_SKIP_ROWS, saved[2]);
    pPixelStorei(GL_UNPACK_ALIGNMENT, saved[3]);
    pBindTexture(GL_TEXTURE_2D, (unsigned)saved[4]);
    return tex;
}

unsigned load_image_mem(const unsigned char* data, unsigned len, int* out_w, int* out_h, bool repeat) {
    if (!data || len == 0) return 0;
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(data, (int)len, &w, &h, &comp, 4);
    if (!px) return 0;
    unsigned tex = upload_rgba(px, w, h, repeat);
    stbi_image_free(px);
    if (tex && out_w && out_h) { *out_w = w; *out_h = h; }
    return tex;
}

// --- shader compilation ---
unsigned compile_shader(unsigned type, const char* src) {
    unsigned sh = pCreateShader(type);
    if (!sh) return 0;
    pShaderSource(sh, 1, &src, nullptr);
    pCompileShader(sh);
    int ok = 0;
    pGetShaderiv(sh, 0x8B81, &ok); // GL_COMPILE_STATUS = 0x8B81
    if (!ok) {
        char log[1024] = {};
        int len = 0;
        pGetShaderInfoLog(sh, 1024, &len, log);
        rlog::logf("menu_bg: shader fail: %s", log);
        pDeleteShader(sh);
        return 0;
    }
    return sh;
}

unsigned create_program(const char* vs, const char* fs) {
    unsigned v = compile_shader(GL_VERTEX_SHADER, vs);
    unsigned f = compile_shader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) {
        if (v) pDeleteShader(v);
        if (f) pDeleteShader(f);
        return 0;
    }
    unsigned prog = pCreateProgram();
    pAttachShader(prog, v);
    pAttachShader(prog, f);
    pLinkProgram(prog);
    int ok = 0;
    pGetProgramiv(prog, 0x8B82, &ok); // GL_LINK_STATUS = 0x8B82
    pDeleteShader(v);
    pDeleteShader(f);
    if (!ok) {
        char log[1024] = {};
        int len = 0;
        pGetProgramInfoLog(prog, 1024, &len, log);
        rlog::logf("menu_bg: link fail: %s", log);
        pDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// Resolve aPos/aTexCoord locations for a linked program.
void resolve_attribs(unsigned prog, int& loc_pos, int& loc_uv) {
    loc_pos = pGetAttribLocation(prog, "aPos");
    loc_uv = pGetAttribLocation(prog, "aTexCoord");
    if (loc_pos < 0) loc_pos = 0;
    if (loc_uv < 0) loc_uv = 1;
}

// Decode the embedded gif into one GL texture per frame + per-frame delays.
void load_gif_mem(const unsigned char* data, unsigned len) {
    if (!data || len == 0) return;
    int* delays = nullptr;
    int w = 0, h = 0, frames = 0, comp = 0;
    unsigned char* px = stbi_load_gif_from_memory(data, (int)len, &delays,
        &w, &h, &frames, &comp, 4);
    if (!px || frames <= 0 || w <= 0 || h <= 0) {
        rlog::logf("menu_bg: gif decode failed (px=%p frames=%d %dx%d)",
            (void*)px, frames, w, h);
        if (px) stbi_image_free(px);
        if (delays) stbi_image_free(delays);
        return;
    }
    int n = frames > k_max_gif_frames ? k_max_gif_frames : frames;
    size_t frame_bytes = (size_t)w * (size_t)h * 4u;
    for (int i = 0; i < n; i++) {
        int d = delays ? delays[i] : 40;
        if (d <= 0) d = 40;
        s_gif_delays_ms[i] = d;
        s_gif_frames[i] = upload_rgba(px + frame_bytes * (size_t)i, w, h, false);
    }
    s_gif_frame_count = n;
    s_gif_w = w;
    s_gif_h = h;
    stbi_image_free(px);
    if (delays) stbi_image_free(delays);
    rlog::logf("menu_bg: gif decoded %d frames %dx%d first=%u",
        s_gif_frame_count, s_gif_w, s_gif_h, s_gif_frame_count ? s_gif_frames[0] : 0u);
}

// Frame index for the current wall-clock time.
int gif_frame_now() {
    if (s_gif_frame_count <= 0) return -1;
    long long now = now_us();
    if (!s_gif_last_us) { s_gif_last_us = now; return 0; }
    double dt = (double)(now - s_gif_last_us) / 1000.0;
    s_gif_last_us = now;
    if (dt < 0.0 || dt > 250.0) dt = 0.0;
    s_gif_clock_ms += dt;
    double total = 0.0;
    for (int i = 0; i < s_gif_frame_count; i++) total += s_gif_delays_ms[i];
    if (total <= 0.0) return 0;
    double t = fmod(s_gif_clock_ms, total);
    for (int i = 0; i < s_gif_frame_count; i++) {
        if (t < (double)s_gif_delays_ms[i]) return i;
        t -= (double)s_gif_delays_ms[i];
    }
    return s_gif_frame_count - 1;
}

// Load all embedded assets once (GL thread).
void ensure_textures() {
    if (s_textures_loaded) return;
    s_textures_loaded = true;
    s_menu_tex = load_image_mem(menu2_data, menu2_data_len, &s_menu_w, &s_menu_h, false);
    s_stars_tex = load_image_mem(stars_data, stars_data_len, &s_stars_w, &s_stars_h, true);
    load_gif_mem(gif_data, gif_data_len);
    s_logo_tex = load_image_mem(logo_data, logo_data_len, nullptr, nullptr, false);
    s_edition_tex = load_image_mem(edition_data, edition_data_len, nullptr, nullptr, false);
    unsigned char dark[4] = {12, 12, 14, 255};
    s_dark_tex = upload_rgba(dark, 1, 1, false);
    rlog::logf("menu_bg: assets menu=%u(%dx%d) stars=%u(%dx%d) dark=%u logo=%u edition=%u",
        s_menu_tex, s_menu_w, s_menu_h, s_stars_tex, s_stars_w, s_stars_h, s_dark_tex,
        s_logo_tex, s_edition_tex);
}

// --- quad setup: pixel-space quad with texcoords ---
// Vertices: x, y, u, v (6 vertices = 2 triangles)
void setup_quad() {
    float quad[] = {
        0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 0.0f,  1.0f, 0.0f,
        1.0f, 1.0f,  1.0f, 1.0f,
        0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 1.0f,  1.0f, 1.0f,
        0.0f, 1.0f,  0.0f, 1.0f,
    };
    int prev_vao = get_int(GL_VERTEX_ARRAY_BINDING);
    int prev_buf = get_int(GL_ARRAY_BUFFER_BINDING);
    pGenVertexArrays(1, &s_quad_vao);
    pBindVertexArray(s_quad_vao);
    pGenBuffers(1, &s_quad_vbo);
    pBindBuffer(GL_ARRAY_BUFFER, s_quad_vbo);
    pBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    pEnableVertexAttribArray(0);
    pVertexAttribPointer(0, 2, GL_FLOAT, 0, 4 * sizeof(float), (void*)0);
    pEnableVertexAttribArray(1);
    pVertexAttribPointer(1, 2, GL_FLOAT, 0, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    pBindVertexArray((unsigned)prev_vao);
    pBindBuffer(GL_ARRAY_BUFFER, (unsigned)prev_buf);
}

// Re-specify attrib pointers for the active program right before a draw, so
// aPos/aTexCoord are bound to whatever locations the linker chose (the code
// never assumes 0/1).
void quad_attrib(int loc_pos, int loc_uv) {
    pEnableVertexAttribArray((unsigned)loc_pos);
    pVertexAttribPointer((unsigned)loc_pos, 2, GL_FLOAT, 0, 4 * sizeof(float), (void*)0);
    pEnableVertexAttribArray((unsigned)loc_uv);
    pVertexAttribPointer((unsigned)loc_uv, 2, GL_FLOAT, 0, 4 * sizeof(float),
        (void*)(2 * sizeof(float)));
}

// Upload 6 verts (x,y,u,v) and draw them.
void draw_quad(int loc_pos, int loc_uv, const float* verts, int vert_bytes) {
    pBindVertexArray(s_quad_vao);
    pBindBuffer(GL_ARRAY_BUFFER, s_quad_vbo);
    pBufferData(GL_ARRAY_BUFFER, vert_bytes, verts, GL_DYNAMIC_DRAW);
    quad_attrib(loc_pos, loc_uv);
    pDrawArrays(GL_TRIANGLES, 0, 6);
    pBindVertexArray(0);
}

// Current framebuffer size in real pixels (MC's Screen.width is a *scaled*
// gui size, useless for glViewport).
bool viewport_size(int& w, int& h) {
    if (!pGetIntegerv) return false;
    int vp[4] = {0, 0, 0, 0};
    pGetIntegerv(GL_VIEWPORT_ENUM, vp);
    if (vp[2] <= 0 || vp[3] <= 0) return false;
    w = vp[2];
    h = vp[3];
    return true;
}

bool s_init_attempted = false;

// Aspect-fill: compute UV rect that covers the target while preserving AR.
// Returns uv offsets in [0,1] range.
void compute_aspect_fill(int tex_w, int tex_h, int win_w, int win_h,
                         float& u0, float& v0, float& u1, float& v1) {
    float tex_ar = (float)tex_w / (float)tex_h;
    float win_ar = (float)win_w / (float)win_h;
    if (tex_ar > win_ar) {
        // Texture wider: crop horizontally.
        float scale = win_ar / tex_ar;
        u0 = (1.0f - scale) * 0.5f;
        u1 = 1.0f - u0;
        v0 = 0.0f;
        v1 = 1.0f;
    } else {
        // Texture taller: crop vertically.
        float scale = tex_ar / win_ar;
        v0 = (1.0f - scale) * 0.5f;
        v1 = 1.0f - v0;
        u0 = 0.0f;
        u1 = 1.0f;
    }
}

} // anonymous namespace

bool init() {
    if (s_init_attempted) return gl_ok;
    s_init_attempted = true;
    rlog::logf("menu_bg: init starting");
    if (!load_gl()) {
        rlog::logf("menu_bg: GL load failed");
        return false;
    }
    s_bg_program = create_program(k_bg_vs, k_bg_fs);
    s_star_program = create_program(k_star_vs, k_star_fs);
    if (!s_bg_program || !s_star_program) {
        rlog::logf("menu_bg: shaders failed (bg=%u star=%u)", s_bg_program, s_star_program);
        return false;
    }
    resolve_attribs(s_bg_program, s_bg_loc_pos, s_bg_loc_uv);
    resolve_attribs(s_star_program, s_star_loc_pos, s_star_loc_uv);
    setup_quad();
    if (!s_quad_vao || !s_quad_vbo) {
        rlog::logf("menu_bg: quad setup failed");
        return false;
    }
    rlog::logf("menu_bg: init OK (bg=%u star=%u attribs bg=%d/%d star=%d/%d)",
        s_bg_program, s_star_program, s_bg_loc_pos, s_bg_loc_uv,
        s_star_loc_pos, s_star_loc_uv);
    return true;
}

bool is_custom_bg_screen(void* env_v, void* screen_v) {
    JNIEnv* env = (JNIEnv*)env_v;
    jobject screen = (jobject)screen_v;
    if (!env || !screen) return false;
    if (!g_title_class) {
        jclass local = env->FindClass("net/minecraft/class_442");
        if (env->ExceptionCheck()) { env->ExceptionClear(); local = nullptr; }
        if (local) {
            g_title_class = env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
        }
    }
    return g_title_class && env->IsInstanceOf(screen, (jclass)g_title_class);
}

bool is_loading_screen(void* env_v, void* screen_v) {
    JNIEnv* env = (JNIEnv*)env_v;
    jobject screen = (jobject)screen_v;
    if (!env || !screen) return false;
    if (!g_progress_class) {
        jclass local = env->FindClass("net/minecraft/class_435");
        if (env->ExceptionCheck()) { env->ExceptionClear(); local = nullptr; }
        if (local) {
            g_progress_class = env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
        }
    }
    return g_progress_class && env->IsInstanceOf(screen, (jclass)g_progress_class);
}

void draw_background() {
    if (!load_gl() || !s_bg_program || !s_quad_vao) return;
    ensure_textures();
    if (!s_menu_tex) return;
    int pw = 0, ph = 0;
    if (!viewport_size(pw, ph)) return;

    // Compute aspect-fill UVs.
    float u0, v0, u1, v1;
    compute_aspect_fill(s_menu_w, s_menu_h, pw, ph, u0, v0, u1, v1);

    float quad[] = {
        0.0f, 0.0f,  u0, v0,
        (float)pw, 0.0f,  u1, v0,
        (float)pw, (float)ph,  u1, v1,
        0.0f, 0.0f,  u0, v0,
        (float)pw, (float)ph,  u1, v1,
        0.0f, (float)ph,  u0, v1,
    };

    GlState st = save_state();
    prep_draw(pw, ph);

    pUseProgram(s_bg_program);
    pUniform2f(pGetUniformLocation(s_bg_program, "uResolution"), (float)pw, (float)ph);
    pUniform1f(pGetUniformLocation(s_bg_program, "uBlack"), 0.0f);
    if (pActiveTexture) pActiveTexture(GL_TEXTURE0);
    pBindTexture(GL_TEXTURE_2D, s_menu_tex);
    pUniform1i(pGetUniformLocation(s_bg_program, "uTexture"), 0);
    draw_quad(s_bg_loc_pos, s_bg_loc_uv, quad, sizeof(quad));

    // Heartbeat so we can tell from the log whether the quad really reached
    // the GPU (and with which viewport / GL error).
    {
        static int s_n = 0;
        s_n++;
        if (s_n == 1 || (s_n % 600) == 0) {
            unsigned err = pGetError ? pGetError() : 0u;
            while (pGetError && pGetError() != 0) {}
            rlog::logf("menu_bg: draw_background #%d vp=%dx%d tex=%u err=0x%x",
                s_n, pw, ph, s_menu_tex, err);
        }
    }

    restore_state(st);
}

void draw_stars() {
    if (!load_gl() || !s_star_program || !s_quad_vao) return;
    ensure_textures();
    if (!s_stars_tex) return;
    int pw = 0, ph = 0;
    if (!viewport_size(pw, ph)) return;

    GlState st = save_state();
    prep_draw(pw, ph);
    pBlendFunc(GL_SRC_ALPHA, GL_ONE); // Additive bloom.

    pUseProgram(s_star_program);
    pUniform2f(pGetUniformLocation(s_star_program, "uResolution"), (float)pw, (float)ph);
    pUniform1f(pGetUniformLocation(s_star_program, "uTime"), s_anim_time);
    pUniform1f(pGetUniformLocation(s_star_program, "uAlpha"), 0.7f);
    if (pActiveTexture) pActiveTexture(GL_TEXTURE0);
    pBindTexture(GL_TEXTURE_2D, s_stars_tex);
    pUniform1i(pGetUniformLocation(s_star_program, "uTexture"), 0);

    float quad[] = {
        0.0f, 0.0f,  0.0f, 0.0f,
        (float)pw, 0.0f,  1.0f, 0.0f,
        (float)pw, (float)ph,  1.0f, 1.0f,
        0.0f, 0.0f,  0.0f, 0.0f,
        (float)pw, (float)ph,  1.0f, 1.0f,
        0.0f, (float)ph,  0.0f, 1.0f,
    };
    draw_quad(s_star_loc_pos, s_star_loc_uv, quad, sizeof(quad));

    restore_state(st);
}

void draw_loading() {
    if (!load_gl() || !s_bg_program || !s_quad_vao) return;
    ensure_textures();
    int frame = gif_frame_now();
    if (frame < 0 || !s_gif_frames[frame]) return;
    int pw = 0, ph = 0;
    if (!viewport_size(pw, ph)) return;

    // Center the gif with aspect-fit on a dark background.
    float gif_ar = (float)s_gif_w / (float)s_gif_h;
    float win_ar = (float)pw / (float)ph;
    float draw_w, draw_h, draw_x, draw_y;
    if (gif_ar > win_ar) {
        draw_w = (float)pw * 0.8f;
        draw_h = draw_w / gif_ar;
    } else {
        draw_h = (float)ph * 0.8f;
        draw_w = draw_h * gif_ar;
    }
    draw_x = ((float)pw - draw_w) * 0.5f;
    draw_y = ((float)ph - draw_h) * 0.5f;

    GlState st = save_state();
    prep_draw(pw, ph);

    pUseProgram(s_bg_program);
    pUniform2f(pGetUniformLocation(s_bg_program, "uResolution"), (float)pw, (float)ph);
    pUniform1f(pGetUniformLocation(s_bg_program, "uBlack"), 0.0f);

    // Dark fullscreen backdrop.
    float dark[] = {
        0.0f, 0.0f,                    0.0f, 0.0f,
        (float)pw, 0.0f,               1.0f, 0.0f,
        (float)pw, (float)ph,          1.0f, 1.0f,
        0.0f, 0.0f,                    0.0f, 0.0f,
        (float)pw, (float)ph,          1.0f, 1.0f,
        0.0f, (float)ph,               0.0f, 1.0f,
    };
    if (pActiveTexture) pActiveTexture(GL_TEXTURE0);
    pBindTexture(GL_TEXTURE_2D, s_dark_tex);
    pUniform1i(pGetUniformLocation(s_bg_program, "uTexture"), 0);
    draw_quad(s_bg_loc_pos, s_bg_loc_uv, dark, sizeof(dark));

    // Current gif frame, centered.
    float quad[] = {
        draw_x, draw_y,                    0.0f, 0.0f,
        draw_x + draw_w, draw_y,           1.0f, 0.0f,
        draw_x + draw_w, draw_y + draw_h,  1.0f, 1.0f,
        draw_x, draw_y,                    0.0f, 0.0f,
        draw_x + draw_w, draw_y + draw_h,  1.0f, 1.0f,
        draw_x, draw_y + draw_h,           0.0f, 1.0f,
    };
    pBindTexture(GL_TEXTURE_2D, s_gif_frames[frame]);
    draw_quad(s_bg_loc_pos, s_bg_loc_uv, quad, sizeof(quad));

    restore_state(st);
}

// Vanilla TitleScreen logo layout, in GUI-scaled units:
//   logo:    x = gui_w/2 - 128, y = 30, size 256x44, region 256x64
//   edition: x = gui_w/2 -  64, y = 67, size 128x14, region 128x16
// Drawn as a black silhouette so the title reads over a bright background.
void draw_logo_black(int gui_w, int gui_h) {
    (void)gui_h;
    if (!load_gl() || !s_bg_program || !s_quad_vao) return;
    ensure_textures();
    if (!s_logo_tex || gui_w <= 0) return;
    int pw = 0, ph = 0;
    if (!viewport_size(pw, ph)) return;
    float scale = (float)pw / (float)gui_w;

    GlState st = save_state();
    prep_draw(pw, ph);
    pUseProgram(s_bg_program);
    pUniform2f(pGetUniformLocation(s_bg_program, "uResolution"), (float)pw, (float)ph);
    pUniform1f(pGetUniformLocation(s_bg_program, "uBlack"), 1.0f);
    if (pActiveTexture) pActiveTexture(GL_TEXTURE0);
    pUniform1i(pGetUniformLocation(s_bg_program, "uTexture"), 0);

    auto quad_at = [&](float x, float y, float w, float h, float v1) {
        float q[] = {
            x, y,           0.0f, 0.0f,
            x + w, y,       1.0f, 0.0f,
            x + w, y + h,   1.0f, v1,
            x, y,           0.0f, 0.0f,
            x + w, y + h,   1.0f, v1,
            x, y + h,       0.0f, v1,
        };
        draw_quad(s_bg_loc_pos, s_bg_loc_uv, q, sizeof(q));
    };

    pBindTexture(GL_TEXTURE_2D, s_logo_tex);
    quad_at(((float)gui_w * 0.5f - 128.0f) * scale, 30.0f * scale,
        256.0f * scale, 44.0f * scale, 44.0f / 64.0f);

    if (s_edition_tex) {
        pBindTexture(GL_TEXTURE_2D, s_edition_tex);
        quad_at(((float)gui_w * 0.5f - 64.0f) * scale, 67.0f * scale,
            128.0f * scale, 14.0f * scale, 14.0f / 16.0f);
    }

    restore_state(st);
}

void tick(float dt_seconds) {
    if (dt_seconds > 0.0f && dt_seconds < 0.25f)
        s_anim_time += dt_seconds;
}

}}}} // namespace flaway::modules::better_minecraft::menu_bg
