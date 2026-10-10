#include "hud_internal.h"
#include "../../utils/rlog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <zlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../data/stb_image.h"

// ---------------------------------------------------------------------------
// Mini texture loader for the HUD: reads vanilla item PNGs straight out of
// the version jar (own ZIP reader + zlib inflate) and player skins out of the
// asset cache, then uploads them as NEAREST-filtered GL textures.
// ---------------------------------------------------------------------------
namespace hud_icons {
namespace {

// GL loader -----------------------------------------------------------------
typedef void (*PFN_GENTEX)(int, unsigned*);
typedef void (*PFN_BINDTEX)(unsigned, unsigned);
typedef void (*PFN_TEXPARAM)(unsigned, unsigned, int);
typedef void (*PFN_TEXIMAGE)(unsigned, int, int, int, int, int, unsigned, unsigned, const void*);
typedef void (*PFN_DELTEX)(int, const unsigned*);
typedef void (*PFN_PIXELSTORE)(unsigned, int);
typedef void (*PFN_GETINTEGERV)(unsigned, int*);
typedef void (*PFN_BINDBUFFER)(unsigned, unsigned);
typedef unsigned (*PFN_GETERROR)(void);

PFN_GENTEX pGenTextures = nullptr;
PFN_BINDTEX pBindTexture = nullptr;
PFN_TEXPARAM pTexParameteri = nullptr;
PFN_TEXIMAGE pTexImage2D = nullptr;
PFN_DELTEX pDeleteTextures = nullptr;
PFN_PIXELSTORE pPixelStorei = nullptr;
PFN_GETINTEGERV pGetIntegerv = nullptr;
// Optional: only needed to unbind a stale pixel-unpack buffer (see
// upload_rgba). Missing symbols must never fail the loader.
PFN_BINDBUFFER pBindBuffer = nullptr;

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
    pBindBuffer = (PFN_BINDBUFFER)dlsym(RTLD_DEFAULT, "glBindBuffer"); // optional
    gl_ok = pGenTextures && pBindTexture && pTexParameteri && pTexImage2D &&
            pDeleteTextures && pPixelStorei && pGetIntegerv;
    if (!gl_ok) rlog::logf("hud_icons: failed to resolve GL texture functions");
    return gl_ok;
}

// GL constants (avoid pulling GL headers into this TU).
constexpr unsigned kTex2D = 0x0DE1;
constexpr unsigned kRGBA = 0x1908;
constexpr unsigned kUnsignedByte = 0x1401;
constexpr unsigned kMinFilter = 0x2801;
constexpr unsigned kMagFilter = 0x2800;
constexpr unsigned kNearest = 0x2600;
constexpr unsigned kLinear = 0x2601;
constexpr unsigned kWrapS = 0x2802;
constexpr unsigned kWrapT = 0x2803;
constexpr unsigned kClampToEdge = 0x812F;
constexpr unsigned kUnpackAlignment = 0x0CF5;
constexpr unsigned kUnpackRowLength = 0x0CF2;
constexpr unsigned kUnpackSkipRows = 0x0CF3;
constexpr unsigned kUnpackSkipPixels = 0x0CF4;
constexpr unsigned kTexture2D = 0x0DE1;
constexpr unsigned kTextureBinding2D = 0x806A;
constexpr unsigned kPixelUnpackBuffer = 0x88EB;      // GL_PIXEL_UNPACK_BUFFER
constexpr unsigned kUnpackBufferBinding = 0x88EC;    // ..._BINDING

unsigned upload_rgba(const unsigned char* pixels, int w, int h) {
    if (!pixels || w <= 0 || h <= 0 || !load_gl()) return 0;
    // The game leaves GL_UNPACK_ROW_LENGTH / SKIP_ROWS / SKIP_PIXELS set after
    // its own atlas uploads (observed: row_length=18, skip_rows=54). Left alone
    // they shift the source pointer and the icon comes out as scrambled rows —
    // zero them for our upload and put every value back exactly as found.
    // Same for the pixel-store alignment and the binding of the active unit.
    int saved[6] = {0, 0, 0, 4, 0, 0};
    pGetIntegerv(kUnpackRowLength, &saved[0]);
    pGetIntegerv(kUnpackSkipPixels, &saved[1]);
    pGetIntegerv(kUnpackSkipRows, &saved[2]);
    pGetIntegerv(kUnpackAlignment, &saved[3]);
    pGetIntegerv(kTextureBinding2D, &saved[4]);
    pGetIntegerv(kUnpackBufferBinding, &saved[5]);
    // Mesa/Intel returns GL_INVALID_ENUM for GL_PIXEL_UNPACK_BUFFER_BINDING —
    // clear the error so the game's GL debug callback doesn't spam the log.
    {
        static PFN_GETERROR pGetError = nullptr;
        static bool s_ge_tried = false;
        if (!s_ge_tried)
        {
            s_ge_tried = true;
            pGetError = (PFN_GETERROR)dlsym(RTLD_DEFAULT, "glGetError");
        }
        if (pGetError) while (pGetError() != 0) {}
    }
    unsigned tex = 0;
    pGenTextures(1, &tex);
    if (!tex) return 0;
    pBindTexture(kTexture2D, tex);
    pPixelStorei(kUnpackRowLength, 0);
    pPixelStorei(kUnpackSkipPixels, 0);
    pPixelStorei(kUnpackSkipRows, 0);
    pPixelStorei(kUnpackAlignment, 1);
    // Minification (16px -> ~15px GUI size) gets LINEAR so a fractional scale
    // does not drop/alias texels; magnification stays NEAREST for crisp pixels.
    pTexParameteri(kTexture2D, kMinFilter, (int)kLinear);
    pTexParameteri(kTexture2D, kMagFilter, (int)kNearest);
    pTexParameteri(kTexture2D, kWrapS, (int)kClampToEdge);
    pTexParameteri(kTexture2D, kWrapT, (int)kClampToEdge);
    // A pixel-unpack buffer left bound by the game would make glTexImage2D
    // read from offset 0 of that PBO instead of our pixels (garbage texture).
    if (saved[5] && pBindBuffer) pBindBuffer(kPixelUnpackBuffer, 0);
    pTexImage2D(kTexture2D, 0, (int)kRGBA, w, h, 0, kRGBA, kUnsignedByte, pixels);
    if (saved[5] && pBindBuffer) pBindBuffer(kPixelUnpackBuffer, (unsigned)saved[5]);
    pPixelStorei(kUnpackRowLength, saved[0]);
    pPixelStorei(kUnpackSkipPixels, saved[1]);
    pPixelStorei(kUnpackSkipRows, saved[2]);
    pPixelStorei(kUnpackAlignment, saved[3]);
    pBindTexture(kTexture2D, (unsigned)saved[4]);
    return tex;
}

unsigned decode_png(const unsigned char* data, size_t len) {
    if (!data || len < 8) return 0;
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(data, (int)len, &w, &h, &comp, 4);
    if (!px) return 0;
    unsigned tex = upload_rgba(px, w, h);
    stbi_image_free(px);
    return tex;
}

// ZIP reader ----------------------------------------------------------------
struct ZipEntry {
    uint32_t local_off = 0, comp = 0, uncomp = 0;
    uint16_t method = 0;
    std::string owner;         // jar this entry was indexed from
};

inline uint16_t rd16(const char* p) { return (uint16_t)((unsigned char)p[0] | ((unsigned char)p[1] << 8)); }
inline uint32_t rd32(const char* p) {
    return (uint32_t)((unsigned char)p[0] | ((unsigned char)p[1] << 8) |
                      ((unsigned char)p[2] << 16) | ((unsigned char)p[3] << 24));
}

std::vector<std::string> g_jars;
bool g_jars_scanned = false;
std::map<std::string, ZipEntry> g_index;      // "assets/minecraft/textures/..." -> entry
std::set<std::string> g_indexed_jars;

void scan_jars() {
    g_jars_scanned = true;
    const char* home = getenv("HOME");
    if (!home) return;
    std::string root = std::string(home) + "/.minecraft/versions";
    DIR* d = opendir(root.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string sub = root + "/" + e->d_name;
        DIR* d2 = opendir(sub.c_str());
        if (!d2) continue;
        while (dirent* e2 = readdir(d2)) {
            size_t n = strlen(e2->d_name);
            if (n > 4 && strcmp(e2->d_name + n - 4, ".jar") == 0)
                g_jars.push_back(sub + "/" + e2->d_name);
        }
        closedir(d2);
    }
    closedir(d);
    std::sort(g_jars.begin(), g_jars.end());
    rlog::logf("hud_icons: found %d version jars", (int)g_jars.size());
}

bool index_jar(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 22) { fclose(f); return false; }

    // End-of-central-directory lives in the last 64KB (comment can be long).
    long scan_len = sz < 70000 ? sz : 70000;
    std::vector<char> tail((size_t)scan_len);
    fseek(f, sz - scan_len, SEEK_SET);
    if (fread(tail.data(), 1, (size_t)scan_len, f) != (size_t)scan_len) { fclose(f); return false; }
    long eocd = -1;
    for (long i = scan_len - 22; i >= 0; --i) {
        if (rd32(tail.data() + i) == 0x06054b50u) { eocd = i; break; }
    }
    if (eocd < 0) { fclose(f); return false; }
    uint32_t cd_size = rd32(tail.data() + eocd + 12);
    uint32_t cd_off = rd32(tail.data() + eocd + 16);
    if (cd_size == 0 || (long)(cd_off + cd_size) > sz) { fclose(f); return false; }

    std::vector<char> cd(cd_size);
    fseek(f, (long)cd_off, SEEK_SET);
    if (fread(cd.data(), 1, cd_size, f) != cd_size) { fclose(f); return false; }
    fclose(f);

    const std::string prefix = "assets/minecraft/textures/";
    size_t p = 0;
    size_t added = 0;
    while (p + 46 <= cd_size) {
        const char* e = cd.data() + p;
        if (rd32(e) != 0x02014b50u) break;
        uint16_t method = rd16(e + 10);
        uint32_t comp = rd32(e + 20);
        uint32_t uncomp = rd32(e + 24);
        uint16_t nlen = rd16(e + 28);
        uint16_t elen = rd16(e + 30);
        uint16_t clen = rd16(e + 32);
        uint32_t lho = rd32(e + 42);
        if (p + 46 + nlen > cd_size) break;
        std::string name(e + 46, nlen);
        if (name.compare(0, prefix.size(), prefix) == 0 && name.size() > prefix.size() + 4) {
            ZipEntry ze{lho, comp, uncomp, method, path};
            g_index.emplace(std::move(name), ze);
            ++added;
        }
        p += 46u + nlen + elen + clen;
    }
    rlog::logf("hud_icons: indexed %s -> %d texture entries (total %d)",
               path.c_str() + (path.size() > 40 ? path.size() - 40 : 0), (int)added, (int)g_index.size());
    return added > 0;
}

bool zip_read(const std::string& jar, const ZipEntry& e, std::vector<unsigned char>& out) {
    FILE* f = fopen(jar.c_str(), "rb");
    if (!f) return false;
    bool ok = false;
    do {
        if (fseek(f, (long)e.local_off, SEEK_SET) != 0) break;
        char lh[30];
        if (fread(lh, 1, 30, f) != 30) break;
        if (rd32(lh) != 0x04034b50u) break;
        uint16_t nlen = rd16(lh + 26), elen = rd16(lh + 28);
        if (fseek(f, (long)e.local_off + 30 + nlen + elen, SEEK_SET) != 0) break;
        std::vector<unsigned char> comp(e.comp);
        if (e.comp && fread(comp.data(), 1, e.comp, f) != e.comp) break;
        if (e.method == 0) {
            out = std::move(comp);
            ok = true;
        } else if (e.method == 8 && e.uncomp) {
            out.resize(e.uncomp);
            z_stream zs;
            memset(&zs, 0, sizeof(zs));
            zs.next_in = comp.data();
            zs.avail_in = e.comp;
            zs.next_out = out.data();
            zs.avail_out = e.uncomp;
            if (inflateInit2(&zs, -MAX_WBITS) == Z_OK) {
                int r = inflate(&zs, Z_FINISH);
                ok = (r == Z_STREAM_END) || (r == Z_OK && zs.total_out == e.uncomp);
                inflateEnd(&zs);
            }
            if (!ok) out.clear();
        }
    } while (false);
    fclose(f);
    return ok;
}

// caches --------------------------------------------------------------------
std::map<std::string, unsigned> g_item_tex;    // suffix -> GL tex (0 = no texture)
std::map<std::string, unsigned> g_skin_tex;    // hash -> GL tex (0 = failed)
std::map<const void*, unsigned> g_mem_tex;     // embedded PNG -> GL tex
std::vector<unsigned> g_all_tex;

unsigned cache_add(unsigned tex) {
    if (tex) g_all_tex.push_back(tex);
    return tex;
}

} // namespace

unsigned white() {
    static unsigned tex = 0;
    static bool done = false;
    if (done) return tex;
    done = true;
    unsigned char px[4] = {255, 255, 255, 255};
    tex = cache_add(upload_rgba(px, 1, 1));
    return tex;
}

unsigned item(const std::string& suffix) {
    if (suffix.empty()) return 0;
    auto it = g_item_tex.find(suffix);
    if (it != g_item_tex.end()) return it->second;
    if (!g_jars_scanned) scan_jars();

    std::vector<unsigned char> bytes;
    std::string key;
    // Prefer item/, fall back to block/ (held blocks use block textures).
    for (const char* dir : { "item", "block" }) {
        std::string name = std::string("assets/minecraft/textures/") + dir + "/" + suffix + ".png";
        if (!g_jars_scanned) break;
        for (const std::string& jar : g_jars) {
            auto e = g_index.find(name);
            if (e == g_index.end() && g_indexed_jars.insert(jar).second) {
                if (!index_jar(jar)) continue;
                e = g_index.find(name);
            }
            // Read only from the jar the entry was indexed from: a local header
            // offset is meaningless in a different archive.
            if (e != g_index.end() && e->second.owner == jar &&
                zip_read(jar, e->second, bytes)) {
                key = name;
                break;
            }
        }
        if (!bytes.empty()) break;
    }

    unsigned tex = 0;
    if (!bytes.empty()) tex = decode_png(bytes.data(), bytes.size());
    if (!tex) {
        // Some versions index nothing until jars are walked — remember the miss.
        rlog::logf("hud_icons: no texture for '%s'", suffix.c_str());
    }
    unsigned cached = cache_add(tex);
    g_item_tex[suffix] = cached;
    return cached;
}

unsigned skin(const std::string& hash) {
    if (hash.size() < 4) return 0;
    auto it = g_skin_tex.find(hash);
    if (it != g_skin_tex.end()) return it->second;
    const char* home = getenv("HOME");
    if (!home) return 0;
    char path[640];
    snprintf(path, sizeof(path), "%s/.minecraft/assets/skins/%c%c/%s", home,
             hash[0], hash[1], hash.c_str());
    FILE* f = fopen(path, "rb");
    if (!f) {
        // Not in the client assets cache — fetch from Mojang, then retry.
        char url[320], tmp[768], cmd[2048];
        snprintf(url, sizeof(url), "https://textures.minecraft.net/texture/%s", hash.c_str());
        snprintf(tmp, sizeof(tmp), "%s/.minecraft/flaway_skin_%s", home, hash.c_str());
        snprintf(cmd, sizeof(cmd),
                 "curl -fsSL --max-time 8 '%s' -o '%s' 2>/dev/null",
                 url, tmp);
        int rc = system(cmd);
        if (rc == 0) f = fopen(tmp, "rb");
        if (!f) {
            // Known Mojang default skins (offline / missing profile textures).
            static const char* k_defaults[] = {
                "c9037d53d60811835e8b1a1b0e0e0e0e",
                "86df3d8080d90d5593519d3b093e9a2059a671e3",
                "e3d5360e0d2f1b80e2c8d3c5",
            };
            for (const char* d : k_defaults) {
                if (strcmp(d, hash.c_str()) == 0) continue;
                char durl[320], dtmp[768], dcmd[2048];
                snprintf(durl, sizeof(durl), "https://textures.minecraft.net/texture/%s", d);
                snprintf(dtmp, sizeof(dtmp), "%s/.minecraft/flaway_skin_%s", home, d);
                snprintf(dcmd, sizeof(dcmd),
                         "curl -fsSL --max-time 5 '%s' -o '%s' 2>/dev/null",
                         durl, dtmp);
                if (system(dcmd) == 0) {
                    f = fopen(dtmp, "rb");
                    if (f) break;
                }
            }
        }
    }
    if (!f) { g_skin_tex[hash] = 0; return 0; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> bytes(sz > 0 ? (size_t)sz : 0);
    bool ok = !bytes.empty() && fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
    fclose(f);
    unsigned tex = ok ? decode_png(bytes.data(), bytes.size()) : 0;
    if (!tex) rlog::logf("hud_icons: skin decode failed for %.16s", hash.c_str());
    unsigned cached = cache_add(tex);
    g_skin_tex[hash] = cached;
    return cached;
}

// Embedded PNG (compiled-in byte array): decoded once, cached by pointer so a
// caller can pass a static array every frame without re-uploading.
unsigned png(const unsigned char* data, size_t len) {
    if (!data || !len) return 0;
    auto it = g_mem_tex.find(data);
    if (it != g_mem_tex.end()) return it->second;
    unsigned tex = cache_add(decode_png(data, len));
    g_mem_tex[data] = tex;
    return tex;
}

void shutdown() {
    if (pDeleteTextures && !g_all_tex.empty())
        pDeleteTextures((int)g_all_tex.size(), g_all_tex.data());
    g_all_tex.clear();
    g_item_tex.clear();
    g_skin_tex.clear();
    g_mem_tex.clear();
}

} // namespace hud_icons
