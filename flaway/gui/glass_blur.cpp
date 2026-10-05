#include "glass_blur.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// GL function pointer types
typedef void   (*PFNGLGENFRAMEBUFFERSPROC)(int, unsigned*);
typedef void   (*PFNGLBINDFRAMEBUFFERPROC)(unsigned, unsigned);
typedef void   (*PFNGLDELETEFRAMEBUFFERSPROC)(int, const unsigned*);
typedef void   (*PFNGLFRAMEBUFFERTEXTURE2DPROC)(unsigned, unsigned, unsigned, unsigned, int);
typedef void   (*PFNGLGENRENDERBUFFERSPROC)(int, unsigned*);
typedef void   (*PFNGLBINDRENDERBUFFERPROC)(unsigned, unsigned);
typedef void   (*PFNGLRENDERBUFFERSTORAGEPROC)(unsigned, unsigned, int, int);
typedef void   (*PFNGLFRAMEBUFFERRENDERBUFFERPROC)(unsigned, unsigned, unsigned, unsigned);
typedef unsigned (*PFNGLCHECKFRAMEBUFFERSTATUSPROC)(unsigned);
typedef void   (*PFNGLDELETERENDERBUFFERSPROC)(int, const unsigned*);
typedef void   (*PFNGLBLITFRAMEBUFFERPROC)(int, int, int, int, int, int, int, int, unsigned, unsigned);
typedef void   (*PFNGLUSEPROGRAMPROC)(unsigned);
typedef unsigned (*PFNGLCREATEPROGRAMPROC)();
typedef void   (*PFNGLDELETEPROGRAMPROC)(unsigned);
typedef unsigned (*PFNGLCREATESHADERPROC)(unsigned);
typedef void   (*PFNGLDELETESHADERPROC)(unsigned);
typedef void   (*PFNGLSHADERSOURCEPROC)(unsigned, int, const char**, const int*);
typedef void   (*PFNGLCOMPILESHADERPROC)(unsigned);
typedef void   (*PFNGLGETSHADERIVPROC)(unsigned, unsigned, int*);
typedef void   (*PFNGLATTACHSHADERPROC)(unsigned, unsigned);
typedef void   (*PFNGLLINKPROGRAMPROC)(unsigned);
typedef void   (*PFNGLGETPROGRAMIVPROC)(unsigned, unsigned, int*);
typedef void   (*PFNGLGETPROGRAMINFOLOGPROC)(unsigned, int, int*, char*);
typedef void   (*PFNGLGETSHADERINFOLOGPROC)(unsigned, int, int*, char*);
typedef int    (*PFNGLGETUNIFORMLOCATIONPROC)(unsigned, const char*);
typedef void   (*PFNGLUNIFORM1IPROC)(unsigned, int);
typedef void   (*PFNGLUNIFORM1FPROC)(unsigned, float);
typedef void   (*PFNGLUNIFORM2FPROC)(unsigned, float, float);
typedef void   (*PFNGLACTIVETEXTUREPROC)(unsigned);
typedef void   (*PFNGLBINDTEXTUREPROC)(unsigned, unsigned);
typedef void   (*PFNGLGENTEXTURESPROC)(int, unsigned*);
typedef void   (*PFNGLDELETETEXTURESPROC)(int, const unsigned*);
typedef void   (*PFNGLTEXIMAGE2DPROC)(unsigned, int, int, int, int, int, unsigned, unsigned, const void*);
typedef void   (*PFNGLTEXPARAMETERIPROC)(unsigned, unsigned, int);
typedef void   (*PFNGLPIXELSTOREIPROC)(unsigned, int);
typedef void   (*PFNGLREADPIXELSPROC)(int, int, int, int, unsigned, unsigned, void*);
typedef void   (*PFNGLDISABLEPROC)(unsigned);
typedef void   (*PFNGLENABLEPROC)(unsigned);
typedef void   (*PFNGLVIEWPORTPROC)(int, int, int, int);
typedef void   (*PFNGLSCISSORPROC)(int, int, int, int);
typedef void   (*PFNGLGETINTEGERVPROC)(unsigned, int*);
typedef unsigned char (*PFNGLISENABLEDPROC)(unsigned);
typedef void   (*PFNGLBLENDFUNCPROC)(unsigned, unsigned);
typedef void   (*PFNGLBINDVERTEXARRAYPROC)(unsigned);
typedef void   (*PFNGLGENVERTEXARRAYSPROC)(int, unsigned*);
typedef void   (*PFNGLDELETEVERTEXARRAYSPROC)(int, const unsigned*);
typedef void   (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(unsigned);
typedef void   (*PFNGLDISABLEVERTEXATTRIBARRAYPROC)(unsigned);
typedef void   (*PFNGLVERTEXATTRIBPOINTERPROC)(unsigned, int, unsigned, unsigned char, int, const void*);
typedef void   (*PFNGLGENBUFFERSPROC)(int, unsigned*);
typedef void   (*PFNGLDELETEBUFFERSPROC)(int, const unsigned*);
typedef void   (*PFNGLBINDBUFFERPROC)(unsigned, unsigned);
typedef void   (*PFNGLBUFFERDATAPROC)(unsigned, long long, const void*, unsigned);
typedef void   (*PFNGLDRAWARRAYSPROC)(unsigned, int, int);

// Loaded function pointers
static PFNGLGENFRAMEBUFFERSPROC _glGenFramebuffers = nullptr;
static PFNGLBINDFRAMEBUFFERPROC _glBindFramebuffer = nullptr;
static PFNGLDELETEFRAMEBUFFERSPROC _glDeleteFramebuffers = nullptr;
static PFNGLFRAMEBUFFERTEXTURE2DPROC _glFramebufferTexture2D = nullptr;
static PFNGLGENRENDERBUFFERSPROC _glGenRenderbuffers = nullptr;
static PFNGLBINDRENDERBUFFERPROC _glBindRenderbuffer = nullptr;
static PFNGLRENDERBUFFERSTORAGEPROC _glRenderbufferStorage = nullptr;
static PFNGLFRAMEBUFFERRENDERBUFFERPROC _glFramebufferRenderbuffer = nullptr;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC _glCheckFramebufferStatus = nullptr;
static PFNGLDELETERENDERBUFFERSPROC _glDeleteRenderbuffers = nullptr;
static PFNGLBLITFRAMEBUFFERPROC _glBlitFramebuffer = nullptr;
static PFNGLUSEPROGRAMPROC _glUseProgram = nullptr;
static PFNGLCREATEPROGRAMPROC _glCreateProgram = nullptr;
static PFNGLDELETEPROGRAMPROC _glDeleteProgram = nullptr;
static PFNGLCREATESHADERPROC _glCreateShader = nullptr;
static PFNGLDELETESHADERPROC _glDeleteShader = nullptr;
static PFNGLSHADERSOURCEPROC _glShaderSource = nullptr;
static PFNGLCOMPILESHADERPROC _glCompileShader = nullptr;
static PFNGLGETSHADERIVPROC _glGetShaderiv = nullptr;
static PFNGLATTACHSHADERPROC _glAttachShader = nullptr;
static PFNGLLINKPROGRAMPROC _glLinkProgram = nullptr;
static PFNGLGETPROGRAMIVPROC _glGetProgramiv = nullptr;
static PFNGLGETPROGRAMINFOLOGPROC _glGetProgramInfoLog = nullptr;
static PFNGLGETSHADERINFOLOGPROC _glGetShaderInfoLog = nullptr;
static PFNGLGETUNIFORMLOCATIONPROC _glGetUniformLocation = nullptr;
static PFNGLUNIFORM1IPROC _glUniform1i = nullptr;
static PFNGLUNIFORM1FPROC _glUniform1f = nullptr;
static PFNGLUNIFORM2FPROC _glUniform2f = nullptr;
static PFNGLACTIVETEXTUREPROC _glActiveTexture = nullptr;
static PFNGLBINDTEXTUREPROC _glBindTexture = nullptr;
static PFNGLGENTEXTURESPROC _glGenTextures = nullptr;
static PFNGLDELETETEXTURESPROC _glDeleteTextures = nullptr;
static PFNGLTEXIMAGE2DPROC _glTexImage2D = nullptr;
static PFNGLTEXPARAMETERIPROC _glTexParameteri = nullptr;
static PFNGLPIXELSTOREIPROC _glPixelStorei = nullptr;
static PFNGLREADPIXELSPROC _glReadPixels = nullptr;
static PFNGLDISABLEPROC _glDisable = nullptr;
static PFNGLENABLEPROC _glEnable = nullptr;
static PFNGLVIEWPORTPROC _glViewport = nullptr;
static PFNGLSCISSORPROC _glScissor = nullptr;
static PFNGLGETINTEGERVPROC _glGetIntegerv = nullptr;
static PFNGLISENABLEDPROC _glIsEnabled = nullptr;
static PFNGLBLENDFUNCPROC _glBlendFunc = nullptr;
static PFNGLBINDVERTEXARRAYPROC _glBindVertexArray = nullptr;
static PFNGLGENVERTEXARRAYSPROC _glGenVertexArrays = nullptr;
static PFNGLDELETEVERTEXARRAYSPROC _glDeleteVertexArrays = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC _glEnableVertexAttribArray = nullptr;
static PFNGLDISABLEVERTEXATTRIBARRAYPROC _glDisableVertexAttribArray = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC _glVertexAttribPointer = nullptr;
static PFNGLGENBUFFERSPROC _glGenBuffers = nullptr;
static PFNGLDELETEBUFFERSPROC _glDeleteBuffers = nullptr;
static PFNGLBINDBUFFERPROC _glBindBuffer = nullptr;
static PFNGLBUFFERDATAPROC _glBufferData = nullptr;
static PFNGLDRAWARRAYSPROC _glDrawArrays = nullptr;

// GL constants
static const unsigned GL_FRAMEBUFFER = 0x8D40;
static const unsigned GL_READ_FRAMEBUFFER = 0x8CA8;
static const unsigned GL_DRAW_FRAMEBUFFER = 0x8CA9;
static const unsigned GL_COLOR_ATTACHMENT0 = 0x8CE0;
static const unsigned GL_TEXTURE_2D = 0x0DE1;
static const unsigned GL_TEXTURE0 = 0x84C0;
static const unsigned GL_RGBA = 0x1908;
static const unsigned GL_RGBA8 = 0x8058;
static const unsigned GL_UNSIGNED_BYTE = 0x1401;
static const unsigned GL_LINEAR = 0x2601;
static const unsigned GL_CLAMP_TO_EDGE = 0x812F;
static const unsigned GL_VERTEX_SHADER = 0x8B31;
static const unsigned GL_FRAGMENT_SHADER = 0x8B30;
static const unsigned GL_COMPILE_STATUS = 0x8B81;
static const unsigned GL_LINK_STATUS = 0x8B82;
static const unsigned GL_TRIANGLES = 0x0004;
static const unsigned GL_TRIANGLE_FAN = 0x0006;
static const unsigned GL_DEPTH_TEST = 0x0B71;
static const unsigned GL_FLOAT = 0x1406;
static const unsigned GL_ARRAY_BUFFER = 0x8892;
static const unsigned GL_STATIC_DRAW = 0x88E4;
static const unsigned GL_SCISSOR_TEST = 0x0C11;
static const unsigned GL_NEAREST = 0x2600;
static const unsigned GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
static const unsigned GL_TEXTURE = 0x1702;
static const unsigned GL_TEXTURE_MIN_FILTER = 0x2801;
static const unsigned GL_TEXTURE_MAG_FILTER = 0x2800;
static const unsigned GL_TEXTURE_WRAP_S = 0x2802;
static const unsigned GL_TEXTURE_WRAP_T = 0x2803;

static bool s_initialized = false;

static unsigned int s_src_fbo = 0;
static unsigned int s_src_tex = 0;
static unsigned int s_ping_fbo = 0;
static unsigned int s_ping_tex = 0;
static unsigned int s_pong_fbo = 0;
static unsigned int s_pong_tex = 0;
static unsigned int s_blur_program = 0;
static unsigned int s_display_program = 0;
static unsigned int s_quad_vao = 0;
static unsigned int s_quad_vbo = 0;
static int s_width = 0;
static int s_height = 0;
static int s_blur_tex_w = 0;
static int s_blur_tex_h = 0;

static const int BLUR_SCALE = 4;

static bool load_gl() {
    static bool loaded = false;
    if (loaded) return true;
    _glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)dlsym(RTLD_DEFAULT, "glGenFramebuffers");
    _glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)dlsym(RTLD_DEFAULT, "glBindFramebuffer");
    _glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)dlsym(RTLD_DEFAULT, "glDeleteFramebuffers");
    _glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)dlsym(RTLD_DEFAULT, "glFramebufferTexture2D");
    _glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)dlsym(RTLD_DEFAULT, "glGenRenderbuffers");
    _glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)dlsym(RTLD_DEFAULT, "glBindRenderbuffer");
    _glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)dlsym(RTLD_DEFAULT, "glRenderbufferStorage");
    _glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)dlsym(RTLD_DEFAULT, "glFramebufferRenderbuffer");
    _glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)dlsym(RTLD_DEFAULT, "glCheckFramebufferStatus");
    _glDeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSPROC)dlsym(RTLD_DEFAULT, "glDeleteRenderbuffers");
    _glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)dlsym(RTLD_DEFAULT, "glBlitFramebuffer");
    _glUseProgram = (PFNGLUSEPROGRAMPROC)dlsym(RTLD_DEFAULT, "glUseProgram");
    _glCreateProgram = (PFNGLCREATEPROGRAMPROC)dlsym(RTLD_DEFAULT, "glCreateProgram");
    _glDeleteProgram = (PFNGLDELETEPROGRAMPROC)dlsym(RTLD_DEFAULT, "glDeleteProgram");
    _glCreateShader = (PFNGLCREATESHADERPROC)dlsym(RTLD_DEFAULT, "glCreateShader");
    _glDeleteShader = (PFNGLDELETESHADERPROC)dlsym(RTLD_DEFAULT, "glDeleteShader");
    _glShaderSource = (PFNGLSHADERSOURCEPROC)dlsym(RTLD_DEFAULT, "glShaderSource");
    _glCompileShader = (PFNGLCOMPILESHADERPROC)dlsym(RTLD_DEFAULT, "glCompileShader");
    _glGetShaderiv = (PFNGLGETSHADERIVPROC)dlsym(RTLD_DEFAULT, "glGetShaderiv");
    _glAttachShader = (PFNGLATTACHSHADERPROC)dlsym(RTLD_DEFAULT, "glAttachShader");
    _glLinkProgram = (PFNGLLINKPROGRAMPROC)dlsym(RTLD_DEFAULT, "glLinkProgram");
    _glGetProgramiv = (PFNGLGETPROGRAMIVPROC)dlsym(RTLD_DEFAULT, "glGetProgramiv");
    _glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)dlsym(RTLD_DEFAULT, "glGetProgramInfoLog");
    _glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)dlsym(RTLD_DEFAULT, "glGetShaderInfoLog");
    _glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)dlsym(RTLD_DEFAULT, "glGetUniformLocation");
    _glUniform1i = (PFNGLUNIFORM1IPROC)dlsym(RTLD_DEFAULT, "glUniform1i");
    _glUniform1f = (PFNGLUNIFORM1FPROC)dlsym(RTLD_DEFAULT, "glUniform1f");
    _glUniform2f = (PFNGLUNIFORM2FPROC)dlsym(RTLD_DEFAULT, "glUniform2f");
    _glActiveTexture = (PFNGLACTIVETEXTUREPROC)dlsym(RTLD_DEFAULT, "glActiveTexture");
    _glBindTexture = (PFNGLBINDTEXTUREPROC)dlsym(RTLD_DEFAULT, "glBindTexture");
    _glGenTextures = (PFNGLGENTEXTURESPROC)dlsym(RTLD_DEFAULT, "glGenTextures");
    _glDeleteTextures = (PFNGLDELETETEXTURESPROC)dlsym(RTLD_DEFAULT, "glDeleteTextures");
    _glTexImage2D = (PFNGLTEXIMAGE2DPROC)dlsym(RTLD_DEFAULT, "glTexImage2D");
    _glTexParameteri = (PFNGLTEXPARAMETERIPROC)dlsym(RTLD_DEFAULT, "glTexParameteri");
    _glPixelStorei = (PFNGLPIXELSTOREIPROC)dlsym(RTLD_DEFAULT, "glPixelStorei");
    _glReadPixels = (PFNGLREADPIXELSPROC)dlsym(RTLD_DEFAULT, "glReadPixels");
    _glDisable = (PFNGLDISABLEPROC)dlsym(RTLD_DEFAULT, "glDisable");
    _glEnable = (PFNGLENABLEPROC)dlsym(RTLD_DEFAULT, "glEnable");
    _glViewport = (PFNGLVIEWPORTPROC)dlsym(RTLD_DEFAULT, "glViewport");
    _glScissor = (PFNGLSCISSORPROC)dlsym(RTLD_DEFAULT, "glScissor");
    _glGetIntegerv = (PFNGLGETINTEGERVPROC)dlsym(RTLD_DEFAULT, "glGetIntegerv");
    _glIsEnabled = (PFNGLISENABLEDPROC)dlsym(RTLD_DEFAULT, "glIsEnabled");
    _glBlendFunc = (PFNGLBLENDFUNCPROC)dlsym(RTLD_DEFAULT, "glBlendFunc");
    _glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)dlsym(RTLD_DEFAULT, "glBindVertexArray");
    _glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)dlsym(RTLD_DEFAULT, "glGenVertexArrays");
    _glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)dlsym(RTLD_DEFAULT, "glDeleteVertexArrays");
    _glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)dlsym(RTLD_DEFAULT, "glEnableVertexAttribArray");
    _glDisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)dlsym(RTLD_DEFAULT, "glDisableVertexAttribArray");
    _glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)dlsym(RTLD_DEFAULT, "glVertexAttribPointer");
    _glGenBuffers = (PFNGLGENBUFFERSPROC)dlsym(RTLD_DEFAULT, "glGenBuffers");
    _glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)dlsym(RTLD_DEFAULT, "glDeleteBuffers");
    _glBindBuffer = (PFNGLBINDBUFFERPROC)dlsym(RTLD_DEFAULT, "glBindBuffer");
    _glBufferData = (PFNGLBUFFERDATAPROC)dlsym(RTLD_DEFAULT, "glBufferData");
    _glDrawArrays = (PFNGLDRAWARRAYSPROC)dlsym(RTLD_DEFAULT, "glDrawArrays");
    loaded = _glGenFramebuffers && _glBindFramebuffer && _glDeleteFramebuffers
        && _glFramebufferTexture2D && _glCheckFramebufferStatus
        && _glCreateProgram && _glCreateShader && _glShaderSource
        && _glCompileShader && _glGetShaderiv && _glAttachShader
        && _glLinkProgram && _glGetProgramiv && _glDeleteShader
        && _glDeleteProgram && _glUseProgram && _glGetUniformLocation
        && _glUniform1i && _glUniform2f && _glActiveTexture
        && _glBindTexture && _glGenTextures && _glDeleteTextures
        && _glTexImage2D && _glTexParameteri && _glGetIntegerv
        && _glGenVertexArrays && _glBindVertexArray && _glGenBuffers
        && _glBindBuffer && _glBufferData && _glEnableVertexAttribArray
        && _glVertexAttribPointer && _glDrawArrays && _glDisable
        && _glEnable && _glViewport && _glScissor && _glBlendFunc
        && _glIsEnabled
        && _glBlitFramebuffer;
    return loaded;
}

static bool check_fbo(unsigned fbo) {
    _glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    unsigned status = _glCheckFramebufferStatus(GL_FRAMEBUFFER);
    _glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return status == 0x8CD5; // GL_FRAMEBUFFER_COMPLETE
}

// --- state save/restore -----------------------------------------------------
// This context belongs to the game: everything we touch has to come back
// exactly as we found it (Minecraft/Sodium may cache some of it).
static const unsigned GL_ACTIVE_TEXTURE = 0x84E0; // GL_ACTIVE_TEXTURE (0x84E0, NOT 0x84C0 = GL_TEXTURE0)
static const unsigned GL_TEXTURE_BINDING_2D = 0x806A;
static const unsigned GL_VERTEX_ARRAY_BINDING = 0x85B5;
static const unsigned GL_ARRAY_BUFFER_BINDING = 0x8894;
static const unsigned GL_READ_FRAMEBUFFER_BINDING = 0x8CAA;

static int get_int(unsigned pname) {
    int v = 0;
    if (_glGetIntegerv) _glGetIntegerv(pname, &v);
    return v;
}

// Minecraft's GlStateManager caches the active texture unit and the texture
// bound on it: if we change either without it knowing, the game will skip its
// own glBindTexture/glActiveTexture forever and the world samples whatever
// texture we left behind (unit0 = the block atlas -> black world). Everything
// we do with textures therefore happens on GL_TEXTURE0 and is put back exactly.
struct TexState { unsigned active; int binding0; };

static TexState save_tex_state() {
    TexState s;
    s.active = (unsigned)get_int(GL_ACTIVE_TEXTURE);
    if (!s.active) s.active = GL_TEXTURE0;
    if (_glActiveTexture) _glActiveTexture(GL_TEXTURE0);
    s.binding0 = get_int(GL_TEXTURE_BINDING_2D);
    if (_glActiveTexture) _glActiveTexture(s.active);
    return s;
}

static void restore_tex_state(const TexState& s) {
    if (_glActiveTexture) _glActiveTexture(GL_TEXTURE0);
    if (_glBindTexture) _glBindTexture(GL_TEXTURE_2D, (unsigned)s.binding0);
    if (_glActiveTexture) _glActiveTexture(s.active);
}

static unsigned create_tex_fbo(int w, int h, unsigned* fbo_out) {
    TexState tex_st = save_tex_state();
    unsigned prev_fbo = (unsigned)get_int(0x8CA6); // GL_DRAW_FRAMEBUFFER_BINDING
    unsigned prev_read_fbo = (unsigned)get_int(GL_READ_FRAMEBUFFER_BINDING);
    if (_glActiveTexture) _glActiveTexture(GL_TEXTURE0);
    unsigned tex = 0;
    unsigned fbo = 0;
    _glGenTextures(1, &tex);
    _glBindTexture(GL_TEXTURE_2D, tex);
    _glTexParameteri(GL_TEXTURE_2D, 0x2801, GL_LINEAR); // GL_TEXTURE_MIN_FILTER
    _glTexParameteri(GL_TEXTURE_2D, 0x2800, GL_LINEAR); // GL_TEXTURE_MAG_FILTER
    _glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // GL_TEXTURE_WRAP_S
    _glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // GL_TEXTURE_WRAP_T
    _glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    _glGenFramebuffers(1, &fbo);
    _glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    _glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    bool ok = check_fbo(fbo);

    if (!ok) {
        _glDeleteTextures(1, &tex);
        _glDeleteFramebuffers(1, &fbo);
        if (fbo_out) *fbo_out = 0;
        _glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read_fbo);
        _glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_fbo);
        restore_tex_state(tex_st);
        return 0;
    }
    if (fbo_out) *fbo_out = fbo;
    _glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read_fbo);
    _glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_fbo);
    restore_tex_state(tex_st);
    return tex;
}

static unsigned compile_shader(unsigned type, const char* src) {
    unsigned s = _glCreateShader(type);
    _glShaderSource(s, 1, &src, nullptr);
    _glCompileShader(s);
    int ok = 0;
    _glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        _glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "[blur] shader compile error: %s\n", log);
        _glDeleteShader(s);
        return 0;
    }
    return s;
}

static unsigned create_program(const char* vert_src, const char* frag_src) {
    unsigned vs = compile_shader(GL_VERTEX_SHADER, vert_src);
    unsigned fs = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    if (!vs || !fs) {
        if (vs) _glDeleteShader(vs);
        if (fs) _glDeleteShader(fs);
        return 0;
    }
    unsigned prog = _glCreateProgram();
    _glAttachShader(prog, vs);
    _glAttachShader(prog, fs);
    _glLinkProgram(prog);
    int ok = 0;
    _glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        _glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "[blur] program link error: %s\n", log);
        _glDeleteProgram(prog);
        prog = 0;
    }
    _glDeleteShader(vs);
    _glDeleteShader(fs);
    return prog;
}

static const char* k_quad_vs = R"(
#version 150
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vTexCoord;
void main() {
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* k_blur_frag = R"(
#version 150
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec2 uDirection;
uniform vec2 uTexelSize;
void main() {
    vec3 result = texture(uTexture, vTexCoord).rgb * 0.227027;
    float weights[4];
    weights[0] = 0.1945946;
    weights[1] = 0.1216216;
    weights[2] = 0.054054;
    weights[3] = 0.016216;
    for (int i = 0; i < 4; i++) {
        vec2 offset = uDirection * uTexelSize * float(i) * 1.5;
        result += texture(uTexture, vTexCoord + offset).rgb * weights[i];
        result += texture(uTexture, vTexCoord - offset).rgb * weights[i];
    }
    FragColor = vec4(result, 1.0);
}
)";

static const char* k_display_frag = R"(
#version 150
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D uTexture;
void main() {
    FragColor = texture(uTexture, vTexCoord);
}
)";

static void setup_quad() {
    float quad[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
    };
    // Runs during the ImGui build pass, outside RenderDrawData's state
    // backup: restore BOTH bindings or the game keeps drawing with VAO 0
    // (Sodium caches it) and the world comes out black.
    int prev_array_buf = get_int(GL_ARRAY_BUFFER_BINDING);
    int prev_vao = get_int(GL_VERTEX_ARRAY_BINDING);
    _glGenVertexArrays(1, &s_quad_vao);
    _glBindVertexArray(s_quad_vao);
    _glGenBuffers(1, &s_quad_vbo);
    _glBindBuffer(GL_ARRAY_BUFFER, s_quad_vbo);
    _glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    _glEnableVertexAttribArray(0);
    _glVertexAttribPointer(0, 2, GL_FLOAT, false, 4 * sizeof(float), (void*)0);
    _glEnableVertexAttribArray(1);
    _glVertexAttribPointer(1, 2, GL_FLOAT, false, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    _glBindVertexArray((unsigned)prev_vao);
    _glBindBuffer(GL_ARRAY_BUFFER, (unsigned)prev_array_buf);
}

static void draw_fullscreen() {
    int prev_vao = get_int(GL_VERTEX_ARRAY_BINDING);
    _glBindVertexArray(s_quad_vao);
    _glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    _glBindVertexArray((unsigned)prev_vao);
}

bool glass_blur::init() {
    if (s_initialized) return true;
    if (!load_gl()) {
        fprintf(stderr, "[blur] failed to load GL functions\n");
        return false;
    }

    s_blur_program = create_program(k_quad_vs, k_blur_frag);
    if (!s_blur_program) {
        fprintf(stderr, "[blur] failed to create blur shader program\n");
        return false;
    }

    s_display_program = create_program(k_quad_vs, k_display_frag);
    if (!s_display_program) {
        fprintf(stderr, "[blur] failed to create display shader program\n");
        _glDeleteProgram(s_blur_program); s_blur_program = 0;
        return false;
    }

    setup_quad();
    if (!s_quad_vao || !s_quad_vbo) {
        fprintf(stderr, "[blur] failed to setup quad\n");
        _glDeleteProgram(s_blur_program); s_blur_program = 0;
        _glDeleteProgram(s_display_program); s_display_program = 0;
        return false;
    }
    s_initialized = true;
    fprintf(stderr, "[blur] initialized OK\n");
    return true;
}

void glass_blur::resize(int w, int h) {
    if (!s_initialized) return;
    int bw = w / BLUR_SCALE;
    int bh = h / BLUR_SCALE;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;
    if (bw == s_blur_tex_w && bh == s_blur_tex_h) return;

    if (s_src_tex) { _glDeleteTextures(1, &s_src_tex); s_src_tex = 0; }
    if (s_src_fbo) { _glDeleteFramebuffers(1, &s_src_fbo); s_src_fbo = 0; }
    if (s_ping_tex) { _glDeleteTextures(1, &s_ping_tex); s_ping_tex = 0; }
    if (s_ping_fbo) { _glDeleteFramebuffers(1, &s_ping_fbo); s_ping_fbo = 0; }
    if (s_pong_tex) { _glDeleteTextures(1, &s_pong_tex); s_pong_tex = 0; }
    if (s_pong_fbo) { _glDeleteFramebuffers(1, &s_pong_fbo); s_pong_fbo = 0; }

    s_width = w;
    s_height = h;
    s_blur_tex_w = bw;
    s_blur_tex_h = bh;

    s_src_tex = create_tex_fbo(w, h, &s_src_fbo);
    s_ping_tex = create_tex_fbo(bw, bh, &s_ping_fbo);
    s_pong_tex = create_tex_fbo(bw, bh, &s_pong_fbo);

    fprintf(stderr, "[blur] resized %dx%d -> blur %dx%d\n", w, h, bw, bh);
}

void glass_blur::capture_framebuffer(int width, int height) {
    if (!s_initialized) return;
    // Always resize if dimensions changed (handles window resize)
    if (width != s_width || height != s_height) resize(width, height);
    if (!s_src_fbo) return;

    // Save current viewport + scissor flag
    int saved_viewport[4];
    _glGetIntegerv(0x0BA2, saved_viewport); // GL_VIEWPORT
    unsigned saved_draw_fbo = (unsigned)get_int(0x8CA6); // GL_DRAW_FRAMEBUFFER_BINDING
    unsigned saved_read_fbo = (unsigned)get_int(GL_READ_FRAMEBUFFER_BINDING);
    int saved_scissor = _glIsEnabled(0x0C11) ? 1 : 0; // GL_SCISSOR_TEST

    // The capture blit must copy the whole frame: a game scissor box would
    // clip it (blits are scissored) and leave stale pixels in the blur source.
    _glDisable(0x0C11);

    // Downscale during capture: blit from default FB (full) into src_fbo (already at blur scale)
    _glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    _glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_src_fbo);
    _glBlitFramebuffer(0, 0, width, height, 0, 0, s_blur_tex_w, s_blur_tex_h, 0x00004000, GL_NEAREST);
    _glBindFramebuffer(GL_READ_FRAMEBUFFER, saved_read_fbo);
    _glBindFramebuffer(GL_DRAW_FRAMEBUFFER, saved_draw_fbo);

    // Restore viewport + scissor
    if (saved_scissor) _glEnable(0x0C11); else _glDisable(0x0C11);
    _glViewport(saved_viewport[0], saved_viewport[1], saved_viewport[2], saved_viewport[3]);
}

static void apply_blur_pass(unsigned src_tex, unsigned dst_fbo, int w, int h, float dx, float dy) {
    _glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    _glViewport(0, 0, w, h);
    _glDisable(GL_SCISSOR_TEST);

    _glUseProgram(s_blur_program);
    _glActiveTexture(GL_TEXTURE0);
    _glBindTexture(GL_TEXTURE_2D, src_tex);
    _glUniform1i(_glGetUniformLocation(s_blur_program, "uTexture"), 0);
    _glUniform2f(_glGetUniformLocation(s_blur_program, "uDirection"), dx, dy);
    _glUniform2f(_glGetUniformLocation(s_blur_program, "uTexelSize"), 1.0f / (float)w, 1.0f / (float)h);

    draw_fullscreen();
}

unsigned int glass_blur::blur(int passes) {
    if (!s_initialized || !s_src_fbo) return 0;
    if (!s_ping_fbo || !s_pong_fbo) return 0;

    int bw = s_blur_tex_w;
    int bh = s_blur_tex_h;

    // Save GL state
    int saved_viewport[4];
    _glGetIntegerv(0x0BA2, saved_viewport); // GL_VIEWPORT
    unsigned saved_program = 0;
    _glGetIntegerv(0x8B8D, (int*)&saved_program); // GL_CURRENT_PROGRAM
    unsigned saved_fbo = 0;
    _glGetIntegerv(0x8CA6, (int*)&saved_fbo); // GL_FRAMEBUFFER_BINDING
    unsigned saved_read_fbo = (unsigned)get_int(GL_READ_FRAMEBUFFER_BINDING);
    int saved_scissor = _glIsEnabled(0x0C11) ? 1 : 0; // GL_SCISSOR_TEST
    TexState tex_st = save_tex_state();

    // Our blits and fullscreen draws must cover the whole target: scissor is a
    // clip for the game, not for us — disable it here, put it back at the end.
    _glDisable(0x0C11);

    // Downscale source into ping
    _glBindFramebuffer(GL_READ_FRAMEBUFFER, s_src_fbo);
    _glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_ping_fbo);
    _glBlitFramebuffer(0, 0, s_blur_tex_w, s_blur_tex_h, 0, 0, bw, bh, 0x00004000, GL_NEAREST);
    _glBindFramebuffer(GL_READ_FRAMEBUFFER, saved_read_fbo);
    _glBindFramebuffer(GL_DRAW_FRAMEBUFFER, saved_fbo);

    // Multi-pass Gaussian blur (ping-pong)
    for (int i = 0; i < passes; i++) {
        apply_blur_pass(s_ping_tex, s_pong_fbo, bw, bh, 1.0f, 0.0f);
        apply_blur_pass(s_pong_tex, s_ping_fbo, bw, bh, 0.0f, 1.0f);
    }

    // Restore GL state
    _glBindFramebuffer(GL_READ_FRAMEBUFFER, saved_read_fbo);
    _glBindFramebuffer(GL_DRAW_FRAMEBUFFER, saved_fbo);
    _glViewport(saved_viewport[0], saved_viewport[1], saved_viewport[2], saved_viewport[3]);
    _glUseProgram(saved_program);
    if (saved_scissor) _glEnable(0x0C11); else _glDisable(0x0C11); // GL_SCISSOR_TEST
    restore_tex_state(tex_st);

    return s_ping_tex;
}

unsigned int glass_blur::get_blurred_texture() {
    return s_ping_tex;
}

void glass_blur::draw_blur_background() {
    if (!s_initialized || !s_ping_tex || !s_display_program) return;
    TexState tex_st = save_tex_state();
    unsigned prev_program = (unsigned)get_int(0x8B8D); // GL_CURRENT_PROGRAM
    int prev_vao = get_int(GL_VERTEX_ARRAY_BINDING);
    int prev_depth = _glIsEnabled(0x0B71) ? 1 : 0; // GL_DEPTH_TEST (glIsEnabled only)

    _glDisable(GL_DEPTH_TEST);
    _glUseProgram(s_display_program);
    _glActiveTexture(GL_TEXTURE0);
    _glBindTexture(GL_TEXTURE_2D, s_ping_tex);
    _glUniform1i(_glGetUniformLocation(s_display_program, "uTexture"), 0);
    _glBindVertexArray(s_quad_vao);
    _glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    _glBindVertexArray((unsigned)prev_vao);
    _glUseProgram(prev_program);
    if (prev_depth) _glEnable(0x0B71); else _glDisable(0x0B71);
    restore_tex_state(tex_st);
}

void glass_blur::shutdown() {
    if (s_src_tex) { _glDeleteTextures(1, &s_src_tex); s_src_tex = 0; }
    if (s_src_fbo) { _glDeleteFramebuffers(1, &s_src_fbo); s_src_fbo = 0; }
    if (s_ping_tex) { _glDeleteTextures(1, &s_ping_tex); s_ping_tex = 0; }
    if (s_ping_fbo) { _glDeleteFramebuffers(1, &s_ping_fbo); s_ping_fbo = 0; }
    if (s_pong_tex) { _glDeleteTextures(1, &s_pong_tex); s_pong_tex = 0; }
    if (s_pong_fbo) { _glDeleteFramebuffers(1, &s_pong_fbo); s_pong_fbo = 0; }
    if (s_blur_program) { _glDeleteProgram(s_blur_program); s_blur_program = 0; }
    if (s_display_program) { _glDeleteProgram(s_display_program); s_display_program = 0; }
    if (s_quad_vao) { _glDeleteVertexArrays(1, &s_quad_vao); s_quad_vao = 0; }
    if (s_quad_vbo) { _glDeleteBuffers(1, &s_quad_vbo); s_quad_vbo = 0; }
    s_initialized = false;
    s_blur_tex_w = s_blur_tex_h = 0;
}
