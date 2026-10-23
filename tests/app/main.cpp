// Runs the app tests inside a QApplication, without a screen.

#include <catch2/catch_session.hpp>

#include <QApplication>

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
