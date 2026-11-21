-- mod-temucraft-dungeon-tokens: its player commands (all under .temucraft) as RBAC permissions 102350-102399. Linked to the stock player
-- role 195 and the Temucraft player role 1010, so every account has them by default. To take one away, remove its
-- link here (or deny it per account with .rbac account deny <account> <id>); to give it to staff only, link it to
-- 1011-1014 instead.
DELETE FROM `rbac_linked_permissions` WHERE `linkedId` BETWEEN 102350 AND 102399;
DELETE FROM `rbac_permissions` WHERE `id` BETWEEN 102350 AND 102399;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(102350, 'Command: temucraft tokens');
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(195, 102350),
(1010, 102350);
