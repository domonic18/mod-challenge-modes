-- 硬核挑战数据存储重构：从旧表迁移数据并删除旧表
-- 适用于已存在 hardcore_challenge_completed / hardcore_challenge_failed 的服务器

SET @completed_exists = (SELECT COUNT(*) FROM `information_schema`.`tables`
    WHERE `table_schema` = DATABASE() AND `table_name` = 'hardcore_challenge_completed');

SET @failed_exists = (SELECT COUNT(*) FROM `information_schema`.`tables`
    WHERE `table_schema` = DATABASE() AND `table_name` = 'hardcore_challenge_failed');

-- 1. 创建新表（若不存在）
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

-- 2. 迁移旧表数据（仅在旧表存在时执行）
SET @migrate_completed = IF(@completed_exists = 1,
    'INSERT INTO `hardcore_challenge_success` (`character_guid`, `character_name`, `completed_level`, `total_spent_time`, `completed_at`)
     SELECT c.`character_guid`, COALESCE(ch.`name`, \'\'), MAX(c.`character_level`), MAX(c.`total_spent_time`), NOW()
     FROM `hardcore_challenge_completed` c
     LEFT JOIN `characters` ch ON ch.`guid` = c.`character_guid`
     GROUP BY c.`character_guid`;',
    'SELECT 1;');
PREPARE stmt_completed FROM @migrate_completed;
EXECUTE stmt_completed;
DEALLOCATE PREPARE stmt_completed;

SET @migrate_failed = IF(@failed_exists = 1,
    'INSERT INTO `hardcore_challenge_failure` (`character_guid`, `character_name`, `character_level`, `death_reason`, `death_location_map_id`, `death_location_zone_id`, `death_location_area_id`, `death_location_x`, `death_location_y`, `death_location_z`, `killer_info`, `total_spent_time`, `failed_at`)
     SELECT f.`character_guid`, COALESCE(ch.`name`, \'\'), f.`character_level`, f.`death_reason`, 0, 0, 0, 0.0, 0.0, 0.0, NULL, f.`total_spent_time`, NOW()
     FROM `hardcore_challenge_failed` f
     LEFT JOIN `characters` ch ON ch.`guid` = f.`character_guid`;',
    'SELECT 1;');
PREPARE stmt_failed FROM @migrate_failed;
EXECUTE stmt_failed;
DEALLOCATE PREPARE stmt_failed;

-- 3. 删除旧表
DROP TABLE IF EXISTS `hardcore_challenge_completed`;
DROP TABLE IF EXISTS `hardcore_challenge_failed`;
