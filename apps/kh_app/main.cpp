#include <QApplication>

#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Khan"));
    QApplication::setOrganizationName(QStringLiteral("Khan"));

    kh::app::MainWindow window;
    window.show();
    return QApplication::exec();
}
