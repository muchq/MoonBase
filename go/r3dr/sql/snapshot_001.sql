CREATE TABLE `urls` (
    `id` bigint(20) NOT NULL AUTO_INCREMENT,
    `short_url` varchar(20) NOT NULL,
    `long_url` varchar(1000) NOT NULL,
    `created_at` timestamp NULL DEFAULT current_timestamp(),
    `expires_at` timestamp DEFAULT NULL,
    `created_by` varchar(40) DEFAULT NULL,
    PRIMARY KEY (`id`),
    KEY `slug_idx` (`short_url`) USING HASH
);
