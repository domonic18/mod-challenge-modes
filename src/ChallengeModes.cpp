/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "ChallengeModes.h"
#include "WorldSessionMgr.h"
#include "BanMgr.h"
#include "SpellMgr.h"
#include "StringFormat.h"

ChallengeModes* ChallengeModes::instance()
{
    static ChallengeModes instance;
    return &instance;
}

bool ChallengeModes::challengeEnabledForPlayer(ChallengeModeSettings setting, Player* player) const
{
    if (!enabled() || !challengeEnabled(setting))
    {
        return false;
    }
    return player->GetPlayerSetting("mod-challenge-modes", setting).value;
}

bool ChallengeModes::challengeEnabledCheckbyToken(ChallengeModeSettings setting, Player* player) const
{
    const int Hardcore_token_item = 90002;

    if (!enabled() || !challengeEnabled(setting))
    {
        return false;
    }
    // 通过角色是否拥有Token物品进行相关判断
    return player->HasItemCount(Hardcore_token_item, 1, true);
}



bool ChallengeModes::challengeEnabled(ChallengeModeSettings setting) const
{
    switch (setting)
    {
        case SETTING_HARDCORE:
            return hardcoreEnable;
        case SETTING_SEMI_HARDCORE:
            return semiHardcoreEnable;
        case SETTING_SELF_CRAFTED:
            return selfCraftedEnable;
        case SETTING_ITEM_QUALITY_LEVEL:
            return itemQualityLevelEnable;
        case SETTING_SLOW_XP_GAIN:
            return slowXpGainEnable;
        case SETTING_VERY_SLOW_XP_GAIN:
            return verySlowXpGainEnable;
        case SETTING_QUEST_XP_ONLY:
            return questXpOnlyEnable;
        case SETTING_IRON_MAN:
            return ironManEnable;
    }
    return false;
}

float ChallengeModes::getXpBonusForChallenge(ChallengeModeSettings setting) const
{
    switch (setting)
    {
        case SETTING_HARDCORE:
            return hardcoreXpBonus;
        case SETTING_SEMI_HARDCORE:
            return semiHardcoreXpBonus;
        case SETTING_SELF_CRAFTED:
            return selfCraftedXpBonus;
        case SETTING_ITEM_QUALITY_LEVEL:
            return itemQualityLevelXpBonus;
        case SETTING_SLOW_XP_GAIN:
            return 0.5f;
        case SETTING_VERY_SLOW_XP_GAIN:
            return 0.25f;
        case SETTING_QUEST_XP_ONLY:
            return questXpOnlyXpBonus;
        case SETTING_IRON_MAN:
            return 1;
    }
    return 1;
}

const std::unordered_map<uint8, uint32> *ChallengeModes::getTitleMapForChallenge(ChallengeModeSettings setting) const
{
    switch (setting)
    {
        case SETTING_HARDCORE:
            return &hardcoreTitleRewards;
        case SETTING_SEMI_HARDCORE:
            return &semiHardcoreTitleRewards;
        case SETTING_SELF_CRAFTED:
            return &selfCraftedTitleRewards;
        case SETTING_ITEM_QUALITY_LEVEL:
            return &itemQualityLevelTitleRewards;
        case SETTING_SLOW_XP_GAIN:
            return &slowXpGainTitleRewards;
        case SETTING_VERY_SLOW_XP_GAIN:
            return &verySlowXpGainTitleRewards;
        case SETTING_QUEST_XP_ONLY:
            return &questXpOnlyTitleRewards;
        case SETTING_IRON_MAN:
            return &ironManTitleRewards;
    }
    return {};
}

const std::unordered_map<uint8, uint32> *ChallengeModes::getTalentMapForChallenge(ChallengeModeSettings setting) const
{
    switch (setting)
    {
        case SETTING_HARDCORE:
            return &hardcoreTalentRewards;
        case SETTING_SEMI_HARDCORE:
            return &semiHardcoreTalentRewards;
        case SETTING_SELF_CRAFTED:
            return &selfCraftedTalentRewards;
        case SETTING_ITEM_QUALITY_LEVEL:
            return &itemQualityLevelTalentRewards;
        case SETTING_SLOW_XP_GAIN:
            return &slowXpGainTalentRewards;
        case SETTING_VERY_SLOW_XP_GAIN:
            return &verySlowXpGainTalentRewards;
        case SETTING_QUEST_XP_ONLY:
            return &questXpOnlyTalentRewards;
        case SETTING_IRON_MAN:
            return &ironManTalentRewards;
    }
    return {};
}

const std::unordered_map<uint8, uint32> *ChallengeModes::getItemMapForChallenge(ChallengeModeSettings setting) const
{
    switch (setting)
    {
        case SETTING_HARDCORE:
            return &hardcoreItemRewards;
        case SETTING_SEMI_HARDCORE:
            return &semiHardcoreItemRewards;
        case SETTING_SELF_CRAFTED:
            return &selfCraftedItemRewards;
        case SETTING_ITEM_QUALITY_LEVEL:
            return &itemQualityLevelItemRewards;
        case SETTING_SLOW_XP_GAIN:
            return &slowXpGainItemRewards;
        case SETTING_VERY_SLOW_XP_GAIN:
            return &verySlowXpGainItemRewards;
        case SETTING_QUEST_XP_ONLY:
            return &questXpOnlyItemRewards;
        case SETTING_IRON_MAN:
            return &ironManItemRewards;
    }
    return {};
}

class ChallengeModes_WorldScript : public WorldScript
{
public:
    ChallengeModes_WorldScript()
        : WorldScript("ChallengeModes_WorldScript")
    {}

    void OnBeforeConfigLoad(bool /*reload*/) override
    {
        LoadConfig();
    }

private:
    static void LoadStringToMap(std::unordered_map<uint8, uint32> &mapToLoad, const std::string &configString)
    {
        std::string delimitedValue;
        std::stringstream configIdStream;

        configIdStream.str(configString);
        // Process each config ID in the string, delimited by the comma - "," and then space " "
        while (std::getline(configIdStream, delimitedValue, ','))
        {
            std::string pairOne, pairTwo;
            std::stringstream configPairStream(delimitedValue);
            configPairStream>>pairOne>>pairTwo;
            auto configLevel = atoi(pairOne.c_str());
            auto rewardValue = atoi(pairTwo.c_str());
            mapToLoad[configLevel] = rewardValue;
        }
    }

    static void LoadConfig()
    {
        sChallengeModes->challengesEnabled = sConfigMgr->GetOption<bool>("ChallengeModes.Enable", false);
        if (sChallengeModes->enabled())
        {
            for (auto& [confName, rewardMap] : sChallengeModes->rewardConfigMap)
            {
                rewardMap->clear();
                LoadStringToMap(*rewardMap, sConfigMgr->GetOption<std::string>(confName, ""));
            }

            sChallengeModes->hardcoreEnable          = sConfigMgr->GetOption<bool>("Hardcore.Enable", true);
            sChallengeModes->semiHardcoreEnable      = sConfigMgr->GetOption<bool>("SemiHardcore.Enable", true);
            sChallengeModes->selfCraftedEnable       = sConfigMgr->GetOption<bool>("SelfCrafted.Enable", true);
            sChallengeModes->itemQualityLevelEnable  = sConfigMgr->GetOption<bool>("ItemQualityLevel.Enable", true);
            sChallengeModes->slowXpGainEnable        = sConfigMgr->GetOption<bool>("SlowXpGain.Enable", true);
            sChallengeModes->verySlowXpGainEnable    = sConfigMgr->GetOption<bool>("VerySlowXpGain.Enable", true);
            sChallengeModes->questXpOnlyEnable       = sConfigMgr->GetOption<bool>("QuestXpOnly.Enable", true);
            sChallengeModes->ironManEnable           = sConfigMgr->GetOption<bool>("IronMan.Enable", true);

            sChallengeModes->hardcoreXpBonus         = sConfigMgr->GetOption<float>("Hardcore.XPMultiplier", 1.0f);
            sChallengeModes->semiHardcoreXpBonus     = sConfigMgr->GetOption<float>("SemiHardcore.XPMultiplier", 1.0f);
            sChallengeModes->selfCraftedXpBonus      = sConfigMgr->GetOption<float>("SelfCrafted.XPMultiplier", 1.0f);
            sChallengeModes->itemQualityLevelXpBonus = sConfigMgr->GetOption<float>("ItemQualityLevel.XPMultiplier", 1.0f);
            sChallengeModes->questXpOnlyXpBonus      = sConfigMgr->GetOption<float>("QuestXpOnly.XPMultiplier", 1.0f);
        }
    }
};

class ChallengeMode : public PlayerScript
{
public:
    explicit ChallengeMode(const char *scriptName,
                           ChallengeModeSettings settingName)
            : PlayerScript(scriptName), settingName(settingName)
    { }

    static bool mapContainsKey(const std::unordered_map<uint8, uint32>* mapToCheck, uint8 key)
    {
        return (mapToCheck->find(key) != mapToCheck->end());
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 /*xpSource*/) override
    {
        if (!sChallengeModes->challengeEnabledForPlayer(settingName, player))
        {
            return;
        }
        amount *= sChallengeModes->getXpBonusForChallenge(settingName);
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldlevel*/) override
    {
        if (!sChallengeModes->challengeEnabledCheckbyToken(settingName, player))
        {
            return;
        }
        const std::unordered_map<uint8, uint32> *titleRewardMap = sChallengeModes->getTitleMapForChallenge(settingName);
        const std::unordered_map<uint8, uint32> *talentRewardMap = sChallengeModes->getTalentMapForChallenge(settingName);
        const std::unordered_map<uint8, uint32> *itemRewardMap = sChallengeModes->getItemMapForChallenge(settingName);
        uint8 level = player->GetLevel();
        if (mapContainsKey(titleRewardMap, level))
        {
            CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(titleRewardMap->at(level));
            if (!titleInfo)
            {
                LOG_ERROR("mod-challenge-modes", "Invalid title ID {}!", titleRewardMap->at(level));
                return;
            }
            ChatHandler handler(player->GetSession());
            std::string tNameLink = handler.GetNameLink(player);
            std::string titleNameStr = Acore::StringFormat(player->getGender() == GENDER_MALE ? titleInfo->nameMale[handler.GetSessionDbcLocale()] : titleInfo->nameFemale[handler.GetSessionDbcLocale()], player->GetName());
            player->SetTitle(titleInfo);

            std::string plr = player->GetName();
            std::string tag_colour = "7bbef7";
            std::string plr_colour = "ffff00";
            std::ostringstream stream;
            stream << "|CFF" << plr_colour << "[硬核模式挑战]|r|CFF" << tag_colour <<
                " 角色 |r|cff" << plr_colour << plr << "|r|cff" << tag_colour <<
                " 完成 " << static_cast<int>(level) << "级硬核挑战，获得" << "|r|cff" << plr_colour << titleNameStr << "|r|cff" << tag_colour <<
                " 头衔奖励，恭喜！|r";
            sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, stream.str().c_str());
        }
        if (mapContainsKey(talentRewardMap, level))
        {
            player->RewardExtraBonusTalentPoints(talentRewardMap->at(level));
        }
        if (mapContainsKey(itemRewardMap, level))
        {
            // Mail item to player
            uint32 itemEntry = itemRewardMap->at(level);
            player->SendItemRetrievalMail({ { itemEntry, 1 } });
        }

        CharacterDatabase.Execute("INSERT INTO hardcore_challenge_completed (character_guid, character_level, achievement, total_spent_time) "
            "VALUES ({}, {}, '{}',{})",
            player->GetGUID().GetCounter(), player->GetLevel(), "", player->GetTotalPlayedTime());
    }

private:
    ChallengeModeSettings settingName;
};

class ChallengeMode_Hardcore : public ChallengeMode
{
public:
    ChallengeMode_Hardcore() : ChallengeMode("ChallengeMode_Hardcore", SETTING_HARDCORE) {}

    const std::string Hardcore_ban_time = "999999999s";




    void OnPlayerReleasedGhost(Player* player) override
    {
        if (!sChallengeModes->challengeEnabledCheckbyToken(SETTING_HARDCORE, player))
        {
            return;
        }


        CharacterDatabase.Execute("INSERT INTO hardcore_challenge_failed (character_guid, character_level, death_reason, total_spent_time) "
            "VALUES ({}, {}, '{}',{})",
            player->GetGUID().GetCounter(), player->GetLevel(), "death", player->GetTotalPlayedTime());


        std::string playername = player->GetName();

        std::string tag_colour = "7bbef7";
        std::string plr_colour = "ffff00";
        std::ostringstream stream;
        stream << "|CFF" << plr_colour << "[硬核模式挑战]|r|CFF" << tag_colour <<
            " 角色 |r|cff" << plr_colour << playername << " |r|cff" << tag_colour <<
            " 挑战失败，但失败并不意味着结束，它只是一个新的起点，祝越来越好！"  ;
        sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, stream.str().c_str());

        std::string PlayerName;
        PlayerName = player->GetName();
        std::string bantime;
        sBan->BanCharacter(PlayerName, Hardcore_ban_time, "Failed to chanlledge Hardcore", "Server");

    }

    void OnPlayerKilledByCreature(Creature* killer, Player* player) override
    {
        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_HARDCORE, player))
        {
            return;
        }


        std::string playername = player->GetName();
        uint8 playerlevel = player->GetLevel();
        uint8 playerGUID = player->GetGUID().GetCounter();

        std::string killername = killer->GetNameForLocaleIdx(sObjectMgr->GetDBCLocaleIndex());
        uint8 killerlevel = killer->GetLevel();

        uint8 totalplayertime = player->GetTotalPlayedTime();
        uint8 minutes = totalplayertime / 60;
        uint8 hours = minutes / 60;
        uint8 days = hours / 24;
        minutes %= 60;
        hours %= 24;

        std::string tag_colour = "7bbef7";
        std::string plr_colour = "ffff00";
        std::ostringstream stream;
        stream << "|CFF" << plr_colour << "[硬核模式挑战]|r|CFF" << tag_colour <<
            " 角色 |r|cff" << plr_colour << playername << "[" << static_cast<int>(playerlevel) << "]级" << " |r|cff" << tag_colour <<
            " 被" <<
            " 生物 |r|cff" << plr_colour << killername << "[" << static_cast<int>(killerlevel) << "]级" << " |r|cff" << tag_colour <<
            " 所杀，角色生存时间：|r" << " |r|cff" << plr_colour << static_cast<int>(days) << "天" << static_cast<int>(hours) << "小时" << static_cast<int>(minutes) << "分钟";
        sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, stream.str().c_str());


    }

    void OnPlayerResurrect(Player* player, float /*restore_percent*/, bool /*applySickness*/) override
    {
        if (!sChallengeModes->challengeEnabledCheckbyToken(SETTING_HARDCORE, player))
        {
            return;
        }
        // A better implementation is to not allow the resurrect but this will need a new hook added first
        player->KillPlayer();
    }


    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* victim, uint8 xpSource) override
    {
        if (sChallengeModes->challengeEnabledCheckbyToken(SETTING_HARDCORE, player))
        {
            player->RemoveAura(90000);      //移除双倍经验的效果
        }

        ChallengeMode::OnPlayerGiveXP(player, amount, victim, xpSource);
    }

    void OnPlayerLevelChanged(Player* player, uint8 oldlevel) override
    {
        ChallengeMode::OnPlayerLevelChanged(player, oldlevel);
    }

    bool OnPlayerCanEquipItem(Player* player, uint8 /*slot*/, uint16& /*dest*/, Item* pItem, bool /*swap*/, bool /*not_loading*/) override
    {

        if (!sChallengeModes->challengeEnabledCheckbyToken(SETTING_HARDCORE, player))
        {
            return true;
        }

        return pItem->GetTemplate()->Quality <= ITEM_QUALITY_NORMAL;
    }

    bool OnPlayerCanInitTrade(Player* player, Player* target) override
    {
        if (!sChallengeModes->challengeEnabledCheckbyToken(SETTING_HARDCORE, player))
        {
            return true;
        }
        return false;
    }


    bool OnPlayerCanApplyEnchantment(Player* player, Item* item, EnchantmentSlot slot, bool /*apply*/, bool /*apply_dur*/, bool /*ignore_condition*/) override
    {
        if (!sChallengeModes->challengeEnabledCheckbyToken(SETTING_HARDCORE, player))
        {
            return true;
        }
        // Are there any exceptions in WotLK? If so need to be added here
        //
        uint32 enchantId = uint32(item->GetEnchantmentId(slot));
        LOG_INFO("module", "GetEnchantmentId  is {} .", item->GetEnchantmentId(slot));

        int enchantIds[] = { 1666,2,12,524,1667,1668,2635,3782,3783,3784,       //冰封武器1~9级
                             5,4,3,523,1665,1666,2634,3779,3780,3781,           //火舌武器1~9级
                             1,6,29,3032,                                       //石化武器1~4级
                             283,284,525,1669,2636,3785,3786,3787               //风怒武器1~9级
             };
        int size = sizeof(enchantIds) / sizeof(enchantIds[0]);

        for (int i = 0; i < size; i++) {
            if (enchantId == enchantIds[i]) {
                return true;
            }
        }

        return false;
    }

    bool OnPlayerCanUseItem(Player* player, ItemTemplate const* proto, InventoryResult& /*result*/) override
    {
        if (!sChallengeModes->challengeEnabledCheckbyToken(SETTING_HARDCORE, player))
        {
            return true;
        }
        //return !(proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_FLASK);
        //LOG_INFO("module", "proto->Class  is {} ;  proto->SubClass  is  {}", proto->Class, proto->SubClass);

        // Do not allow using elixir, potion, or flask
        if ((proto->Class == ITEM_CLASS_CONSUMABLE) &&
            (proto->SubClass == ITEM_SUBCLASS_POTION || proto->SubClass == ITEM_SUBCLASS_ELIXIR || proto->SubClass == ITEM_SUBCLASS_FLASK))
        {
            return false;
        }

        // Do not allow food that gives food buffs
        if (proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_FOOD)
        {
            for (const auto& Spell : proto->Spells)
            {
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(Spell.SpellId);
                if (!spellInfo)
                    continue;

                for (uint8 i = 0; i < 3; i++)
                {
                    if (spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_PERIODIC_TRIGGER_SPELL)
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool OnPlayerCanGroupInvite(Player* player, std::string& /*membername*/) override
    {
        if (!sChallengeModes->challengeEnabledCheckbyToken(SETTING_HARDCORE, player))
        {
            return true;
        }
        return false;
    }

    bool OnPlayerCanGroupAccept(Player* player, Group* /*group*/) override
    {
        if (!sChallengeModes->challengeEnabledCheckbyToken(SETTING_HARDCORE, player))
        {
            return true;
        }
        return false;
    }
};

//class ChallengeMode_SemiHardcore : public ChallengeMode
//{
//public:
//    ChallengeMode_SemiHardcore() : ChallengeMode("ChallengeMode_SemiHardcore", SETTING_SEMI_HARDCORE) {}
//
//    void OnPlayerKilledByCreature(Creature* /*killer*/, Player* player) override
//    {
//        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_SEMI_HARDCORE, player))
//        {
//            return;
//        }
//        for (uint8 i = 0; i < EQUIPMENT_SLOT_END; ++i)
//        {
//            if (Item* pItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
//            {
//                if (pItem->GetTemplate() && !pItem->IsEquipped())
//                    continue;
//                uint8 slot = pItem->GetSlot();
//                ChatHandler(player->GetSession()).PSendSysMessage("|cffDA70D6You have lost your |cffffffff|Hitem:%d:0:0:0:0:0:0:0:0|h[%s]|h|r", pItem->GetEntry(), pItem->GetTemplate()->Name1.c_str());
//                player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
//            }
//        }
//        player->SetMoney(0);
//    }
//
//    void OnGiveXP(Player* player, uint32& amount, Unit* victim) override
//    {
//        ChallengeMode::OnGiveXP(player, amount, victim);
//    }
//
//    void OnLevelChanged(Player* player, uint8 oldlevel) override
//    {
//        ChallengeMode::OnLevelChanged(player, oldlevel);
//    }
//};
//
//class ChallengeMode_SelfCrafted : public ChallengeMode
//{
//public:
//    ChallengeMode_SelfCrafted() : ChallengeMode("ChallengeMode_SelfCrafted", SETTING_SELF_CRAFTED) {}
//
//    bool CanEquipItem(Player* player, uint8 /*slot*/, uint16& /*dest*/, Item* pItem, bool /*swap*/, bool /*not_loading*/) override
//    {
//        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_SELF_CRAFTED, player))
//        {
//            return true;
//        }
//        if (!pItem->GetTemplate()->HasSignature())
//        {
//            return false;
//        }
//        return pItem->GetGuidValue(ITEM_FIELD_CREATOR) == player->GetGUID();
//    }
//
//    void OnGiveXP(Player* player, uint32& amount, Unit* victim) override
//    {
//        ChallengeMode::OnGiveXP(player, amount, victim);
//    }
//
//    void OnLevelChanged(Player* player, uint8 oldlevel) override
//    {
//        ChallengeMode::OnLevelChanged(player, oldlevel);
//    }
//};
//
//class ChallengeMode_ItemQualityLevel : public ChallengeMode
//{
//public:
//    ChallengeMode_ItemQualityLevel() : ChallengeMode("ChallengeMode_ItemQualityLevel", SETTING_ITEM_QUALITY_LEVEL) {}
//
//    bool CanEquipItem(Player* player, uint8 /*slot*/, uint16& /*dest*/, Item* pItem, bool /*swap*/, bool /*not_loading*/) override
//    {
//        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_ITEM_QUALITY_LEVEL, player))
//        {
//            return true;
//        }
//        return pItem->GetTemplate()->Quality <= ITEM_QUALITY_NORMAL;
//    }
//
//    void OnGiveXP(Player* player, uint32& amount, Unit* victim) override
//    {
//        ChallengeMode::OnGiveXP(player, amount, victim);
//    }
//
//    void OnLevelChanged(Player* player, uint8 oldlevel) override
//    {
//        ChallengeMode::OnLevelChanged(player, oldlevel);
//    }
//};
//
//class ChallengeMode_SlowXpGain : public ChallengeMode
//{
//public:
//    ChallengeMode_SlowXpGain() : ChallengeMode("ChallengeMode_SlowXpGain", SETTING_SLOW_XP_GAIN) {}
//
//    void OnGiveXP(Player* player, uint32& amount, Unit* victim) override
//    {
//        ChallengeMode::OnGiveXP(player, amount, victim);
//    }
//
//    void OnLevelChanged(Player* player, uint8 oldlevel) override
//    {
//        ChallengeMode::OnLevelChanged(player, oldlevel);
//    }
//};
//
//class ChallengeMode_VerySlowXpGain : public ChallengeMode
//{
//public:
//    ChallengeMode_VerySlowXpGain() : ChallengeMode("ChallengeMode_VerySlowXpGain", SETTING_VERY_SLOW_XP_GAIN) {}
//
//    void OnGiveXP(Player* player, uint32& amount, Unit* victim) override
//    {
//        ChallengeMode::OnGiveXP(player, amount, victim);
//    }
//
//    void OnLevelChanged(Player* player, uint8 oldlevel) override
//    {
//        ChallengeMode::OnLevelChanged(player, oldlevel);
//    }
//};
//
//class ChallengeMode_QuestXpOnly : public ChallengeMode
//{
//public:
//    ChallengeMode_QuestXpOnly() : ChallengeMode("ChallengeMode_QuestXpOnly", SETTING_QUEST_XP_ONLY) {}
//
//    void OnGiveXP(Player* player, uint32& amount, Unit* victim) override
//    {
//        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_QUEST_XP_ONLY, player))
//        {
//            return;
//        }
//        if (victim)
//        {
//            amount = 0;
//        }
//        else
//        {
//            ChallengeMode::OnGiveXP(player, amount, victim);
//        }
//    }
//
//    void OnLevelChanged(Player* player, uint8 oldlevel) override
//    {
//        ChallengeMode::OnLevelChanged(player, oldlevel);
//    }
//};
//
//class ChallengeMode_IronMan : public ChallengeMode
//{
//public:
//    ChallengeMode_IronMan() : ChallengeMode("ChallengeMode_IronMan", SETTING_IRON_MAN) {}
//
//    void OnPlayerResurrect(Player* player, float /*restore_percent*/, bool /*applySickness*/) override
//    {
//        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_IRON_MAN, player))
//        {
//            return;
//        }
//        // A better implementation is to not allow the resurrect but this will need a new hook added first
//        player->KillPlayer();        
//    }
//
//    void OnGiveXP(Player* player, uint32& amount, Unit* victim) override
//    {
//        ChallengeMode::OnGiveXP(player, amount, victim);
//    }
//
//    void OnLevelChanged(Player* player, uint8 oldlevel) override
//    {
//        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_IRON_MAN, player))
//        {
//            return;
//        }
//        player->SetFreeTalentPoints(0); // Remove all talent points
//        ChallengeMode::OnLevelChanged(player, oldlevel);
//    }
//
//    void OnTalentsReset(Player* player, bool /*noCost*/) override
//    {
//        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_IRON_MAN, player))
//        {
//            return;
//        }
//        player->SetFreeTalentPoints(0); // Remove all talent points
//    }
//
//    bool CanEquipItem(Player* player, uint8 /*slot*/, uint16& /*dest*/, Item* pItem, bool /*swap*/, bool /*not_loading*/) override
//    {
//        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_IRON_MAN, player))
//        {
//            return true;
//        }
//        return pItem->GetTemplate()->Quality <= ITEM_QUALITY_NORMAL;
//    }
//
//    bool CanApplyEnchantment(Player* player, Item* /*item*/, EnchantmentSlot /*slot*/, bool /*apply*/, bool /*apply_dur*/, bool /*ignore_condition*/) override
//    {
//        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_IRON_MAN, player))
//        {
//            return true;
//        }
//        // Are there any exceptions in WotLK? If so need to be added here
//        return false;
//    }
//
//    void OnLearnSpell(Player* player, uint32 spellID) override
//    {
//        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_IRON_MAN, player))
//        {
//            return;
//        }
//        // These professions are class skills so they are always acceptable
//        switch (spellID)
//        {
//            case RUNEFORGING:
//            case POISONS:
//            case BEAST_TRAINING:
//                return;
//            default:
//                break;
//        }
//        // Do not allow learning any trade skills
//        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
//        if (!spellInfo)
//            return;
//        bool shouldForget = false;
//        for (uint8 i = 0; i < 3; i++)
//        {
//            if (spellInfo->Effects[i].Effect == SPELL_EFFECT_TRADE_SKILL)
//            {
//                shouldForget = true;
//            }
//        }
//        if (shouldForget)
//        {
//            player->removeSpell(spellID, SPEC_MASK_ALL, false);
//        }
//    }
//
//    bool CanUseItem(Player* player, ItemTemplate const* proto, InventoryResult& /*result*/) override
//    {
//        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_IRON_MAN, player))
//        {
//            return true;
//        }
//        // Do not allow using elixir, potion, or flask
//        if (proto->Class == ITEM_CLASS_CONSUMABLE &&
//                (proto->SubClass == ITEM_SUBCLASS_POTION ||
//                proto->SubClass == ITEM_SUBCLASS_ELIXIR ||
//                proto->SubClass == ITEM_SUBCLASS_FLASK))
//        {
//            return false;
//        }
//        // Do not allow food that gives food buffs
//        if (proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_FOOD)
//        {
//            for (const auto & Spell : proto->Spells)
//            {
//                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(Spell.SpellId);
//                if (!spellInfo)
//                    continue;
//
//                for (uint8 i = 0; i < 3; i++)
//                {
//                    if (spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_PERIODIC_TRIGGER_SPELL)
//                    {
//                        return false;
//                    }
//                }
//            }
//        }
//        return true;
//    }
//
//    bool CanGroupInvite(Player* player, std::string& /*membername*/) override
//    {
//        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_IRON_MAN, player))
//        {
//            return true;
//        }
//        return false;
//    }
//
//    bool CanGroupAccept(Player* player, Group* /*group*/) override
//    {
//        if (!sChallengeModes->challengeEnabledForPlayer(SETTING_IRON_MAN, player))
//        {
//            return true;
//        }
//        return false;
//    }
//
//};

class gobject_challenge_modes : public GameObjectScript
{
private:
    static bool playerSettingEnabled(Player* player, uint8 settingIndex)
    {
        return player->GetPlayerSetting("mod-challenge-modes", settingIndex).value;
    }

public:
    gobject_challenge_modes() : GameObjectScript("gobject_challenge_modes") { }

    struct gobject_challenge_modesAI: GameObjectAI
    {
        explicit gobject_challenge_modesAI(GameObject* object) : GameObjectAI(object) { };

        bool CanBeSeen(Player const* player) override
        {
            // 禁止死亡骑士看到硬核模式神像
            if (player->getClass() == CLASS_DEATH_KNIGHT)
            {
                return false;
            }

            // 其他职业只有1级角色可以看到神像
            if (player->GetLevel() > 1)
            {
                return false;
            }

            return sChallengeModes->enabled();
        }
    };

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        // 禁止死亡骑士选择硬核模式
        if (player->getClass() != CLASS_DEATH_KNIGHT && sChallengeModes->challengeEnabled(SETTING_HARDCORE) && !playerSettingEnabled(player, SETTING_HARDCORE) && !playerSettingEnabled(player, SETTING_SEMI_HARDCORE))
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "开始硬核挑战模式", 0, SETTING_HARDCORE, "选择开启硬核挑战模式，系统将销毁当前已装备的各种装备。\n你确定要继续吗？\n\n",0, false);
        }
        if (sChallengeModes->challengeEnabled(SETTING_SEMI_HARDCORE) && !playerSettingEnabled(player, SETTING_HARDCORE) && !playerSettingEnabled(player, SETTING_SEMI_HARDCORE))
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Enable Semi-Hardcore Mode", 0, SETTING_SEMI_HARDCORE);
        }
        if (sChallengeModes->challengeEnabled(SETTING_SELF_CRAFTED) && !playerSettingEnabled(player, SETTING_SELF_CRAFTED) && !playerSettingEnabled(player, SETTING_IRON_MAN))
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Enable Self-Crafted Mode", 0, SETTING_SELF_CRAFTED);
        }
        if (sChallengeModes->challengeEnabled(SETTING_ITEM_QUALITY_LEVEL) && !playerSettingEnabled(player, SETTING_ITEM_QUALITY_LEVEL))
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Enable Low Quality Item Mode", 0, SETTING_ITEM_QUALITY_LEVEL);
        }
        if (sChallengeModes->challengeEnabled(SETTING_SLOW_XP_GAIN) && !playerSettingEnabled(player, SETTING_SLOW_XP_GAIN) && !playerSettingEnabled(player, SETTING_VERY_SLOW_XP_GAIN))
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Enable Slow XP Mode", 0, SETTING_SLOW_XP_GAIN);
        }
        if (sChallengeModes->challengeEnabled(SETTING_VERY_SLOW_XP_GAIN) && !playerSettingEnabled(player, SETTING_SLOW_XP_GAIN) && !playerSettingEnabled(player, SETTING_VERY_SLOW_XP_GAIN))
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Enable Very Slow XP Mode", 0, SETTING_VERY_SLOW_XP_GAIN);
        }
        if (sChallengeModes->challengeEnabled(SETTING_QUEST_XP_ONLY) && !playerSettingEnabled(player, SETTING_QUEST_XP_ONLY))
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Enable Quest XP Only Mode", 0, SETTING_QUEST_XP_ONLY);
        }
        if (sChallengeModes->challengeEnabled(SETTING_IRON_MAN) && !playerSettingEnabled(player, SETTING_IRON_MAN) && !playerSettingEnabled(player, SETTING_SELF_CRAFTED))
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Enable Iron Man Mode", 0, SETTING_IRON_MAN);
        }
        SendGossipMenuFor(player, 12669, go->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, GameObject* /*go*/, uint32 /*sender*/, uint32 action) override
    {
        if (player->GetGroup() != NULL)
            return false;
        player->UpdatePlayerSetting("mod-challenge-modes", action, 1);
        ChatHandler(player->GetSession()).PSendSysMessage("硬核挑战模式开启。");
        if (!sChallengeModes->challengeEnabledCheckbyToken(SETTING_HARDCORE, player))
        {
            player->AddItem(90002, 1);

            for (uint8 i = 0; i < EQUIPMENT_SLOT_END; ++i)
            {
                if (Item* pItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                {
                    if (pItem->GetTemplate() && !pItem->IsEquipped())
                        continue;
                    uint8 slot = pItem->GetSlot();
                    player->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
                }
            }


        }
        std::string plr = player->GetName();
        std::string tag_colour = "7bbef7";
        std::string plr_colour = "ffff00";
        std::ostringstream stream;
        stream << "|CFF" << plr_colour << "[硬核模式挑战]|r|CFF" << tag_colour <<
            " 角色 |r|cff" << plr_colour << plr << "|r|cff" << tag_colour <<
            " 开启硬核挑战模式，祝好运！|r";
        sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, stream.str().c_str());

        CloseGossipMenuFor(player);
        return true;
    }

    GameObjectAI* GetAI(GameObject* object) const override
    {
        return new gobject_challenge_modesAI(object);
    }
};

// Add all scripts in one
void AddSC_mod_challenge_modes()
{
    new ChallengeModes_WorldScript();
    new gobject_challenge_modes();
    new ChallengeMode_Hardcore();
    //new ChallengeMode_SemiHardcore();
    //new ChallengeMode_SelfCrafted();
    //new ChallengeMode_ItemQualityLevel();
    //new ChallengeMode_SlowXpGain();
    //new ChallengeMode_VerySlowXpGain();
    //new ChallengeMode_QuestXpOnly();
    //new ChallengeMode_IronMan();
}
