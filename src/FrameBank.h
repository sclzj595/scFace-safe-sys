// FrameBank.h —— 最新帧槽（覆盖式单槽，生产不阻塞消费）
// 与 SmartSurveillance 同模式：延迟上界 = 单帧处理耗时，杜绝队列堆积
#ifndef FRAMEBANK_H
#define FRAMEBANK_H

#include <opencv2/opencv.hpp>
#include <QMutex>
#include <QWaitCondition>
#include <QElapsedTimer>

class FrameBank
{
public:
    void store(const cv::Mat& frame) {
        _mutex.lock();
        frame.copyTo(_latest);
        _hasNew = true;
        _mutex.unlock();
        _cond.wakeAll();
    }

    // 带超时等待新帧（识别线程用，无忙轮询）；返回 false = 超时无新帧
    bool waitForNew(cv::Mat& out, unsigned long timeoutMs) {
        QMutexLocker locker(&_mutex);
        if (!_hasNew && !_cond.wait(&_mutex, timeoutMs))
            return false;
        _latest.copyTo(out);
        _hasNew = false;
        return !out.empty();
    }

    // 非阻塞取当前帧（渲染/注册用）
    bool load(cv::Mat& out) const {
        QMutexLocker locker(&_mutex);
        if (_latest.empty()) return false;
        _latest.copyTo(out);
        return true;
    }

private:
    mutable QMutex _mutex;
    QWaitCondition _cond;
    cv::Mat _latest;
    bool _hasNew = false;
};

#endif // FRAMEBANK_H
