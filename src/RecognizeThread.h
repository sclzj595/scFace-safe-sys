// RecognizeThread.h —— 识别线程（Active Object：等新帧 → FaceAlgorithm → 队列信号）
// 业务规则（权限/开闸/日志）不在此线程，只发结构化结果给 GUI 判定
#ifndef RECOGNIZETHREAD_H
#define RECOGNIZETHREAD_H

#include <QThread>
#include <QMetaType>
#include <atomic>
#include <facealgorithm/FaceAlgorithm.h>
#include "FrameBank.h"

class RecognizeThread : public QThread
{
    Q_OBJECT
public:
    explicit RecognizeThread(FrameBank* bank, falgo::FaceAlgorithm* algo, QObject* parent = nullptr)
        : QThread(parent), _bank(bank), _algo(algo) {}

    void shutdown() { _running = false; }

    void run() override {
        cv::Mat frame;
        while (_running) {
            if (!_bank->waitForNew(frame, 200))
                continue;                       // 5Hz 节流超时兜底
            const auto r = _algo->detectAndRecognize(frame);
            emit recognized(r);
        }
    }

signals:
    void recognized(const falgo::FrameResult& result);

private:
    std::atomic_bool _running{true};
    FrameBank* _bank;
    falgo::FaceAlgorithm* _algo;
};

Q_DECLARE_METATYPE(falgo::FrameResult)

#endif // RECOGNIZETHREAD_H
