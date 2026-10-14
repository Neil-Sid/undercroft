// UNDERCROFT in the browser: a WebGL 2 canvas, keyboard events and the Emscripten main loop.
// Built by build_web.ps1 in place of main.cpp; everything else is the same game code.
#include "app.h"
#include "audio.h"
#include "keys.h"
#include "models.h"
#include "paint.h"
#include "textures.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <algorithm>
#include <cmath>

namespace {

Renderer* renderer;
App* app;
double last = 0;
bool firstFrame = true;

EM_JS(void, hideLoading, (), {
    const el = document.getElementById('loading');
    if (el) el.remove();
});

EM_JS(void, showError, (const char* text), {
    const el = document.getElementById('loading');
    if (el) el.textContent = UTF8ToString(text);
});

// The canvas fills the page; its backing store matches the screen's real pixels.
void fitCanvas(int& w, int& h) {
    double cssW, cssH;
    emscripten_get_element_css_size("#canvas", &cssW, &cssH);
    double dpr = emscripten_get_device_pixel_ratio();
    w = std::max(1, int(std::lround(cssW * dpr))), h = std::max(1, int(std::lround(cssH * dpr)));
    int cw, ch;
    emscripten_get_canvas_element_size("#canvas", &cw, &ch);
    if (cw != w || ch != h) emscripten_set_canvas_element_size("#canvas", w, h);
}

void frame() {
    double now = emscripten_get_now();
    float dt = float(std::min(0.1, (now - last) / 1000));
    last = now;
    app->quit = false;  // "Leave" has nowhere to go in a browser tab
    app->update(dt);
    int w, h;
    fitCanvas(w, h);
    app->render();
    renderer->present(w, h);
    if (firstFrame) hideLoading(), firstFrame = false;
}

// Keys the browser would otherwise use for scrolling, focus or find.
bool claimed(int vk) { return vk == VK_TAB || vk == VK_SPACE || vk == VK_RETURN || vk == VK_F1 || (vk >= VK_LEFT && vk <= VK_DOWN); }

bool keyDown(int, const EmscriptenKeyboardEvent* e, void*) {
    int vk = int(e->keyCode);
    if (e->ctrlKey || e->metaKey || e->altKey) return false;
    if (!e->repeat) app->key(vk);
    return claimed(vk);
}

bool keyUp(int, const EmscriptenKeyboardEvent* e, void*) {
    app->keyUp(int(e->keyCode));
    return claimed(int(e->keyCode));
}

bool blur(int, const EmscriptenFocusEvent*, void*) {
    app->focusLost();
    return false;
}

}  // namespace

int main() {
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.alpha = attrs.depth = attrs.stencil = attrs.antialias = false;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE gl = emscripten_webgl_create_context("#canvas", &attrs);
    if (gl <= 0) {
        showError("Undercroft needs WebGL 2, which this browser doesn't offer.");
        return 1;
    }
    emscripten_webgl_make_context_current(gl);

    paint::startup();
    renderer = new Renderer;
    renderer->init(buildAtlas());
    buildModels(*renderer);
    audio::start();
    app = new App(*renderer, uint32_t(emscripten_random() * 4294967295.0));

    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, keyDown);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, keyUp);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, blur);
    last = emscripten_get_now();
    emscripten_set_main_loop(frame, 0, false);
    return 0;
}
