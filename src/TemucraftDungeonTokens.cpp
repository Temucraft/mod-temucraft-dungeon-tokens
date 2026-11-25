/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

// Dungeon tokens: two account-bound currencies earned in 5-player dungeons, only for gear and looks.
// - Gear-Up Tokens from normal dungeons, Transmog Tokens from heroics: for every boss killed and more for finishing the
//   dungeon (its last boss), more at higher level brackets. Both are account-bound items, so they can be mailed to
//   any of your characters.
// - The Token Quartermaster (.temucraft tokens, or the NPC) sells dungeon boss loot: with Gear-Up Tokens the gear of your level
//   bracket that you can use (pricier at higher brackets), with Transmog Tokens any dungeon boss item up to your level
//   as a look to keep (soulbound, for transmogrification).
// Bosses and their loot come from instance_encounters and the loot tables; a shop list of your own can replace them
// (mod_temucraft_token_shop).

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Item.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "TemucraftModuleText.h"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    constexpr char const* MODULE = "mod-temucraft-dungeon-tokens";
    constexpr uint32 SENDER = 0x70C0;
    constexpr uint32 PAGE_SIZE = 25;

    enum Text : uint32
    {
        T_EARNED = 1, T_FINISHED, T_MENU_GEAR, T_MENU_LOOK, T_SLOT, T_MORE, T_BACK, T_BOUGHT, T_POOR, T_NOTHING, T_FULL, T_HEAD
    };
    char const* Say(Player* p, uint32 id, char const* fallback) { return TemucraftModuleText(MODULE, p, id, fallback); }

    struct Settings
    {
        bool enable = true;
        uint32 gearToken = 610001, lookToken = 610002;
        std::vector<std::pair<uint32, uint32>> perBoss;    // from level -> tokens per boss
        uint32 finishMultiplier = 3;
        std::vector<std::pair<uint32, uint32>> gearCost;   // from level -> cost
        uint32 lookCost = 5;
        uint32 minQuality = 2;
        bool raids = false;
        uint32 quartermaster = 610010;
    } settings;

    struct Boss { bool last = false; };
    std::unordered_map<uint32, Boss> bosses;               // creature entry -> boss (5-player dungeons)
    std::unordered_set<uint32> raidBosses;                 // when raids give tokens too
    std::vector<uint32> gearItems, lookItems;              // shop lists (item entries)
    // Boss kills already paid out, kept on the boss's own Map (Map::CustomData): every instance copy numbers its
    // creatures from 1, so the same boss in two copies has the same ObjectGuid. The data goes away with the map, and a
    // map's kill hooks only run on that map's update thread, so no lock is needed.
    struct PaidBosses : DataMap::Base
    {
        std::unordered_set<ObjectGuid::LowType> spawns;    // DB spawns (spawn id)
        std::unordered_set<ObjectGuid> summons;            // summoned bosses (no spawn id)
    };
    constexpr char const* PAID_KEY = "temucraft_dungeon_tokens_paid";

    bool MarkPaid(Map* map, Creature* boss)
    {
        PaidBosses* paid = map->CustomData.GetDefault<PaidBosses>(PAID_KEY);
        if (ObjectGuid::LowType const spawn = boss->GetSpawnId())
            return paid->spawns.insert(spawn).second;
        return paid->summons.insert(boss->GetGUID()).second;
    }

    std::vector<std::pair<uint32, uint32>> ParseSteps(std::string text)
    {
        std::vector<std::pair<uint32, uint32>> out;
        std::replace(text.begin(), text.end(), ',', ' ');
        std::istringstream in(text);
        std::string item;
        while (in >> item)
            if (std::size_t colon = item.find(':'); colon != std::string::npos)
                out.emplace_back(uint32(std::strtoul(item.substr(0, colon).c_str(), nullptr, 10)), uint32(std::strtoul(item.substr(colon + 1).c_str(), nullptr, 10)));
        std::sort(out.begin(), out.end());
        return out;
    }

    uint32 Step(std::vector<std::pair<uint32, uint32>> const& steps, uint32 level)
    {
        uint32 value = steps.empty() ? 1 : steps.front().second;
        for (auto const& [from, v] : steps)
            if (level >= from)
                value = v;
        return value;
    }

    void LoadSettings()
    {
        settings.enable = sConfigMgr->GetOption<bool>("Temucraft.DungeonTokens.Enable", true);
        settings.gearToken = sConfigMgr->GetOption<uint32>("Temucraft.DungeonTokens.GearToken", 610001);
        settings.lookToken = sConfigMgr->GetOption<uint32>("Temucraft.DungeonTokens.TransmogToken", 610002);
        settings.perBoss = ParseSteps(sConfigMgr->GetOption<std::string>("Temucraft.DungeonTokens.PerBoss", "1:1 20:2 40:3 60:4 70:5"));
        settings.finishMultiplier = sConfigMgr->GetOption<uint32>("Temucraft.DungeonTokens.FinishMultiplier", 3);
        settings.gearCost = ParseSteps(sConfigMgr->GetOption<std::string>("Temucraft.DungeonTokens.GearCost", "1:5 20:8 40:12 60:16 70:20"));
        settings.lookCost = sConfigMgr->GetOption<uint32>("Temucraft.DungeonTokens.TransmogCost", 5);
        settings.minQuality = sConfigMgr->GetOption<uint32>("Temucraft.DungeonTokens.MinQuality", 2);
        settings.raids = sConfigMgr->GetOption<bool>("Temucraft.DungeonTokens.Raids", false);
        settings.quartermaster = sConfigMgr->GetOption<uint32>("Temucraft.DungeonTokens.Quartermaster", 610010);
    }

    bool Wearable(ItemTemplate const* proto)
    {
        return proto && (proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR) && proto->InventoryType != INVTYPE_NON_EQUIP &&
            proto->Quality >= settings.minQuality && proto->Quality <= ITEM_QUALITY_EPIC;
    }

    // bosses from instance_encounters; their loot (direct and one reference deep) for the shop
    void Load()
    {
        bosses.clear();
        raidBosses.clear();
        gearItems.clear();
        lookItems.clear();

        std::set<uint32> lootIds;
        if (QueryResult r = WorldDatabase.Query("SELECT `entry`, `creditEntry`, `lastEncounterDungeon` FROM `instance_encounters` WHERE `creditType` = 0"))
            do
            {
                Field* f = r->Fetch();
                DungeonEncounterEntry const* enc = sDungeonEncounterStore.LookupEntry(f[0].Get<uint32>());
                MapEntry const* map = enc ? sMapStore.LookupEntry(enc->mapId) : nullptr;
                uint32 const entry = f[1].Get<uint32>();
                if (!map || !map->IsDungeon())
                    continue;
                if (map->IsRaid())
                {
                    raidBosses.insert(entry);
                    continue;
                }
                bosses[entry].last = f[2].Get<uint32>() != 0;
                if (CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(entry))
                {
                    if (ct->lootid)
                        lootIds.insert(ct->lootid);
                    for (uint32 d : ct->DifficultyEntry)
                        if (d)
                        {
                            bosses[d].last = bosses[entry].last;
                            if (CreatureTemplate const* hc = sObjectMgr->GetCreatureTemplate(d))
                                if (hc->lootid)
                                    lootIds.insert(hc->lootid);
                        }
                }
            } while (r->NextRow());

        // own shop list wins when there is one
        if (QueryResult r = WorldDatabase.Query("SELECT `token`, `item` FROM `mod_temucraft_token_shop`"))
        {
            do
            {
                Field* f = r->Fetch();
                (f[0].Get<uint8>() == 2 ? lookItems : gearItems).push_back(f[1].Get<uint32>());
            } while (r->NextRow());
        }
        else if (!lootIds.empty())
        {
            std::ostringstream ids;
            bool first = true;
            for (uint32 id : lootIds)
            {
                ids << (first ? "" : ",") << id;
                first = false;
            }
            std::set<uint32> items, refs;
            if (QueryResult r = WorldDatabase.Query("SELECT `Item`, `Reference` FROM `creature_loot_template` WHERE `Entry` IN ({})", ids.str()))
                do
                {
                    Field* f = r->Fetch();
                    if (uint32 ref = f[1].Get<uint32>())
                        refs.insert(ref);
                    else
                        items.insert(f[0].Get<uint32>());
                } while (r->NextRow());
            if (!refs.empty())
            {
                std::ostringstream rids;
                first = true;
                for (uint32 id : refs)
                {
                    rids << (first ? "" : ",") << id;
                    first = false;
                }
                if (QueryResult r = WorldDatabase.Query("SELECT `Item` FROM `reference_loot_template` WHERE `Entry` IN ({}) AND `Reference` = 0", rids.str()))
                    do items.insert(r->Fetch()[0].Get<uint32>()); while (r->NextRow());
            }
            for (uint32 item : items)
                if (Wearable(sObjectMgr->GetItemTemplate(item)))
                {
                    gearItems.push_back(item);
                    lookItems.push_back(item);
                }
        }
        LOG_INFO("server.loading", ">> mod-temucraft-dungeon-tokens: {} dungeon bosses, {} gear / {} look items", bosses.size(), gearItems.size(), lookItems.size());
    }

    void Give(Player* p, uint32 token, uint32 count, bool finished)
    {
        if (!count)
            return;
        ItemPosCountVec dest;
        uint32 noSpace = 0;
        if (p->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, token, count, &noSpace) != EQUIP_ERR_OK)
        {
            // full bags: send them by mail
            MailDraft draft("Dungeon tokens", "Your bags were full.");
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            if (Item* item = Item::CreateItem(token, count, p))
            {
                item->SaveToDB(trans);
                draft.AddItem(item);
            }
            draft.SendMailTo(trans, MailReceiver(p), MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_DEFAULT));
            CharacterDatabase.CommitTransaction(trans);
            ChatHandler(p->GetSession()).SendSysMessage(Say(p, T_FULL, "Your bags were full: the tokens are in your mailbox."));
            return;
        }
        if (Item* item = p->StoreNewItem(dest, token, true))
            p->SendNewItem(item, count, true, false);
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(token);
        ChatHandler(p->GetSession()).PSendSysMessage(finished ? Say(p, T_FINISHED, "|cffa335eeDungeon complete:|r {} x {}.") : Say(p, T_EARNED, "|cffa335eeBoss defeated:|r {} x {}."),
            count, proto ? proto->Name1 : "token");
    }

    void BossKilled(Player* killer, Creature* boss)
    {
        if (!settings.enable || !killer || !boss)
            return;
        Map* map = boss->GetMap();
        if (!map || !map->IsDungeon())
            return;
        bool const raid = map->IsRaid();
        uint32 const entry = boss->GetEntry();
        auto itr = bosses.find(entry);
        if (raid ? (!settings.raids || !raidBosses.count(entry)) : itr == bosses.end())
            return;
        if (!MarkPaid(map, boss))

            return;
        bool const heroic = map->GetDifficulty() != DUNGEON_DIFFICULTY_NORMAL;
        uint32 const token = heroic ? settings.lookToken : settings.gearToken;
        bool const last = !raid && itr->second.last;

        std::vector<Player*> members;
        if (Group* g = killer->GetGroup())
        {
            for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
                if (Player* m = ref->GetSource())
                    if (m->GetMap() == map)
                        members.push_back(m);
        }
        else
            members.push_back(killer);
        for (Player* m : members)
        {
            uint32 const per = Step(settings.perBoss, m->GetLevel());
            Give(m, token, per, false);
            if (last && settings.finishMultiplier)
                Give(m, token, per * settings.finishMultiplier, true);
        }
    }

    // shop
    enum Slot : uint32 { S_HEAD = 1, S_NECK, S_SHOULDER, S_BACK, S_CHEST, S_WRIST, S_HANDS, S_WAIST, S_LEGS, S_FEET, S_FINGER, S_TRINKET, S_WEAPON, S_OFFHAND, S_RANGED, S_COUNT };

    uint32 SlotOf(ItemTemplate const* p)
    {
        switch (p->InventoryType)
        {
            case INVTYPE_HEAD: return S_HEAD;
            case INVTYPE_NECK: return S_NECK;
            case INVTYPE_SHOULDERS: return S_SHOULDER;
            case INVTYPE_CLOAK: return S_BACK;
            case INVTYPE_CHEST: case INVTYPE_ROBE: return S_CHEST;
            case INVTYPE_WRISTS: return S_WRIST;
            case INVTYPE_HANDS: return S_HANDS;
            case INVTYPE_WAIST: return S_WAIST;
            case INVTYPE_LEGS: return S_LEGS;
            case INVTYPE_FEET: return S_FEET;
            case INVTYPE_FINGER: return S_FINGER;
            case INVTYPE_TRINKET: return S_TRINKET;
            case INVTYPE_WEAPON: case INVTYPE_2HWEAPON: case INVTYPE_WEAPONMAINHAND: return S_WEAPON;
            case INVTYPE_SHIELD: case INVTYPE_HOLDABLE: case INVTYPE_WEAPONOFFHAND: return S_OFFHAND;
            default: return S_RANGED;
        }
    }

    char const* SlotName(uint32 s)
    {
        static char const* names[] = { "", "Head", "Neck", "Shoulder", "Back", "Chest", "Wrist", "Hands", "Waist", "Legs", "Feet",
                                       "Finger", "Trinket", "Weapon", "Off hand / shield", "Ranged / relic" };
        return s < S_COUNT ? names[s] : "";
    }

    // items of the shop the player may buy, for one slot
    std::vector<ItemTemplate const*> Offer(Player* p, bool look, uint32 slot)
    {
        std::vector<ItemTemplate const*> out;
        uint32 const level = p->GetLevel();
        // gear: the player's level bracket (from the bracket start to the player's level)
        uint32 from = 0;
        for (auto const& [start, cost] : settings.gearCost)
            if (level >= start)
                from = start;
        for (uint32 entry : look ? lookItems : gearItems)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
            if (!proto || SlotOf(proto) != slot)
                continue;
            if (proto->RequiredLevel > level)
                continue;
            if (!look && proto->RequiredLevel < from && proto->ItemLevel < from)
                continue;
            if (!look && p->CanUseItem(proto) != EQUIP_ERR_OK)
                continue;
            if (look && proto->AllowableClass && !(proto->AllowableClass & p->getClassMask()))
                continue;
            out.push_back(proto);
        }
        std::sort(out.begin(), out.end(), [](ItemTemplate const* a, ItemTemplate const* b) { return a->ItemLevel != b->ItemLevel ? a->ItemLevel > b->ItemLevel : a->ItemId < b->ItemId; });
        return out;
    }

    uint32 Cost(Player* p, bool look) { return look ? settings.lookCost : Step(settings.gearCost, p->GetLevel()); }

    // actions: 1 gear menu, 2 look menu; 100 + look*1000 + slot*100... encoded as kind(1)|slot(4 bits)|page or item
    uint32 SlotAction(bool look, uint32 slot, uint32 page) { return 0x10000000 | (look ? 0x08000000 : 0) | (slot << 16) | page; }
    uint32 BuyAction(bool look, uint32 index) { return 0x20000000 | (look ? 0x08000000 : 0) | index; }

    void MainMenu(Player* p, ObjectGuid source)
    {
        ClearGossipMenuFor(p);
        AddGossipItemFor(p, GOSSIP_ICON_VENDOR, Acore::StringFormat(Say(p, T_MENU_GEAR, "Gear for Gear-Up Tokens (you have {})"), p->GetItemCount(settings.gearToken, true)), SENDER, 1);
        AddGossipItemFor(p, GOSSIP_ICON_TABARD, Acore::StringFormat(Say(p, T_MENU_LOOK, "Looks for Transmog Tokens (you have {})"), p->GetItemCount(settings.lookToken, true)), SENDER, 2);
        SendGossipMenuFor(p, DEFAULT_GOSSIP_MESSAGE, source);
    }

    void SlotMenu(Player* p, ObjectGuid source, bool look)
    {
        ClearGossipMenuFor(p);
        for (uint32 s = S_HEAD; s < S_COUNT; ++s)
            if (!Offer(p, look, s).empty())
                AddGossipItemFor(p, GOSSIP_ICON_CHAT, SlotName(s), SENDER, SlotAction(look, s, 0));
        AddGossipItemFor(p, GOSSIP_ICON_CHAT, Say(p, T_BACK, "Back"), SENDER, 3);
        SendGossipMenuFor(p, DEFAULT_GOSSIP_MESSAGE, source);
    }

    std::unordered_map<uint32, std::vector<uint32>> lastList;   // player -> items shown, for buying by index

    void ItemMenu(Player* p, ObjectGuid source, bool look, uint32 slot, uint32 page)
    {
        ClearGossipMenuFor(p);
        std::vector<ItemTemplate const*> items = Offer(p, look, slot);
        uint32 const cost = Cost(p, look);
        std::vector<uint32>& shown = lastList[p->GetGUID().GetCounter()];
        shown.clear();
        uint32 const start = page * PAGE_SIZE;
        for (uint32 i = start; i < items.size() && i < start + PAGE_SIZE; ++i)
        {
            ItemTemplate const* proto = items[i];
            shown.push_back(proto->ItemId);
            std::string const link = Acore::StringFormat("|c{:08x}|Hitem:{}:0:0:0:0:0:0:0:0|h[{}]|h|r (ilvl {})",
                ItemQualityColors[proto->Quality], proto->ItemId, proto->Name1, proto->ItemLevel);
            AddGossipItemFor(p, GOSSIP_ICON_MONEY_BAG, link, SENDER, BuyAction(look, uint32(shown.size() - 1)),
                Acore::StringFormat("{} {} token(s)", cost, look ? "Transmog" : "Gear-Up"), 0, false);
        }
        if (items.size() > start + PAGE_SIZE)
            AddGossipItemFor(p, GOSSIP_ICON_CHAT, Say(p, T_MORE, "More..."), SENDER, SlotAction(look, slot, page + 1));
        AddGossipItemFor(p, GOSSIP_ICON_CHAT, Say(p, T_BACK, "Back"), SENDER, look ? 2 : 1);
        SendGossipMenuFor(p, DEFAULT_GOSSIP_MESSAGE, source);
    }

    void Buy(Player* p, bool look, uint32 index)
    {
        ChatHandler handler(p->GetSession());
        std::vector<uint32> const& shown = lastList[p->GetGUID().GetCounter()];
        if (index >= shown.size())
            return;
        uint32 const entry = shown[index];
        uint32 const token = look ? settings.lookToken : settings.gearToken;
        uint32 const cost = Cost(p, look);
        if (p->GetItemCount(token, false) < cost)
        {
            handler.SendSysMessage(Say(p, T_POOR, "You don't have enough tokens (they must be in your bags)."));
            return;
        }
        ItemPosCountVec dest;
        if (p->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, 1) != EQUIP_ERR_OK)
        {
            p->SendEquipError(EQUIP_ERR_INVENTORY_FULL, nullptr);
            return;
        }
        p->DestroyItemCount(token, cost, true);
        if (Item* item = p->StoreNewItem(dest, entry, true))
        {
            if (look)
                item->SetBinding(true);   // a look to keep: soulbound at once
            p->SendNewItem(item, 1, true, false);
        }
        handler.PSendSysMessage(Say(p, T_BOUGHT, "Bought for {} token(s)."), cost);
    }

    bool Select(Player* p, ObjectGuid source, uint32 sender, uint32 action)
    {
        if (sender != SENDER)
            return false;
        if (action == 1 || action == 2)
            SlotMenu(p, source, action == 2);
        else if (action == 3)
            MainMenu(p, source);
        else if (action & 0x10000000)
            ItemMenu(p, source, action & 0x08000000, (action >> 16) & 0xFF, action & 0xFFFF);
        else if (action & 0x20000000)
        {
            Buy(p, action & 0x08000000, action & 0xFFFF);
            CloseGossipMenuFor(p);
        }
        return true;
    }
}

class TemucraftDungeonTokensWorld : public WorldScript
{
public:
    TemucraftDungeonTokensWorld() : WorldScript("TemucraftDungeonTokensWorld", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP }) { }
    void OnAfterConfigLoad(bool reload) override { LoadSettings(); if (reload) Load(); }
    void OnStartup() override { Load(); }
};

class TemucraftDungeonTokensPlayer : public PlayerScript
{
public:
    TemucraftDungeonTokensPlayer() : PlayerScript("TemucraftDungeonTokensPlayer",
        { PLAYERHOOK_ON_CREATURE_KILL, PLAYERHOOK_ON_CREATURE_KILLED_BY_PET, PLAYERHOOK_ON_LOGOUT }) { }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override { BossKilled(killer, killed); }
    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override { BossKilled(owner, killed); }
    void OnPlayerLogout(Player* p) override { lastList.erase(p->GetGUID().GetCounter()); }
};

// the quartermaster NPC
class npc_temucraft_token_quartermaster : public CreatureScript
{
public:
    npc_temucraft_token_quartermaster() : CreatureScript("npc_temucraft_token_quartermaster") { }

    bool OnGossipHello(Player* p, Creature* creature) override
    {
        if (!settings.enable)
            return false;
        MainMenu(p, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* p, Creature* creature, uint32 sender, uint32 action) override
    {
        return Select(p, creature->GetGUID(), sender, action);
    }
};

// .temucraft tokens: the same shop anywhere (gossip from the player)
class TemucraftDungeonTokensCommands : public CommandScript
{
public:
    TemucraftDungeonTokensCommands() : CommandScript("TemucraftDungeonTokensCommands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable table = { { "temucraft tokens", HandleTokens, 102350, Console::No } };
        return table;
    }

    static bool HandleTokens(ChatHandler* handler)
    {
        if (Player* p = handler->GetPlayer(); p && settings.enable)
            MainMenu(p, p->GetGUID());
        return true;
    }
};

// gossip opened on the player (.temucraft tokens) comes back here
class TemucraftDungeonTokensGossip : public PlayerScript
{
public:
    TemucraftDungeonTokensGossip() : PlayerScript("TemucraftDungeonTokensGossip", { PLAYERHOOK_ON_GOSSIP_SELECT }) { }

    void OnPlayerGossipSelect(Player* p, uint32 menuId, uint32 sender, uint32 action) override
    {
        if (menuId == 0)
            Select(p, p->GetGUID(), sender, action);
    }
};

void AddTemucraftDungeonTokensScripts()
{
    new TemucraftDungeonTokensWorld();
    new TemucraftDungeonTokensPlayer();
    new npc_temucraft_token_quartermaster();
    new TemucraftDungeonTokensCommands();
    new TemucraftDungeonTokensGossip();
}
