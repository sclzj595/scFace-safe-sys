// MainWindow.cpp —— 闸机业务编排
#include "MainWindow.h"
#include <mvcore/Theme.h>

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
#include <QSettings>
#include <mvcore/PathResolver.h>
#include <opencv2/imgproc.hpp>

namespace {
const char* kClearanceNames[] = { "禁用", "访客", "员工", "管理员" };

// 路径锚定 exe 目录（套件硬约束，mvcore::PathResolver 单点）：
// 任意方式启动（IDE/双击/终端），模型/档案/日志/配置全部稳定落位 build/bin
QString modelFile()   { return mvcore::PathResolver::dataPath(QStringLiteral("gate_model.yml")); }
QString personsFile() { return mvcore::PathResolver::dataPath(QStringLiteral("gate_persons.json")); }
QString gateIniFile() { return mvcore::PathResolver::dataPath(QStringLiteral("gate.ini")); }

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

    // 4b：串口协议注入（gate.ini [Serial]，缺省 = 默认板卡 0xA0 帧）
    initGateProtocol();
    // 4d：MediaLink 事件发布（gate.ini [Server] enabled 开关，缺省关闭）
    initEventPublisher();
    updateModeBadge();

    // 档案与算法模型加载（label 两侧一致）
    _store.load(personsFile());
    if (!_algo.loadCascade(mvcore::PathResolver::resource(
            QStringLiteral("resources/haarcascade_frontalface_default.xml")).toStdString()))
        appendLog(QStringLiteral("级联加载失败，人脸功能不可用"), true);
    if (_algo.loadModel(modelFile().toStdString()))
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
    const QString title = QStringLiteral("人脸闸机门禁系统（组件化算法库 FaceAlgorithm.dll）");
    setWindowTitle(title);

    // macOS 风格标题栏：frameless + 左置三灯（mvcore::mactitlebar 单点，SS 后续迁移）
    _titleBar = mvcore::mactitlebar::attach(this, title);

    // 左：视频预览（QSS videoLabel：恒黑 + 描边圆角）
    _video = new QLabel(this);
    _video->setObjectName(QStringLiteral("videoLabel"));
    _video->setMinimumSize(640, 480);
    _video->setAlignment(Qt::AlignCenter);

    // 右：控制面板
    auto* right = new QWidget(this);
    auto* rightLay = new QVBoxLayout(right);

    // 4c：运行模式徽标——模拟=琥珀，真实=绿（QSS modeBadge[mode=...]，一眼可辨）
    _modeBadge = new QLabel(right);
    _modeBadge->setObjectName(QStringLiteral("modeBadge"));
    _modeBadge->setAlignment(Qt::AlignCenter);
    rightLay->addWidget(_modeBadge);

    // 闸机设备组
    auto* gateBox = new QGroupBox(QStringLiteral("闸机控制"), right);
    auto* gateLay = new QFormLayout(gateBox);
    _portCombo = new QComboBox(gateBox);
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts())
        _portCombo->addItem(QStringLiteral("%1 (%2)")
                                .arg(info.portName(), info.description()));
    _connectBtn = new QPushButton(QStringLiteral("连接"), gateBox);
    _gateState = new QLabel(QStringLiteral("模拟模式（未连接串口）"), gateBox);
    _gateState->setObjectName(QStringLiteral("stateLabel"));
    _gateState->setProperty("state", QStringLiteral("warn"));
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
        setStateLabel(_gate.isConnected() ? QStringLiteral("ok") : QStringLiteral("warn"));
        updateModeBadge();   // 4c：徽标随连接状态刷新
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
        // 4b：串口异常显式升级——红色醒目 + 状态置红提示重连
        appendLog(QStringLiteral("串口错误：%1").arg(m), true);
        if (!_gate.isConnected()) {
            _gateState->setText(QStringLiteral("串口异常，请重连"));
            setStateLabel(QStringLiteral("danger"));
        }
        updateModeBadge();
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
    _registerBtn->setObjectName(QStringLiteral("registerBtn"));
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

    // 外层装配：标题栏 row0 + 内容 row1（attach 的缩放命中带要求 margins=0 贴边）
    auto* outer = new QWidget(this);
    auto* outerLay = new QVBoxLayout(outer);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->setSpacing(0);
    outerLay->addWidget(_titleBar);
    outerLay->addWidget(central, 1);
    setCentralWidget(outer);
}

// ---- 4b：gate.ini [Serial] 协议注入（换板卡改配置不改代码）----
// 缺省键值 = 默认 0xA0 帧；hex 键（head/addr/cmd_on/cmd_off/channel）不合法时保留缺省并告警
void MainWindow::initGateProtocol()
{
    GateProtocol proto = _gate.protocol();   // 从缺省起步
    QSettings ini(gateIniFile(), QSettings::IniFormat);
    ini.beginGroup("Serial");
    auto hexKey = [&](const char* key, unsigned char& dst, bool& anyBad) {
        const QString v = ini.value(key).toString().trimmed();
        if (v.isEmpty()) return;
        bool ok = false;
        const uint n = v.toUInt(&ok, 16);
        if (!ok || n > 0xFF) { anyBad = true; return; }
        dst = static_cast<unsigned char>(n);
    };
    bool anyBad = false;
    hexKey("head", proto.head, anyBad);
    hexKey("addr", proto.addr, anyBad);
    hexKey("cmd_on", proto.cmdOn, anyBad);
    hexKey("cmd_off", proto.cmdOff, anyBad);
    hexKey("channel", proto.channel, anyBad);
    if (ini.contains("baud")) {
        bool ok = false;
        const int b = ini.value("baud").toInt(&ok);
        if (ok && b > 0) proto.baud = b; else anyBad = true;
    }
    ini.endGroup();
    if (anyBad)
        appendLog(QStringLiteral("gate.ini [Serial] 存在非法键值，已回退缺省协议帧"), true);
    _gate.setProtocol(proto);
}

// ---- 4d：MediaLink 事件发布（gate.ini [Server] enabled，缺省 false）----
void MainWindow::initEventPublisher()
{
    QSettings ini(gateIniFile(), QSettings::IniFormat);
    const bool enabled = ini.value("Server/enabled", false).toBool();
    if (!enabled)
        return;

    _publisher = std::make_unique<EventPublisher>(true);
    if (_publisher->httpUp() || _publisher->wsUp()) {
        appendLog(QStringLiteral("事件发布已启用：HTTP:%1 WS:%2（契约 v0.3.0）")
            .arg(_publisher->httpPort()).arg(_publisher->wsPort()));
    } else {
        // 端口被占等故障显式留痕——静默失败会让客户端"看不见闸机"
        appendLog(QStringLiteral("事件发布启用失败：%1").arg(_publisher->lastLog()), true);
        _publisher.reset();
    }
}

void MainWindow::publishGateEvent(const QString& name, int label, double similarity,
                                  bool granted, const QString& reason)
{
    if (!_publisher || !_publisher->enabled())
        return;
    _publisher->publish(name, label, similarity, granted, reason);
}

// ---- 4c：模式徽标单点刷新 ----
void MainWindow::updateModeBadge()
{
    const bool sim = _gate.isSimulated();
    _modeBadge->setText(sim
        ? QStringLiteral("模拟模式（未连接继电器，开闸仅为时序演示）")
        : QStringLiteral("真实模式 · %1（继电器指令实时下发）").arg(_gate.protocol().baud));
    // 安全语义经动态属性交由 mvcore 主题着色（禁散落色值）
    _modeBadge->setProperty("mode", sim ? QStringLiteral("sim") : QStringLiteral("real"));
    _modeBadge->style()->unpolish(_modeBadge);
    _modeBadge->style()->polish(_modeBadge);
}

// ---- 状态行语义色（stateLabel[state=ok|warn|danger] → QSS）----
void MainWindow::setStateLabel(const QString& state)
{
    _gateState->setProperty("state", state);
    _gateState->style()->unpolish(_gateState);
    _gateState->style()->polish(_gateState);
}

void MainWindow::startPipeline()
{
    _bank = new FrameBank;
    _capture = new VideoCaptureThread(0, _bank, this);
    connect(_capture, &VideoCaptureThread::failed, this, [this]() {
        appendLog(QStringLiteral("摄像头打开失败（index=0）"), true);
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

        // 4b：未授权分支醒目红字——拒绝必须可追溯（含原因），且绝不触发开门
        appendLog(QStringLiteral("[%1] %2： %3")
            .arg(QTime::currentTime().toString("HH:mm:ss"), who, d.reason), !d.granted);
        _log.append(who, h.label, h.similarity, d.granted, d.reason);

        // 4d：通行/拒绝事件对外发布（契约 v0.3.0 gate_pass/gate_denied）
        publishGateEvent(who, h.label, h.similarity, d.granted, d.reason);

        // 权限红线：只有 granted 才下发开门命令——串口层收不到未授权指令帧
        if (d.granted)
            _gate.pulseOpen(3000);
    }
}

// ---- 注册（三段式：异步检测 → 弹窗确认 → 异步入库+回滚保护）----
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

            // 4a 质量校验：归一化灰度脸须有足够纹理（拒绝纯色/过暗帧入库，
            // 避免低质量样本拉低整体识别区分度）
            {
                cv::Scalar mean, stddev;
                cv::meanStdDev(crop.gray100, mean, stddev);
                if (stddev[0] < 25.0) {
                    appendLog(QStringLiteral("人脸质量不合格（画面过暗/无纹理，标准差 %1 < 25），请改善光照后重试")
                        .arg(stddev[0], 0, 'f', 0), true);
                    _registerBtn->setEnabled(true);
                    return;
                }
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

            const QString name = nameEdit->text().trimmed();
            if (name.isEmpty()) {
                appendLog(QStringLiteral("姓名为空，取消注册"));
                _registerBtn->setEnabled(true);
                return;
            }

            // 4a 入库两段式（GUI 分配 label → 后台 addSample+saveModel）：
            // saveModel 失败时 loadModel 回滚内存样本 + 新建人员撤销，绝不半注册
            const bool existed = _store.findByName(name) != nullptr;
            Person& p = _store.ensurePerson(name, clearanceCombo->currentIndex() + 1);
            const int label = p.label;
            const int clearance = p.clearance;
            const cv::Mat gray100 = crop.gray100.clone();

            auto* saveWatcher = new QFutureWatcher<bool>(this);
            connect(saveWatcher, &QFutureWatcher<bool>::finished, this,
                [this, saveWatcher, name, label, clearance, existed]() {
                    saveWatcher->deleteLater();
                    const bool ok = saveWatcher->result();

                    // 失败回滚：内存样本丢弃（重载磁盘模型）+ 新建人员撤销
                    if (!ok) {
                        _algo.loadModel(modelFile().toStdString());
                        if (!existed)
                            _store.remove(label);
                        _store.save(personsFile());
                        refreshPersonTable();
                        appendLog(QStringLiteral("注册失败：模型写入失败（%1），已回滚，请检查磁盘")
                            .arg(modelFile()), true);
                        _registerBtn->setEnabled(true);
                        return;
                    }
                    onEntrySaved(name, label, clearance, _algo.sampleCount(label));
                });
            saveWatcher->setFuture(QtConcurrent::run([this, gray100, label]() -> bool {
                // 算法库接口级互斥（Pimpl 内部锁），与识别线程天然串行，安全
                if (!_algo.addSample(gray100, label))
                    return false;
                return _algo.saveModel(modelFile().toStdString());
            }));
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

// ---- 入库成功收尾（GUI 线程）：档案持久化 + 表刷新 + 日志 ----
void MainWindow::onEntrySaved(const QString& name, int label, int clearance, int samples)
{
    _store.save(personsFile());
    refreshPersonTable();
    appendLog(QStringLiteral("注册成功：%1（label=%2，权限=%3，样本=%4）")
        .arg(name).arg(label)
        .arg(QString::fromUtf8(kClearanceNames[clearance]))
        .arg(samples));
    _registerBtn->setEnabled(true);
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
                    _store.save(personsFile());
                    refreshPersonsSnapshot();   // 权限变更即时外发（不重建表格）
                    appendLog(QStringLiteral("权限更新：label=%1 → %2")
                        .arg(label).arg(QString::fromUtf8(kClearanceNames[idx])));
                });
        _personTable->setCellWidget(row, 1, combo);
        _personTable->setItem(row, 2,
            new QTableWidgetItem(QString::number(_algo.sampleCount(p.label))));
    }
    refreshPersonsSnapshot();   // 档案变化路径均经此刷新（启动/注册/回滚），顺带推快照
}

// ---- v0.4.0：档案快照推送（PersonStore + 算法样本数 → 契约 PersonInfo 形状）----
// PersonStore 归 GUI 线程所有，快照互斥解耦（HTTP 线程在 EventPublisher 内读取副本）
void MainWindow::refreshPersonsSnapshot()
{
    if (!_publisher)
        return;
    nlohmann::json arr = nlohmann::json::array();
    for (const Person& p : _store.all()) {
        arr.push_back({
            {"label", p.label},
            {"name", p.name.toStdString()},
            {"clearance", p.clearance},
            {"samples", _algo.sampleCount(p.label)}   // 算法库接口级互斥，GUI 调用安全
        });
    }
    _publisher->refreshPersonsSnapshot(std::move(arr));
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

    if (_gate.isOpen()) {   // 开闸横幅：4c 模拟态显式标注，杜绝误当真实放行
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(200, 30, 30, 200));
        painter.drawRect(0, 0, img.width(), 34);
        painter.setPen(Qt::white);
        painter.drawText(img.rect().adjusted(0, 6, 0, -img.height() + 28),
                         Qt::AlignHCenter,
                         _gate.isSimulated() ? QStringLiteral("模拟放行") : QStringLiteral("放 行 中"));
    }
    painter.end();

    _video->setPixmap(QPixmap::fromImage(img).scaled(
        _video->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// ---- 杂项 ----
void MainWindow::appendLog(const QString& line, bool highlight)
{
    auto* item = new QListWidgetItem(line);
    if (highlight)
        item->setForeground(mvcore::uitheme::dark().danger);   // 拒绝/错误：红色醒目可追溯
    _logList->insertItem(0, item);
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
