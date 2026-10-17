// MainWindow.cpp —— 闸机业务编排
#include "MainWindow.h"

#include "PermissionService.h"
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QListWidget>
#include <QTableWidget>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSerialPortInfo>
#include <QTimer>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QPainter>
#include <QHeaderView>

namespace {
const char* kModelFile   = "gate_model.yml";
const char* kPersonsFile = "gate_persons.json";
const char* kClearanceNames[] = { "禁用", "访客", "员工", "管理员" };

// 识别结果 → 0~100 相似度已在算法库内给出；此处只做阈值过滤展示
QString hitLabel(const falgo::FaceHit& h)
{
    if (h.known) return QStringLiteral("已登记");
    if (h.similarity >= 0) return QStringLiteral("陌生人 %1%").arg(h.similarity, 0, 'f', 0);
    return QStringLiteral("陌生人");
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    buildUi();

    // 档案与算法模型加载（label 两侧一致）
    _store.load(kPersonsFile);
    if (!_algo.loadCascade("resources/haarcascade_frontalface_default.xml"))
        appendLog(QStringLiteral("级联加载失败，人脸功能不可用"));
    if (_algo.loadModel(kModelFile))
        appendLog(QStringLiteral("已加载人脸模型与档案（%1 人）").arg(_store.all().size()));
    else
        appendLog(QStringLiteral("无历史模型，请先注册人脸"));

    refreshPersonTable();
    startPipeline();
}

MainWindow::~MainWindow()
{
    if (_renderTimer) _renderTimer->stop();
    if (_recognizer) {
        _recognizer->shutdown();
        _recognizer->wait(2000);
    }
    if (_capture) {
        _capture->stopCapture();
        _capture->wait(2000);
    }
    _gate.close();
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("人脸闸机门禁系统（组件化算法库 FaceAlgorithm.dll）"));

    // 左：视频预览
    _video = new QLabel(this);
    _video->setMinimumSize(640, 480);
    _video->setAlignment(Qt::AlignCenter);
    _video->setStyleSheet("background:#111; color:#888;");

    // 右：控制面板
    auto* right = new QWidget(this);
    auto* rightLay = new QVBoxLayout(right);

    // 闸机设备组
    auto* gateBox = new QGroupBox(QStringLiteral("闸机控制"), right);
    auto* gateLay = new QFormLayout(gateBox);
    _portCombo = new QComboBox(gateBox);
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts())
        _portCombo->addItem(QStringLiteral("%1 (%2)")
                                .arg(info.portName(), info.description()));
    _connectBtn = new QPushButton(QStringLiteral("连接"), gateBox);
    _gateState = new QLabel(QStringLiteral("模拟模式（未连接串口）"), gateBox);
    _gateState->setStyleSheet("color:#c80; font-weight:bold;");
    gateLay->addRow(QStringLiteral("串口"), _portCombo);
    gateLay->addRow(QString(), _connectBtn);
    gateLay->addRow(QStringLiteral("闸机状态"), _gateState);
    rightLay->addWidget(gateBox);

    connect(_connectBtn, &QPushButton::clicked, this, [this]() {
        if (_gate.isConnected()) {
            _gate.close();
        } else if (_portCombo->currentIndex() >= 0) {
            _gate.open(_portCombo->currentText().section(' ', 0, 0));
        }
        _connectBtn->setText(_gate.isConnected() ? QStringLiteral("断开")
                                                 : QStringLiteral("连接"));
        _gateState->setText(_gate.isConnected()
            ? QStringLiteral("串口已连接，真实开闸") : QStringLiteral("模拟模式（未连接串口）"));
        _gateState->setStyleSheet(_gate.isConnected()
            ? "color:#080; font-weight:bold;" : "color:#c80; font-weight:bold;");
    });
    connect(&_gate, &SerialGate::gateOpened, this, [this]() {
        _gateState->setText(_gate.isConnected()
            ? QStringLiteral("闸机已开（继电器吸合）") : QStringLiteral("闸机已开（模拟）"));
        appendLog(QStringLiteral(">> 开闸"));
    });
    connect(&_gate, &SerialGate::gateClosed, this, [this]() {
        _gateState->setText(_gate.isConnected()
            ? QStringLiteral("串口已连接，待通行") : QStringLiteral("模拟模式（未连接串口）"));
        appendLog(QStringLiteral("<< 关闸"));
    });
    connect(&_gate, &SerialGate::errorText, this, [this](const QString& m) {
        appendLog(QStringLiteral("串口错误：%1").arg(m));
    });

    // 通行策略组
    auto* policyBox = new QGroupBox(QStringLiteral("通行策略"), right);
    auto* policyLay = new QFormLayout(policyBox);
    _requiredCombo = new QComboBox(policyBox);
    _requiredCombo->addItems({ QStringLiteral("访客及以上"), QStringLiteral("员工及以上"),
                               QStringLiteral("仅管理员") });
    _thresholdSpin = new QSpinBox(policyBox);
    _thresholdSpin->setRange(40, 90);
    _thresholdSpin->setValue(55);
    _thresholdSpin->setSuffix(" %");
    policyLay->addRow(QStringLiteral("通行要求"), _requiredCombo);
    policyLay->addRow(QStringLiteral("识别阈值"), _thresholdSpin);
    rightLay->addWidget(policyBox);

    connect(_requiredCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i) { _requiredClearance = i + 1; });
    connect(_thresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) { _algo.setKnownThreshold(v); });

    // 注册与人员管理组
    auto* personBox = new QGroupBox(QStringLiteral("人员管理"), right);
    auto* personLay = new QVBoxLayout(personBox);
    _registerBtn = new QPushButton(QStringLiteral("注册人脸"), personBox);
    personLay->addWidget(_registerBtn);

    _personTable = new QTableWidget(0, 3, personBox);
    _personTable->setHorizontalHeaderLabels({ QStringLiteral("姓名"),
        QStringLiteral("权限"), QStringLiteral("样本") });
    _personTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    _personTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _personTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    personLay->addWidget(_personTable, 1);
    rightLay->addWidget(personBox, 1);

    connect(_registerBtn, &QPushButton::clicked, this, &MainWindow::onRegisterClicked);

    // 签到日志组
    auto* logBox = new QGroupBox(QStringLiteral("签到 / 通行记录"), right);
    auto* logLay = new QVBoxLayout(logBox);
    _logList = new QListWidget(logBox);
    logLay->addWidget(_logList);
    rightLay->addWidget(logBox, 1);

    auto* central = new QWidget(this);
    auto* lay = new QHBoxLayout(central);
    lay->addWidget(_video, 3);
    lay->addWidget(right, 2);
    setCentralWidget(central);
}

void MainWindow::startPipeline()
{
    _bank = new FrameBank;
    _capture = new VideoCaptureThread(0, _bank, this);
    connect(_capture, &VideoCaptureThread::failed, this, [this]() {
        appendLog(QStringLiteral("摄像头打开失败（index=0）"));
    });
    _capture->start();

    qRegisterMetaType<falgo::FrameResult>("falgo::FrameResult");
    _recognizer = new RecognizeThread(_bank, &_algo, this);
    connect(_recognizer, &RecognizeThread::recognized,
            this, &MainWindow::onRecognized);   // 队列投递回 GUI
    _recognizer->start();

    _renderTimer = new QTimer(this);
    connect(_renderTimer, &QTimer::timeout, this, &MainWindow::renderFrame);
    _renderTimer->start(33);
}

// ---- 识别结果 → 权限判定 → 开闸/日志（业务核心）----
void MainWindow::onRecognized(const falgo::FrameResult& result)
{
    _lastResult = result;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const falgo::FaceHit& h : result.faces) {
        const PermissionService::Decision d =
            PermissionService::decide(h, _store, _requiredClearance);
        const QString who = h.known
            ? (_store.findByLabel(h.label) ? _store.findByLabel(h.label)->name
                                           : QStringLiteral("label%1").arg(h.label))
            : QStringLiteral("陌生人");

        // 节流：同人 3s 一条日志/一次开闸，避免连续帧刷屏
        if (!throttled(QStringLiteral("gate:%1").arg(who), now, 3000))
            continue;

        appendLog(QStringLiteral("[%1] %2： %3")
            .arg(QTime::currentTime().toString("HH:mm:ss"), who, d.reason));
        _log.append(who, h.label, h.similarity, d.granted, d.reason);

        if (d.granted)
            _gate.pulseOpen(3000);
    }
}

// ---- 注册（两段式：异步检测 → 弹窗 → GUI 同步入库）----
void MainWindow::onRegisterClicked()
{
    _registerBtn->setEnabled(false);
    appendLog(QStringLiteral("正在检测人脸…"));

    auto* watcher = new QFutureWatcher<falgo::FaceCrop>(this);
    connect(watcher, &QFutureWatcher<falgo::FaceCrop>::finished, this,
        [this, watcher]() {
            watcher->deleteLater();
            const falgo::FaceCrop crop = watcher->result();
            if (crop.gray100.empty()) {
                appendLog(QStringLiteral("未检测到可注册的人脸"));
                _registerBtn->setEnabled(true);
                return;
            }

            // 弹窗：预览 + 姓名 + 初始权限
            QDialog dlg(this);
            dlg.setWindowTitle(QStringLiteral("注册人员"));
            auto* lay = new QVBoxLayout(&dlg);
            auto* preview = new QLabel(&dlg);
            const QImage previewImg(crop.bgr.data, crop.bgr.cols, crop.bgr.rows,
                crop.bgr.step, QImage::Format_BGR888);
            preview->setPixmap(QPixmap::fromImage(previewImg)
                .scaled(240, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            preview->setAlignment(Qt::AlignCenter);
            lay->addWidget(preview);

            auto* nameEdit = new QLineEdit(&dlg);
            // 建议名"用户N"：跳过已占用编号
            for (int i = 1; ; ++i) {
                const QString cand = QStringLiteral("用户%1").arg(i);
                if (!_store.findByName(cand)) { nameEdit->setText(cand); break; }
            }
            auto* clearanceCombo = new QComboBox(&dlg);
            clearanceCombo->addItems({ QStringLiteral("访客"), QStringLiteral("员工"),
                                       QStringLiteral("管理员") });
            auto* form = new QFormLayout;
            form->addRow(QStringLiteral("姓名"), nameEdit);
            form->addRow(QStringLiteral("权限"), clearanceCombo);
            lay->addLayout(form);

            auto* btns = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("注册"));
            btns->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
            connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            lay->addWidget(btns);

            if (dlg.exec() != QDialog::Accepted) {
                _registerBtn->setEnabled(true);
                return;
            }

            // 入库（GUI 线程同步：<50ms，规避与识别线程的档案读写竞态）
            const QString name = nameEdit->text().trimmed();
            if (name.isEmpty()) {
                appendLog(QStringLiteral("姓名为空，取消注册"));
                _registerBtn->setEnabled(true);
                return;
            }
            Person& p = _store.ensurePerson(name, clearanceCombo->currentIndex() + 1);
            _algo.addSample(crop.gray100, p.label);
            _algo.saveModel(kModelFile);
            _store.save(kPersonsFile);

            refreshPersonTable();
            appendLog(QStringLiteral("注册成功：%1（label=%2，权限=%3，样本=%4）")
                .arg(name).arg(p.label)
                .arg(QString::fromUtf8(kClearanceNames[p.clearance]))
                .arg(_algo.sampleCount(p.label)));
            _registerBtn->setEnabled(true);
        });
    watcher->setFuture(QtConcurrent::run([this]() -> falgo::FaceCrop {
        cv::Mat frame;
        if (!_bank->load(frame))
            return falgo::FaceCrop{};
        falgo::FaceCrop crop;
        _algo.detectLargest(frame, crop);
        return crop;
    }));
}

// ---- 人员表 ----
void MainWindow::refreshPersonTable()
{
    _personTable->setRowCount(_store.all().size());
    for (int row = 0; row < _store.all().size(); ++row) {
        const Person& p = _store.all()[row];
        _personTable->setItem(row, 0, new QTableWidgetItem(p.name));

        auto* combo = new QComboBox(_personTable);
        combo->addItems({ QStringLiteral("禁用"), QStringLiteral("访客"),
                          QStringLiteral("员工"), QStringLiteral("管理员") });
        combo->setCurrentIndex(p.clearance);
        const int label = p.label;
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, label](int idx) {
                    _store.setClearance(label, idx);
                    _store.save(kPersonsFile);
                    appendLog(QStringLiteral("权限更新：label=%1 → %2")
                        .arg(label).arg(QString::fromUtf8(kClearanceNames[idx])));
                });
        _personTable->setCellWidget(row, 1, combo);
        _personTable->setItem(row, 2,
            new QTableWidgetItem(QString::number(_algo.sampleCount(p.label))));
    }
}

// ---- 渲染节拍（GUI 线程仅取帧 + 绘制，零 CV 计算）----
void MainWindow::renderFrame()
{
    cv::Mat frame;
    if (!_bank->load(frame)) return;

    QImage img(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_BGR888);
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);

    for (const falgo::FaceHit& h : _lastResult.faces) {
        const QRect qr(h.rect.x, h.rect.y, h.rect.width, h.rect.height);
        painter.setPen(QPen(h.known ? QColor(0, 220, 0) : QColor(240, 200, 0), 2));
        painter.drawRect(qr);
        const Person* p = h.known ? _store.findByLabel(h.label) : nullptr;
        const QString text = p ? p->name : hitLabel(h);
        painter.setPen(h.known ? Qt::green : Qt::yellow);
        painter.drawText(qr.adjusted(0, -16, 0, 0), text);
    }

    if (_gate.isOpen()) {   // 闸机开闸横幅
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(200, 30, 30, 200));
        painter.drawRect(0, 0, img.width(), 34);
        painter.setPen(Qt::white);
        painter.drawText(img.rect().adjusted(0, 6, 0, -img.height() + 28),
                         Qt::AlignHCenter, QStringLiteral("放 行 中"));
    }
    painter.end();

    _video->setPixmap(QPixmap::fromImage(img).scaled(
        _video->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// ---- 杂项 ----
void MainWindow::appendLog(const QString& line)
{
    _logList->insertItem(0, line);
    while (_logList->count() > 100)
        _logList->takeItem(_logList->count() - 1);
}

bool MainWindow::throttled(const QString& key, qint64 nowMs, qint64 windowMs)
{
    const auto it = _throttle.find(key);
    if (it != _throttle.end() && nowMs - it.value() < windowMs)
        return false;
    _throttle.insert(key, nowMs);
    return true;
}
