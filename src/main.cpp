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
    w.show();
    return app.exec();
}
