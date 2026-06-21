-- 硬核挑战数据存储重构：从旧表迁移数据并删除旧表
-- 适用于已存在 hardcore_challenge_completed / hardcore_challenge_failed 的服务器
-- 修复：success 表只保留 milestone 等级（60 / 70 / 80），非 milestone 且持有 Token 的记录进入 progress

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

SET @completed_exists = (SELECT COUNT(*) FROM `information_schema`.`tables`
    WHERE `table_schema` = DATABASE() AND `table_name` = 'hardcore_challenge_completed');

SET @failed_exists = (SELECT COUNT(*) FROM `information_schema`.`tables`
    WHERE `table_schema` = DATABASE() AND `table_name` = 'hardcore_challenge_failed');

-- ============================================================
-- 1. 创建新表（若不存在）
-- ============================================================
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

-- ============================================================
-- 2. 迁移旧 completed 表中的 milestone 记录到 success
--    milestone 等级由 IronManEnhanced.TitleRewards 配置决定：60 / 70 / 80
-- ============================================================
SET @migrate_completed_milestone = IF(@completed_exists = 1,
    'INSERT INTO `hardcore_challenge_success`
        (`character_guid`, `character_name`, `completed_level`, `total_spent_time`, `completed_at`)
     SELECT
         c.`character_guid`,
         COALESCE(ch.`name`, \'\'),
         c.`character_level`,
         MAX(c.`total_spent_time`),
         NOW()
     FROM `hardcore_challenge_completed` c
     LEFT JOIN `characters` ch ON ch.`guid` = c.`character_guid`
     WHERE c.`character_level` IN (60, 70, 80)
     GROUP BY c.`character_guid`, c.`character_level`
     ON DUPLICATE KEY UPDATE
         `character_name` = VALUES(`character_name`),
         `total_spent_time` = VALUES(`total_spent_time`),
         `completed_at` = VALUES(`completed_at`);',
    'SELECT 1;');
PREPARE stmt_completed_milestone FROM @migrate_completed_milestone;
EXECUTE stmt_completed_milestone;
DEALLOCATE PREPARE stmt_completed_milestone;

-- ============================================================
-- 3. 迁移旧 completed 表中的非 milestone 且持有 Token 的记录到 progress
--    当前等级优先取 characters.level，角色已删除则取 completed 表中的最高等级
--    Token item entry = 90002（HARDCORE_TOKEN_ITEM_ID）
-- ============================================================
SET @migrate_completed_progress = IF(@completed_exists = 1,
    'INSERT INTO `hardcore_challenge_progress`
        (`character_guid`, `character_name`, `current_level`, `total_spent_time`)
     SELECT
         c.`character_guid`,
         COALESCE(ch.`name`, \'\'),
         COALESCE(ch.`level`, MAX(c.`character_level`)),
         MAX(c.`total_spent_time`)
     FROM `hardcore_challenge_completed` c
     LEFT JOIN `characters` ch ON ch.`guid` = c.`character_guid`
     JOIN `item_instance` ii ON ii.`owner_guid` = c.`character_guid` AND ii.`itemEntry` = 90002
     WHERE c.`character_level` NOT IN (60, 70, 80)
     GROUP BY c.`character_guid`
     ON DUPLICATE KEY UPDATE
         `character_name` = VALUES(`character_name`),
         `current_level` = VALUES(`current_level`),
         `total_spent_time` = VALUES(`total_spent_time`);',
    'SELECT 1;');
PREPARE stmt_completed_progress FROM @migrate_completed_progress;
EXECUTE stmt_completed_progress;
DEALLOCATE PREPARE stmt_completed_progress;

-- ============================================================
-- 4. 迁移失败记录（位置信息无法补全，置为 0 / NULL）
-- ============================================================
SET @migrate_failed = IF(@failed_exists = 1,
    'INSERT INTO `hardcore_challenge_failure`
        (`character_guid`, `character_name`, `character_level`, `death_reason`,
         `death_location_map_id`, `death_location_zone_id`, `death_location_area_id`,
         `death_location_x`, `death_location_y`, `death_location_z`, `killer_info`,
         `total_spent_time`, `failed_at`)
     SELECT
         f.`character_guid`,
         COALESCE(ch.`name`, \'\'),
         f.`character_level`,
         f.`death_reason`,
         0, 0, 0, 0.0, 0.0, 0.0,
         NULL,
         f.`total_spent_time`,
         NOW()
     FROM `hardcore_challenge_failed` f
     LEFT JOIN `characters` ch ON ch.`guid` = f.`character_guid`
     ON DUPLICATE KEY UPDATE
         `character_name` = VALUES(`character_name`),
         `total_spent_time` = VALUES(`total_spent_time`);',
    'SELECT 1;');
PREPARE stmt_failed FROM @migrate_failed;
EXECUTE stmt_failed;
DEALLOCATE PREPARE stmt_failed;

-- ============================================================
-- 5. 删除旧表
-- ============================================================
DROP TABLE IF EXISTS `hardcore_challenge_completed`;
DROP TABLE IF EXISTS `hardcore_challenge_failed`;

SET FOREIGN_KEY_CHECKS = 1;
