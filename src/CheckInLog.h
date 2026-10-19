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
#include <QVector>
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

    // 当日日志自持锁读（审查 #1）：与 append 共用 _mutex，HTTP 回查线程
    // 不会读到半行截断数据；坏行（空解析）跳过。返回解析成功对象（按文件顺序）
    QVector<QJsonObject> readToday()
    {
        QVector<QJsonObject> rows;
        QMutexLocker locker(&_mutex);
        const QString path = _root + "/" +
            QDateTime::currentDateTime().toString("yyyy-MM-dd") + ".jsonl";
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return rows;
        while (!f.atEnd()) {
            const QJsonObject o = QJsonDocument::fromJson(f.readLine()).object();
            if (!o.isEmpty())
                rows.append(o);
        }
        return rows;
    }

private:
    QString _root;
    QMutex _mutex;      // 识别线程 + GUI 注册流程都可能写
};

#endif // CHECKINLOG_H
