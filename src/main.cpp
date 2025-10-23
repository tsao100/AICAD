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
