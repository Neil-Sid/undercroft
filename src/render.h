#pragma once
// The PS1-style renderer. The world is drawn at 320x240 with vertex snapping, affine
// texture mapping, per-vertex (Gouraud) lighting, depth fog and dithered 15-bit colour.
// Menus and text are drawn into a 640x480 layer on top, then the whole frame is
// scaled to the window with crisp pixels and pillarboxed to 4:3.
#include "gl.h"
#include "paint.h"
#include "vecmath.h"
#include <cstdint>
#include <string_view>
#include <vector>

constexpr int VIEW_W = 320, VIEW_H = 240;
constexpr int UI_W = 640, UI_H = 480;
constexpr int ATLAS_CELLS = 16, ATLAS_CELL_PX = 64, ATLAS_PX = ATLAS_CELLS * ATLAS_CELL_PX;

// Packed colour in GPU byte order R,G,B,A.
constexpr uint32_t rgba(int r, int g, int b, int a = 255) {
    return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24;
}
constexpr uint32_t hex(uint32_t rrggbb, int a = 255) { return rgba(rrggbb >> 16 & 255, rrggbb >> 8 & 255, rrggbb & 255, a); }

// uv repeats inside a single atlas cell (`tile`). Texels with alpha below 0.5 are
// cut out; alpha between 0.5 and 0.9 marks emissive texels that ignore lighting.
struct Vertex {
    Vec3 pos;
    Vec3 normal{0, 1, 0};
    float u = 0, v = 0, tile = 0;
    uint32_t color = 0xFFFFFFFF;
};

struct Mesh {
    int first = 0, count = 0;  // a range in the shared model buffer
};

struct PointLight {
    Vec3 pos;
    float radius = 1;
    Vec3 color;
};

enum class Blend : uint8_t { Opaque, Alpha, Add };
enum class Font : uint8_t { Small, Body, Title, Count };
enum class Align : uint8_t { Left, Center, Right };

struct SceneParams {
    Mat4 view, proj;
    Vec3 eye;
    Vec3 fogColor;
    float fogStart = 2, fogEnd = 12;
    Vec3 ambient;
    std::vector<PointLight> lights;  // the 8 nearest to the eye are used
};

class Renderer {
public:
    bool init(const std::vector<uint32_t>& atlas);  // ATLAS_PX square, top row first

    // 3D pass, 320x240.
    void beginScene(const SceneParams& p);
    void setLevel(const std::vector<Vertex>& verts);
    void drawLevel();
    Mesh addMesh(const std::vector<Vertex>& verts);
    void bounds(Mesh m, const Mat4& xf, Vec3& lo, Vec3& hi) const;  // grows lo/hi to hold the mesh placed by xf
    void draw(Mesh m, const Mat4& model, Vec4 tint = {1, 1, 1, 1}, Blend blend = Blend::Opaque, bool unlit = false);
    void drawVerts(const std::vector<Vertex>& verts, const Mat4& model, Vec4 tint = {1, 1, 1, 1}, Blend blend = Blend::Opaque, bool unlit = false);
    void beginViewmodel(const Mat4& proj);  // clears depth; later draws use this projection
    void endScene(float brightness, Vec4 flash);  // composites the 3D frame into the UI layer

    // 2D pass, 640x480, origin top-left.
    void rect(float x, float y, float w, float h, uint32_t color);
    void tile(int tile, float x, float y, float w, float h, uint32_t color = 0xFFFFFFFF);
    float text(Font f, float x, float y, std::string_view s, uint32_t color, Align align = Align::Left, bool shadow = true);
    float textWidth(Font f, std::string_view s) const;
    int lineHeight(Font f) const { return fonts[int(f)].lineHeight; }

    void present(int windowW, int windowH);
    Pixels readFrame();  // the 640x480 frame, top row first

private:
    struct UiVertex {
        float x, y, u, v;
        uint32_t color;
    };
    void flushUi();
    void uiQuad(GLuint tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t color);
    void setBlend(Blend b);
    void setDrawUniforms(const Mat4& model, Vec4 tint, bool unlit);
    void uploadModels();

    GLuint worldProg = 0, compositeProg = 0, uiProg = 0, presentProg = 0;
    GLuint sceneFbo = 0, sceneTex = 0, sceneDepth = 0, uiFbo = 0, uiTex = 0;
    GLuint atlasTex = 0, fontTex[int(Font::Count)] = {};
    FontAtlas fonts[int(Font::Count)];
    GLuint levelVao = 0, levelVbo = 0, modelVao = 0, modelVbo = 0, streamVao = 0, streamVbo = 0, uiVao = 0, uiVbo = 0, emptyVao = 0;
    int levelCount = 0;
    std::vector<Vertex> modelVerts;
    size_t modelUploaded = 0;
    std::vector<UiVertex> uiVerts;
    std::vector<std::pair<GLuint, int>> uiRuns;  // texture, vertex count
    SceneParams scene;
    struct {
        GLint model, view, proj, ambient, lightCount, lightPos, lightColor, fog, fogColor, unlit, tint, atlas;
    } u{};
};
