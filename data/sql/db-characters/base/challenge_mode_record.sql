CREATE TABLE IF NOT EXISTS `hardcore_challenge_completed` (
  `id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  `character_guid` BIGINT(20) UNSIGNED NOT NULL,
  `character_level` TINYINT(3) UNSIGNED NOT NULL,
  `achievement` VARCHAR(255),
  `total_spent_time` INT(10) UNSIGNED NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `hardcore_challenge_failed` (
  `id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  `character_guid` BIGINT(20) UNSIGNED NOT NULL,
  `character_level` TINYINT(3) UNSIGNED NOT NULL,
  `death_reason` VARCHAR(255),
  `total_spent_time` INT(10) UNSIGNED NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;