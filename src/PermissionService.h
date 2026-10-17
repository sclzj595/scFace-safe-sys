// PermissionService.h —— 权限校验（纯函数，无状态；闸机业务规则核心）
//
// 业务规则（与监控项目的告警规则相互独立，算法库不感知）：
//   1. 识别命中 + 档案存在 + clearance >= 通行要求 → 放行，脉冲开闸
//   2. 识别命中但权限不足 → 拒绝，仅记日志
//   3. 陌生人 / 无样本 → 拒绝，仅记日志
#ifndef PERMISSIONSERVICE_H
#define PERMISSIONSERVICE_H

#pragma execution_character_set("utf-8")

#include <QString>
#include <facealgorithm/FaceAlgorithm.h>
#include "PersonStore.h"

namespace PermissionService {

struct Decision {
    bool granted = false;
    QString reason;          // 中文结论（日志/界面直接展示）
};

inline Decision decide(const falgo::FaceHit& hit, const PersonStore& store,
                       int requiredClearance)
{
    Decision d;
    if (!hit.known) {
        d.reason = QStringLiteral("陌生人，拒绝通行");
        return d;
    }
    const Person* p = store.findByLabel(hit.label);
    if (!p) {
        d.reason = QStringLiteral("识别命中但档案缺失(label=%1)，拒绝").arg(hit.label);
        return d;
    }
    if (p->clearance <= 0) {
        d.reason = QStringLiteral("%1 已被禁用，拒绝通行").arg(p->name);
        return d;
    }
    if (p->clearance < requiredClearance) {
        static const char* kNames[] = { "禁用", "访客", "员工", "管理员" };
        d.reason = QStringLiteral("%1 权限(%2)低于通行要求(%3)，拒绝")
                       .arg(p->name, QString::fromUtf8(kNames[p->clearance]),
                            QString::fromUtf8(kNames[requiredClearance]));
        return d;
    }
    d.granted = true;
    d.reason = QStringLiteral("%1 权限校验通过（相似度 %2%）")
                   .arg(p->name).arg(hit.similarity, 0, 'f', 0);
    return d;
}

} // namespace PermissionService

#endif // PERMISSIONSERVICE_H
