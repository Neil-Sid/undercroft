#include "render.h"
#include <algorithm>
#include <cstdio>

namespace {

// Shaders are written for both GLSL 3.30 and GLSL ES 3.00 (WebGL 2); compile() adds the header.
// ES has no `noperspective`, so screen-space (affine) interpolation, the PS1's texture warp and
// flat Gouraud, is done by hand: varyings leave the vertex shader multiplied by w and the
// fragment shader divides by the interpolated w, which cancels the perspective correction.
const char* WORLD_VS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aUV;
layout(location = 3) in vec4 aColor;
uniform mat4 uModel, uView, uProj;
uniform vec3 uAmbient;
uniform int uLightCount;
uniform vec4 uLightPos[8];
uniform vec3 uLightColor[8];
uniform vec2 uFog;
uniform float uUnlit;
uniform vec4 uTint;
out vec3 vUVw;
flat out vec2 vCell;
out vec4 vColorW;
out vec4 vBaseFogW;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vec3 n = normalize(mat3(uModel) * aNormal);
    vec3 light = uAmbient;
    for (int i = 0; i < uLightCount; i++) {
        vec3 d = uLightPos[i].xyz - world.xyz;
        float dist = length(d);
        float att = clamp(1.0 - dist / uLightPos[i].w, 0.0, 1.0);
        float facing = dist > 0.001 ? max(dot(n, d / dist), 0.0) * 0.75 + 0.25 : 1.0;
        light += uLightColor[i] * (att * att * facing);
    }
    vec4 color = vec4(aColor.rgb * mix(light, vec3(1.0), uUnlit), aColor.a) * uTint;
    vec3 base = mix(vec3(1.0), aColor.rgb * uTint.rgb, uUnlit);
    vec4 view = uView * world;
    float fog = clamp((-view.z - uFog.x) / max(uFog.y - uFog.x, 0.001), 0.0, 1.0);
    vec4 clip = uProj * view;
    // Snap to the 320x240 pixel grid, as the PS1's integer GTE output did.
    const vec2 grid = vec2(160.0, 120.0);
    if (clip.w > 0.0) clip.xy = floor(clip.xy / clip.w * grid + 0.5) / grid * clip.w;
    gl_Position = clip;
    vUVw = vec3(aUV.xy, 1.0) * clip.w;
    vColorW = color * clip.w;
    vBaseFogW = vec4(base, fog) * clip.w;
    vCell = vec2(mod(aUV.z, 16.0), floor(aUV.z / 16.0));
}
)";

// Emissive texels ignore lighting and baked shading; in unlit draws (effects) they take the
// vertex colour and tint, so one white glow texture serves every colour of spark.
const char* WORLD_FS = R"(
in vec3 vUVw;
flat in vec2 vCell;
in vec4 vColorW;
in vec4 vBaseFogW;
uniform sampler2D uAtlas;
uniform vec3 uFogColor;
out vec4 frag;
const float DITHER[16] = float[](-4.0, 0.0, -3.0, 1.0, 2.0, -2.0, 3.0, -1.0, -3.0, 1.0, -4.0, 0.0, 3.0, -1.0, 2.0, -2.0);
void main() {
    float w = vUVw.z;
    vec2 local = vUVw.xy / w;
    vec4 color = vColorW / w;
    vec4 baseFog = vBaseFogW / w;
    vec2 uv = (vCell + (fract(local) * 63.0 + 0.5) / 64.0) / 16.0;
    vec4 tex = texture(uAtlas, uv);
    if (tex.a < 0.5) discard;
    vec3 c = tex.rgb * (tex.a < 0.9 ? baseFog.rgb : color.rgb);
    c = mix(c, uFogColor, baseFog.a);
    ivec2 p = ivec2(gl_FragCoord.xy) & 3;
    c = floor(clamp(c * 255.0 + DITHER[p.y * 4 + p.x], 0.0, 255.0) / 8.0) * 8.0 / 255.0;
    frag = vec4(c, color.a);
}
)";

const char* FULLSCREEN_VS = R"(
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* COMPOSITE_FS = R"(
in vec2 vUV;
uniform sampler2D uTex;
uniform float uBrightness;
uniform vec4 uFlash;
out vec4 frag;
void main() {
    vec3 c = texture(uTex, vUV).rgb * uBrightness;
    frag = vec4(mix(c, uFlash.rgb, uFlash.a), 1.0);
}
)";

// Sharp bilinear: nearest-neighbour inside each source pixel, a one-pixel blend at edges.
const char* PRESENT_FS = R"(
in vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uSource;
uniform float uScale;
out vec4 frag;
void main() {
    vec2 texel = vUV * uSource;
    vec2 base = floor(texel);
    vec2 centre = fract(texel) - 0.5;
    vec2 region = vec2(0.5 - 0.5 / uScale);
    vec2 f = (centre - clamp(centre, -region, region)) * uScale + 0.5;
    frag = vec4(texture(uTex, (base + f) / uSource).rgb, 1.0);
}
)";

const char* UI_VS = R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = vec4(aPos.x / 320.0 - 1.0, 1.0 - aPos.y / 240.0, 0.0, 1.0);
}
)";

const char* UI_FS = R"(
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 frag;
void main() { frag = texture(uTex, vUV) * vColor; }
)";

GLuint compile(GLenum type, const char* src) {
#ifdef __EMSCRIPTEN__
    const char* header = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
#else
    const char* header = "#version 330 core\n";
#endif
    const char* parts[2] = {header, src};
    GLuint s = glCreateShader(type);
    glShaderSource(s, 2, parts, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        if (FILE* f = std::fopen("undercroft_log.txt", "a")) std::fprintf(f, "shader error:\n%s\n", log), std::fclose(f);
    }
    return s;
}

GLuint program(const char* vs, const char* fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    return p;
}

GLuint texture(int w, int h, const void* pixels, GLint filter) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

GLuint framebuffer(GLuint colour, GLuint depth) {
    GLuint f;
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0);
    if (depth) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    return f;
}

void vertexLayout(GLuint vao, GLuint vbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    GLsizei stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(Vertex, color)));
}

}  // namespace

bool Renderer::init(const std::vector<uint32_t>& atlas) {
    worldProg = program(WORLD_VS, WORLD_FS);
    compositeProg = program(FULLSCREEN_VS, COMPOSITE_FS);
    presentProg = program(FULLSCREEN_VS, PRESENT_FS);
    uiProg = program(UI_VS, UI_FS);
    auto L = [&](const char* name) { return glGetUniformLocation(worldProg, name); };
    u = {L("uModel"), L("uView"), L("uProj"), L("uAmbient"), L("uLightCount"), L("uLightPos"), L("uLightColor"),
         L("uFog"), L("uFogColor"), L("uUnlit"), L("uTint"), L("uAtlas")};

    atlasTex = texture(ATLAS_PX, ATLAS_PX, atlas.data(), GL_NEAREST);
    const wchar_t* serif = L"Palatino Linotype";
    fonts[int(Font::Small)] = paint::font(serif, 13, false);
    fonts[int(Font::Body)] = paint::font(serif, 17, false);
    fonts[int(Font::Title)] = paint::font(L"Georgia", 46, true);
    for (int i = 0; i < int(Font::Count); i++) fontTex[i] = texture(fonts[i].image.w, fonts[i].image.h, fonts[i].image.rgba.data(), GL_NEAREST);

    sceneTex = texture(VIEW_W, VIEW_H, nullptr, GL_NEAREST);
    glGenRenderbuffers(1, &sceneDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, VIEW_W, VIEW_H);
    sceneFbo = framebuffer(sceneTex, sceneDepth);
    uiTex = texture(UI_W, UI_H, nullptr, GL_LINEAR);
    uiFbo = framebuffer(uiTex, 0);

    GLuint vaos[5], vbos[4];
    glGenVertexArrays(5, vaos);
    glGenBuffers(4, vbos);
    levelVao = vaos[0], modelVao = vaos[1], streamVao = vaos[2], uiVao = vaos[3], emptyVao = vaos[4];
    levelVbo = vbos[0], modelVbo = vbos[1], streamVbo = vbos[2], uiVbo = vbos[3];
    vertexLayout(levelVao, levelVbo);
    vertexLayout(modelVao, modelVbo);
    vertexLayout(streamVao, streamVbo);
    glBindVertexArray(uiVao);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), reinterpret_cast<void*>(offsetof(UiVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), reinterpret_cast<void*>(offsetof(UiVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UiVertex), reinterpret_cast<void*>(offsetof(UiVertex, color)));
    glBindVertexArray(0);
    return glGetError() == GL_NO_ERROR;
}

void Renderer::beginScene(const SceneParams& p) {
    scene = p;
    std::sort(scene.lights.begin(), scene.lights.end(), [&](const PointLight& a, const PointLight& b) {
        return length(a.pos - p.eye) - a.radius < length(b.pos - p.eye) - b.radius;
    });
    if (scene.lights.size() > 8) scene.lights.resize(8);

    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
    glViewport(0, 0, VIEW_W, VIEW_H);
    glClearColor(p.fogColor.x, p.fogColor.y, p.fogColor.z, 1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);

    glUseProgram(worldProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTex);
    glUniform1i(u.atlas, 0);
    glUniformMatrix4fv(u.view, 1, GL_FALSE, p.view.m);
    glUniformMatrix4fv(u.proj, 1, GL_FALSE, p.proj.m);
    glUniform3f(u.ambient, p.ambient.x, p.ambient.y, p.ambient.z);
    glUniform2f(u.fog, p.fogStart, p.fogEnd);
    glUniform3f(u.fogColor, p.fogColor.x, p.fogColor.y, p.fogColor.z);
    float pos[32] = {}, col[24] = {};
    for (size_t i = 0; i < scene.lights.size(); i++) {
        const PointLight& l = scene.lights[i];
        pos[i * 4] = l.pos.x, pos[i * 4 + 1] = l.pos.y, pos[i * 4 + 2] = l.pos.z, pos[i * 4 + 3] = l.radius;
        col[i * 3] = l.color.x, col[i * 3 + 1] = l.color.y, col[i * 3 + 2] = l.color.z;
    }
    glUniform1i(u.lightCount, int(scene.lights.size()));
    glUniform4fv(u.lightPos, 8, pos);
    glUniform3fv(u.lightColor, 8, col);
}

void Renderer::setLevel(const std::vector<Vertex>& verts) {
    glBindBuffer(GL_ARRAY_BUFFER, levelVbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(Vertex)), verts.data(), GL_STATIC_DRAW);
    levelCount = int(verts.size());
}

void Renderer::setBlend(Blend b) {
    if (b == Blend::Opaque) {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        return;
    }
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    if (b == Blend::Add) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::setDrawUniforms(const Mat4& model, Vec4 tint, bool unlit) {
    glUniformMatrix4fv(u.model, 1, GL_FALSE, model.m);
    glUniform4f(u.tint, tint.x, tint.y, tint.z, tint.w);
    glUniform1f(u.unlit, unlit ? 1.0f : 0.0f);
}

void Renderer::drawLevel() {
    setBlend(Blend::Opaque);
    setDrawUniforms(Mat4{}, {1, 1, 1, 1}, false);
    glBindVertexArray(levelVao);
    glDrawArrays(GL_TRIANGLES, 0, levelCount);
}

Mesh Renderer::addMesh(const std::vector<Vertex>& verts) {
    Mesh m{int(modelVerts.size()), int(verts.size())};
    modelVerts.insert(modelVerts.end(), verts.begin(), verts.end());
    return m;
}

void Renderer::bounds(Mesh m, const Mat4& xf, Vec3& lo, Vec3& hi) const {
    for (int i = m.first; i < m.first + m.count; i++) {
        Vec3 p = xf.point(modelVerts[size_t(i)].pos);
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
}

void Renderer::uploadModels() {
    if (modelUploaded == modelVerts.size()) return;
    glBindBuffer(GL_ARRAY_BUFFER, modelVbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(modelVerts.size() * sizeof(Vertex)), modelVerts.data(), GL_STATIC_DRAW);
    modelUploaded = modelVerts.size();
}

void Renderer::draw(Mesh m, const Mat4& model, Vec4 tint, Blend blend, bool unlit) {
    if (m.count == 0) return;
    uploadModels();
    setBlend(blend);
    setDrawUniforms(model, tint, unlit);
    glBindVertexArray(modelVao);
    glDrawArrays(GL_TRIANGLES, m.first, m.count);
}

void Renderer::drawVerts(const std::vector<Vertex>& verts, const Mat4& model, Vec4 tint, Blend blend, bool unlit) {
    if (verts.empty()) return;
    setBlend(blend);
    setDrawUniforms(model, tint, unlit);
    glBindVertexArray(streamVao);
    glBindBuffer(GL_ARRAY_BUFFER, streamVbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(Vertex)), verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size()));
}

void Renderer::beginViewmodel(const Mat4& proj) {
    glDepthMask(GL_TRUE);  // a blended draw may have left depth writes off, which would skip the clear
    glClear(GL_DEPTH_BUFFER_BIT);
    glUniformMatrix4fv(u.proj, 1, GL_FALSE, proj.m);
}

void Renderer::endScene(float brightness, Vec4 flash) {
    setBlend(Blend::Opaque);
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, uiFbo);
    glViewport(0, 0, UI_W, UI_H);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(compositeProg);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    glUniform1i(glGetUniformLocation(compositeProg, "uTex"), 0);
    glUniform1f(glGetUniformLocation(compositeProg, "uBrightness"), brightness);
    glUniform4f(glGetUniformLocation(compositeProg, "uFlash"), flash.x, flash.y, flash.z, flash.w);
    glBindVertexArray(emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Renderer::uiQuad(GLuint tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t color) {
    if (uiRuns.empty() || uiRuns.back().first != tex) uiRuns.push_back({tex, 0});
    UiVertex a{x, y, u0, v0, color}, b{x + w, y, u1, v0, color}, c{x + w, y + h, u1, v1, color}, d{x, y + h, u0, v1, color};
    uiVerts.insert(uiVerts.end(), {a, b, c, a, c, d});
    uiRuns.back().second += 6;
}

void Renderer::rect(float x, float y, float w, float h, uint32_t color) {
    float t = 0.5f / ATLAS_PX;  // centre of cell 0, which is plain white
    uiQuad(atlasTex, x, y, w, h, t, t, t, t, color);
}

void Renderer::tile(int t, float x, float y, float w, float h, uint32_t color) {
    float u0 = float(t % ATLAS_CELLS) / ATLAS_CELLS, v0 = float(t / ATLAS_CELLS) / ATLAS_CELLS, s = 1.0f / ATLAS_CELLS;
    uiQuad(atlasTex, x, y, w, h, u0, v0, u0 + s, v0 + s, color);
}

// ASCII maps straight to the atlas; the UTF-8 middle dot (C2 B7) uses slot 127.
static int glyphIndex(char ch) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (c == 0xB7) return 127 - 32;
    return c >= 32 && c < 127 ? c - 32 : -1;
}

float Renderer::textWidth(Font f, std::string_view s) const {
    float w = 0;
    for (char c : s)
        if (int g = glyphIndex(c); g >= 0) w += float(fonts[int(f)].glyphs[g].advance);
    return w;
}

float Renderer::text(Font f, float x, float y, std::string_view s, uint32_t color, Align align, bool shadow) {
    float w = textWidth(f, s);
    if (align == Align::Center) x -= std::floor(w / 2);
    if (align == Align::Right) x -= w;
    x = std::floor(x), y = std::floor(y);
    const FontAtlas& fa = fonts[int(f)];
    float iw = 1.0f / float(fa.image.w), ih = 1.0f / float(fa.image.h);
    for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
        float pen = x, off = pass == 0 ? 1.0f : 0.0f;
        uint32_t c = pass == 0 ? (color & 0xFF000000) : color;
        for (char ch : s) {
            int gi = glyphIndex(ch);
            if (gi < 0) continue;
            const Glyph& g = fa.glyphs[gi];
            uiQuad(fontTex[int(f)], pen + float(g.offset) + off, y + off, float(g.w), float(g.h), g.x * iw, g.y * ih, (g.x + g.w) * iw,
                   (g.y + g.h) * ih, c);
            pen += float(g.advance);
        }
    }
    return w;
}

void Renderer::flushUi() {
    if (uiVerts.empty()) return;
    glBindFramebuffer(GL_FRAMEBUFFER, uiFbo);
    glViewport(0, 0, UI_W, UI_H);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(uiProg);
    glUniform1i(glGetUniformLocation(uiProg, "uTex"), 0);
    glBindVertexArray(uiVao);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(uiVerts.size() * sizeof(UiVertex)), uiVerts.data(), GL_STREAM_DRAW);
    int first = 0;
    for (auto [tex, count] : uiRuns) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glDrawArrays(GL_TRIANGLES, first, count);
        first += count;
    }
    uiVerts.clear();
    uiRuns.clear();
    glDisable(GL_BLEND);
}

void Renderer::present(int windowW, int windowH) {
    flushUi();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowW, windowH);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    int h = std::min(windowH, windowW * 3 / 4), w = h * 4 / 3;
    glViewport((windowW - w) / 2, (windowH - h) / 2, w, h);
    glUseProgram(presentProg);
    glBindTexture(GL_TEXTURE_2D, uiTex);
    glUniform1i(glGetUniformLocation(presentProg, "uTex"), 0);
    glUniform2f(glGetUniformLocation(presentProg, "uSource"), float(UI_W), float(UI_H));
    glUniform1f(glGetUniformLocation(presentProg, "uScale"), std::max(1.0f, float(h) / UI_H));
    glBindVertexArray(emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

Pixels Renderer::readFrame() {
    flushUi();
    Pixels img{UI_W, UI_H, std::vector<uint32_t>(size_t(UI_W) * UI_H)};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, uiFbo);
    glReadPixels(0, 0, UI_W, UI_H, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    for (int y = 0; y < UI_H / 2; y++)
        for (int x = 0; x < UI_W; x++) std::swap(img.rgba[size_t(y) * UI_W + x], img.rgba[size_t(UI_H - 1 - y) * UI_W + x]);
    return img;
}
