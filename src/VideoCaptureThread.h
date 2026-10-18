// VideoCaptureThread.h —— 采集线程（USB 摄像头 → FrameBank）
// release 仅在线程循环退出后由本线程执行，无跨线程竞态
#ifndef VIDEOCAPTURETHREAD_H
#define VIDEOCAPTURETHREAD_H

#include <QThread>
#include <QDebug>
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
        // ⚠️ 套件硬约束（与 SS VideoSource 同款协商序列）：
        // 1. 显式 CAP_DSHOW——CAP_ANY 落到 MSMF，枚举慢且部分驱动黑帧
        // 2. 先设曝光再设 FOURCC——DSHOW 下设曝光会触发 capture pin 重连，
        //    媒体类型回退 YUY2（~1fps 卡顿）；顺序颠倒即"加载慢+黑帧"根因
        // 3. 显式 MJPG+固定分辨率/帧率+单帧缓冲——绕开缺省协商坏模式
        cv::VideoCapture cap;
        if (!cap.open(_index, cv::CAP_DSHOW)) {
            emit failed();
            return;
        }
        cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.75);   // 自动连续（0.25=手动菜单标志）
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        cap.set(cv::CAP_PROP_FRAME_WIDTH,  640);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        cap.set(cv::CAP_PROP_FPS,          30);
        cap.set(cv::CAP_PROP_BUFFERSIZE,   1);
        if (!cap.isOpened()) {
            emit failed();
            return;
        }
        // 协商核验：非 MJPG 立即告警（YUY2 → ~1fps 教训，SS VideoSource 同款）
        const qint64 fcc = static_cast<qint64>(cap.get(cv::CAP_PROP_FOURCC));
        char fccStr[5] = { char(fcc & 0xFF), char((fcc >> 8) & 0xFF),
                           char((fcc >> 16) & 0xFF), char((fcc >> 24) & 0xFF), 0 };
        if (qstrncmp(fccStr, "MJPG", 4) != 0)
            qWarning() << "[FaceGate] MJPG negotiation FAILED, got" << fccStr
                       << "—— 画面可能黑帧/低帧率";
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
