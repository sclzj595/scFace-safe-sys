// VideoCaptureThread.h —— 采集线程（USB 摄像头 → FrameBank）
// release 仅在线程循环退出后由本线程执行，无跨线程竞态
#ifndef VIDEOCAPTURETHREAD_H
#define VIDEOCAPTURETHREAD_H

#include <QThread>
#include <atomic>
#include <opencv2/opencv.hpp>   // VideoCapture / CAP_PROP_*（mvcore::FrameBank 仅传递 core）
#include <mvcore/FrameBank.h>

// FrameBank 统一使用套件共享库 mvcore（与 SmartSurveillance 同一份实现）
using mvcore::FrameBank;

class VideoCaptureThread : public QThread
{
    Q_OBJECT
public:
    explicit VideoCaptureThread(int index, FrameBank* bank, QObject* parent = nullptr)
        : QThread(parent), _index(index), _bank(bank) {}

    void stopCapture() { _running = false; }

    void run() override {
        cv::VideoCapture cap(_index);
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        if (!cap.isOpened()) {
            emit failed();
            return;
        }
        cv::Mat f;
        while (_running) {
            if (!cap.read(f) || f.empty()) { msleep(10); continue; }
            _bank->store(f);
            msleep(10);
        }
        cap.release();
    }

signals:
    void failed();

private:
    std::atomic_bool _running{true};
    int _index;
    FrameBank* _bank;
};

#endif // VIDEOCAPTURETHREAD_H
