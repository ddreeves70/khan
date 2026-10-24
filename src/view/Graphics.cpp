#include "kh/view/Graphics.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include "OcctGl.h"

namespace kh::view {

void prepareGraphics() {
    // OCCT shares shaders and meshes between all views, so their contexts must share.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
#if defined(__linux__)
    // OCCT 7 draws through GLX, so Linux needs X11; Wayland desktops run it
    // through XWayland. Falls back to Wayland (and the flat view) if there's no X.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "xcb;wayland");
#endif
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
#if defined(__APPLE__)
    // macOS only offers OpenGL newer than 2.1 with a core profile.
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
#else
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
#endif
    QSurfaceFormat::setDefaultFormat(format);
}

QString graphicsProblem() {
    static const QString problem = []() -> QString {
        if (!qEnvironmentVariableIsEmpty("KH_NO_3D")) {
            return QCoreApplication::translate("kh::view",
                                               "The 3D view is turned off because KH_NO_3D is set. To "
                                               "turn it back on, remove KH_NO_3D and restart Khan.");
        }
        const QString platform = QGuiApplication::platformName();
        if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal")) {
            return QCoreApplication::translate(
                "kh::view", "The 3D view is off because Khan is running without a screen.");
        }
#if defined(__linux__)
        if (platform != QLatin1String("xcb")) {
            return QCoreApplication::translate(
                "kh::view",
                "The 3D view needs X11, and Khan couldn't reach it. Install XWayland (or log in to an X11 "
                "session), then restart Khan.");
        }
#endif
        const QString update = QCoreApplication::translate(
            "kh::view",
            "Update the graphics driver (in a virtual machine, turn on 3D acceleration), then restart Khan.");
        QOpenGLContext context;
        QOffscreenSurface surface;
        surface.create();
        if (!context.create() || !context.makeCurrent(&surface)) {
            return QCoreApplication::translate("kh::view",
                                               "The 3D view can't start: this computer's graphics "
                                               "don't offer OpenGL. ") +
                   update;
        }
        const auto f = context.format();
        context.doneCurrent();
        if (f.majorVersion() < 2 || (f.majorVersion() == 2 && f.minorVersion() < 1)) {
            return QCoreApplication::translate("kh::view",
                                               "The 3D view needs OpenGL 2.1 or newer, and this "
                                               "computer has OpenGL %1.%2. ")
                       .arg(f.majorVersion())
                       .arg(f.minorVersion()) +
                   update;
        }
        if (detail::sharedDriver().IsNull()) {
            return QCoreApplication::translate("kh::view", "The 3D view couldn't connect to the screen. ") +
                   update;
        }
        return {};
    }();
    return problem;
}

}  // namespace kh::view
