// RecognizeThread.h —— 识别线程（Active Object：等新帧 → FaceAlgorithm → 队列信号）
// 业务规则（权限/开闸/日志）不在此线程，只发结构化结果给 GUI 判定
#ifndef RECOGNIZETHREAD_H
#define RECOGNIZETHREAD_H

#include <QThread>
#include <QMetaType>
#include <atomic>
#include <facealgorithm/FaceAlgorithm.h>
#include <mvcore/FrameBank.h>

// FrameBank 统一使用套件共享库 mvcore（版本号语义：waitForNew 按版本等待，
// 采集快于识别时自动跳帧到最新，识别慢不拖累采集）
using mvcore::FrameBank;

class RecognizeThread : public QThread
{
    Q_OBJECT
public:
    explicit RecognizeThread(FrameBank* bank, falgo::FaceAlgorithm* algo, QObject* parent = nullptr)
        : QThread(parent), _bank(bank), _algo(algo) {}

    void shutdown() { _running = false; }

    void run() override {
        cv::Mat frame;
        int seen = 0;                      // 版本游标（mvcore FrameBank 版本从 1 起步）
        int newVer = seen;
        while (_running) {
            if (!_bank->waitForNew(seen, 200, frame, newVer))
                continue;                  // 节流超时兜底（断流/暂停）
            seen = newVer;
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
