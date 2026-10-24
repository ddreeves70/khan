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

bool attachContext(const Handle(V3d_View) & view, bool coreProfile, Aspect_Drawable native, int width,
                   int height) {
    Handle(OpenGl_Context) gl = new OpenGl_Context();
    if (!gl->Init(coreProfile)) return false;
    if (!native) native = gl->Window();
    Handle(Aspect_NeutralWindow) window = Handle(Aspect_NeutralWindow)::DownCast(view->Window());
    if (window.IsNull()) {
        window = new Aspect_NeutralWindow();
        window->SetVirtual(true);
    }
    window->SetNativeHandle(native);
    window->SetSize(std::max(1, width), std::max(1, height));
    view->SetWindow(window, gl->RenderingContext());
    return true;
}

bool useQtFramebuffer(const Handle(V3d_View) & view) {
    Handle(OpenGl_View) glView = Handle(OpenGl_View)::DownCast(view->View());
    if (glView.IsNull() || glView->GlWindow().IsNull()) return false;
    const Handle(OpenGl_Context)& gl = glView->GlWindow()->GetGlContext();
    Handle(OpenGl_FrameBuffer) fbo = gl->DefaultFrameBuffer();
    if (fbo.IsNull()) {
        fbo = new QtFrameBuffer();
        gl->SetDefaultFrameBuffer(fbo);
    }
    if (!fbo->InitWrapper(gl)) return false;
    // Follow the size of Qt's buffer (in device pixels).
    Handle(Aspect_NeutralWindow) window = Handle(Aspect_NeutralWindow)::DownCast(view->Window());
    Graphic3d_Vec2i old;
    window->Size(old.x(), old.y());
    const Graphic3d_Vec2i now = fbo->GetVPSize();
    if (now != old) {
        window->SetSize(now.x(), now.y());
        view->MustBeResized();
    }
    return true;
}

}  // namespace kh::view::detail
