#include "kh/view/ViewWidget.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPointer>
#include <QWheelEvent>

#include <AIS_AnimationCamera.hxx>
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_ViewController.hxx>
#include <AIS_ViewCube.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_AmbientLight.hxx>
#include <V3d_DirectionalLight.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cmath>
#include <map>

#include "OcctGl.h"
#include "kh/kernel_occt/OcctShape.h"
#include "kh/view/Graphics.h"

namespace kh::view {

namespace {

Quantity_Color rgb(int r, int g, int b) {
    return Quantity_Color(r / 255.0, g / 255.0, b / 255.0, Quantity_TOC_sRGB);
}

// Colours, matching the flat view and the docs pictures.
const Quantity_Color kEdges = rgb(44, 52, 64);
const Quantity_Color kSelected = rgb(255, 140, 26);
const Quantity_Color kHover = rgb(90, 170, 255);
const Quantity_Color kSkyTop = rgb(214, 222, 233);
const Quantity_Color kSkyBottom = rgb(250, 251, 253);

// An overlay, drawn from plain lines or dots. It has no selection, so clicks go
// through it, and it isn't a shape, so Fit all ignores it.
class OverlayObject final : public AIS_InteractiveObject {
public:
    explicit OverlayObject(const ViewOverlay& o) : overlay_(o) {}

    void Compute(const Handle(PrsMgr_PresentationManager) &, const Handle(Prs3d_Presentation) & prs,
                 const Standard_Integer) override {
        const auto& o = overlay_;
        const Quantity_ColorRGBA color(rgb(o.color.r, o.color.g, o.color.b),
                                       static_cast<float>(std::clamp(o.alpha, 0, 255) / 255.0));
        Handle(Graphic3d_Group) group = prs->NewGroup();
        const bool dots = o.shape == OverlayShape::Points || (o.shape == OverlayShape::Lattice && o.dots);
        if (dots) {
            const auto points = o.shape == OverlayShape::Points ? o.points : latticePoints(o);
            if (points.empty()) return;
            Handle(Graphic3d_ArrayOfPoints) array =
                new Graphic3d_ArrayOfPoints(static_cast<int>(points.size()));
            for (const auto& p : points) array->AddVertex(p.x, p.y, p.z);
            Handle(Graphic3d_AspectMarker3d) aspect =
                new Graphic3d_AspectMarker3d(Aspect_TOM_POINT, color.GetRGB(), std::max(1.0, o.size));
            aspect->SetInteriorColor(color);  // with its alpha
            if (o.alpha < 255) aspect->SetAlphaMode(Graphic3d_AlphaMode_Blend);
            group->SetGroupPrimitivesAspect(aspect);
            group->AddPrimitiveArray(array);
        } else {
            const auto points = o.shape == OverlayShape::Lines ? o.points : latticeLines(o);
            if (points.size() < 2) return;
            const int count = static_cast<int>(points.size() / 2 * 2);
            Handle(Graphic3d_ArrayOfSegments) array = new Graphic3d_ArrayOfSegments(count);
            for (int i = 0; i < count; ++i) {
                const auto& p = points[static_cast<std::size_t>(i)];
                array->AddVertex(p.x, p.y, p.z);
            }
            Handle(Graphic3d_AspectLine3d) aspect =
                new Graphic3d_AspectLine3d(color.GetRGB(), Aspect_TOL_SOLID, std::max(1.0, o.size));
            aspect->SetInteriorColor(color);  // with its alpha
            if (o.alpha < 255) aspect->SetAlphaMode(Graphic3d_AlphaMode_Blend);
            group->SetGroupPrimitivesAspect(aspect);
            group->AddPrimitiveArray(array);
        }
    }
    void ComputeSelection(const Handle(SelectMgr_Selection) &, const Standard_Integer) override {}

private:
    ViewOverlay overlay_;
};

constexpr double kAxisLength = 25.0;  // mm
constexpr double kCubeSize = 40.0;    // pixels, before scaling for high-DPI screens
constexpr int kCubeOffset = 62;       // pixels from the corner to the cube's middle

Aspect_VKeyMouse toButtons(Qt::MouseButtons b) {
    Aspect_VKeyMouse out = Aspect_VKeyMouse_NONE;
    if (b & Qt::LeftButton) out |= Aspect_VKeyMouse_LeftButton;
    if (b & Qt::MiddleButton) out |= Aspect_VKeyMouse_MiddleButton;
    if (b & Qt::RightButton) out |= Aspect_VKeyMouse_RightButton;
    return out;
}

Aspect_VKeyFlags toFlags(Qt::KeyboardModifiers m) {
    Aspect_VKeyFlags out = Aspect_VKeyFlags_NONE;
    if (m & Qt::ShiftModifier) out |= Aspect_VKeyFlags_SHIFT;
    if (m & Qt::ControlModifier) out |= Aspect_VKeyFlags_CTRL;
    if (m & Qt::AltModifier) out |= Aspect_VKeyFlags_ALT;
    if (m & Qt::MetaModifier) out |= Aspect_VKeyFlags_META;
    return out;
}

QString drawFailed() {
    return QCoreApplication::translate("kh::view",
                                       "The 3D view couldn't start OpenGL on this computer. Update the "
                                       "graphics driver (in a virtual machine, "
                                       "turn on 3D acceleration), then restart Khan.");
}

}  // namespace

// A mouse button plus keys held, as OCCT's gesture maps want them.
static unsigned int with(Aspect_VKeyMouse button, Aspect_VKeyFlags keys) {
    return static_cast<unsigned int>(button) | static_cast<unsigned int>(keys);
}

// OCCT's mouse handling for one view. It turns clicks into picks and asks for
// more frames while the camera animates.
class ViewController final : public AIS_ViewController {
public:
    explicit ViewController(ViewWidget& widget) : widget_(widget) {
        // Left drag orbits; right, middle or Shift+left drag pans; the wheel zooms.
        auto& gestures = ChangeMouseGestureMap();
        gestures.Clear();
        gestures.Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_RotateOrbit);
        gestures.Bind(with(Aspect_VKeyMouse_LeftButton, Aspect_VKeyFlags_CTRL), AIS_MouseGesture_RotateOrbit);
        gestures.Bind(with(Aspect_VKeyMouse_LeftButton, Aspect_VKeyFlags_SHIFT), AIS_MouseGesture_Pan);
        gestures.Bind(Aspect_VKeyMouse_MiddleButton, AIS_MouseGesture_Pan);
        gestures.Bind(Aspect_VKeyMouse_RightButton, AIS_MouseGesture_Pan);
        // A click selects; Ctrl- or Shift-click adds or removes.
        auto& schemes = ChangeMouseSelectionSchemes();
        schemes.Clear();
        schemes.Bind(Aspect_VKeyMouse_LeftButton, AIS_SelectionScheme_Replace);
        schemes.Bind(with(Aspect_VKeyMouse_LeftButton, Aspect_VKeyFlags_CTRL), AIS_SelectionScheme_XOR);
        schemes.Bind(with(Aspect_VKeyMouse_LeftButton, Aspect_VKeyFlags_SHIFT), AIS_SelectionScheme_XOR);
        SetLockOrbitZUp(true);  // Z stays up while orbiting, so the floor stays the floor
    }

    void OnSelectionChanged(const Handle(AIS_InteractiveContext) & ctx,
                            const Handle(V3d_View) & view) override;

protected:
    void handleViewRedraw(const Handle(AIS_InteractiveContext) & ctx,
                          const Handle(V3d_View) & view) override {
        AIS_ViewController::handleViewRedraw(ctx, view);
        if (toAskNextFrame()) {
            // The camera is still moving (the view cube's turn): draw again soon.
            QPointer<ViewWidget> w(&widget_);
            QMetaObject::invokeMethod(
                &widget_,
                [w] {
                    if (w) w->update();
                },
                Qt::QueuedConnection);
        }
    }

private:
    ViewWidget& widget_;
};

struct ViewWidget::Impl {
    explicit Impl(ViewWidget& w) : controller(w) {}

    struct Shown {
        std::uint64_t shapeId = 0;  // the ShapeHandle it was made from
        bool ghost = false;
        Rgb color;
        Handle(AIS_Shape) object;
    };

    ViewController controller;
    Handle(V3d_Viewer) viewer;
    Handle(AIS_InteractiveContext) context;
    Handle(V3d_View) view;
    Handle(AIS_ViewCube) cube;
    std::map<FeatureId, Shown> shown;
    SceneContent content;
    std::vector<Handle(OverlayObject)> overlays;
    bool fitPending = true;
    bool everShown = false;  // has the view had a body to fit yet?
    bool ready = false;
    bool clickAdds = false;  // the last click had Ctrl or Shift held
    QString problem;

    void fit() {
        Bnd_Box box;
        for (const auto& [id, s] : shown) BRepBndLib::Add(s.object->Shape(), box);
        if (box.IsVoid()) box.Update(-50, -50, 0, 50, 50, 30);  // around the origin
        view->FitAll(box, 0.12, false);
        view->ZFitAll();
        fitPending = false;
    }

    void fail(ViewWidget& w, const QString& why) {
        if (!problem.isEmpty()) return;
        problem = why;
        ready = false;
        emit w.failed(why);
    }
};

void ViewController::OnSelectionChanged(const Handle(AIS_InteractiveContext) & ctx,
                                        const Handle(V3d_View) &) {
    auto& d = *widget_.impl_;
    std::vector<FeatureId> now;
    for (ctx->InitSelected(); ctx->MoreSelected(); ctx->NextSelected()) {
        const Handle(AIS_InteractiveObject) obj = ctx->SelectedInteractive();
        for (const auto& [id, s] : d.shown) {
            if (s.object == obj) now.push_back(id);
        }
    }
    // Ctrl-click keeps selected steps this view doesn't draw (one that failed to build, say).
    if (d.clickAdds) {
        for (const auto& id : d.content.selected) {
            if (!d.shown.count(id)) now.push_back(id);
        }
    }
    auto merged = mergePick(d.content.selected, now);
    if (merged == d.content.selected) return;
    // Tell the world after OCCT has finished handling the click.
    QPointer<ViewWidget> w(&widget_);
    QMetaObject::invokeMethod(
        &widget_,
        [w, merged] {
            if (w) emit w->picked(merged);
        },
        Qt::QueuedConnection);
}

ViewWidget::ViewWidget(QWidget* parent) : QOpenGLWidget(parent), impl_(std::make_unique<Impl>(*this)) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(120, 90);
    auto& d = *impl_;
    d.problem = graphicsProblem();
    if (!d.problem.isEmpty()) return;
    try {
        d.viewer = new V3d_Viewer(detail::sharedDriver());
        // Soft light all round, plus a lamp over the student's left shoulder that
        // turns with the camera, so the faces of a box come out in different shades.
        d.viewer->AddLight(new V3d_AmbientLight(Quantity_NOC_WHITE));
        d.viewer->AddLight(new V3d_DirectionalLight(gp_Dir(0.45, -0.6, -1.0), Quantity_NOC_WHITE, true));
        d.viewer->SetLightOn();
        d.context = new AIS_InteractiveContext(d.viewer);
        // Selected bodies turn orange, and the body under the mouse light blue: whole
        // faces, not just their edges.
        for (auto type : {Prs3d_TypeOfHighlight_Selected, Prs3d_TypeOfHighlight_LocalSelected,
                          Prs3d_TypeOfHighlight_Dynamic, Prs3d_TypeOfHighlight_LocalDynamic}) {
            const bool selected =
                type == Prs3d_TypeOfHighlight_Selected || type == Prs3d_TypeOfHighlight_LocalSelected;
            d.context->HighlightStyle(type)->SetColor(selected ? kSelected : kHover);
            d.context->HighlightStyle(type)->SetDisplayMode(AIS_Shaded);
        }

        d.view = d.viewer->CreateView();
        d.view->SetImmediateUpdate(false);
        d.view->SetBgGradientColors(kSkyTop, kSkyBottom, Aspect_GFM_VER, false);
        d.view->ChangeRenderingParams().NbMsaaSamples = 4;
        d.controller.ViewAnimation()->SetView(d.view);

        // The axes through the origin: X red, Y green, Z blue. They can't be picked.
        const struct {
            gp_Pnt end;
            Quantity_Color color;
        } axes[] = {{gp_Pnt(kAxisLength, 0, 0), rgb(209, 69, 59)},
                    {gp_Pnt(0, kAxisLength, 0), rgb(46, 155, 79)},
                    {gp_Pnt(0, 0, kAxisLength), rgb(52, 101, 196)}};
        for (const auto& a : axes) {
            Handle(AIS_Shape) axis = new AIS_Shape(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), a.end).Edge());
            axis->SetColor(a.color);
            axis->SetWidth(2.0);
            d.context->Display(axis, AIS_WireFrame, -1, false);
        }
        setStandardView(StandardView::Iso);
    } catch (const Standard_Failure&) {
        d.problem = drawFailed();
        d.view.Nullify();
        d.context.Nullify();
        d.viewer.Nullify();
    }
}

ViewWidget::~ViewWidget() {
    // OCCT frees OpenGL resources as the view goes, so make Qt's context current.
    if (impl_->view && context()) {
        makeCurrent();
        try {
            impl_->context->RemoveAll(false);
            impl_->view->Remove();
        } catch (const Standard_Failure&) {
        }
        doneCurrent();
    }
}

void ViewWidget::setContent(const SceneContent& content) {
    auto& d = *impl_;
    d.content = content;
    if (!d.context) return;
    try {
        std::map<FeatureId, Impl::Shown> next;
        for (const auto& body : content.bodies) {
            const TopoDS_Shape* shape = kernel::occtShape(body.shape);
            if (!shape || shape->IsNull()) continue;
            auto it = d.shown.find(body.feature);
            if (it != d.shown.end() && it->second.shapeId == body.shape.id() &&
                it->second.ghost == body.ghost && it->second.color == body.color) {
                next.insert(d.shown.extract(it));
                continue;
            }
            // Draw a copy: the view meshes what it draws, and the design's own shape
            // must not change (exports mesh it too, and must not depend on the view).
            Handle(AIS_Shape) obj = new AIS_Shape(BRepBuilderAPI_Copy(*shape, false, false).Shape());
            obj->SetColor(rgb(body.color.r, body.color.g, body.color.b));
            obj->Attributes()->SetDeviationAngle(0.1);  // radians: smooth round edges
            if (body.ghost) {
                obj->SetTransparency(0.7);
                // Drawn just in front of the body it became part of, so their shared
                // faces don't flicker between the two.
                obj->Attributes()->ShadingAspect()->Aspect()->SetPolygonOffsets(Aspect_POM_Fill, -1.0f,
                                                                                -2.0f);
            } else {
                obj->Attributes()->SetFaceBoundaryDraw(true);
                obj->Attributes()->SetFaceBoundaryAspect(new Prs3d_LineAspect(kEdges, Aspect_TOL_SOLID, 1.0));
            }
            d.context->Display(obj, AIS_Shaded, 0, false);
            next[body.feature] = {body.shape.id(), body.ghost, body.color, obj};
        }
        for (auto& [id, s] : d.shown) d.context->Remove(s.object, false);
        d.shown = std::move(next);

        d.context->ClearSelected(false);
        for (const auto& id : content.selected) {
            if (auto it = d.shown.find(id); it != d.shown.end())
                d.context->AddOrRemoveSelected(it->second.object, false);
        }
        if (!d.everShown && !d.shown.empty()) {
            d.everShown = true;
            d.fitPending = true;
        }
    } catch (const Standard_Failure&) {
        d.fail(*this, drawFailed());
    }
    update();
}

void ViewWidget::setOverlays(const std::vector<ViewOverlay>& overlays) {
    auto& d = *impl_;
    if (!d.context) return;
    try {
        for (auto& o : d.overlays) d.context->Remove(o, false);
        d.overlays.clear();
        for (const auto& o : overlays) {
            Handle(OverlayObject) obj = new OverlayObject(o);
            d.context->Display(obj, 0, -1, false);  // -1: never picked
            d.overlays.push_back(obj);
        }
    } catch (const Standard_Failure&) {
        d.fail(*this, drawFailed());
    }
    update();
}

int ViewWidget::overlayCount() const {
    return static_cast<int>(impl_->overlays.size());
}

void ViewWidget::setStandardView(StandardView v) {
    auto& d = *impl_;
    if (!d.view) return;
    const ViewDirection dir = direction(v);
    d.controller.AbortViewAnimation();
    d.view->SetProj(dir.toEye.x, dir.toEye.y, dir.toEye.z);
    d.view->SetUp(dir.up.x, dir.up.y, dir.up.z);
    d.fitPending = true;
    update();
}

void ViewWidget::fitAll() {
    impl_->fitPending = true;
    update();
}

ViewDirection ViewWidget::camera() const {
    const auto& d = *impl_;
    if (!d.view) return direction(StandardView::Iso);
    const Handle(Graphic3d_Camera)& cam = d.view->Camera();
    const gp_Dir look = cam->Direction();
    const gp_Dir up = cam->Up();
    return {{-look.X(), -look.Y(), -look.Z()}, {up.X(), up.Y(), up.Z()}};
}

const SceneContent& ViewWidget::content() const {
    return impl_->content;
}

int ViewWidget::shownCount() const {
    return static_cast<int>(impl_->shown.size());
}

bool ViewWidget::ready() const {
    return impl_->ready && impl_->problem.isEmpty();
}

QString ViewWidget::problem() const {
    return impl_->problem;
}

void ViewWidget::clickAt(QPoint point, bool add) {
    auto& d = *impl_;
    if (!d.view) return;
    const double ratio = devicePixelRatioF();
    const Graphic3d_Vec2i p(static_cast<int>(point.x() * ratio), static_cast<int>(point.y() * ratio));
    const Aspect_VKeyFlags flags = add ? Aspect_VKeyFlags_CTRL : Aspect_VKeyFlags_NONE;
    d.clickAdds = add;
    d.controller.UpdateMousePosition(p, Aspect_VKeyMouse_NONE, flags, false);
    d.controller.UpdateMouseButtons(p, Aspect_VKeyMouse_LeftButton, flags, false);
    d.controller.UpdateMouseButtons(p, Aspect_VKeyMouse_NONE, flags, false);
    update();
}

QSize ViewWidget::sizeHint() const {
    return {640, 480};
}

void ViewWidget::initializeGL() {
    auto& d = *impl_;
    if (!d.view) return;
    try {
        // OCCT wants a native window. On Windows, the one Qt made the context
        // current on (0 asks for it); elsewhere Qt draws into a buffer, so the
        // top-level window (asking our own widget would make it native, which
        // QOpenGLWidget dislikes).
#if defined(_WIN32)
        const Aspect_Drawable native = nullptr;
#else
        const auto native = static_cast<Aspect_Drawable>(window()->winId());
#endif
        const double ratio = devicePixelRatioF();
        const bool first = d.view->Window().IsNull();
        // Called again when the panel moves to another window: OCCT takes the new context.
        if (!detail::attachContext(d.view, context()->format().profile() == QSurfaceFormat::CoreProfile,
                                   native, static_cast<int>(width() * ratio),
                                   static_cast<int>(height() * ratio))) {
            d.fail(*this, drawFailed());
            return;
        }
        if (first) {
            d.cube = new AIS_ViewCube();
            d.cube->SetSize(kCubeSize * ratio);
            d.cube->SetFontHeight(9 * ratio);
            d.cube->SetDrawAxes(false);  // the axes are drawn at the origin instead
            d.cube->SetViewAnimation(d.controller.ViewAnimation());
            d.cube->SetFixedAnimationLoop(false);
            d.cube->SetAutoStartAnimation(true);
            const int offset = static_cast<int>(kCubeOffset * ratio);
            d.cube->SetTransformPersistence(new Graphic3d_TransformPers(
                Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER, Graphic3d_Vec2i(offset, offset)));
            d.context->Display(d.cube, 0, 0, false);
        }
    } catch (const Standard_Failure&) {
        d.fail(*this, drawFailed());
    }
}

void ViewWidget::paintGL() {
    auto& d = *impl_;
    if (!d.view || d.view->Window().IsNull() || !d.problem.isEmpty()) return;
    try {
        if (!detail::useQtFramebuffer(d.view)) {
            d.fail(*this, drawFailed());
            return;
        }
        if (d.fitPending) d.fit();
        d.view->Invalidate();
        d.view->InvalidateImmediate();
        d.controller.FlushViewEvents(d.context, d.view, true);
        d.ready = true;
    } catch (const Standard_Failure&) {
        d.fail(*this, drawFailed());
    }
}

void ViewWidget::mousePressEvent(QMouseEvent* event) {
    QOpenGLWidget::mousePressEvent(event);
    emit activated();
    auto& d = *impl_;
    if (!d.view) return;
    d.clickAdds = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    const double ratio = devicePixelRatioF();
    const Graphic3d_Vec2i p(static_cast<int>(event->position().x() * ratio),
                            static_cast<int>(event->position().y() * ratio));
    if (d.controller.UpdateMouseButtons(p, toButtons(event->buttons()), toFlags(event->modifiers()), false))
        update();
}

void ViewWidget::mouseReleaseEvent(QMouseEvent* event) {
    QOpenGLWidget::mouseReleaseEvent(event);
    auto& d = *impl_;
    if (!d.view) return;
    const double ratio = devicePixelRatioF();
    const Graphic3d_Vec2i p(static_cast<int>(event->position().x() * ratio),
                            static_cast<int>(event->position().y() * ratio));
    if (d.controller.UpdateMouseButtons(p, toButtons(event->buttons()), toFlags(event->modifiers()), false))
        update();
}

void ViewWidget::mouseMoveEvent(QMouseEvent* event) {
    QOpenGLWidget::mouseMoveEvent(event);
    auto& d = *impl_;
    if (!d.view) return;
    const double ratio = devicePixelRatioF();
    const Graphic3d_Vec2i p(static_cast<int>(event->position().x() * ratio),
                            static_cast<int>(event->position().y() * ratio));
    if (d.controller.UpdateMousePosition(p, toButtons(event->buttons()), toFlags(event->modifiers()), false))
        update();
}

void ViewWidget::wheelEvent(QWheelEvent* event) {
    QOpenGLWidget::wheelEvent(event);
    auto& d = *impl_;
    if (!d.view) return;
    const double ratio = devicePixelRatioF();
    const Graphic3d_Vec2i p(static_cast<int>(event->position().x() * ratio),
                            static_cast<int>(event->position().y() * ratio));
    if (d.controller.UpdateMouseScroll(Aspect_ScrollDelta(p, event->angleDelta().y() / 8.0))) update();
}

void ViewWidget::leaveEvent(QEvent* event) {
    QOpenGLWidget::leaveEvent(event);
    if (impl_->context) {
        try {
            impl_->context->ClearDetected(false);
        } catch (const Standard_Failure&) {
        }
        update();
    }
}

}  // namespace kh::view
