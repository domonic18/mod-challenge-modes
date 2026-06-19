/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "ChallengeMode_IronManEnhanced.h"
#include "StringFormat.h"
#include "Creature.h"
#include "Chat.h"
#include "DBCStores.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldSessionMgr.h"
#include <cstring>
#include <set>
#include <sstream>
#include <string>

static std::string GetLocalizedCreatureName(Creature const* creature)
{
    std::string name = creature->GetName();
    if (CreatureLocale const* cl = sObjectMgr->GetCreatureLocale(creature->GetEntry()))
    {
        LocaleConstant locale = sWorld->GetDefaultDbcLocale();
        if (cl->Name.size() > static_cast<size_t>(locale) && !cl->Name[locale].empty())
        {
            name = cl->Name[locale];
        }
    }
    return name;
}

static std::string GetDisplayDeathReason(char const* reason)
{
    if (strcmp(reason, "environmental") == 0)
    {
        return "非正常死亡";
    }

    if (strcmp(reason, "resurrect") == 0)
    {
        return "被复活";
    }

    return reason;
}

ChallengeMode_IronMan_Enhanced::ChallengeMode_IronMan_Enhanced()
    : PlayerScript("ChallengeMode_IronMan_Enhanced")
{ }

void ChallengeMode_IronMan_Enhanced::OnPlayerGiveXP(Player* player, uint32& /*amount*/,
    Unit* /*victim*/, uint8 /*xpSource*/)
{
    if (!IsEnhancedActive(player))
    {
        return;
    }

    player->RemoveAura(DOUBLE_EXPERIENCE_AURA);
}

bool ChallengeMode_IronMan_Enhanced::OnPlayerCanResurrect(Player* player)
{
    LOG_DEBUG("entities.player", "IronManEnhanced::OnPlayerCanResurrect called for {} (level {}, active={})",
        player->GetName(), uint32(player->GetLevel()), IsEnhancedActive(player));

    if (!IsEnhancedActive(player))
    {
        return true;
    }

    LOG_INFO("entities.player", "IronManEnhanced: blocking resurrection for {} (level {})",
        player->GetName(), uint32(player->GetLevel()));

    RecordFailure(player, "resurrect");
    DeleteProgress(player);
    return false;
}

void ChallengeMode_IronMan_Enhanced::OnPlayerResurrect(Player* player,
    float /*restore_percent*/, bool& /*applySickness*/)
{
    LOG_DEBUG("entities.player", "IronManEnhanced::OnPlayerResurrect called for {} (level {}, active={})",
        player->GetName(), uint32(player->GetLevel()), IsEnhancedActive(player));

    if (!IsEnhancedActive(player))
    {
        return;
    }

    LOG_WARN("entities.player", "IronManEnhanced: player {} was resurrected despite OnPlayerCanResurrect; forcing death",
        player->GetName());

    RecordFailure(player, "resurrect");
    DeleteProgress(player);
    player->KillPlayer();
    player->GetSession()->KickPlayer("硬核挑战角色已死亡");
}

void ChallengeMode_IronMan_Enhanced::OnPlayerJustDied(Player* player)
{
    LOG_DEBUG("entities.player", "IronManEnhanced::OnPlayerJustDied called for {} (level {}, active={})",
        player->GetName(), uint32(player->GetLevel()), IsEnhancedActive(player));

    if (!IsEnhancedActive(player))
    {
        return;
    }

    LOG_INFO("entities.player", "IronManEnhanced: player {} died; progress will be handled by OnUnitDeath",
        player->GetName(), uint32(player->GetLevel()));

    DeleteProgress(player);
}

void ChallengeMode_IronMan_Enhanced::OnPlayerReleasedGhost(Player* player)
{
    LOG_DEBUG("entities.player", "IronManEnhanced::OnPlayerReleasedGhost called for {} (level {}, active={})",
        player->GetName(), uint32(player->GetLevel()), IsEnhancedActive(player));

    if (!IsEnhancedActive(player))
    {
        return;
    }

    LOG_INFO("entities.player", "IronManEnhanced: kicking {} after releasing ghost", player->GetName());
    player->GetSession()->KickPlayer("硬核挑战角色已死亡");
}

void ChallengeMode_IronMan_Enhanced::OnPlayerLevelChanged(Player* player,
    uint8 /*oldlevel*/)
{
    // 等级变化后重新评估挑战雕像等 GameObject 的可见性
    player->UpdateVisibilityForPlayer();

    if (!IsEnhancedActive(player))
    {
        return;
    }

    uint8 level = player->GetLevel();
    UpsertProgress(player);

    if (IsMilestoneLevel(level))
    {
        LOG_INFO("entities.player", "IronManEnhanced: recording milestone success for {} (level {})",
            player->GetName(), uint32(level));

        RewardMilestone(player, level);

        CharacterDatabase.Execute(
            "INSERT INTO hardcore_challenge_success "
            "(character_guid, character_name, completed_level, total_spent_time) "
            "VALUES ({}, '{}', {}, {})",
            player->GetGUID().GetCounter(), player->GetName(), level,
            player->GetTotalPlayedTime());

        std::string titleName;
        if (std::unordered_map<uint8, uint32> const* titleMap = sChallengeModes->getTitleMapForChallenge(SETTING_IRON_MAN_ENHANCED))
        {
            auto it = titleMap->find(level);
            if (it != titleMap->end())
            {
                if (CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(it->second))
                {
                    char const* rawName = player->getGender() == GENDER_MALE
                        ? titleInfo->nameMale[sWorld->GetDefaultDbcLocale()]
                        : titleInfo->nameFemale[sWorld->GetDefaultDbcLocale()];
                    std::string rawNameStr = rawName ? rawName : "";
                    size_t pos = 0;
                    while ((pos = rawNameStr.find("%s", pos)) != std::string::npos)
                    {
                        rawNameStr.replace(pos, 2, "{}");
                        pos += 2;
                    }
                    titleName = Acore::StringFormat(rawNameStr, player->GetName());
                }
            }
        }

        std::string plr = player->GetName();
        std::string tagColour = "7bbef7";
        std::string plrColour = "ffff00";
        std::ostringstream stream;
        stream << "|CFF" << plrColour << "[硬核模式挑战]|r|CFF" << tagColour <<
            " 角色 |r|cff" << plrColour << plr << "|r|cff" << tagColour <<
            " 达到 " << uint32(level) << " 级里程碑";
        if (!titleName.empty())
        {
            stream << "，获得头衔 " << titleName;
        }
        stream << "！|r";
        sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, stream.str().c_str());

        if (IsFinalMilestone(level))
        {
            DeleteProgress(player);
        }
    }
}

void ChallengeMode_IronMan_Enhanced::OnPlayerLogin(Player* player)
{
    bool hasToken = player->HasItemCount(HARDCORE_TOKEN_ITEM_ID, 1, true);
    bool alive = player->getDeathState() == DeathState::Alive;

    LOG_DEBUG("entities.player", "IronManEnhanced::OnPlayerLogin for {} (level {}, hasToken={}, alive={})",
        player->GetName(), uint32(player->GetLevel()), hasToken, alive);

    if (!hasToken)
    {
        LOG_INFO("entities.player", "IronManEnhanced: login without token for {}; exiting challenge",
            player->GetName());

        HandleChallengeExit(player);
        return;
    }

    if (!alive)
    {
        LOG_INFO("entities.player", "IronManEnhanced: login with token but dead for {}; recording failure and kicking",
            player->GetName());

        RecordFailure(player);
        DeleteProgress(player);
        player->GetSession()->KickPlayer("硬核挑战角色已死亡");
        return;
    }

    UpsertProgress(player);
}

void ChallengeMode_IronMan_Enhanced::OnPlayerAfterMoveItemFromInventory(Player* player,
    Item* it, uint8 /*bag*/, uint8 /*slot*/, bool /*update*/)
{
    if (!it || it->GetEntry() != HARDCORE_TOKEN_ITEM_ID)
    {
        return;
    }

    LOG_DEBUG("entities.player", "IronManEnhanced::OnPlayerAfterMoveItemFromInventory for {} (moved token)",
        player->GetName());

    if (!player->HasItemCount(HARDCORE_TOKEN_ITEM_ID, 1, true))
    {
        LOG_INFO("entities.player", "IronManEnhanced: token removed for {}; exiting challenge",
            player->GetName());

        HandleChallengeExit(player);
    }
}

bool ChallengeMode_IronMan_Enhanced::OnPlayerCanApplyEnchantment(Player* player,
    Item* item, EnchantmentSlot slot, bool /*apply*/, bool /*apply_dur*/,
    bool /*ignore_condition*/)
{
    if (!IsEnhancedActive(player))
    {
        return true;
    }

    // 仅允许萨满武器附魔（冰封 / 火舌 / 石化 / 风怒）
    uint32 const shamanWeaponEnchantIds[] = {
        1666, 2, 12, 524, 1667, 1668, 2635, 3782, 3783, 3784,
        5, 4, 3, 523, 1665, 1666, 2634, 3779, 3780, 3781,
        1, 6, 29, 3032,
        283, 284, 525, 1669, 2636, 3785, 3786, 3787
    };

    uint32 enchantId = uint32(item->GetEnchantmentId(slot));
    for (uint32 shamanEnchantId : shamanWeaponEnchantIds)
    {
        if (enchantId == shamanEnchantId)
        {
            return true;
        }
    }

    return false;
}

bool ChallengeMode_IronMan_Enhanced::OnPlayerCanSendMail(Player* player,
    ObjectGuid /*receiverGuid*/, ObjectGuid /*mailbox*/, std::string& /*subject*/,
    std::string& /*body*/, uint32 /*money*/, uint32 /*COD*/, Item* /*item*/)
{
    if (!IsEnhancedActive(player))
    {
        return true;
    }

    ChatHandler(player->GetSession()).PSendSysMessage("硬核挑战模式下无法发送邮件。");
    return false;
}

bool ChallengeMode_IronMan_Enhanced::OnPlayerCanJoinLfg(Player* player,
    uint8 /*roles*/, std::set<uint32>& /*dungeons*/, const std::string& /*comment*/)
{
    if (!IsEnhancedActive(player))
    {
        return true;
    }

    ChatHandler(player->GetSession()).PSendSysMessage("硬核挑战模式下无法使用地下城查找器。");
    return false;
}

bool ChallengeMode_IronMan_Enhanced::OnPlayerCanJoinInBattlegroundQueue(Player* player,
    ObjectGuid /*BattlemasterGuid*/, BattlegroundTypeId /*BGTypeID*/,
    uint8 /*joinAsGroup*/, GroupJoinBattlegroundResult& /*err*/)
{
    if (!IsEnhancedActive(player))
    {
        return true;
    }

    ChatHandler(player->GetSession()).PSendSysMessage("硬核挑战模式下无法进入战场。");
    return false;
}

bool ChallengeMode_IronMan_Enhanced::OnPlayerCanJoinInArenaQueue(Player* player,
    ObjectGuid /*BattlemasterGuid*/, uint8 /*arenaslot*/, BattlegroundTypeId /*BGTypeID*/,
    uint8 /*joinAsGroup*/, uint8 /*IsRated*/, GroupJoinBattlegroundResult& /*err*/)
{
    if (!IsEnhancedActive(player))
    {
        return true;
    }

    ChatHandler(player->GetSession()).PSendSysMessage("硬核挑战模式下无法进入竞技场。");
    return false;
}

bool ChallengeMode_IronMan_Enhanced::OnPlayerCanInitTrade(Player* player,
    Player* target)
{
    bool playerHardcore = IsEnhancedActive(player);
    bool targetHardcore = target && IsEnhancedActive(target);

    if (!playerHardcore && !targetHardcore)
    {
        return true;
    }

    ChatHandler(player->GetSession()).PSendSysMessage("硬核挑战模式下无法进行交易。");
    return false;
}

bool ChallengeMode_IronMan_Enhanced::OnPlayerCanGroupInvite(Player* player,
    std::string& /*membername*/)
{
    if (!IsEnhancedActive(player))
    {
        return true;
    }

    ChatHandler(player->GetSession()).PSendSysMessage("硬核挑战模式下无法邀请组队。");
    return false;
}

bool ChallengeMode_IronMan_Enhanced::OnPlayerCanGroupAccept(Player* player,
    Group* /*group*/)
{
    if (!IsEnhancedActive(player))
    {
        return true;
    }

    ChatHandler(player->GetSession()).PSendSysMessage("硬核挑战模式下无法接受组队邀请。");
    return false;
}

bool ChallengeMode_IronMan_Enhanced::OnPlayerCanEquipItem(Player* player,
    uint8 /*slot*/, uint16& /*dest*/, Item* item, bool /*swap*/, bool /*not_loading*/)
{
    if (!IsEnhancedActive(player))
    {
        return true;
    }

    if (!item)
    {
        return true;
    }

    uint32 quality = item->GetTemplate()->Quality;
    if (quality > ITEM_QUALITY_NORMAL)
    {
        ChatHandler(player->GetSession()).PSendSysMessage(
            "硬核挑战模式下无法装备品质高于普通的物品。");
        return false;
    }

    return true;
}

void ChallengeMode_IronMan_Enhanced::InitializeProgress(Player* player)
{
    LOG_INFO("entities.player", "IronManEnhanced: initializing progress for {} (level {})",
        player->GetName(), uint32(player->GetLevel()));

    CharacterDatabase.Execute(
        "INSERT INTO hardcore_challenge_progress "
        "(character_guid, character_name, current_level, total_spent_time) "
        "VALUES ({}, '{}', {}, {}) "
        "ON DUPLICATE KEY UPDATE "
        "character_name = VALUES(character_name), "
        "current_level = VALUES(current_level), "
        "total_spent_time = VALUES(total_spent_time)",
        player->GetGUID().GetCounter(), player->GetName(), player->GetLevel(),
        player->GetTotalPlayedTime());
}

void ChallengeMode_IronMan_Enhanced::UpsertProgress(Player* player)
{
    LOG_DEBUG("entities.player", "IronManEnhanced: upserting progress for {} (level {})",
        player->GetName(), uint32(player->GetLevel()));

    CharacterDatabase.Execute(
        "INSERT INTO hardcore_challenge_progress "
        "(character_guid, character_name, current_level, total_spent_time) "
        "VALUES ({}, '{}', {}, {}) "
        "ON DUPLICATE KEY UPDATE "
        "character_name = VALUES(character_name), "
        "current_level = VALUES(current_level), "
        "total_spent_time = VALUES(total_spent_time)",
        player->GetGUID().GetCounter(), player->GetName(), player->GetLevel(),
        player->GetTotalPlayedTime());
}

void ChallengeMode_IronMan_Enhanced::DeleteProgress(Player* player)
{
    LOG_DEBUG("entities.player", "IronManEnhanced: deleting progress for {} (level {})",
        player->GetName(), uint32(player->GetLevel()));

    CharacterDatabase.Execute(
        "DELETE FROM hardcore_challenge_progress WHERE character_guid = {}",
        player->GetGUID().GetCounter());
}

void ChallengeMode_IronMan_Enhanced::HandleChallengeExit(Player* player)
{
    LOG_DEBUG("entities.player", "IronManEnhanced::HandleChallengeExit called for {} (level {}, active={})",
        player->GetName(), uint32(player->GetLevel()), IsEnhancedActive(player));

    if (!IsEnhancedActive(player))
    {
        return;
    }

    LOG_INFO("entities.player", "IronManEnhanced: exiting challenge for {} (level {})",
        player->GetName(), uint32(player->GetLevel()));

    CharacterDatabase.Execute(
        "INSERT INTO hardcore_challenge_exit "
        "(character_guid, character_name, current_level, total_spent_time) "
        "VALUES ({}, '{}', {}, {})",
        player->GetGUID().GetCounter(), player->GetName(), player->GetLevel(),
        player->GetTotalPlayedTime());

    DeleteProgress(player);
}

bool ChallengeMode_IronMan_Enhanced::IsEnhancedActive(Player* player)
{
    bool enhancedEnable = sChallengeModes->ironManEnhancedEnable;
    bool hasToken = player->HasItemCount(HARDCORE_TOKEN_ITEM_ID, 1, true);

    LOG_DEBUG("entities.player", "IronManEnhanced::IsEnhancedActive check for {}: enhancedEnable={}, hasToken={}",
        player->GetName(), enhancedEnable, hasToken);

    return enhancedEnable && hasToken;
}

bool ChallengeMode_IronMan_Enhanced::IsMilestoneLevel(uint8 level)
{
    std::unordered_map<uint8, uint32> const* titleMap =
        sChallengeModes->getTitleMapForChallenge(SETTING_IRON_MAN_ENHANCED);
    return titleMap && titleMap->find(level) != titleMap->end();
}

bool ChallengeMode_IronMan_Enhanced::IsFinalMilestone(uint8 level)
{
    std::unordered_map<uint8, uint32> const* titleMap =
        sChallengeModes->getTitleMapForChallenge(SETTING_IRON_MAN_ENHANCED);
    if (!titleMap || titleMap->empty())
    {
        return false;
    }

    uint8 maxLevel = 0;
    for (auto const& [milestoneLevel, titleId] : *titleMap)
    {
        (void)titleId;
        if (milestoneLevel > maxLevel)
        {
            maxLevel = milestoneLevel;
        }
    }

    return level == maxLevel;
}

void ChallengeMode_IronMan_Enhanced::RewardMilestone(Player* player, uint8 level)
{
    if (std::unordered_map<uint8, uint32> const* titleMap = sChallengeModes->getTitleMapForChallenge(SETTING_IRON_MAN_ENHANCED))
    {
        auto it = titleMap->find(level);
        if (it != titleMap->end())
        {
            CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(it->second);
            if (titleInfo)
            {
                player->SetTitle(titleInfo);
            }
            else
            {
                LOG_ERROR("entities.player", "IronManEnhanced: invalid title ID {} for level {}",
                    it->second, uint32(level));
            }
        }
    }

    if (std::unordered_map<uint8, uint32> const* talentMap = sChallengeModes->getTalentMapForChallenge(SETTING_IRON_MAN_ENHANCED))
    {
        auto it = talentMap->find(level);
        if (it != talentMap->end())
        {
            player->RewardExtraBonusTalentPoints(it->second);
        }
    }

    if (std::unordered_map<uint8, uint32> const* itemMap = sChallengeModes->getItemMapForChallenge(SETTING_IRON_MAN_ENHANCED))
    {
        auto it = itemMap->find(level);
        if (it != itemMap->end())
        {
            uint32 itemAmount = sChallengeModes->getItemRewardAmount(SETTING_IRON_MAN_ENHANCED);
            player->SendItemRetrievalMail({ { it->second, itemAmount } });
        }
    }

    if (std::unordered_map<uint8, uint32> const* achievementMap = sChallengeModes->getAchievementMapForChallenge(SETTING_IRON_MAN_ENHANCED))
    {
        auto it = achievementMap->find(level);
        if (it != achievementMap->end())
        {
            AchievementEntry const* achievementInfo = sAchievementStore.LookupEntry(it->second);
            if (achievementInfo)
            {
                player->CompletedAchievement(achievementInfo);
            }
            else
            {
                LOG_ERROR("entities.player", "IronManEnhanced: invalid achievement ID {} for level {}",
                    it->second, uint32(level));
            }
        }
    }
}

void ChallengeMode_IronMan_Enhanced::RecordFailure(Player* player, char const* reason, Unit* killer)
{
    LOG_INFO("entities.player", "IronManEnhanced: RecordFailure for {} (level {}, reason={})",
        player->GetName(), uint32(player->GetLevel()), reason);

    std::string killerInfo;
    std::string displayReason = GetDisplayDeathReason(reason);

    if (strcmp(reason, "resurrect") != 0)
    {
        if (!killer)
        {
            killer = player->getAttackerForHelper();
        }

        if (killer && killer != player)
        {
            Player* owningPlayer = killer->GetCharmerOrOwnerPlayerOrPlayerItself();
            if (killer->IsPlayer() || owningPlayer)
            {
                reason = "player";
                Player* pk = owningPlayer ? owningPlayer : killer->ToPlayer();
                if (pk)
                {
                    killerInfo = Acore::StringFormat("player:{}:level{}:class{}",
                        pk->GetName(), pk->GetLevel(), uint32(pk->getClass()));
                    displayReason = Acore::StringFormat("被玩家 {} (等级 {}) 击杀",
                        pk->GetName(), uint32(pk->GetLevel()));
                }
            }
            else if (killer->IsCreature())
            {
                reason = "creature";
                if (Creature* ck = killer->ToCreature())
                {
                    std::string creatureName = GetLocalizedCreatureName(ck);
                    killerInfo = Acore::StringFormat("creature:{}:{}:level{}",
                        ck->GetEntry(), creatureName, ck->GetLevel());
                    displayReason = Acore::StringFormat("被怪物 {} (等级 {}) 击杀",
                        creatureName, uint32(ck->GetLevel()));
                }
            }
        }
    }

    CharacterDatabase.Execute(
        "INSERT INTO hardcore_challenge_failure "
        "(character_guid, character_name, character_level, death_reason, "
        "death_location_map_id, death_location_zone_id, death_location_area_id, "
        "death_location_x, death_location_y, death_location_z, killer_info, "
        "total_spent_time) "
        "VALUES ({}, '{}', {}, '{}', {}, {}, {}, {:.3f}, {:.3f}, {:.3f}, '{}', {})",
        player->GetGUID().GetCounter(), player->GetName(), player->GetLevel(),
        reason, player->GetMapId(), player->GetZoneId(), player->GetAreaId(),
        player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(),
        killerInfo, player->GetTotalPlayedTime());

    BanCharacter(player);

    if (strcmp(reason, "resurrect") == 0)
    {
        return;
    }

    std::string plr = player->GetName();
    std::string tagColour = "ff0000";
    std::string plrColour = "ffff00";
    std::ostringstream stream;
    stream << "|CFF" << plrColour << "[硬核模式挑战]|r|CFF" << tagColour <<
        " 角色 |r|cff" << plrColour << plr << "|r|cff" << tagColour <<
        " 挑战失败！原因：" << displayReason << "|r";
    sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, stream.str().c_str());
}

void ChallengeMode_IronMan_Enhanced::BanCharacter(Player* player)
{
    char const* banReason = "Failed to chanlledge Hardcore";

    LOG_INFO("entities.player", "IronManEnhanced: banning character {} (level {}, reason={})",
        player->GetName(), uint32(player->GetLevel()), banReason);

    CharacterDatabasePreparedStatement* updateStmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_BAN);
    updateStmt->SetData(0, player->GetGUID().GetCounter());
    CharacterDatabase.Execute(updateStmt);

    CharacterDatabasePreparedStatement* insertStmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHARACTER_BAN);
    insertStmt->SetData(0, player->GetGUID().GetCounter());
    insertStmt->SetData(1, uint32(0));
    insertStmt->SetData(2, "IronManEnhanced");
    insertStmt->SetData(3, banReason);
    CharacterDatabase.Execute(insertStmt);
}

ChallengeMode_IronMan_Enhanced_UnitScript::ChallengeMode_IronMan_Enhanced_UnitScript()
    : UnitScript("ChallengeMode_IronMan_Enhanced_UnitScript")
{ }

void ChallengeMode_IronMan_Enhanced_UnitScript::OnUnitDeath(Unit* unit, Unit* killer)
{
    Player* player = unit->ToPlayer();
    if (!player)
    {
        return;
    }

    if (player->IsAlive())
    {
        return;
    }

    if (!ChallengeMode_IronMan_Enhanced::IsEnhancedActive(player))
    {
        return;
    }

    LOG_INFO("entities.player", "IronManEnhanced: OnUnitDeath recording failure for {} (level {}, killer={})",
        player->GetName(), uint32(player->GetLevel()), killer ? killer->GetName() : "none");

    ChallengeMode_IronMan_Enhanced::DeleteProgress(player);
    ChallengeMode_IronMan_Enhanced::RecordFailure(player, "environmental", killer);
}
