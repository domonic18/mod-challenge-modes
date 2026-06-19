# 硬核挑战数据存储重构方案

## 1. 现状与问题

当前模块在数据库中主要使用以下两张表：

- `hardcore_challenge_completed`：每升一级插入一条记录，字段仅包含 `character_guid`、`character_level`、`achievement`、`total_spent_time`。
- `hardcore_challenge_failed`：死亡/失败时插入一条记录，字段仅包含 `character_guid`、`character_level`、`death_reason`、`total_spent_time`。

存在的问题：

1. **数据量过大**：`completed` 表按每级一条记录增长，对于“挑战中”的展示极不友好。
2. **无法表示“退出挑战”**：玩家摧毁硬核挑战凭证（Token）后， IronMan 设置实际上已失效，但数据库中仍可能保留“挑战中”状态。
3. **失败信息太简单**：只有 `death_reason`（如 `pvp`、`death`、`resurrect`、`ghost`），缺少死亡位置、击杀者等详情。

## 2. 设计目标

1. **挑战成功**：在 60/70/80 级等关键 milestone 记录一条成功记录，包含角色名、等级、花费时间，便于 Web 展示。
2. **挑战失败**：记录角色名、等级、死亡原因、死亡地图/区域/坐标、击杀者信息、花费时间。
3. **挑战中**：每个角色仅保留一条记录，实时更新等级、角色名、在线时长。
4. **退出挑战**：摧毁 Token 后立即从“挑战中”移除该角色，并记录退出审计日志（不记录为失败）。
5. **成功记录保留**：即使成功后 Token 被摧毁，成功记录仍保留。
6. **便于上游合并**：`ChallengeMode_IronMan` 尽量保持上游一致，所有自定义数据逻辑放到独立的 `ChallengeMode_IronMan_Enhanced` 源文件中。
7. **规则简单**：增强硬核模式以 Token 为唯一状态依据，玩家易于理解，代码不依赖 `character_settings` 中的延迟持久化标记。

## 3. 新表结构

建议新增/替换为四张表（使用 InnoDB + utf8mb4）：

```sql
-- 挑战成功表：每个角色每个 milestone 一条记录
CREATE TABLE IF NOT EXISTS `hardcore_challenge_success` (
  `id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  `character_guid` BIGINT(20) UNSIGNED NOT NULL,
  `character_name` VARCHAR(12) NOT NULL,
  `completed_level` TINYINT(3) UNSIGNED NOT NULL,
  `total_spent_time` INT(10) UNSIGNED NOT NULL,
  `completed_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_character_level` (`character_guid`, `completed_level`),
  KEY `idx_completed_level` (`completed_level`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 挑战失败表：每次失败一条记录
CREATE TABLE IF NOT EXISTS `hardcore_challenge_failure` (
  `id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  `character_guid` BIGINT(20) UNSIGNED NOT NULL,
  `character_name` VARCHAR(12) NOT NULL,
  `character_level` TINYINT(3) UNSIGNED NOT NULL,
  `death_reason` VARCHAR(50) NOT NULL,
  `death_location_map_id` SMALLINT(5) UNSIGNED NOT NULL,
  `death_location_zone_id` MEDIUMINT(8) UNSIGNED NOT NULL,
  `death_location_area_id` MEDIUMINT(8) UNSIGNED NOT NULL,
  `death_location_x` FLOAT NOT NULL,
  `death_location_y` FLOAT NOT NULL,
  `death_location_z` FLOAT NOT NULL,
  `killer_info` VARCHAR(255) DEFAULT NULL,
  `total_spent_time` INT(10) UNSIGNED NOT NULL,
  `failed_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_character_guid` (`character_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 挑战中表：每个角色仅一条记录，更新等级与时间
CREATE TABLE IF NOT EXISTS `hardcore_challenge_progress` (
  `character_guid` BIGINT(20) UNSIGNED NOT NULL,
  `character_name` VARCHAR(12) NOT NULL,
  `current_level` TINYINT(3) UNSIGNED NOT NULL,
  `started_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `total_spent_time` INT(10) UNSIGNED NOT NULL,
  PRIMARY KEY (`character_guid`),
  KEY `idx_current_level` (`current_level`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 退出审计表：记录玩家主动/被动退出挑战的事件，便于追溯
CREATE TABLE IF NOT EXISTS `hardcore_challenge_exit` (
  `id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  `character_guid` BIGINT(20) UNSIGNED NOT NULL,
  `character_name` VARCHAR(12) NOT NULL,
  `current_level` TINYINT(3) UNSIGNED NOT NULL,
  `total_spent_time` INT(10) UNSIGNED NOT NULL,
  `exited_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_character_guid` (`character_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
```

> 旧表 `hardcore_challenge_completed`、`hardcore_challenge_failed` 迁移后直接删除。

## 4. 代码改动点

### 4.1 文件组织

为降低未来与上游 `ChallengeModes.cpp` 合并的冲突概率，把铁人增强模式相关逻辑拆分为独立文件：

```
modules/mod-challenge-modes/src/
├── ChallengeModes.cpp                   # 尽量保持上游一致
├── ChallengeModes.h
├── ChallengeMode_IronManEnhanced.h      # 增强类声明 + DB 工具函数
└── ChallengeMode_IronManEnhanced.cpp    # 增强类实现 + DB 操作
```

`AddSC_mod_challenge_modes()` 中**先**注册 `new ChallengeMode_IronMan_Enhanced();`，**再**注册 `new ChallengeMode_IronMan();`，确保 `OnPlayerResurrect` 中 Enhanced 能先处理，再由 IronMan 执行兜底。

### 4.2 增强硬核模式状态判定

为简化玩家理解与代码实现，**增强硬核模式（Enhanced IronMan）以背包中是否持有「硬核挑战凭证（Token）」作为唯一生效标准**：

- 持有 Token → 处于增强硬核模式，所有限制与死亡规则生效。
- 未持有 Token → 不处于增强硬核模式，视为已退出或从未加入。

```cpp
bool ChallengeMode_IronMan_Enhanced::IsEnhancedActive(Player* player)
{
    return sChallengeModes->ironManEnhancedEnable &&
           player->HasItemCount(HARDCORE_TOKEN_ITEM_ID, 1, true);
}
```

> 说明：`SETTING_IRON_MAN` 玩家设置仍由上游 `ChallengeMode_IronMan` 和 gossip 入口维护，但 `ChallengeMode_IronMan_Enhanced` 不再依赖该设置判断状态。这样即使旧角色或异常角色只持有 Token 而缺少设置，也能正常进入硬核流程；Token 被销毁后立即失效，行为直观。
> 增强模式**不使用** `IRON_MAN_DEAD` 等延迟持久化的 `character_settings` 标记，所有关键状态以 Token 和数据库表为准。

### 4.3 核心 Hook 与行为

在 `ChallengeMode_IronMan_Enhanced` 中实现：

| 事件 | Hook | 行为 |
|------|------|------|
| 开启硬核挑战 | `gobject_challenge_modes::OnGossipSelect` 中增强分支 | 设置 `SETTING_IRON_MAN` 并发放/确认 Token，初始化 `progress` 记录（INSERT OR REPLACE），写入当前等级、角色名、开始时间、总游戏时间。 |
| 升级 | `OnPlayerLevelChanged` | 若持有 Token，更新 `progress` 表等级与时间；若达到 milestone，向 `success` 表插入记录；若达到最终 milestone，删除 `progress` 记录。 |
| 死亡（真实死亡） | `OnPlayerJustDied` | 若持有 Token，向 `failure` 表插入死亡详情，删除 `progress` 记录。**不踢出玩家**，保留幽灵状态以便道别。 |
| 尝试复活 | `OnPlayerCanResurrect` | 若持有 Token，直接返回 `false` 阻止任何复活（灵魂医者、治疗复活、灵魂石、诈尸等）；记录 `failure`（reason=`resurrect`），删除 `progress`。 |
| 释放灵魂 | `OnPlayerReleasedGhost` | 若持有 Token，将玩家踢出游戏，结束该角色的硬核挑战流程。 |
| 登录 | `OnPlayerLogin` | 若持有 Token 且角色已死亡（CORPSE/DEAD/GHOST），按失败处理并踢出；若持有 Token 且存活，更新 `progress` 记录；若无 Token，执行“退出挑战”逻辑。 |
| 物品移动/销毁 | `OnPlayerAfterMoveItemFromInventory` | 若移动的是 Token，检查持有数量；若为 0，立即执行“退出挑战”逻辑。 |

### 4.4 成功 milestone 判定

**已确认**：复用现有配置 `IronMan.TitleRewards` 的等级键作为 milestone。

```ini
IronMan.TitleRewards = "60 178, 70 179, 80 180"
```

解析后得到 milestone 集合 `{60, 70, 80}`。当 `OnPlayerLevelChanged` 触发且新等级在该集合中时，即视为一次成功记录。

### 4.5 Token 退出机制（立即生效）

通过 `OnPlayerAfterMoveItemFromInventory` 实时检测 Token 被移除；`OnPlayerLogin` 作为兜底。

```cpp
void ChallengeMode_IronMan_Enhanced::HandleChallengeExit(Player* player)
{
    if (!IsEnhancedActive(player))
        return;

    // 记录退出审计日志
    CharacterDatabase.Execute(
        "INSERT INTO hardcore_challenge_exit "
        "(character_guid, character_name, current_level, total_spent_time) "
        "VALUES ({}, '{}', {}, {})",
        player->GetGUID().GetCounter(),
        player->GetName(),
        player->GetLevel(),
        player->GetTotalPlayedTime());

    // 从挑战中表移除
    CharacterDatabase.Execute(
        "DELETE FROM hardcore_challenge_progress WHERE character_guid = {}",
        player->GetGUID().GetCounter());

    // 关闭上游 IronMan 设置，避免继续受到上游规则限制
    player->UpdatePlayerSetting("mod-challenge-modes", SETTING_IRON_MAN, 0);
}
```

触发点：

- `OnPlayerAfterMoveItemFromInventory`（即时）：
  ```cpp
  if (it->GetEntry() == HARDCORE_TOKEN_ITEM_ID &&
      !player->HasItemCount(HARDCORE_TOKEN_ITEM_ID, 1, true))
      HandleChallengeExit(player);
  ```
- `OnPlayerLogin`（兜底）：
  ```cpp
  if (!player->HasItemCount(HARDCORE_TOKEN_ITEM_ID, 1, true))
      HandleChallengeExit(player);
  ```

Token 摧毁后**不广播**。

### 4.6 死亡详情采集（统一在 `OnPlayerJustDied`）

```cpp
void ChallengeMode_IronMan_Enhanced::OnPlayerJustDied(Player* player)
{
    if (!IsEnhancedActive(player))
        return;

    std::string reason = "environmental";
    std::string killerInfo;

    if (Unit* killer = player->getAttackerForHelper())
    {
        if (killer->IsPlayer())
        {
            reason = "player";
            if (Player* pk = killer->ToPlayer())
                killerInfo = Acore::StringFormat("player:{}:level{}:class{}",
                    pk->GetName(), pk->GetLevel(), uint32(pk->getClass()));
        }
        else if (killer->IsCreature())
        {
            reason = "creature";
            if (Creature* ck = killer->ToCreature())
                killerInfo = Acore::StringFormat("creature:{}:{}:level{}",
                    ck->GetEntry(), ck->GetName(), ck->GetLevel());
        }
    }

    CharacterDatabase.Execute(
        "INSERT INTO hardcore_challenge_failure "
        "(character_guid, character_name, character_level, death_reason, "
        "death_location_map_id, death_location_zone_id, death_location_area_id, "
        "death_location_x, death_location_y, death_location_z, killer_info, total_spent_time) "
        "VALUES ({}, '{}', {}, '{}', {}, {}, {}, {:.3f}, {:.3f}, {:.3f}, '{}', {})",
        player->GetGUID().GetCounter(),
        player->GetName(),
        player->GetLevel(),
        reason,
        player->GetMapId(),
        player->GetZoneId(),
        player->GetAreaId(),
        player->GetPositionX(),
        player->GetPositionY(),
        player->GetPositionZ(),
        killerInfo,
        player->GetTotalPlayedTime());

    DeleteProgress(player);
}
```

死亡原因说明：

- `player`：被玩家击杀，`killer_info` 包含玩家名/等级/职业。
- `creature`：被怪物击杀，`killer_info` 包含 creature entry/名称/等级。
- `environmental`：环境伤害（坠落、岩浆、溺水等）。
- `resurrect`：尝试复活被系统击杀（挑战规则违规）。

> 若未来需要精确区分“高处坠落”等环境子类型，需要 AzerothCore 核心新增 `OnPlayerEnvironmentalDamage` hook；当前方案先用 `environmental` 统一归类。

### 4.7 释放灵魂时踢出玩家

死亡时保留幽灵状态，允许玩家与其他人交流；点击释放灵魂后再踢出：

```cpp
void ChallengeMode_IronMan_Enhanced::OnPlayerReleasedGhost(Player* player)
{
    if (!IsEnhancedActive(player))
        return;

    player->GetSession()->KickPlayer("硬核挑战角色已死亡");
}
```

### 4.8 登录兜底

玩家可能在死亡后、释放灵魂前掉线或服务器重启。登录时以 Token + 角色死亡状态为准：

```cpp
void ChallengeMode_IronMan_Enhanced::OnPlayerLogin(Player* player)
{
    if (!player->HasItemCount(HARDCORE_TOKEN_ITEM_ID, 1, true))
    {
        HandleChallengeExit(player);
        return;
    }

    if (player->getDeathState() != DeathState::Alive)
    {
        RecordFailure(player);
        DeleteProgress(player);
        player->GetSession()->KickPlayer("硬核挑战角色已死亡");
        return;
    }

    UpsertProgress(player);
}
```

### 4.9 progress 表更新

```cpp
void ChallengeMode_IronMan_Enhanced::UpsertProgress(Player* player)
{
    CharacterDatabase.Execute(
        "INSERT INTO hardcore_challenge_progress "
        "(character_guid, character_name, current_level, total_spent_time) "
        "VALUES ({}, '{}', {}, {}) "
        "ON DUPLICATE KEY UPDATE "
        "character_name = VALUES(character_name), "
        "current_level = VALUES(current_level), "
        "total_spent_time = VALUES(total_spent_time)",
        player->GetGUID().GetCounter(),
        player->GetName(),
        player->GetLevel(),
        player->GetTotalPlayedTime());
}
```

## 5. 数据流转

```
开启硬核挑战
    -> 销毁装备 / 发放 Token
    -> UPSERT progress (guid, name, level, time)

升级
    -> UPSERT progress (level, time)
    -> IF level IN milestone (60/70/80)
         -> INSERT success (guid, name, level, time)
         -> IF level == 最终 milestone
              -> DELETE progress

真实死亡（玩家/怪物/环境）
    -> INSERT failure (guid, name, level, reason, location, killer, time)
    -> DELETE progress
    -> 玩家保留幽灵状态，可继续交流

释放灵魂
    -> KickPlayer("硬核挑战角色已死亡")

尝试复活（灵魂医者/治疗/灵魂石/诈尸等）
    -> OnPlayerCanResurrect 返回 false，阻止复活
    -> INSERT failure (guid, name, level, reason=resurrect, location, killer, time)
    -> DELETE progress

摧毁 Token
    -> INSERT exit (guid, name, level, time)
    -> DELETE progress
    -> UpdatePlayerSetting(SETTING_IRON_MAN, 0)

登录
    -> IF 无 Token
         -> HandleChallengeExit
    -> ELSE IF 角色已死亡（CORPSE/DEAD/GHOST）
         -> INSERT failure
         -> DELETE progress
         -> KickPlayer("硬核挑战角色已死亡")
    -> ELSE
         -> UPSERT progress (name, level, time)
```

## 6. 迁移方案

1. 创建新表 `hardcore_challenge_success`、`hardcore_challenge_failure`、`hardcore_challenge_progress`、`hardcore_challenge_exit`。
2. 将旧表数据迁移到新表。
3. 迁移完成后直接删除旧表：

```sql
DROP TABLE IF EXISTS `hardcore_challenge_completed`;
DROP TABLE IF EXISTS `hardcore_challenge_failed`;
```

### 6.1 迁移 SQL 示例

```sql
-- 从旧的 completed 表迁移每个角色的最高等级作为一次成功记录
INSERT INTO hardcore_challenge_success (character_guid, character_name, completed_level, total_spent_time, completed_at)
SELECT
    c.character_guid,
    COALESCE(ch.name, ''),
    MAX(c.character_level) AS completed_level,
    MAX(c.total_spent_time) AS total_spent_time,
    NOW()
FROM hardcore_challenge_completed c
LEFT JOIN characters ch ON ch.guid = c.character_guid
GROUP BY c.character_guid;

-- 迁移失败记录（位置信息无法补全，置为 0/NULL）
INSERT INTO hardcore_challenge_failure (character_guid, character_name, character_level, death_reason, death_location_map_id, death_location_zone_id, death_location_area_id, death_location_x, death_location_y, death_location_z, killer_info, total_spent_time, failed_at)
SELECT
    f.character_guid,
    COALESCE(ch.name, ''),
    f.character_level,
    f.death_reason,
    0, 0, 0, 0.0, 0.0, 0.0,
    NULL,
    f.total_spent_time,
    NOW()
FROM hardcore_challenge_failed f
LEFT JOIN characters ch ON ch.guid = f.character_guid;
```

## 7. Web 展示查询示例

```sql
-- 挑战成功榜单
SELECT character_name, completed_level, total_spent_time, completed_at
FROM hardcore_challenge_success
ORDER BY completed_at DESC;

-- 挑战失败榜单
SELECT character_name, character_level, death_reason, death_location_map_id,
       death_location_zone_id, death_location_area_id,
       death_location_x, death_location_y, death_location_z,
       killer_info, total_spent_time, failed_at
FROM hardcore_challenge_failure
ORDER BY failed_at DESC;

-- 挑战中角色
SELECT character_name, current_level, total_spent_time, started_at, last_updated_at
FROM hardcore_challenge_progress
ORDER BY last_updated_at DESC;

-- 退出审计（管理后台使用）
SELECT character_name, current_level, total_spent_time, exited_at
FROM hardcore_challenge_exit
ORDER BY exited_at DESC;
```

## 8. 与上游合并的兼容性

- `ChallengeMode_IronMan` 保持上游逻辑，仅保留 `OnPlayerCanApplyEnchantment` 的委托行为（返回 `ironManEnhancedEnable`）。
- 所有新增 DB 逻辑、Token 退出检测、死亡详情、成功 milestone 全部集中在 `ChallengeMode_IronMan_Enhanced` 独立 `.h/.cpp` 文件中。
- SQL 更新放到 `data/sql/updates/pending_db_characters/`，遵循 AzerothCore SQL 规范（幂等、4 空格缩进、InnoDB）。
- 不修改 `data/sql/base/` 下的旧 SQL；新表通过 pending update 创建，旧表通过迁移脚本删除。

## 9. 已确认事项

1. 成功 milestone 复用 `IronMan.TitleRewards` 的等级键。
2. Token 作为增强硬核模式的唯一状态依据，不依赖 `SETTING_IRON_MAN` 或 `IRON_MAN_DEAD`。
3. Token 摧毁后立即生效，通过 `OnPlayerAfterMoveItemFromInventory` 检测。
4. 死亡原因使用 `player` / `creature` / `environmental` / `resurrect`，并记录击杀者名/等级/职业或 creature entry/名/等级。
5. `failure` 表统一在 `OnPlayerJustDied` 时写入，不再在复活/释放灵魂时写入。
6. 迁移后旧表直接删除。
7. `ChallengeMode_IronMan_Enhanced` 拆分为独立 `.h/.cpp` 文件。
8. Token 摧毁后不广播。
9. 退出挑战记录到独立的 `hardcore_challenge_exit` 审计表，再删除 `progress`，符合审计追踪最佳实践。
10. 增强模式不使用 `character_settings` 中的延迟持久化标记作为关键判定。
