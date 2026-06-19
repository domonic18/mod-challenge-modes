/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef AZEROTHCORE_CHALLENGEMODE_IRONMAN_ENHANCED_H
#define AZEROTHCORE_CHALLENGEMODE_IRONMAN_ENHANCED_H

#include "ScriptMgr.h"
#include "Player.h"
#include "ChallengeModes.h"
#include <set>

class ChallengeMode_IronMan_Enhanced : public PlayerScript
{
public:
    ChallengeMode_IronMan_Enhanced();

    void OnPlayerGiveXP(Player* player, uint32& /*amount*/, Unit* /*victim*/,
        uint8 /*xpSource*/) override;
    bool OnPlayerCanResurrect(Player* player) override;
    void OnPlayerResurrect(Player* player, float /*restore_percent*/, bool& /*applySickness*/) override;
    void OnPlayerJustDied(Player* player) override;
    void OnPlayerReleasedGhost(Player* player) override;
    void OnPlayerLevelChanged(Player* player, uint8 /*oldlevel*/) override;
    void OnPlayerLogin(Player* player) override;
    void OnPlayerAfterMoveItemFromInventory(Player* player, Item* it, uint8 /*bag*/,
        uint8 /*slot*/, bool /*update*/) override;

    bool OnPlayerCanApplyEnchantment(Player* player, Item* item,
        EnchantmentSlot /*slot*/, bool /*apply*/, bool /*apply_dur*/,
        bool /*ignore_condition*/) override;
    bool OnPlayerCanEquipItem(Player* player, uint8 /*slot*/, uint16& /*dest*/,
        Item* item, bool /*swap*/, bool /*not_loading*/) override;
    bool OnPlayerCanSendMail(Player* player, ObjectGuid /*receiverGuid*/,
        ObjectGuid /*mailbox*/, std::string& /*subject*/, std::string& /*body*/,
        uint32 /*money*/, uint32 /*COD*/, Item* /*item*/) override;
    bool OnPlayerCanInitTrade(Player* player, Player* /*target*/) override;
    bool OnPlayerCanGroupInvite(Player* player, std::string& /*membername*/) override;
    bool OnPlayerCanGroupAccept(Player* player, Group* /*group*/) override;
    bool OnPlayerCanJoinLfg(Player* player, uint8 /*roles*/,
        std::set<uint32>& /*dungeons*/, const std::string& /*comment*/) override;
    bool OnPlayerCanJoinInBattlegroundQueue(Player* player,
        ObjectGuid /*BattlemasterGuid*/, BattlegroundTypeId /*BGTypeID*/,
        uint8 /*joinAsGroup*/, GroupJoinBattlegroundResult& /*err*/) override;
    bool OnPlayerCanJoinInArenaQueue(Player* player,
        ObjectGuid /*BattlemasterGuid*/, uint8 /*arenaslot*/,
        BattlegroundTypeId /*BGTypeID*/, uint8 /*joinAsGroup*/,
        uint8 /*IsRated*/, GroupJoinBattlegroundResult& /*err*/) override;

    static void InitializeProgress(Player* player);
    static void UpsertProgress(Player* player);
    static void DeleteProgress(Player* player);
    static void HandleChallengeExit(Player* player);
    static bool IsEnhancedActive(Player* player);
    static void BanCharacter(Player* player);
    static void RecordFailure(Player* player, char const* reason = "environmental", Unit* killer = nullptr);

private:
    static bool IsMilestoneLevel(uint8 level);
    static bool IsFinalMilestone(uint8 level);
    static void RewardMilestone(Player* player, uint8 level);
};

class ChallengeMode_IronMan_Enhanced_UnitScript : public UnitScript
{
public:
    ChallengeMode_IronMan_Enhanced_UnitScript();

    void OnUnitDeath(Unit* unit, Unit* killer) override;
};

#endif // AZEROTHCORE_CHALLENGEMODE_IRONMAN_ENHANCED_H
