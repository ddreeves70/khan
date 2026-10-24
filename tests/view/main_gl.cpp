// Runs the 3D view tests in a QApplication with a real screen and OpenGL
// (on Linux CI, a virtual X screen from xvfb-run with Mesa's software OpenGL).

#include <catch2/catch_session.hpp>

#include <QApplication>

#include "kh/view/Graphics.h"

int main(int argc, char** argv) {
    kh::view::prepareGraphics();
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
