# 硬核挑战数据迁移问题修复方案

## 问题描述

`data/sql/db-characters/updates/challenge_mode_data_redesign.sql` 在迁移旧表 `hardcore_challenge_completed` 时，把每个角色的**最高等级**直接作为一条 `hardcore_challenge_success` 记录插入，导致大量未达到 milestone 的等级被错误地标记为“挑战成功”。

旧表 `hardcore_challenge_completed` 原本每升一级就记录一条，逻辑上只是“曾经到达过该等级”，并不表示“完成挑战”。新表 `hardcore_challenge_success` 应该只保存 milestone 等级（60 / 70 / 80）的达成记录，低于 milestone 的等级应进入 `hardcore_challenge_progress`（挑战中）。

## 线上数据现状（acore_characters）

| 表 | 记录数 | 最低等级 | 最高等级 | < 60 | 60-69 | 70 |
|---|---|---|---|---|---|---|
| `hardcore_challenge_success` | 1589 | 2 | 70 | 1519 | 57 | 13 |
| `hardcore_challenge_progress` | 0 | - | - | - | - | - |
| `hardcore_challenge_failure` | 813 | 1 | 70 | 805 | 7 | 1 |

Token（`item_entry = 90002`）持有情况：

| 分类 | 数量 |
|---|---|
| success 表中有 Token 的角色 | 1290 |
| success 表中无 Token 的角色 | 299 |
| failure 表中有 Token 的角色 | 778 |
| failure 表中无 Token 的角色 | 33 |
| 持有 Token 但未出现在 success 的角色 | 281 |
| 持有 Token 但未出现在 failure 的角色 | 793 |

结论：

1. `hardcore_challenge_success` 中 1519 条低于 60 级、57 条 60-69 级的记录都不是真正的“成功”，应迁移或清理。
2. `hardcore_challenge_progress` 为空，但大量仍在挑战中的角色（持有 Token 且未达到最终 milestone）应当出现在这里。
3. 无 Token 的角色不应再保留在 progress 表中。

## 修复目标

1. `hardcore_challenge_success` 只保留 milestone 等级记录（60 / 70 / 80）。
2. 非 milestone 的成功记录：
   - 若角色仍持有 Token → 迁移到 `hardcore_challenge_progress`。
   - 若角色未持有 Token → 直接删除。
3. `hardcore_challenge_progress` 清理无 Token 的记录。
4. 后续新服部署时，修复迁移 SQL，避免再次产生脏数据。

## 修复 SQL（已执行）

生产环境 `acore_characters` 已按以下逻辑完成修复。该 SQL 仅供留档参考，如需在类似环境重做，可直接复制执行。

```sql
SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- 0. 修复前计数
SELECT 'BEFORE_FIX' AS stage,
       (SELECT COUNT(*) FROM hardcore_challenge_success) AS success_count,
       (SELECT COUNT(*) FROM hardcore_challenge_progress) AS progress_count,
       (SELECT COUNT(*) FROM hardcore_challenge_failure) AS failure_count,
       (SELECT COUNT(*) FROM hardcore_challenge_success WHERE completed_level NOT IN (60, 70, 80)) AS success_non_milestone_count;

-- 1. 非 milestone 且持有 Token 的记录迁移到 progress
INSERT INTO hardcore_challenge_progress
    (character_guid, character_name, current_level, total_spent_time)
SELECT
    s.character_guid,
    COALESCE(ch.name, s.character_name),
    COALESCE(ch.level, s.completed_level),
    s.total_spent_time
FROM hardcore_challenge_success s
LEFT JOIN characters ch ON ch.guid = s.character_guid
JOIN item_instance ii ON ii.owner_guid = s.character_guid AND ii.itemEntry = 90002
WHERE s.completed_level NOT IN (60, 70, 80)
ON DUPLICATE KEY UPDATE
    character_name = VALUES(character_name),
    current_level = VALUES(current_level),
    total_spent_time = VALUES(total_spent_time);

-- 2. 删除 success 表中所有非 milestone 记录
DELETE FROM hardcore_challenge_success
WHERE completed_level NOT IN (60, 70, 80);

-- 3. 清理 progress 表中无 Token 的记录
DELETE p
FROM hardcore_challenge_progress p
LEFT JOIN item_instance ii ON ii.owner_guid = p.character_guid AND ii.itemEntry = 90002
WHERE ii.owner_guid IS NULL;

-- 4. 修复后计数
SELECT 'AFTER_FIX' AS stage,
       (SELECT COUNT(*) FROM hardcore_challenge_success) AS success_count,
       (SELECT COUNT(*) FROM hardcore_challenge_progress) AS progress_count,
       (SELECT COUNT(*) FROM hardcore_challenge_failure) AS failure_count,
       (SELECT COUNT(*) FROM hardcore_challenge_success WHERE completed_level NOT IN (60, 70, 80)) AS success_non_milestone_count;

SET FOREIGN_KEY_CHECKS = 1;
```

执行前建议先备份 `acore_characters` 数据库：

```bash
mysqldump -h <host> -P <port> -u <user> -p acore_characters \
  hardcore_challenge_success \
  hardcore_challenge_progress \
  hardcore_challenge_failure \
  hardcore_challenge_exit \
  > hardcore_challenge_backup_$(date +%Y%m%d_%H%M%S).sql
```

## 修复步骤

1. 备份数据库。
2. 在 `acore_characters` 中执行上述修复 SQL。
3. 检查执行日志中的前后计数，确认符合预期。
4. 重启 worldserver，让登录/升级 Hook 自动补齐当前在线角色的 progress 记录。

## 修复后的预期数据

- `hardcore_challenge_success`：仅保留 `completed_level IN (60, 70, 80)` 的记录。
- `hardcore_challenge_progress`：包含持有 Token 且未到达最终 milestone 的角色，等级为当前角色等级。
- 无 Token 的角色不再出现在 progress 表中。

## 未来生产环境：从旧表正确迁移到新表

`data/sql/db-characters/updates/challenge_mode_data_redesign.sql` 已更新为正确的迁移逻辑。未来从旧表升级时，直接执行该文件即可：

```bash
mysql -h <host> -P <port> -u <user> -p acore_characters \
  < modules/mod-challenge-modes/data/sql/db-characters/updates/challenge_mode_data_redesign.sql
```

### 新迁移逻辑要点

1. **创建新表**：`hardcore_challenge_success` / `hardcore_challenge_failure` / `hardcore_challenge_progress` / `hardcore_challenge_exit`。
2. **milestone 记录进入 success**：只把旧 `hardcore_challenge_completed` 中 `character_level IN (60, 70, 80)` 的记录写入 success。
3. **非 milestone 且持有 Token 进入 progress**：
   - 当前角色仍存活 → 使用 `characters.level` 作为 `current_level`。
   - 角色已删除 → 使用旧 completed 表中的最高等级作为 fallback。
   - 无 Token 的角色视为已退出，不进入 progress。
4. **失败记录进入 failure**：原样迁移，位置信息补 0 / NULL。
5. **删除旧表**：迁移完成后删除 `hardcore_challenge_completed` 和 `hardcore_challenge_failed`。

### 测试步骤

1. 准备一份包含旧表的测试数据库（备份自生产或手动构造）。
2. 确认 `item_instance` 中存在 `itemEntry = 90002` 的 Token。
3. 执行新的 `challenge_mode_data_redesign.sql`。
4. 验证：
   - `hardcore_challenge_success` 只包含 60 / 70 / 80 级记录。
   - `hardcore_challenge_progress` 只包含持有 Token 的非 milestone 角色。
   - `hardcore_challenge_failure` 与原 `hardcore_challenge_failed` 记录数一致。
   - 旧表已被删除。

### 测试验证 SQL

```sql
-- success 中不应出现非 milestone 等级
SELECT completed_level, COUNT(*) AS cnt
FROM hardcore_challenge_success
GROUP BY completed_level;

-- progress 中不应有无 Token 的角色
SELECT COUNT(*) AS progress_without_token
FROM hardcore_challenge_progress p
LEFT JOIN item_instance ii ON ii.owner_guid = p.character_guid AND ii.itemEntry = 90002
WHERE ii.owner_guid IS NULL;

-- 新旧 failure 记录数对比（旧表已删除，可在执行前记录）
SELECT COUNT(*) FROM hardcore_challenge_failure;
```

> 说明：milestone 集合（60 / 70 / 80）来自当前部署配置 `IronManEnhanced.TitleRewards = "60 178, 70 179, 80 180"`。如果后续变更 milestone，应同步调整 SQL 中的常量。

## 注意事项

- 本次修复针对**已执行过旧迁移**的环境。旧表 `hardcore_challenge_completed` / `hardcore_challenge_failed` 已被删除，无法重新迁移，只能基于现有新表修复。
- 修复后，部分角色的 progress 等级可能与真实当前等级不一致（如果角色在迁移后又升了级）。这些差异会在角色下次登录或升级时由 `ChallengeMode_IronMan_Enhanced::OnPlayerLogin` / `OnPlayerLevelChanged` 自动校正。
- `hardcore_challenge_failure` 中无 Token 的 33 条记录属于历史脏数据，可选择性清理；因不影响当前挑战状态，本次不作为必做项。
