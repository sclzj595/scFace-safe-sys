// main.cpp —— 人脸闸机门禁系统入口
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("FaceGate");
    app.setOrganizationName("scsys");

    MainWindow w;
    w.resize(1080, 620);
    w.show();
    return app.exec();
}
