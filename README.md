# mod-temucraft-dungeon-tokens

Two dungeon currencies, only for gear and looks.

- **Gear-Up Tokens** from normal 5-player dungeons, **Transmog Tokens** from heroics: every boss gives some (1 per
  boss below level 20, up to 5 from level 70), and the dungeon's last boss gives three times that again for finishing
  it. Everyone in the group inside the dungeon gets them; full bags send them by mail.
- Both are **account-bound** (heirloom quality): mail them to any of your characters.
- The **Token Quartermaster** (`.tokens` anywhere, or NPC 610010 for your cities: `.npc add 610010`) sells dungeon boss
  loot by slot:
  - Gear-Up Tokens: the gear of your level bracket you can use (bosses' uncommon to epic drops); a piece costs
    more at higher brackets (5 below 20 up to 20 from 70).
  - Transmog Tokens: any dungeon boss item up to your level as a look to keep (soulbound at once, for
    transmogrification), 5 tokens each.

The bosses come from `instance_encounters` (raids left out unless `Raids = 1`), the shop from those bosses' loot
tables (normal and heroic, one reference deep). A hand-made list in `mod_temucraft_token_shop` replaces it.

## Install

Copy the module into `modules/`, re-run CMake and build. The world SQL adds the tokens (610001, 610002), the
quartermaster (610010), the optional shop table and the texts.

Part of the Temucraft modules for AzerothCore 3.3.5a.
