// CheckInLog.h —— 签到/通行日志（JSONL 追加，互斥保护，任意线程可调）
//
// 布局：logs/2026-10-17.jsonl，一行一条：
//   {"ts":..,"name":..,"label":..,"similarity":..,"granted":true/false,"reason":".."}
#ifndef CHECKINLOG_H
#define CHECKINLOG_H

#pragma execution_character_set("utf-8")

#include <QString>
#include <QMutex>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

class CheckInLog
{
public:
    explicit CheckInLog(QString rootDir = QStringLiteral("./logs"))
        : _root(std::move(rootDir)) {}

    void append(const QString& name, int label, double similarity,
                bool granted, const QString& reason)
    {
        QJsonObject o;
        o.insert("ts", QDateTime::currentMSecsSinceEpoch());
        o.insert("name", name);
        if (label >= 0) o.insert("label", label);
        o.insert("similarity", similarity);
        o.insert("granted", granted);
        o.insert("reason", reason);

        QMutexLocker locker(&_mutex);
        QDir().mkpath(_root);
        const QString path = _root + "/" +
            QDateTime::currentDateTime().toString("yyyy-MM-dd") + ".jsonl";
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            return;
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.write("\n");
    }

private:
    QString _root;
    QMutex _mutex;      // 识别线程 + GUI 注册流程都可能写
};

#endif // CHECKINLOG_H
