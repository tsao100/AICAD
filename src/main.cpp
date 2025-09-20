#include <QApplication>
#include "CadView.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    AICAD w;
    w.resize(800, 600);
    w.show();

    return app.exec();
}
