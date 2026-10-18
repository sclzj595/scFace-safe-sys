// main.cpp —— 人脸闸机门禁系统入口
#include <QApplication>
#include <opencv2/opencv.hpp>   // CAP_PROP_* 常量（mvcore::FrameBank 仅传递 core）
#include <mvcore/Theme.h>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("FaceGate");
    app.setOrganizationName("scsys");

    MainWindow w;
    mvcore::theme::apply(&w);   // 套件统一深色主题（令牌 QSS + DWM 暗色标题栏）
    w.resize(1080, 620);
    w.show();
    return app.exec();
}
