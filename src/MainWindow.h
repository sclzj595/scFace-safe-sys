// MainWindow.h —— 闸机主界面与业务编排（识别 → 权限 → 开闸 → 签到日志）
#ifndef GATEMAINWINDOW_H
#define GATEMAINWINDOW_H

#pragma execution_character_set("utf-8")

#include <QMainWindow>
#include <QHash>
#include <QImage>
#include <memory>
#include <facealgorithm/FaceAlgorithm.h>
#include <mvcore/FrameBank.h>
#include <mvcore/PathResolver.h>

// FrameBank 统一使用套件共享库 mvcore（与 SmartSurveillance 同一份实现）
using mvcore::FrameBank;
#include "VideoCaptureThread.h"
#include "RecognizeThread.h"
#include "SerialGate.h"
#include "PersonStore.h"
#include "CheckInLog.h"
#include "EventPublisher.h"

class QLabel;
class QTableWidget;
class QComboBox;
class QSpinBox;
class QPushButton;
class QListWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRecognized(const falgo::FrameResult& result);
    void onRegisterClicked();

private:
    void buildUi();
    void startPipeline();
    void refreshPersonTable();
    void appendLog(const QString& line, bool highlight = false);   // highlight=红色醒目行
    void renderFrame();                      // 渲染节拍（GUI 线程，仅绘制）
    bool throttled(const QString& key, qint64 nowMs, qint64 windowMs);
    void updateModeBadge();                  // 4c：模拟/真实模式徽标单点刷新
    void initGateProtocol();                 // 4b：gate.ini [Serial] 协议注入
    void initEventPublisher();               // 4d：MediaLink 事件发布（gate.ini [Server] 开关）
    void publishGateEvent(const QString& name, int label, double similarity,
                          bool granted, const QString& reason);
    void onEntrySaved(const QString& name, int label, int clearance, int samples);

    // 管线
    FrameBank* _bank = nullptr;
    VideoCaptureThread* _capture = nullptr;
    RecognizeThread* _recognizer = nullptr;
    falgo::FaceAlgorithm _algo;              // 共享算法库实例（闸机 = 监控同款 DLL）
    QTimer* _renderTimer = nullptr;
    falgo::FrameResult _lastResult;

    // 业务
    PersonStore _store;
    SerialGate _gate;
    CheckInLog _log{mvcore::PathResolver::dataPath(QStringLiteral("logs"))};   // 锚定 exe 目录
    std::unique_ptr<EventPublisher> _publisher;   // 4d：initEventPublisher 按开关构建
    int _requiredClearance = 1;              // 通行要求：1=访客 2=员工 3=管理员
    QHash<QString, qint64> _throttle;        // 日志/开闸节流（3s）

    // UI
    QLabel* _video = nullptr;
    QLabel* _gateState = nullptr;
    QLabel* _modeBadge = nullptr;            // 4c：模拟=橙 / 真实=绿（端口名）
    QPushButton* _registerBtn = nullptr;
    QTableWidget* _personTable = nullptr;
    QComboBox* _portCombo = nullptr;
    QPushButton* _connectBtn = nullptr;
    QComboBox* _requiredCombo = nullptr;
    QSpinBox* _thresholdSpin = nullptr;
    QListWidget* _logList = nullptr;
};

#endif // GATEMAINWINDOW_H
