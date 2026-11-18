-- mod-temucraft-dungeon-tokens: the two tokens (account-bound: heirloom quality + bind-to-account flag), the
-- quartermaster, an optional hand-made shop list and the texts.
DELETE FROM `item_template` WHERE `entry` IN (610001, 610002);
INSERT INTO `item_template` (`entry`, `class`, `subclass`, `name`, `displayid`, `Quality`, `Flags`, `BuyCount`, `BuyPrice`, `SellPrice`,
    `InventoryType`, `ItemLevel`, `RequiredLevel`, `maxcount`, `stackable`, `bonding`, `description`, `Material`, `VerifiedBuild`) VALUES
(610001, 10, 0, 'Gear-Up Token', 40753, 7, 134217728, 1, 0, 0, 0, 1, 0, 0, 2000, 1,
    'Earned in normal dungeons. Trade for gear of your level at the Token Quartermaster (.tokens). Account-bound.', -1, -1811),
(610002, 10, 0, 'Transmog Token', 62232, 7, 134217728, 1, 0, 0, 0, 1, 0, 0, 2000, 1,
    'Earned in heroic dungeons. Trade for dungeon looks at the Token Quartermaster (.tokens). Account-bound.', -1, -1811);

DELETE FROM `creature_template` WHERE `entry` = 610010;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `unit_class`, `unit_flags`, `type`, `ScriptName`, `VerifiedBuild`) VALUES
(610010, 'Token Quartermaster', 'Dungeon Tokens', 80, 80, 35, 1, 1, 2, 7, 'npc_temucraft_token_quartermaster', -1811);
DELETE FROM `creature_template_model` WHERE `CreatureID` = 610010;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(610010, 0, 28131, 1, 1, -1811);

-- Hand-made shop (optional): rows here replace the automatic list of dungeon boss loot. token 1 = Gear-Up, 2 = Transmog.
CREATE TABLE IF NOT EXISTS `mod_temucraft_token_shop` (
    `token` TINYINT UNSIGNED NOT NULL,
    `item`  INT UNSIGNED NOT NULL,
    PRIMARY KEY (`token`, `item`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-dungeon-tokens';
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-dungeon-tokens', 1, '|cffa335eeBoss defeated:|r {} x {}.'),
('mod-temucraft-dungeon-tokens', 2, '|cffa335eeDungeon complete:|r {} x {}.'),
('mod-temucraft-dungeon-tokens', 3, 'Gear for Gear-Up Tokens (you have {})'),
('mod-temucraft-dungeon-tokens', 4, 'Looks for Transmog Tokens (you have {})'),
('mod-temucraft-dungeon-tokens', 5, '{}'),
('mod-temucraft-dungeon-tokens', 6, 'More...'),
('mod-temucraft-dungeon-tokens', 7, 'Back'),
('mod-temucraft-dungeon-tokens', 8, 'Bought for {} token(s).'),
('mod-temucraft-dungeon-tokens', 9, 'You don''t have enough tokens (they must be in your bags).'),
('mod-temucraft-dungeon-tokens', 10, 'Nothing for you here yet.'),
('mod-temucraft-dungeon-tokens', 11, 'Your bags were full: the tokens are in your mailbox.'),
('mod-temucraft-dungeon-tokens', 12, 'Token Quartermaster');
