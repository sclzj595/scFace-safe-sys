// PersonStore.cpp
#include "PersonStore.h"
#include <QFile>
#include <QJsonDocument>
#include <algorithm>

bool PersonStore::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;                       // 首次运行无档案，非错误
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray())
        return false;

    _persons.clear();
    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        Person p;
        p.label = o.value("label").toInt(-1);
        p.name = o.value("name").toString();
        p.clearance = o.value("clearance").toInt(1);
        if (p.label >= 0 && !p.name.isEmpty())
            _persons.append(p);
    }
    return true;
}

bool PersonStore::save(const QString& path) const
{
    QJsonArray arr;
    for (const Person& p : _persons) {
        QJsonObject o;
        o.insert("label", p.label);
        o.insert("name", p.name);
        o.insert("clearance", p.clearance);
        arr.append(o);
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(arr).toJson());
    return true;
}

const Person* PersonStore::findByLabel(int label) const
{
    for (const Person& p : _persons)
        if (p.label == label) return &p;
    return nullptr;
}

const Person* PersonStore::findByName(const QString& name) const
{
    for (const Person& p : _persons)
        if (p.name == name) return &p;
    return nullptr;
}

Person& PersonStore::ensurePerson(const QString& name, int clearance)
{
    for (Person& p : _persons)
        if (p.name == name) return p;   // 同名 = 补充样本，档案复用

    Person p;
    p.label = _persons.isEmpty() ? 1
        : std::max_element(_persons.cbegin(), _persons.cend(),
            [](const Person& a, const Person& b) { return a.label < b.label; })->label + 1;
    p.name = name;
    p.clearance = clearance;
    _persons.append(p);
    return _persons.last();
}

void PersonStore::setClearance(int label, int clearance)
{
    for (Person& p : _persons)
        if (p.label == label) { p.clearance = clearance; return; }
}

void PersonStore::remove(int label)
{
    _persons.erase(std::remove_if(_persons.begin(), _persons.end(),
        [label](const Person& p) { return p.label == label; }), _persons.end());
}
