// Include ECL headers FIRST, before any Qt headers
#include <ecl/ecl.h>

// Undefine slots macro to avoid conflict with ECL
#ifdef slots
#undef slots
#endif

// Now include application headers (which contain Qt headers)
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char **argv) {
#ifdef _WIN32
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
#else
    // Force Qt to use XCB before QApplication is created
    const char* session = std::getenv("XDG_SESSION_TYPE");

    if (session && std::strcmp(session, "wayland") == 0) {
        qDebug("Detected Wayland session → forcing xcb");
        qputenv("QT_QPA_PLATFORM", QByteArray("xcb"));
    }
#endif
    QApplication app(argc, argv);
    MainWindow w;

    // Pass command line args to window
    // if (argc > 1) {
    //     w.loadFileFromCommandLine(QString::fromUtf8(argv[1]));
    // }

    w.show();
    //w.showMaximized();
    return app.exec();
}
