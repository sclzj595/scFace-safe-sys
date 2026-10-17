// PersonStore.h —— 人员档案（姓名/通行权限）与算法 label 的映射，JSON 持久化
//
// 算法库只认数字 label；姓名与权限是业务概念，存这里（与 FaceAlgorithm 的
// 模型文件 gate_model.yml 配对使用：label 两侧一致）。
#ifndef PERSONSTORE_H
#define PERSONSTORE_H

#pragma execution_character_set("utf-8")

#include <QObject>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>

struct Person {
    int label = -1;
    QString name;
    int clearance = 1;      // 0=禁用 1=访客 2=员工 3=管理员
};

class PersonStore
{
public:
    bool load(const QString& path);
    bool save(const QString& path) const;

    const QVector<Person>& all() const { return _persons; }
    const Person* findByLabel(int label) const;
    const Person* findByName(const QString& name) const;

    // 取同名已有人员，否则新建（label 取最大值+1，与算法库对齐）
    Person& ensurePerson(const QString& name, int clearance);
    void setClearance(int label, int clearance);
    void remove(int label);

private:
    QVector<Person> _persons;
};

#endif // PERSONSTORE_H
