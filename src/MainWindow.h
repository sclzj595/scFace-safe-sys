// MainWindow.h —— 闸机主界面与业务编排（识别 → 权限 → 开闸 → 签到日志）
#ifndef GATEMAINWINDOW_H
#define GATEMAINWINDOW_H

#pragma execution_character_set("utf-8")

#include <QMainWindow>
#include <QHash>
#include <QImage>
#include <facealgorithm/FaceAlgorithm.h>
#include "FrameBank.h"
#include "VideoCaptureThread.h"
#include "RecognizeThread.h"
#include "SerialGate.h"
#include "PersonStore.h"
#include "CheckInLog.h"

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
    void appendLog(const QString& line);
    void renderFrame();                      // 渲染节拍（GUI 线程，仅绘制）
    bool throttled(const QString& key, qint64 nowMs, qint64 windowMs);

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
    CheckInLog _log;
    int _requiredClearance = 1;              // 通行要求：1=访客 2=员工 3=管理员
    QHash<QString, qint64> _throttle;        // 日志/开闸节流（3s）

    // UI
    QLabel* _video = nullptr;
    QLabel* _gateState = nullptr;
    QPushButton* _registerBtn = nullptr;
    QTableWidget* _personTable = nullptr;
    QComboBox* _portCombo = nullptr;
    QPushButton* _connectBtn = nullptr;
    QComboBox* _requiredCombo = nullptr;
    QSpinBox* _thresholdSpin = nullptr;
    QListWidget* _logList = nullptr;
};

#endif // GATEMAINWINDOW_H
