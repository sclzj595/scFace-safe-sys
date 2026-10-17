// EventPublisher.h —— FaceGate → MediaLink 事件发布（套件第三个消费者，4d）
//
// 职责：把通行判定结果以契约事件（gate_pass / gate_denied，契约 v0.3.0 新增类型）
// 推送到 WS :wsPort，并作为 Provider 供 /api/events 回查（读 CheckInLog JSONL）。
//
// 边界：MediaLink 零业务侵入；本类是纯适配器——不判定权限、不写日志，
// CheckInLog 是唯一事实源（WS 实时推 + HTTP 回查同源，客户端按 id 对账）。
//
// 配置：exe 旁 medialink.json（mlink::loadMediaLinkConfig）；文件缺失时使用
// FaceGate 缺省端口 8090/8091（与 SS 实例 8080/8081 同机错开，契约 §2.1）。
// gate.ini [Server] enabled=false（缺省）时宿主不启用本类——离线演示零依赖。
#ifndef EVENTPUBLISHER_H
#define EVENTPUBLISHER_H

#pragma execution_character_set("utf-8")

#include <medialink/MediaLink.h>
#include <medialink/MediaLinkConfig.h>
#include <mvcore/EventId.h>
#include <mvcore/PathResolver.h>

#include <QString>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

#include <nlohmann/json.hpp>
#include <cstdio>

class EventPublisher
{
public:
    EventPublisher(const EventPublisher&) = delete;
    EventPublisher& operator=(const EventPublisher&) = delete;

    explicit EventPublisher(bool enabled = false) : _enabled(enabled)
    {
        if (!_enabled)
            return;

        // 配置：单一来源 exe 旁 medialink.json；缺失回退 FaceGate 缺省端口
        std::string err;
        if (!mlink::loadMediaLinkConfig("medialink.json", _cfg, err)) {
            _cfg.httpPort = 8090;
            _cfg.wsPort = 8091;
        }
        _svc.setLogger([this](const std::string& m) { _lastLog = QString::fromStdString(m); });

        mlink::Provider p;
        p.events = [this](int limit, const std::string& type) {
            return readTodayEvents(limit, type);
        };
        p.persons = []() { return nlohmann::json::array(); };   // 档案管理在本地 UI，不外发
        _svc.setProviders(p);

        _httpUp = _svc.startHttp(_cfg.httpPort);
        _wsUp = _svc.startWs(_cfg.wsPort);
    }

    ~EventPublisher()
    {
        if (!_enabled)
            return;
        _svc.stopWs();
        _svc.stopHttp();
    }

    bool enabled() const { return _enabled; }
    bool httpUp() const { return _httpUp; }
    bool wsUp() const { return _wsUp; }
    const QString& lastLog() const { return _lastLog; }
    int httpPort() const { return _cfg.httpPort; }
    int wsPort() const { return _cfg.wsPort; }

    // 通行/拒绝事件实时推送（GUI 线程调用；CheckInLog.append 由调用方负责，本类不落盘）
    void publish(const QString& name, int label, double similarity,
                 bool granted, const QString& reason)
    {
        if (!_enabled || !_wsUp)
            return;
        // ID 去碰撞单点（契约 A5）：同毫秒单调 seq（mvcore::EventIdSequencer）
        const mvcore::EventId id = _seq.next(QDateTime::currentMSecsSinceEpoch());
        nlohmann::json ev = {
            {"id", id.toString().toStdString()},
            {"ts", id.tsMs},
            {"seq", id.seq},
            {"type", granted ? "gate_pass" : "gate_denied"},
            {"subject", name.toStdString()},
            {"detail", reason.toStdString()},
            {"similarity", similarity},
            {"image", ""}
        };
        if (label >= 0)
            ev["label"] = label;
        _svc.pushEvent(ev);
    }

private:
    // /api/events 回查：读 CheckInLog 今天的 JSONL（唯一事实源），按契约字段映射，
    // id 由 ts+文件顺序 seq 确定性生成（与实时推送不同源但格式一致，客户端仅作字符串比较）
    nlohmann::json readTodayEvents(int limit, const std::string& type)
    {
        nlohmann::json out = nlohmann::json::array();
        if (limit <= 0)
            limit = 50;
        const QString path = mvcore::PathResolver::dataPath(QStringLiteral("logs")) + "/"
            + QDateTime::currentDateTime().toString("yyyy-MM-dd") + ".jsonl";
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return out;
        qint64 lastTs = -1;
        int seq = 0;
        while (!f.atEnd()) {
            const QJsonObject o = QJsonDocument::fromJson(f.readLine()).object();
            if (o.isEmpty())
                continue;
            const qint64 ts = static_cast<qint64>(o.value("ts").toDouble());
            seq = (ts == lastTs) ? seq + 1 : 0;   // 回读侧同毫秒序号按文件顺序重建
            lastTs = ts;
            const QString evType = o.value("granted").toBool() ? "gate_pass" : "gate_denied";
            if (!type.empty() && type != evType.toStdString())
                continue;
            nlohmann::json ev = {
                {"id", QStringLiteral("e%1-%2").arg(ts).arg(seq).toStdString()},
                {"ts", ts},
                {"seq", seq},
                {"type", evType.toStdString()},
                {"subject", o.value("name").toString().toStdString()},
                {"detail", o.value("reason").toString().toStdString()},
                {"similarity", o.value("similarity").toDouble(-1.0)},
                {"image", ""}
            };
            if (o.contains("label"))
                ev["label"] = o.value("label").toInt();
            out.push_back(std::move(ev));
        }
        // 最新在前，截取 limit
        const int n = static_cast<int>(out.size());
        const int keep = n > limit ? limit : n;
        nlohmann::json sliced = nlohmann::json::array();
        for (int i = n - 1; i >= n - keep; --i)
            sliced.push_back(out[i]);
        return sliced;
    }

    bool _enabled = false;
    bool _httpUp = false;
    bool _wsUp = false;
    mlink::MediaLink _svc;
    mlink::MediaLinkConfig _cfg;
    mvcore::EventIdSequencer _seq;   // 仅 GUI 线程 publish() 触碰
    QString _lastLog;
};

#endif // EVENTPUBLISHER_H
