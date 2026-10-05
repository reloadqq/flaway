#include "hud_internal.h"
#include "../glass_blur.h"
#include "../../utils/rlog.h"

#include <dlfcn.h>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// GPU sheen: an animated caustic overlay (analytic sin/cos caustics, same
// family as the roky sky_caustic shader) rendered inside HUD glass cards.
//
// How it works: the HUD queues one request per card during the ImGui build
// pass; each request becomes an ImDrawList user callback. The callback runs
// inside ImGui_ImplOpenGL3_RenderDrawData, so it only has to set up its own
// program/scissor and draw one quad — it puts the backend's program/VAO back
// itself, which is why no ImDrawCallback_ResetRenderState is needed after it
// (that callback used to re-run the whole state setup once per card).
// ---------------------------------------------------------------------------
namespace hud_fx {
namespace {

// --- GL entry points (resolved with dlsym, same approach as glass_blur) ----
typedef unsigned (*PFN_CREATEPROGRAM)();
typedef void (*PFN_DELETEPROGRAM)(unsigned);
typedef unsigned (*PFN_CREATESHADER)(unsigned);
typedef void (*PFN_DELETESHADER)(unsigned);
typedef void (*PFN_SHADERSOURCE)(unsigned, int, const char**, const int*);
typedef void (*PFN_COMPILESHADER)(unsigned);
typedef void (*PFN_GETSHADERIV)(unsigned, unsigned, int*);
typedef void (*PFN_GETSHADERINFOLOG)(unsigned, int, int*, char*);
typedef void (*PFN_ATTACHSHADER)(unsigned, unsigned);
typedef void (*PFN_LINKPROGRAM)(unsigned);
typedef void (*PFN_BINDATTRIBLOCATION)(unsigned, unsigned, const char*);
typedef void (*PFN_GETPROGRAMIV)(unsigned, unsigned, int*);
typedef void (*PFN_GETPROGRAMINFOLOG)(unsigned, int, int*, char*);
typedef void (*PFN_USEPROGRAM)(unsigned);
typedef int (*PFN_GETUNIFORMLOCATION)(unsigned, const char*);
typedef void (*PFN_UNIFORM1F)(int, float);
typedef void (*PFN_UNIFORM3F)(int, float, float, float);
typedef void (*PFN_UNIFORM4F)(int, float, float, float, float);
typedef void (*PFN_UNIFORMMATRIX4FV)(int, int, unsigned char, const float*);
typedef void (*PFN_GENVERTEXARRAYS)(int, unsigned*);
typedef void (*PFN_BINDVERTEXARRAY)(unsigned);
typedef void (*PFN_DELETEVERTEXARRAYS)(int, const unsigned*);
typedef void (*PFN_GENBUFFERS)(int, unsigned*);
typedef void (*PFN_BINDBUFFER)(unsigned, unsigned);
typedef void (*PFN_BUFFERDATA)(unsigned, long long, const void*, unsigned);
typedef void (*PFN_DELETEBUFFERS)(int, const unsigned*);
typedef void (*PFN_ENABLEVERTEXATTRIBARRAY)(unsigned);
typedef void (*PFN_VERTEXATTRIBPOINTER)(unsigned, int, unsigned, unsigned char, int, const void*);
typedef void (*PFN_DRAWARRAYS)(unsigned, int, int);
typedef void (*PFN_GETINTEGERV)(unsigned, int*);
typedef void (*PFN_SCISSOR)(int, int, int, int);
typedef void (*PFN_ENABLE)(unsigned);
typedef void (*PFN_DISABLE)(unsigned);
typedef unsigned char (*PFN_ISENABLED)(unsigned);

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
PFN_BINDATTRIBLOCATION pBindAttribLocation = nullptr;
PFN_GETPROGRAMIV pGetProgramiv = nullptr;
PFN_GETPROGRAMINFOLOG pGetProgramInfoLog = nullptr;
PFN_USEPROGRAM pUseProgram = nullptr;
PFN_GETUNIFORMLOCATION pGetUniformLocation = nullptr;
PFN_UNIFORM1F pUniform1f = nullptr;
PFN_UNIFORM3F pUniform3f = nullptr;
PFN_UNIFORM4F pUniform4f = nullptr;
PFN_UNIFORMMATRIX4FV pUniformMatrix4fv = nullptr;
PFN_GENVERTEXARRAYS pGenVertexArrays = nullptr;
PFN_BINDVERTEXARRAY pBindVertexArray = nullptr;
PFN_DELETEVERTEXARRAYS pDeleteVertexArrays = nullptr;
PFN_GENBUFFERS pGenBuffers = nullptr;
PFN_BINDBUFFER pBindBuffer = nullptr;
PFN_BUFFERDATA pBufferData = nullptr;
PFN_DELETEBUFFERS pDeleteBuffers = nullptr;
PFN_ENABLEVERTEXATTRIBARRAY pEnableVertexAttribArray = nullptr;
PFN_VERTEXATTRIBPOINTER pVertexAttribPointer = nullptr;
PFN_DRAWARRAYS pDrawArrays = nullptr;
PFN_GETINTEGERV pGetIntegerv = nullptr;
PFN_SCISSOR pScissor = nullptr;
PFN_ENABLE pEnable = nullptr;
PFN_DISABLE pDisable = nullptr;
PFN_ISENABLED pIsEnabled = nullptr;

const unsigned GL_ARRAY_BUFFER_ = 0x8892;
const unsigned GL_STATIC_DRAW_ = 0x88E4;
const unsigned GL_VERTEX_SHADER_ = 0x8B31;
const unsigned GL_FRAGMENT_SHADER_ = 0x8B30;
const unsigned GL_COMPILE_STATUS_ = 0x8B81;
const unsigned GL_LINK_STATUS_ = 0x8B82;
const unsigned GL_SCISSOR_TEST_ = 0x0C11;
const unsigned GL_TRIANGLE_STRIP_ = 0x0005;
const unsigned GL_ARRAY_BUFFER_BINDING_ = 0x8894;
const unsigned GL_FLOAT_ = 0x1406;

bool load_gl() {
    static bool tried = false;
    if (tried) return pCreateProgram != nullptr;
    tried = true;
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
    pBindAttribLocation = (PFN_BINDATTRIBLOCATION)dlsym(RTLD_DEFAULT, "glBindAttribLocation");
    pGetProgramiv = (PFN_GETPROGRAMIV)dlsym(RTLD_DEFAULT, "glGetProgramiv");
    pGetProgramInfoLog = (PFN_GETPROGRAMINFOLOG)dlsym(RTLD_DEFAULT, "glGetProgramInfoLog");
    pUseProgram = (PFN_USEPROGRAM)dlsym(RTLD_DEFAULT, "glUseProgram");
    pGetUniformLocation = (PFN_GETUNIFORMLOCATION)dlsym(RTLD_DEFAULT, "glGetUniformLocation");
    pUniform1f = (PFN_UNIFORM1F)dlsym(RTLD_DEFAULT, "glUniform1f");
    pUniform3f = (PFN_UNIFORM3F)dlsym(RTLD_DEFAULT, "glUniform3f");
    pUniform4f = (PFN_UNIFORM4F)dlsym(RTLD_DEFAULT, "glUniform4f");
    pUniformMatrix4fv = (PFN_UNIFORMMATRIX4FV)dlsym(RTLD_DEFAULT, "glUniformMatrix4fv");
    pGenVertexArrays = (PFN_GENVERTEXARRAYS)dlsym(RTLD_DEFAULT, "glGenVertexArrays");
    pBindVertexArray = (PFN_BINDVERTEXARRAY)dlsym(RTLD_DEFAULT, "glBindVertexArray");
    pDeleteVertexArrays = (PFN_DELETEVERTEXARRAYS)dlsym(RTLD_DEFAULT, "glDeleteVertexArrays");
    pGenBuffers = (PFN_GENBUFFERS)dlsym(RTLD_DEFAULT, "glGenBuffers");
    pBindBuffer = (PFN_BINDBUFFER)dlsym(RTLD_DEFAULT, "glBindBuffer");
    pBufferData = (PFN_BUFFERDATA)dlsym(RTLD_DEFAULT, "glBufferData");
    pDeleteBuffers = (PFN_DELETEBUFFERS)dlsym(RTLD_DEFAULT, "glDeleteBuffers");
    pEnableVertexAttribArray = (PFN_ENABLEVERTEXATTRIBARRAY)dlsym(RTLD_DEFAULT, "glEnableVertexAttribArray");
    pVertexAttribPointer = (PFN_VERTEXATTRIBPOINTER)dlsym(RTLD_DEFAULT, "glVertexAttribPointer");
    pDrawArrays = (PFN_DRAWARRAYS)dlsym(RTLD_DEFAULT, "glDrawArrays");
    pGetIntegerv = (PFN_GETINTEGERV)dlsym(RTLD_DEFAULT, "glGetIntegerv");
    pScissor = (PFN_SCISSOR)dlsym(RTLD_DEFAULT, "glScissor");
    pEnable = (PFN_ENABLE)dlsym(RTLD_DEFAULT, "glEnable");
    pDisable = (PFN_DISABLE)dlsym(RTLD_DEFAULT, "glDisable");
    pIsEnabled = (PFN_ISENABLED)dlsym(RTLD_DEFAULT, "glIsEnabled");
    return pCreateProgram && pCreateShader && pShaderSource && pCompileShader &&
           pGetShaderiv && pAttachShader && pLinkProgram && pGetProgramiv &&
           pUseProgram && pGetUniformLocation && pBindAttribLocation && pUniform4f && pUniformMatrix4fv &&
           pGenVertexArrays && pBindVertexArray && pGenBuffers && pBindBuffer &&
           pBufferData && pEnableVertexAttribArray && pVertexAttribPointer &&
           pDrawArrays && pGetIntegerv && pScissor && pEnable && pDisable && pIsEnabled;
}

const char* k_vs = R"(
#version 150
in vec2 aPos;
uniform mat4 uProj;
uniform vec4 uRect;
out vec2 vPix;
void main() {
    vPix = mix(uRect.xy, uRect.zw, aPos); // card rect in ImGui pixel space
    gl_Position = uProj * vec4(vPix, 0.0, 1.0);
}
)";

// Analytic caustics (5 -> 4 sin/cos domain-warp iterations) + rounded-rect
// SDF mask, so the sheen stays strictly inside the card border.
// uMode picks the effect: 0 caustics, 1 sweeping light streak, 2 edge glow.
const char* k_fs = R"(
#version 150
in vec2 vPix;
out vec4 FragColor;
uniform vec4 uRect;
uniform float uRadius;
uniform float uTime;
uniform float uAlpha;
uniform float uPhase;
uniform float uMode;
uniform vec3 uAccent;

float caustic(vec2 uv, float t) {
    vec2 p = mod(uv * 6.28318530718, 6.28318530718) - 250.0;
    vec2 i = p;
    float c = 1.0;
    float inten = 0.0052;
    for (int n = 0; n < 4; n++) {
        float tn = t * 0.5 + float(n);
        i = p + vec2(cos(tn - i.x) + sin(tn + i.y),
                     sin(tn - i.y) + cos(tn + i.x));
        c += 1.0 / length(vec2(p.x / (sin(i.x + tn) / inten),
                               p.y / (cos(i.y + tn) / inten)));
    }
    c /= 4.0;
    c = 1.17 - pow(c, 1.4);
    return clamp(pow(abs(c), 8.0), 0.0, 1.5);
}

float sd_round(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec2 c = (uRect.xy + uRect.zw) * 0.5;
    vec2 b = (uRect.zw - uRect.xy) * 0.5;
    vec2 p = vPix - c;
    float d = sd_round(p, b, uRadius);
    float mask = 1.0 - smoothstep(-1.0, 1.0, d);
    if (mask <= 0.003) discard;

    float t = uTime * 0.55 + uPhase;
    vec3 col;
    float a;

    if (uMode < 0.5) {
        // caustic sheen (original effect)
        vec2 uv = p / max(b.x, 12.0) * 0.72 + vec2(uPhase * 0.41, uPhase * 0.17);
        float v = caustic(uv, t);
        v += caustic(uv * 1.65 + vec2(4.3, 7.1), t * 0.82 + 2.0) * 0.55;
        v = clamp(v, 0.0, 1.5);
        vec3 hot = mix(uAccent, vec3(1.0), 0.60);
        col = mix(uAccent * 0.55, hot, clamp(v * 0.85, 0.0, 1.0));
        a = clamp(v * uAlpha, 0.0, 1.0) * mask;
    } else if (uMode < 1.5) {
        // light sweep: one soft diagonal streak travelling across the card
        vec2 sz = max(uRect.zw - uRect.xy, vec2(1.0));
        float axis = (vPix.x - uRect.x) / sz.x * 0.68 +
                     (vPix.y - uRect.y) / sz.y * 0.32;
        float pos = fract(uTime * 0.20 + uPhase * 0.17) * 1.9 - 0.45;
        float dd = axis - pos;
        float band = exp(-dd * dd * 26.0);
        // kill the streak at both ends so it enters and leaves cleanly
        band *= smoothstep(0.0, 0.12, pos) * (1.0 - smoothstep(1.3, 1.45, pos));
        vec3 hot = mix(uAccent, vec3(1.0), 0.70);
        col = hot;
        a = band * uAlpha * mask;
    } else {
        // edge glow: accent border light breathing in and out
        float e = smoothstep(-7.0, 0.0, d);
        float pulse = 0.55 + 0.45 * sin(uTime * 2.1 + uPhase);
        vec3 hot = mix(uAccent, vec3(1.0), 0.35);
        col = hot;
        a = e * pulse * uAlpha * mask;
    }

    FragColor = vec4(col, a);
}
)";

unsigned s_prog = 0;
int u_proj = -1, u_rect = -1, u_radius = -1, u_time = -1, u_alpha = -1, u_phase = -1,
    u_accent = -1, u_mode = -1;
unsigned s_vao = 0, s_vbo = 0;
bool s_tried = false;
bool s_ok = false;
// Program/VAO/VBO the backend had bound when the pass started. They do not
// change between our callbacks, so they are read once per frame instead of
// per card (3 glGetIntegerv x every plate x every frame was pure waste).
int s_saved_prog = 0, s_saved_vao = 0, s_saved_vbo = 0;
bool s_state_cached = false;

unsigned compile(unsigned type, const char* src) {
    unsigned s = pCreateShader(type);
    pShaderSource(s, 1, &src, nullptr);
    pCompileShader(s);
    int ok = 0;
    pGetShaderiv(s, GL_COMPILE_STATUS_, &ok);
    if (!ok) {
        char log[512] = {};
        pGetShaderInfoLog(s, sizeof(log), nullptr, log);
        rlog::logf("hud_fx: shader compile error: %s", log);
        pDeleteShader(s);
        return 0;
    }
    return s;
}

// One quad in [0,1]^2 — the vertex shader maps it onto the card rect.
const float k_quad[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };

bool create_pipeline() {
    // Setup runs during the ImGui *build* pass, i.e. outside ImGui's own
    // GL-state backup inside RenderDrawData, so anything we touch here leaks
    // straight into the game's context. Minecraft/Sodium caches its VAO and
    // GL_ARRAY_BUFFER bindings: leaving them at 0 makes the next world draw
    // run with VAO 0 (illegal in a core profile) -> the whole scene renders
    // black. Save both and put them back exactly as found.
    int last_vao = 0, last_vbo = 0;
    pGetIntegerv(0x85B5, &last_vao);            // GL_VERTEX_ARRAY_BINDING
    pGetIntegerv(GL_ARRAY_BUFFER_BINDING_, &last_vbo);
    unsigned vs = compile(GL_VERTEX_SHADER_, k_vs);
    unsigned fs = compile(GL_FRAGMENT_SHADER_, k_fs);
    if (!vs || !fs) {
        if (vs) pDeleteShader(vs);
        if (fs) pDeleteShader(fs);
        return false;
    }
    unsigned prog = pCreateProgram();
    pAttachShader(prog, vs);
    pAttachShader(prog, fs);
    pBindAttribLocation(prog, 0, "aPos"); // deterministic location for our VAO
    pLinkProgram(prog);
    int ok = 0;
    pGetProgramiv(prog, GL_LINK_STATUS_, &ok);
    pDeleteShader(vs);
    pDeleteShader(fs);
    if (!ok) {
        char log[512] = {};
        pGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        rlog::logf("hud_fx: program link error: %s", log);
        pDeleteProgram(prog);
        return false;
    }
    s_prog = prog;
    u_proj = pGetUniformLocation(prog, "uProj");
    u_rect = pGetUniformLocation(prog, "uRect");
    u_radius = pGetUniformLocation(prog, "uRadius");
    u_time = pGetUniformLocation(prog, "uTime");
    u_alpha = pGetUniformLocation(prog, "uAlpha");
    u_phase = pGetUniformLocation(prog, "uPhase");
    u_accent = pGetUniformLocation(prog, "uAccent");
    u_mode = pGetUniformLocation(prog, "uMode");

    pGenVertexArrays(1, &s_vao);
    pBindVertexArray(s_vao);
    pGenBuffers(1, &s_vbo);
    pBindBuffer(GL_ARRAY_BUFFER_, s_vbo);
    pBufferData(GL_ARRAY_BUFFER_, sizeof(k_quad), k_quad, GL_STATIC_DRAW_);
    pEnableVertexAttribArray(0);
    pVertexAttribPointer(0, 2, GL_FLOAT_, 0, 2 * sizeof(float), (const void*)0);
    pBindVertexArray((unsigned)last_vao);
    pBindBuffer(GL_ARRAY_BUFFER_, (unsigned)last_vbo);
    return s_vao != 0 && s_vbo != 0;
}

// --- per-frame request queue ------------------------------------------------
struct Req {
    float x0, y0, x1, y1;
    float radius, alpha, time, phase;
    float ar, ag, ab;
    int mode;
};

// 64 was not enough once several effects can be queued per element (array list
// alone can be 24 rows on top of watermark/coords chips). 256 also covers the
// ESP (one sweep per name/item plate on top of the HUD).
constexpr int k_max_req = 256;
Req s_reqs[k_max_req];
int s_req_n = 0;
int s_req_frame = -999999;

void fx_callback(const ImDrawList*, const ImDrawCmd* cmd) {
    int idx = (int)(intptr_t)cmd->UserCallbackData - 1;
    if (idx < 0 || idx >= s_req_n || !s_prog) return;
    const Req& r = s_reqs[idx];
    if (r.alpha <= 0.002f || r.x1 <= r.x0 || r.y1 <= r.y0) return;

    // ImDrawData is valid after ImGui::Render() and until the next NewFrame(),
    // i.e. for the whole backend pass we are running inside.
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd) return;
    const ImVec2 dpos = dd->DisplayPos;
    const ImVec2 dsize = dd->DisplaySize;
    const float scale_x = dd->FramebufferScale.x > 0.0f ? dd->FramebufferScale.x : 1.0f;
    const float scale_y = dd->FramebufferScale.y > 0.0f ? dd->FramebufferScale.y : 1.0f;
    const float fb_h = dsize.y * scale_y;
    if (fb_h <= 1.0f) return;

    // Scissor: card rect intersected with the current ImGui clip rect, in
    // framebuffer pixels with the Y axis flipped (GL origin is bottom-left).
    float cx0 = cmd->ClipRect.x, cy0 = cmd->ClipRect.y;
    float cx1 = cmd->ClipRect.z, cy1 = cmd->ClipRect.w;
    float sx0 = r.x0 > cx0 ? r.x0 : cx0;
    float sy0 = r.y0 > cy0 ? r.y0 : cy0;
    float sx1 = r.x1 < cx1 ? r.x1 : cx1;
    float sy1 = r.y1 < cy1 ? r.y1 : cy1;
    if (sx1 <= sx0 || sy1 <= sy0) return;

    int sc_x = (int)(sx0 * scale_x);
    int sc_w = (int)(sx1 * scale_x) - sc_x;
    int sc_y = (int)(fb_h - sy1 * scale_y);
    int sc_h = (int)(fb_h - sy0 * scale_y) - sc_y;
    if (sc_w <= 0 || sc_h <= 0) return;

    // Same orthographic projection the backend builds from draw_data.
    const float L = dpos.x;
    const float R = dpos.x + dsize.x;
    const float T = dpos.y;
    const float B = dpos.y + dsize.y;
    const float proj[16] = {
        2.0f / (R - L), 0.0f,          0.0f, 0.0f,
        0.0f,           2.0f / (T - B), 0.0f, 0.0f,
        0.0f,           0.0f,          -1.0f, 0.0f,
        (R + L) / (L - R), (T + B) / (B - T), 0.0f, 1.0f,
    };

    // The backend re-issues glScissor for every command it draws and restores
    // the game's box/enable at the end of the pass, so the clip state needs no
    // save/restore here — only the program/VAO/VBO we are about to rebind do.
    if (!s_state_cached) {
        pGetIntegerv(0x8B8D, &s_saved_prog);   // GL_CURRENT_PROGRAM
        pGetIntegerv(0x85B5, &s_saved_vao);    // GL_VERTEX_ARRAY_BINDING
        pGetIntegerv(GL_ARRAY_BUFFER_BINDING_, &s_saved_vbo);
        s_state_cached = true;
    }

    pEnable(GL_SCISSOR_TEST_);
    pScissor(sc_x, sc_y, sc_w, sc_h);

    pUseProgram(s_prog);
    pUniformMatrix4fv(u_proj, 1, 0, proj);
    pUniform4f(u_rect, r.x0, r.y0, r.x1, r.y1);
    pUniform1f(u_radius, r.radius);
    pUniform1f(u_time, r.time);
    pUniform1f(u_alpha, r.alpha);
    pUniform1f(u_phase, r.phase);
    pUniform1f(u_mode, (float)r.mode);
    pUniform3f(u_accent, r.ar, r.ag, r.ab);
    pBindVertexArray(s_vao);
    pDrawArrays(GL_TRIANGLE_STRIP_, 0, 4);
    // Hand the renderer back exactly as we found it; the backend continues with
    // the next command on its own program/VAO, so no ResetRenderState callback
    // is needed (that callback re-ran the full ~15-call state setup per card).
    pBindVertexArray((unsigned)s_saved_vao);
    pBindBuffer(GL_ARRAY_BUFFER_, (unsigned)s_saved_vbo);
    pUseProgram((unsigned)s_saved_prog);
}

bool ensure() {
    if (s_tried) return s_ok;
    s_tried = true;
    if (!load_gl()) {
        rlog::logf("hud_fx: failed to load GL functions");
        return false;
    }
    s_ok = create_pipeline();
    if (s_ok) rlog::logf("hud_fx: fx pipeline ready (caustic / sweep / edge)");
    return s_ok;
}

} // namespace

void begin_frame() {
    // Frame-guarded: the queue may be filled by several modules in one frame
    // (ESP first, HUD/menu later) — only the first call of a frame resets it,
    // so every request queued this frame stays valid until RenderDrawData.
    int frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
    if (frame == s_req_frame) return;
    s_req_frame = frame;
    s_req_n = 0;
    s_state_cached = false; // the backend rebinds its program/VAO each frame
}

void fx(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float rounding,
        float alpha, float phase, int mode) {
    begin_frame();  // drop leftovers from the previous frame before queueing
    if (!dl || alpha <= 0.002f) return;
    if (p1.x - p0.x < 2.0f || p1.y - p0.y < 2.0f) return;
    if (!ensure()) return;
    if (s_req_n >= k_max_req) return;

    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x < 32.0f || io.DisplaySize.y < 32.0f) return;

    ImU32 ac = hstyle::accent();
    Req& r = s_reqs[s_req_n];
    r.x0 = p0.x; r.y0 = p0.y; r.x1 = p1.x; r.y1 = p1.y;
    r.radius = rounding < 0.0f ? 0.0f : rounding;
    r.alpha = alpha;
    r.time = (float)ImGui::GetTime();
    r.phase = phase;
    r.mode = mode;
    r.ar = (float)(ac & 255) / 255.0f;
    r.ag = (float)((ac >> 8) & 255) / 255.0f;
    r.ab = (float)((ac >> 16) & 255) / 255.0f;

    dl->AddCallback(fx_callback, (void*)(intptr_t)(s_req_n + 1));
    // No ImDrawCallback_ResetRenderState after it: the callback restores the
    // program/VAO/VBO itself and the backend re-issues glScissor per command,
    // so re-running the full state setup per card was only burning frames.
    s_req_n++;
}

void sheen(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float rounding,
           float alpha, float phase) {
    fx(dl, p0, p1, rounding, alpha, phase, FX_CAUSTIC);
}

void shutdown() {
    if (s_prog && pDeleteProgram) pDeleteProgram(s_prog);
    s_prog = 0;
    if (s_vbo && pDeleteBuffers) pDeleteBuffers(1, &s_vbo);
    s_vbo = 0;
    if (s_vao && pDeleteVertexArrays) pDeleteVertexArrays(1, &s_vao);
    s_vao = 0;
    s_tried = false;
    s_ok = false;
    s_req_n = 0;
    s_req_frame = -999999;
    s_state_cached = false;
}

} // namespace hud_fx
