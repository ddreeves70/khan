#include <QApplication>
#include <QPixmap>
#include <QStringList>

#include <cstdio>

#include "MainWindow.h"

namespace {

// `kh_app --smoke-test`: builds the main window, checks every panel is there and
// that a saved layout restores, then quits. CI runs it with
// QT_QPA_PLATFORM=offscreen, so it needs no screen.
int smokeTest(kh::app::MainWindow& window) {
    window.show();
    QApplication::processEvents();

    const QStringList expected = {QStringLiteral("kh.featuretree"), QStringLiteral("kh.properties"),
                                  QStringLiteral("kh.timeline"), QStringLiteral("kh.viewer.main")};
    if (window.dockIds() != expected) {
        std::fprintf(stderr, "smoke test: panels are [%s], expected [%s]\n",
                     qPrintable(window.dockIds().join(QStringLiteral(", "))),
                     qPrintable(expected.join(QStringLiteral(", "))));
        return 1;
    }
    const QByteArray layout = window.saveLayout();
    if (layout.isEmpty() || !window.restoreLayout(layout)) {
        std::fprintf(stderr, "smoke test: the layout didn't save and restore\n");
        return 1;
    }
    if (window.restoreLayout(QByteArray("not a layout"))) {
        std::fprintf(stderr, "smoke test: a damaged layout was accepted\n");
        return 1;
    }
    std::printf("kh_app smoke test: %lld panels, layout saved and restored\n",
                static_cast<long long>(expected.size()));
    return 0;
}

// `kh_app --screenshot <file.png>`: shows the main window and saves a picture of
// it, for the user's guide. Works without a screen (QT_QPA_PLATFORM=offscreen).
int screenshot(kh::app::MainWindow& window, const QString& file) {
    window.resize(1280, 800);
    window.show();
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
    if (!window.grab().save(file)) {
        std::fprintf(stderr, "Khan couldn't save the picture %s. Check the folder exists.\n",
                     qPrintable(file));
        return 1;
    }
    std::printf("wrote %s\n", qPrintable(file));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Khan"));
    QApplication::setOrganizationName(QStringLiteral("Khan"));

    kh::app::MainWindow window;
    const QStringList args = QApplication::arguments();
    if (args.contains(QStringLiteral("--smoke-test"))) return smokeTest(window);
    if (const auto i = args.indexOf(QStringLiteral("--screenshot")); i > 0 && i + 1 < args.size()) {
        return screenshot(window, args.at(i + 1));
    }

    window.show();
    return QApplication::exec();
}
