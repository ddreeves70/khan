#include "OcctGl.h"

// No Qt OpenGL headers here (see OcctGl.h).
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <OpenGl_Context.hxx>
#include <OpenGl_FrameBuffer.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <OpenGl_View.hxx>
#include <OpenGl_Window.hxx>
#include <Standard_Failure.hxx>

#include <algorithm>

namespace kh::view::detail {

namespace {

// Qt's framebuffer, wrapped so OCCT draws into it. Qt's buffer isn't sRGB.
class QtFrameBuffer : public OpenGl_FrameBuffer {
    DEFINE_STANDARD_RTTI_INLINE(QtFrameBuffer, OpenGl_FrameBuffer)
public:
    void BindBuffer(const Handle(OpenGl_Context) & gl) override {
        OpenGl_FrameBuffer::BindBuffer(gl);
        gl->SetFrameBufferSRGB(true, false);
    }
    void BindDrawBuffer(const Handle(OpenGl_Context) & gl) override {
        OpenGl_FrameBuffer::BindDrawBuffer(gl);
        gl->SetFrameBufferSRGB(true, false);
    }
    void BindReadBuffer(const Handle(OpenGl_Context) & gl) override {
        OpenGl_FrameBuffer::BindReadBuffer(gl);
    }
};

}  // namespace

Handle(Graphic3d_GraphicDriver) sharedDriver() {
    static const Handle(Graphic3d_GraphicDriver) driver = []() -> Handle(Graphic3d_GraphicDriver) {
        try {
            Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
            // false: Qt makes the OpenGL contexts, OCCT only draws with them.
            Handle(OpenGl_GraphicDriver) d = new OpenGl_GraphicDriver(display, false);
            d->ChangeOptions().buffersNoSwap = true;       // Qt swaps the buffers
            d->ChangeOptions().buffersOpaqueAlpha = true;  // so Qt's window isn't see-through
            d->ChangeOptions().useSystemBuffer = false;    // draw into OCCT's own buffer, then Qt's
            return d;
        } catch (const Standard_Failure&) {
            return {};
        }
    }();
    return driver;
}

std::string attachContext(const Handle(V3d_View) & view, bool coreProfile, Aspect_Drawable native,
                          Aspect_Drawable fallback, int width, int height, std::string& used) {
    Handle(OpenGl_Context) gl = new OpenGl_Context();
    if (!gl->Init(coreProfile)) return "OpenGl_Context::Init failed on Qt's context";
    used = native ? "given window" : "context's window";
    if (!native) native = gl->Window();
    if (!native) {
        // Qt made its context current on an offscreen buffer (Windows does this
        // for QOpenGLWidget), which has no window: use the top-level window,
        // which Qt draws with OpenGL too, so the context works with it.
        native = fallback;
        used = "top-level window (the context had none)";
    }
    Handle(Aspect_NeutralWindow) window = Handle(Aspect_NeutralWindow)::DownCast(view->Window());
    if (window.IsNull()) {
        window = new Aspect_NeutralWindow();
        window->SetVirtual(true);
    }
    window->SetNativeHandle(native);
    window->SetSize(std::max(1, width), std::max(1, height));
    view->SetWindow(window, gl->RenderingContext());
    return {};
}

std::string useQtFramebuffer(const Handle(V3d_View) & view) {
    Handle(OpenGl_View) glView = Handle(OpenGl_View)::DownCast(view->View());
    if (glView.IsNull()) return "no OpenGl_View";
    if (glView->GlWindow().IsNull()) return "no OpenGl_Window";
    const Handle(OpenGl_Context)& gl = glView->GlWindow()->GetGlContext();
    if (gl.IsNull() || !gl->IsValid()) return "OCCT's OpenGL context isn't valid";
    if (gl->arbFBO == nullptr) return "OCCT's OpenGL context has no framebuffer objects";
    GLint bound = 0;
    gl->core11fwd->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
    const bool hasNative = !view->Window().IsNull() && view->Window()->NativeHandle() != 0;
    const std::string state =
        "framebuffer " + std::to_string(bound) +
        (gl->IsCurrent() ? ", OCCT's context current" : ", OCCT's context not current") +
        (hasNative ? ", native window" : ", no native window");
    Handle(OpenGl_FrameBuffer) fbo = gl->DefaultFrameBuffer();
    if (fbo.IsNull()) {
        fbo = new QtFrameBuffer();
        gl->SetDefaultFrameBuffer(fbo);
    }
    if (!fbo->InitWrapper(gl)) return "wrapping Qt's framebuffer failed (" + state + ")";
    // Follow the size of Qt's buffer (in device pixels).
    Handle(Aspect_NeutralWindow) window = Handle(Aspect_NeutralWindow)::DownCast(view->Window());
    Graphic3d_Vec2i old;
    window->Size(old.x(), old.y());
    const Graphic3d_Vec2i now = fbo->GetVPSize();
    if (now != old) {
        window->SetSize(now.x(), now.y());
        view->MustBeResized();
    }
    return {};
}

}  // namespace kh::view::detail
