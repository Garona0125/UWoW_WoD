/*
* Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
* Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "DatabaseEnv.h"
#include "Opcodes.h"
#include "Log.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "World.h"
#include "ObjectMgr.h"
#include "GroupMgr.h"
#include "Player.h"
#include "Group.h"
#include "SocialMgr.h"
#include "Util.h"
#include "SpellAuras.h"
#include "Vehicle.h"
#include "DB2Structure.h"
#include "DB2Stores.h"
#include "SpellAuraEffects.h"

class Aura;

/* differeces from off:
-you can uninvite yourself - is is useful
-you can accept invitation even if leader went offline
*/
/* todo:
-group_destroyed msg is sent but not shown
-reduce xp gaining when in raid group
-quest sharing has to be corrected
-Master Loor corrections in sending SMSG_LOOT_MASTER_LIST
-raid markers
*/

//! 6.0.3
void WorldSession::SendPartyResult(PartyOperation operation, const std::string& member, PartyResult res, uint32 val /* = 0 */)
{
    WorldPacket data(SMSG_PARTY_COMMAND_RESULT, 4 + member.size() + 1 + 4 + 4 + 8);
    data.WriteBits(member.size(), 9);
    data.WriteBits(operation, 4);
    data.WriteBits(res, 6);                 // LFD cooldown related (used with ERR_PARTY_LFG_BOOT_COOLDOWN_S and ERR_PARTY_LFG_BOOT_NOT_ELIGIBLE_S)
    data << uint32(val);
    data << ObjectGuid::Empty;
    data.WriteString(member);

    SendPacket(&data);
}

//!  6.0.3
void WorldSession::SendGroupInvite(Player* player, bool AlowEnter)
{
    ObjectGuid invitedGuid = player->GetGUID();

    WorldPacket data(SMSG_GROUP_INVITE, 45);
    data.WriteBit(AlowEnter);                           // Inverse already in group
    data.WriteBit(0);                                   // MightCRZYou
    data.WriteBit(0);                                   // MustBeBNetFriend
    data.WriteBit(0);                                   // AllowMultipleRoles
    data.WriteBit(0);                                   // IsXRealm
    data.WriteBits(strlen(GetPlayer()->GetName()), 6);  // Inviter name length

    data << player->GetGUID();
    data << player->GetSession()->GetBattlenetAccountGUID();
    data << uint32(realmHandle.Index);
    data << uint16(0);                                  //unk

    data.FlushBits();

    data.WriteBit(1);                                   // IsLocal
    data.WriteBit(0);                                   // unk2

    data.WriteBits(0, 9);                              // InviterRealmNameActual
    data.WriteBits(0, 9);                              // InviterRealmNameNormalized
    //packet.ReadWoWString("InviterRealmNameActual", bits2);
    //packet.ReadWoWString("InviterRealmNameNormalized", bits258);

    data << int32(0);                                  // ProposedRoles
    data << int32(0);                                  // LfgSlotsCount
    data << int32(0);                                  // LfgCompletedMask

    data.WriteString(GetPlayer()->GetName()); // inviter name

    //for (int i = 0; i < int32; i++)
    //    packet.ReadInt32("LfgSlots", i);

    player->GetSession()->SendPacket(&data);
}

//! 6.0.3
void WorldSession::HandleGroupInviteOpcode(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GROUP_INVITE");

    time_t now = time(NULL);
    if (now - timeLastGroupInviteCommand < 5)
        return;
    else
       timeLastGroupInviteCommand = now;

    ObjectGuid crossRealmGuid; // unused

    recvData.read_skip<uint8>();  // PartyIndex
    recvData.read_skip<uint32>(); // ProposedRoles
    recvData >> crossRealmGuid;
    recvData.read_skip<uint32>(); // TargetCfgRealmID
    
    std::string realmName, memberName;

    uint8 nameLen = recvData.ReadBits(9);
    uint8 realmLen = recvData.ReadBits(9);

    memberName = recvData.ReadString(nameLen);
    realmName = recvData.ReadString(realmLen); // unused
    // attempt add selected player

    // cheating
    if (!normalizePlayerName(memberName))
    {
        SendPartyResult(PARTY_OP_INVITE, memberName, ERR_BAD_PLAYER_NAME_S);
        return;
    }

    Player* player = sObjectAccessor->FindPlayerByName(memberName.c_str());

    // no player
    if (!player)
    {
        SendPartyResult(PARTY_OP_INVITE, memberName, ERR_BAD_PLAYER_NAME_S);
        return;
    }

    // restrict invite to GMs
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_GM_GROUP) && !GetPlayer()->isGameMaster() && player->isGameMaster())
    {
        SendPartyResult(PARTY_OP_INVITE, memberName, ERR_BAD_PLAYER_NAME_S);
        return;
    }

    // can't group with
    if (!GetPlayer()->isGameMaster() && !sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP) && GetPlayer()->GetTeam() != player->GetTeam())
    {
        SendPartyResult(PARTY_OP_INVITE, memberName, ERR_PLAYER_WRONG_FACTION);
        return;
    }
    if (GetPlayer()->GetInstanceId() != 0 && player->GetInstanceId() != 0 && GetPlayer()->GetInstanceId() != player->GetInstanceId() && GetPlayer()->GetMapId() == player->GetMapId())
    {
        SendPartyResult(PARTY_OP_INVITE, memberName, ERR_TARGET_NOT_IN_INSTANCE_S);
        return;
    }
    // just ignore us
    if (player->GetInstanceId() != 0 && player->GetDungeonDifficultyID() != GetPlayer()->GetDungeonDifficultyID())
    {
        SendPartyResult(PARTY_OP_INVITE, memberName, ERR_IGNORING_YOU_S);
        return;
    }

    if (player->GetSocial()->HasIgnore(GetPlayer()->GetGUID()))
    {
        SendPartyResult(PARTY_OP_INVITE, memberName, ERR_IGNORING_YOU_S);
        return;
    }

    Group* group = GetPlayer()->GetGroup();
    if (group && group->isBGGroup())
        group = GetPlayer()->GetOriginalGroup();

    Group* group2 = player->GetGroup();
    if (group2 && group2->isBGGroup())
        group2 = player->GetOriginalGroup();
    // player already in another group or invited
    if (group2 || player->GetGroupInvite())
    {
        SendPartyResult(PARTY_OP_INVITE, memberName, ERR_ALREADY_IN_GROUP_S);

        //! tell the player that they were invited but it failed as they were already in a group
        if (group2) 
            SendGroupInvite(player, false);
        return;
    }

    if (group)
    {
        // not have permissions for invite
        if (!group->IsLeader(GetPlayer()->GetGUID()) && !group->IsAssistant(GetPlayer()->GetGUID()) && !(group->GetGroupType() & GROUPTYPE_EVERYONE_IS_ASSISTANT))
        {
            SendPartyResult(PARTY_OP_INVITE, "", ERR_NOT_LEADER);
            return;
        }
        // not have place
        if (group->IsFull())
        {
            SendPartyResult(PARTY_OP_INVITE, "", ERR_GROUP_FULL);
            return;
        }
    }

    // ok, but group not exist, start a new group
    // but don't create and save the group to the DB until
    // at least one person joins
    if (!group)
    {
        group = new Group;
        // new group: if can't add then delete
        if (!group->AddLeaderInvite(GetPlayer()))
        {
            delete group;
            return;
        }
        if (!group->AddInvite(player))
        {
            delete group;
            return;
        }
    }
    else
    {
        // already existed group: if can't add then just leave
        if (!group->AddInvite(player))
        {
            return;
        }
    }

    // ok, we do it
    SendGroupInvite(player, true);
    SendPartyResult(PARTY_OP_INVITE, memberName, ERR_PARTY_RESULT_OK);
}

//! 6.0.3
void WorldSession::HandleGroupInviteResponseOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GROUP_INVITE_RESPONSE");

    recvData.read_skip<uint8>(); // PartyIndex
    bool accept = recvData.ReadBit();
    bool dword18 = recvData.ReadBit();
    if (dword18)
        recvData.read_skip<uint32>();

    Group* group = GetPlayer()->GetGroupInvite();

    if (!group)
        return;

    if (accept)
    {
        // Remove player from invitees in any case
        group->RemoveInvite(GetPlayer());

        if (group->GetLeaderGUID() == GetPlayer()->GetGUID())
        {
            sLog->outError(LOG_FILTER_NETWORKIO, "HandleGroupAcceptOpcode: player %s(%d) tried to accept an invite to his own group", GetPlayer()->GetName(), GetPlayer()->GetGUID().GetCounter());
            return;
        }

        // Group is full
        if (group->IsFull())
        {
            SendPartyResult(PARTY_OP_INVITE, "", ERR_GROUP_FULL);
            return;
        }

        Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());

        // Forming a new group, create it
        if (!group->IsCreated())
        {
            // This can happen if the leader is zoning. To be removed once delayed actions for zoning are implemented
            if (!leader)
            {
                group->RemoveAllInvites();
                return;
            }

            // If we're about to create a group there really should be a leader present
            ASSERT(leader);
            group->RemoveInvite(leader);
            group->Create(leader);
            sGroupMgr->AddGroup(group);
        }

        // Everything is fine, do it, PLAYER'S GROUP IS SET IN ADDMEMBER!!!
        if (!group->AddMember(GetPlayer()))
            return;

        group->BroadcastGroupUpdate();
    }
    else
    {
        // Remember leader if online (group pointer will be invalid if group gets disbanded)
        Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());

        // uninvite, group can be deleted
        GetPlayer()->UninviteFromGroup();

        if (!leader || !leader->GetSession())
            return;

        // report
        //! 6.0.3
        WorldPacket data(SMSG_GROUP_DECLINE, 20);
        data.WriteBits(strlen(GetPlayer()->GetName()), 6);
        data.WriteString(GetPlayer()->GetName());
        leader->GetSession()->SendPacket(&data);
    }
}

//! 6.0.3
void WorldSession::HandleGroupUninviteOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GROUP_UNINVITE_GUID");

    ObjectGuid guid;
    std::string Reason;

    recvData.read_skip<uint8>(); // PartyIndex
    recvData >> guid;

    uint8 stringSize = recvData.ReadBits(8);
    Reason = recvData.ReadString(stringSize);

    //can't uninvite yourself
    if (guid == GetPlayer()->GetGUID())
    {
        sLog->outError(LOG_FILTER_NETWORKIO, "WorldSession::HandleGroupUninviteGuidOpcode: leader %s(%d) tried to uninvite himself from the group.", GetPlayer()->GetName(), GetPlayer()->GetGUID().GetCounter());
        return;
    }

    PartyResult res = GetPlayer()->CanUninviteFromGroup();
    if (res != ERR_PARTY_RESULT_OK)
    {
        SendPartyResult(PARTY_OP_UNINVITE, "", res);
        return;
    }

    Group* grp = GetPlayer()->GetGroup();
    if (!grp)
        return;

    if (grp->IsLeader(guid))
    {
        SendPartyResult(PARTY_OP_UNINVITE, "", ERR_NOT_LEADER);
        return;
    }

    if (grp->IsMember(guid))
    {
        Player::RemoveFromGroup(grp, guid, GROUP_REMOVEMETHOD_KICK, GetPlayer()->GetGUID(), Reason.c_str());
        return;
    }

    if (Player* player = grp->GetInvited(guid))
    {
        player->UninviteFromGroup();
        return;
    }

    SendPartyResult(PARTY_OP_UNINVITE, "", ERR_TARGET_NOT_IN_GROUP_S);
}

//! 6.0.3
void WorldSession::HandleGroupSetLeaderOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GROUP_SET_LEADER");

    ObjectGuid guid;
    recvData.read_skip<uint8>();
    recvData >> guid;

    Player* player = ObjectAccessor::FindPlayer(guid);
    Group* group = GetPlayer()->GetGroup();

    if (!group || !player)
        return;

    if (!group->IsLeader(GetPlayer()->GetGUID()) || player->GetGroup() != group)
        return;

    // Prevent exploits with instance saves
    for (GroupReference *itr = group->GetFirstMember(); itr != NULL; itr = itr->next())
        if (Player* plr = itr->getSource())
            if (plr->GetMap() && plr->GetMap()->Instanceable())
                return;

    // Everything's fine, accepted.
    group->ChangeLeader(guid);
    group->SendUpdate();
}

//! 6.0.3
void WorldSession::HandleGroupSetRolesOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_SET_ROLE");

    uint32 newRole = 0;
    uint8 unk = 0;
    ObjectGuid guid1; // Assigner GUID
    ObjectGuid guid2; // Target GUID

    guid1 = GetPlayer()->GetGUID();

    recvData >> unk >> guid2 >> newRole;

    ////! 5.4.1
    //WorldPacket data(SMSG_GROUP_SET_ROLE, 24);

    //data.WriteGuidMask<2>(guid1);
    //data.WriteGuidMask<1>(guid2);
    //data.WriteGuidMask<1, 3>(guid1);
    //data.WriteGuidMask<2, 3>(guid2);
    //data.WriteGuidMask<7>(guid1);
    //data.WriteGuidMask<7, 5>(guid2);
    //data.WriteGuidMask<4, 6>(guid1);
    //data.WriteGuidMask<0, 6>(guid2);
    //data.WriteGuidMask<0>(guid1);
    //data.WriteGuidMask<4>(guid2);
    //data.WriteGuidMask<5>(guid1);
    //        
    //data.WriteGuidBytes<1>(guid1);
    //data.WriteGuidBytes<7>(guid2);
    //data.WriteGuidBytes<6>(guid1);
    //data << uint32(0); // Old Role
    //data.WriteGuidBytes<5>(guid1);
    //data << uint8(unk);
    //data.WriteGuidBytes<3>(guid2);
    //data.WriteGuidBytes<3>(guid1);
    //data.WriteGuidBytes<4, 1 ,2>(guid2);
    //data.WriteGuidBytes<0, 7>(guid1);
    //data.WriteGuidBytes<0, 6>(guid2);
    //data << uint32(newRole); // New Role
    //data.WriteGuidBytes<5>(guid2);
    //data.WriteGuidBytes<2, 4>(guid1);

    if (GetPlayer()->GetGroup())
    {
        GetPlayer()->GetGroup()->setGroupMemberRole(guid2, newRole);
        GetPlayer()->GetGroup()->SendUpdate();
        //GetPlayer()->GetGroup()->BroadcastPacket(&data, false);
    }
    //else
    //    SendPacket(&data);
}

//! 6.0.3
void WorldSession::HandleGroupDisbandOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_LEAVE_GROUP");
    recvData.rfinish();

    Group* grp = GetPlayer()->GetGroup();
    if (!grp)
        return;

    if (_player->InBattleground())
    {
        SendPartyResult(PARTY_OP_INVITE, "", ERR_INVITE_RESTRICTED);
        return;
    }

    /** error handling **/
    /********************/

    // everything's fine, do it
    SendPartyResult(PARTY_OP_LEAVE, GetPlayer()->GetName(), ERR_PARTY_RESULT_OK);

    GetPlayer()->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);
}

//! 5.4.1
void WorldSession::HandleLootMasterAskForRoll(WorldPacket& recvData)
{
    ObjectGuid guid;
    uint8 slot = 0;

    recvData >> slot;
    //recvData.ReadGuidMask<0, 4, 2, 1, 5, 3, 7, 6>(guid);
    //recvData.ReadGuidBytes<4, 7, 6, 0, 5, 1, 2, 3>(guid);

    if (!_player->GetGroup() || _player->GetGroup()->GetLooterGuid() != _player->GetGUID())
    {
        _player->SendLootRelease(guid);
        return;
    }

    Loot* loot = NULL;

    if (guid.IsCreatureOrVehicle())
    {
        Creature* creature = GetPlayer()->GetMap()->GetCreature(guid);
        if (!creature)
            return;

        loot = &creature->loot;
    }
    else if (guid.IsGameObject())
    {
        GameObject* pGO = GetPlayer()->GetMap()->GetGameObject(guid);
        if (!pGO)
            return;

        loot = &pGO->loot;
    }

    if (!loot)
        return;

    if (slot >= loot->items.size() + loot->quest_items.size())
    {
        sLog->outDebug(LOG_FILTER_LOOT, "MasterLootItem: Player %s might be using a hack! (slot %d, size %lu)", GetPlayer()->GetName(), slot, (unsigned long)loot->items.size());
        return;
    }

    LootItem& item = slot >= loot->items.size() ? loot->quest_items[slot - loot->items.size()] : loot->items[slot];

    _player->GetGroup()->DoRollForAllMembers(guid, slot, _player->GetMapId(), loot, item, _player);
}

//! 5.4.1
void WorldSession::HandleLootMasterGiveOpcode(WorldPacket& recvData)
{
    ObjectGuid target_playerguid;

    uint32 count = recvData.ReadBits(23);
    std::vector<ObjectGuid> guids(count);
    std::vector<uint8> types(count);

    //recvData.ReadGuidMask<6, 2>(target_playerguid);
    
    if (count > 100)
    {
        recvData.rfinish();
        return;
    }

    for (uint32 i = 0; i < count; ++i)
        //recvData.ReadGuidMask<1, 0, 5, 2, 3, 6, 7, 4>(guids[i]);

    ///

    //recvData.ReadGuidMask<5, 7, 0, 3, 1, 4>(target_playerguid);

    for (uint32 i = 0; i < count; ++i)
    {
        //recvData.ReadGuidBytes<5, 2, 3, 0, 6, 7>(guids[i]);
        recvData >> types[i];
        //recvData.ReadGuidBytes<1, 4>(guids[i]);
    }

    //recvData.ReadGuidBytes<5, 3, 6, 7, 1, 2, 0, 4>(target_playerguid);

    Group* group = _player->GetGroup();
    if (!group || group->isLFGGroup() || group->GetLooterGuid() != _player->GetGUID())
    {
        _player->SendLootRelease(GetPlayer()->GetLootGUID());
        return;
    }

    //target_playerguid = GUID_LOPART(target_playerguid); //WARNING! TMP! plr should have off-like hi-guid, as server not suport it  - cut.

    Player* target = ObjectAccessor::FindPlayer(target_playerguid);
    if (!target)
        return;

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WorldSession::HandleLootMasterGiveOpcode (CMSG_LOOT_MASTER_GIVE, 0x02A3) Target = [%s].", target->GetName());

    for (uint32 i = 0; i < count; ++i)
    {
        ObjectGuid lootguid = guids[i];
        uint8 slotid = types[i];
        Loot* loot = NULL;

        if (lootguid.IsCreatureOrVehicle())
        {
            Creature* creature = GetPlayer()->GetMap()->GetCreature(lootguid);
            if (!creature)
                return;

            loot = &creature->loot;
        }
        else if (lootguid.IsGameObject())
        {
            GameObject* pGO = GetPlayer()->GetMap()->GetGameObject(lootguid);
            if (!pGO)
                return;

            loot = &pGO->loot;
        }

        if (!loot)
            return;

        if (slotid >= loot->items.size() + loot->quest_items.size())
        {
            sLog->outDebug(LOG_FILTER_LOOT, "MasterLootItem: Player %s might be using a hack! (slot %d, size %lu)", GetPlayer()->GetName(), slotid, (unsigned long)loot->items.size());
            return;
        }

        LootItem& item = slotid >= loot->items.size() ? loot->quest_items[slotid - loot->items.size()] : loot->items[slotid];
        if (item.currency)
        {
            sLog->outDebug(LOG_FILTER_LOOT, "WorldSession::HandleLootMasterGiveOpcode: player %s tried to give currency via master loot! Hack alert! Slot %u, currency id %u", GetPlayer()->GetName(), slotid, item.itemid);
            return;
        }

        ItemPosCountVec dest;
        InventoryResult msg = target->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item.itemid, item.count);
        if (item.follow_loot_rules && !item.AllowedForPlayer(target))
            msg = EQUIP_ERR_CANT_EQUIP_EVER;
        if (msg != EQUIP_ERR_OK)
        {
            target->SendEquipError(msg, NULL, NULL, item.itemid);
            // send duplicate of error massage to master looter
            _player->SendEquipError(msg, NULL, NULL, item.itemid);
            return;
        }

        // delete roll's in progress for this aoeSlot
        group->ErraseRollbyRealSlot(slotid, loot);

        // ToDo: check for already rolled items. This could posible on packet spaming (special tools should be writen, no so important now)

        // list of players allowed to receive this item in trade
        GuidSet looters = item.GetAllowedLooters();

        // not move item from loot to target inventory
        Item* newitem = target->StoreNewItem(dest, item.itemid, true, item.randomPropertyId, looters);
        target->SendNewItem(newitem, NULL, uint32(item.count), false, false, true);
        target->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_ITEM, item.itemid, item.count);
        target->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_TYPE, loot->loot_type, item.count);
        target->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_EPIC_ITEM, item.itemid, item.count);

        // mark as looted
        item.count=0;
        item.is_looted=true;

        loot->NotifyItemRemoved(slotid);
        --loot->unlootedCount;
    }
}

//! 6.0.3
void WorldSession::HandleLootMethodOpcode(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_LOOT_METHOD");

    uint8 lootMethod;
    ObjectGuid lootMaster;
    uint32 lootThreshold;

    recvData >> lootMethod;
    recvData.read_skip<uint8>();
    recvData >> lootMaster;
    recvData >> lootThreshold;

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    /** error handling **/
    if (!group->IsLeader(GetPlayer()->GetGUID()) || group->isLFGGroup())
        return;
    /********************/

    // everything's fine, do it
    group->SetLootMethod((LootMethod)lootMethod);
    group->SetLooterGuid(lootMaster);
    group->SetLootThreshold((ItemQualities)lootThreshold);
    group->SendUpdate();
}

//! 5.4.1
void WorldSession::HandleLootRoll(WorldPacket& recvData)
{
    ObjectGuid guid;
    uint8 itemSlot;
    uint8  rollType;

    recvData >> rollType;              // 0: pass, 1: need, 2: greed
    recvData >> itemSlot;              // always 0

    //recvData.ReadGuidMask<7, 1, 2, 4, 5, 6, 3, 0>(guid);
    //recvData.ReadGuidBytes<0, 7, 1, 3, 4, 6, 2, 5>(guid);

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    group->CountRollVote(GetPlayer()->GetGUID(), itemSlot, rollType);

    switch (rollType)
    {
    case ROLL_NEED:
        GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED, 1);
        break;
    case ROLL_GREED:
        GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED, 1);
        break;
    }
}

//! 6.0.3
void WorldSession::HandleMinimapPingOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received MSG_MINIMAP_PING");

    if (!GetPlayer()->GetGroup())
        return;

    float x, y;
    uint8 unk;
    recvData >> x >> y >> unk;

    //sLog->outDebug(LOG_FILTER_GENERAL, "Received opcode MSG_MINIMAP_PING X: %f, Y: %f", x, y);

    /** error handling **/
    /********************/

    // everything's fine, do it
    //! 6.0.3
    WorldPacket data(SMSG_MINIMAP_PING, (8+4+4));
    data << GetPlayer()->GetGUID();
    data << float(x);
    data << float(y);
    GetPlayer()->GetGroup()->BroadcastPacket(&data, true, -1, GetPlayer()->GetGUID());
}

//! 5.4.1
void WorldSession::HandleRandomRollOpcode(WorldPacket& recvData)
{
    uint32 minimum, maximum, roll;
    uint8 unk;
    recvData >> maximum;
    recvData >> minimum;
    recvData >> unk;

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_RANDOM_ROLL min: %u max: %u unk: %u",
        minimum, maximum, unk);

    /** error handling **/
    if (minimum > maximum || maximum > 10000)                // < 32768 for urand call
        return;
    /********************/

    // everything's fine, do it
    roll = urand(minimum, maximum);

    //sLog->outDebug(LOG_FILTER_NETWORKIO, "ROLL: MIN: %u, MAX: %u, ROLL: %u", minimum, maximum, roll);

    WorldPacket data(SMSG_RANDOM_ROLL, 4+4+4+8);
    ObjectGuid guid = GetPlayer()->GetGUID();
    data << uint32(roll);
    data << uint32(minimum);
    data << uint32(maximum);

    //data.WriteGuidMask<6, 1, 2, 7, 4, 5, 0, 3>(guid);
    //data.WriteGuidBytes<0, 6, 4, 3, 7, 2, 5, 1>(guid);

    if (GetPlayer()->GetGroup())
        GetPlayer()->GetGroup()->BroadcastPacket(&data, false);
    else
        SendPacket(&data);
}

//! 5.4.1
void WorldSession::HandleRaidTargetUpdateOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_RAID_TARGET_UPDATE");

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    uint8 x, unk;
    ObjectGuid guid;

    recvData >> unk >> guid >> x;

    /** error handling **/
    /********************/

    // everything's fine, do it
    if (x == 0xFF)                                          // target icon request
        group->SendTargetIconList(this);
    else                                                    // target icon update
    {
        if (!group->IsLeader(GetPlayer()->GetGUID()) && !group->IsAssistant(GetPlayer()->GetGUID()) && !(group->GetGroupType() & GROUPTYPE_EVERYONE_IS_ASSISTANT))
            return;

        group->SetTargetIcon(x, _player->GetGUID(), guid);
    }
}

//! 6.0.3
void WorldSession::HandleGroupRaidConvertOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GROUP_RAID_CONVERT");

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    if (_player->InBattleground())
        return;

    // error handling
    if (!group->IsLeader(GetPlayer()->GetGUID()) || group->GetMembersCount() < 2)
        return;

    // everything's fine, do it (is it 0 (PARTY_OP_INVITE) correct code)
    SendPartyResult(PARTY_OP_INVITE, "", ERR_PARTY_RESULT_OK);

    // New 4.x: it is now possible to convert a raid to a group if member count is 5 or less

    bool Raid;
    recvData >> Raid;

    if(group->isRaidGroup())
        group->ConvertToGroup();
    else
        group->ConvertToRaid();
}

//! 5.4.1
void WorldSession::HandleGroupChangeSubGroupOpcode(WorldPacket& recvData)
{
    time_t now = time(NULL);
    if (now - timeAddIgnoreOpcode < 3)
    {
        recvData.rfinish();
        return;
    }
    else
       timeAddIgnoreOpcode = now;

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GROUP_CHANGE_SUB_GROUP");

    // we will get correct pointer for group here, so we don't have to check if group is BG raid
    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    ObjectGuid guid;
    uint8 groupNr;
    uint8 unk;
    recvData >> unk >> groupNr;

    //recvData.ReadGuidMask<0, 1, 7, 6, 3, 5, 4, 2>(guid);
    //recvData.ReadGuidBytes<6, 3, 7, 5, 1, 4, 2, 0>(guid);

    if (groupNr >= MAX_RAID_SUBGROUPS)
        return;

    ObjectGuid senderGuid = GetPlayer()->GetGUID();
    if (!group->IsLeader(senderGuid) && !group->IsAssistant(senderGuid) && !(group->GetGroupType() & GROUPTYPE_EVERYONE_IS_ASSISTANT))
        return;

    if (!group->HasFreeSlotSubGroup(groupNr))
        return;

    group->ChangeMembersGroup(guid, groupNr);
}

void WorldSession::HandleGroupSwapSubGroupOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GROUP_SWAP_SUB_GROUP");
    uint8 unk1;
    ObjectGuid guid1;
    ObjectGuid guid2;
    uint8 unk2;

    recvData >> unk1;

    guid1[4] = recvData.ReadBit();
    guid1[6] = recvData.ReadBit();
    guid1[5] = recvData.ReadBit();
    guid1[0] = recvData.ReadBit();
    guid2[3] = recvData.ReadBit();
    guid2[4] = recvData.ReadBit();
    guid1[7] = recvData.ReadBit();
    guid1[2] = recvData.ReadBit();

    guid2[7] = recvData.ReadBit();
    guid2[1] = recvData.ReadBit();
    guid2[5] = recvData.ReadBit();
    guid2[6] = recvData.ReadBit();
    guid2[0] = recvData.ReadBit();
    guid1[3] = recvData.ReadBit();
    guid2[2] = recvData.ReadBit();
    guid1[1] = recvData.ReadBit();

    recvData.ReadByteSeq(guid2[0]);
    recvData.ReadByteSeq(guid1[5]);
    recvData.ReadByteSeq(guid1[0]);
    recvData.ReadByteSeq(guid2[7]);
    recvData.ReadByteSeq(guid1[6]);
    recvData.ReadByteSeq(guid2[1]);
    recvData.ReadByteSeq(guid2[5]);
    recvData.ReadByteSeq(guid1[7]);

    recvData.ReadByteSeq(guid1[4]);
    recvData.ReadByteSeq(guid1[3]);
    recvData.ReadByteSeq(guid2[3]);
    recvData.ReadByteSeq(guid1[1]);
    recvData.ReadByteSeq(guid1[4]);
    recvData.ReadByteSeq(guid2[6]);
    recvData.ReadByteSeq(guid2[2]);
    recvData.ReadByteSeq(guid2[2]);

    recvData >> unk2;
}

//! 5.4.1
void WorldSession::HandleGroupEveryoneIsAssistantOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_SET_EVERYONE_IS_ASSISTANT");

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    if (!group->IsLeader(GetPlayer()->GetGUID()))
        return;
    recvData.read_skip<uint8>();
    bool apply = recvData.ReadBit();

    group->ChangeFlagEveryoneAssistant(apply);
}

//! 5.4.1
void WorldSession::HandleGroupAssistantLeaderOpcode(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GROUP_ASSISTANT_LEADER");

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    if (!group->IsLeader(GetPlayer()->GetGUID()))
        return;

    ObjectGuid guid;
    bool apply;
    uint8 unk = 0;
    recvData >> unk;

    //recvData.ReadGuidMask<0, 3, 6, 5>(guid);
    apply = recvData.ReadBit();
    //recvData.ReadGuidMask<7, 2, 4, 1>(guid);
    //recvData.ReadGuidBytes<1, 2, 7, 3, 4, 5, 0, 6>(guid);

    group->SetGroupMemberFlag(guid, apply, MEMBER_FLAG_ASSISTANT);

    group->SendUpdate();
}

//! 5.4.1
void WorldSession::HandlePartyAssignmentOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received MSG_PARTY_ASSIGNMENT");

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    ObjectGuid senderGuid = GetPlayer()->GetGUID();
    if (!group->IsLeader(senderGuid) && !group->IsAssistant(senderGuid) && !(group->GetGroupType() & GROUPTYPE_EVERYONE_IS_ASSISTANT))
        return;

    uint8 assignment;
    uint8 unk;
    bool apply;
    ObjectGuid guid;

    recvData >> unk >> assignment;

    //recvData.ReadGuidMask<3, 5>(guid);
    apply = recvData.ReadBit();
    //recvData.ReadGuidMask<0, 7, 2, 6, 4, 1>(guid);
    //recvData.ReadGuidBytes<4, 1, 0, 6, 5, 3, 7, 2>(guid);

    switch (assignment)
    {
    case GROUP_ASSIGN_MAINASSIST:
        group->RemoveUniqueGroupMemberFlag(MEMBER_FLAG_MAINASSIST);
        group->SetGroupMemberFlag(guid, apply, MEMBER_FLAG_MAINASSIST);
        break;
    case GROUP_ASSIGN_MAINTANK:
        group->RemoveUniqueGroupMemberFlag(MEMBER_FLAG_MAINTANK);           // Remove main assist flag from current if any.
        group->SetGroupMemberFlag(guid, apply, MEMBER_FLAG_MAINTANK);
    default:
        break;
    }

    group->SendUpdate();
}

//! 5.4.1
void WorldSession::HandleRaidLeaderReadyCheck(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_RAID_LEADER_READY_CHECK");

    recvData.read_skip<uint8>(); // unk, 0x00

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    if (!group->IsLeader(GetPlayer()->GetGUID()) && !group->IsAssistant(GetPlayer()->GetGUID()) && !(group->GetGroupType() & GROUPTYPE_EVERYONE_IS_ASSISTANT))
        return;

    ObjectGuid groupGUID = group->GetGUID();
    ObjectGuid leaderGUID = GetPlayer()->GetGUID();

    group->SetReadyCheckCount(1);

    //! 5.4.1
    WorldPacket data(SMSG_RAID_READY_CHECK_STARTED);

    //data.WriteGuidMask<2>(leaderGUID);
    //data.WriteGuidMask<6, 4>(groupGUID);
    //data.WriteGuidMask<7, 4, 1>(leaderGUID);
    //data.WriteGuidMask<1>(groupGUID);
    //data.WriteGuidMask<6>(leaderGUID);
    //data.WriteGuidMask<7, 5>(groupGUID);
    //data.WriteGuidMask<0, 3>(leaderGUID);
    //data.WriteGuidMask<0>(groupGUID);
    //data.WriteGuidMask<5>(leaderGUID);
    //data.WriteGuidMask<3, 2>(groupGUID);

    //data.WriteGuidBytes<5, 0, 4, 2>(groupGUID);
    //data.WriteGuidBytes<6, 5>(leaderGUID);
    //data.WriteGuidBytes<7, 1>(groupGUID);
    //data.WriteGuidBytes<1>(leaderGUID);
    //data.WriteGuidBytes<6>(groupGUID);
    //data.WriteGuidBytes<2, 4, 7, 0>(leaderGUID);
    data << uint8(1);                               // unk 5.0.5
    //data.WriteGuidBytes<3>(groupGUID);
    data << uint32(35000);                          // TimeOut
    //data.WriteGuidBytes<3>(leaderGUID);

    group->BroadcastPacket(&data, false, -1);

    group->OfflineReadyCheck();
}

//! 5.4.1
void WorldSession::HandleRaidConfirmReadyCheck(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_RAID_CONFIRM_READY_CHECK");

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    recvData.read_skip<uint8>(); // unk, 0x00
    recvData.ReadBits(4);
    bool ready = recvData.ReadBit();
    recvData.rfinish();         //not use guid

    ObjectGuid plGUID = GetPlayer()->GetGUID();
    ObjectGuid grpGUID = group->GetGUID();

    group->SetReadyCheckCount(group->GetReadyCheckCount() +1 );

    //! 5.4.1
    WorldPacket data(SMSG_RAID_READY_CHECK_RESPONSE);
    //data.WriteGuidMask<6>(plGUID);
    data.WriteBit(ready);
    //data.WriteGuidMask<5>(plGUID);
    //data.WriteGuidMask<3, 2>(grpGUID);
    //data.WriteGuidMask<1, 0>(plGUID);
    //data.WriteGuidMask<1>(grpGUID);
    //data.WriteGuidMask<2>(plGUID);
    //data.WriteGuidMask<4, 6>(grpGUID);
    //data.WriteGuidMask<3, 4>(plGUID);
    //data.WriteGuidMask<5, 7>(grpGUID);
    //data.WriteGuidMask<7>(plGUID);
    //data.WriteGuidMask<0>(grpGUID);

    data.FlushBits();

    //data.WriteGuidBytes<0, 6>(plGUID);
    //data.WriteGuidBytes<0>(grpGUID);
    //data.WriteGuidBytes<3>(plGUID);
    //data.WriteGuidBytes<5, 6>(grpGUID);
    //data.WriteGuidBytes<2>(plGUID);
    //data.WriteGuidBytes<2>(grpGUID);
    //data.WriteGuidBytes<7>(plGUID);
    //data.WriteGuidBytes<4, 3>(grpGUID);
    //data.WriteGuidBytes<4>(plGUID);
    //data.WriteGuidBytes<1, 7>(grpGUID);
    //data.WriteGuidBytes<5, 1>(plGUID);

    group->BroadcastPacket(&data, true);

    // Send SMSG_RAID_READY_CHECK_COMPLETED
    if(group->GetReadyCheckCount() >= group->GetMembersCount())
    {
        ObjectGuid grpGUID = group->GetGUID();

        //! 5.4.1
        data.Initialize(SMSG_RAID_READY_CHECK_COMPLETED);
        //data.WriteGuidMask<3, 2, 0, 7, 6, 4, 1, 5>(grpGUID);
        //data.WriteGuidBytes<3, 0, 6, 7, 1, 4, 5, 2>(grpGUID);
        data << uint8(1);

        group->BroadcastPacket(&data, true);
    }
}

void WorldSession::BuildPartyMemberStatsChangedPacket(Player* player, WorldPacket* data, bool full /*= false*/)
{
    ObjectGuid guid = player->GetGUID();
    uint32 mask = player->GetGroupUpdateFlag();

    //! 6.0.3
    data->Initialize(SMSG_PARTY_MEMBER_STATS, 200);

    if (full)
    {
        mask = GROUP_UPDATE_FULL;
    }
    else
    {
        if (mask == GROUP_UPDATE_FLAG_NONE)
            return;

        if (mask & GROUP_UPDATE_FLAG_POWER_TYPE)                // if update power type, update current/max power also
            mask |= (GROUP_UPDATE_FLAG_CUR_POWER | GROUP_UPDATE_FLAG_MAX_POWER);

        if (mask & GROUP_UPDATE_FLAG_PET_POWER_TYPE)            // same for pets
            mask |= (GROUP_UPDATE_FLAG_PET_CUR_POWER | GROUP_UPDATE_FLAG_PET_MAX_POWER);
    }

    data->WriteBit(0);          //ForEnemy
    data->WriteBit(full);

    data->WriteBit(0);                              //bit761
    data->WriteBit(mask & GROUP_UPDATE_FLAG_STATUS);
    data->WriteBit(mask & GROUP_UPDATE_FLAG_POWER_TYPE);
    data->WriteBit(0);                              //HasUnk322
    data->WriteBit(mask & GROUP_UPDATE_FLAG_CUR_HP);
    data->WriteBit(mask & GROUP_UPDATE_FLAG_MAX_HP);
    data->WriteBit(mask & GROUP_UPDATE_FLAG_CUR_POWER);
    data->WriteBit(mask & GROUP_UPDATE_FLAG_MAX_POWER);
    data->WriteBit(mask & GROUP_UPDATE_FLAG_LEVEL);
    data->WriteBit(0);                              //HasUnk326
    data->WriteBit(mask & GROUP_UPDATE_FLAG_ZONE);
    data->WriteBit(mask & GROUP_UPDATE_FLAG_SPECIALIZATION);
    data->WriteBit(0);                              //HasUnk776
    data->WriteBit(mask & GROUP_UPDATE_FLAG_POSITION);
    data->WriteBit(mask & GROUP_UPDATE_FLAG_VEHICLE_SEAT);
    data->WriteBit(mask & GROUP_UPDATE_FLAG_AURAS);
    data->WriteBit(mask & GROUP_UPDATE_PET);
    data->WriteBit(mask & GROUP_UPDATE_FLAG_PHASE);

    data->FlushBits();

    *data << guid;

    if (mask & GROUP_UPDATE_FLAG_STATUS)
    {
        if (player->IsPvP())
            *data << uint16(MEMBER_STATUS_ONLINE | MEMBER_STATUS_PVP);
        else
            *data << uint16(MEMBER_STATUS_ONLINE);
    }

    if (mask & GROUP_UPDATE_FLAG_POWER_TYPE)
        *data << uint8(player->getPowerType());

    if (mask & GROUP_UPDATE_FLAG_CUR_HP)
        *data << uint32(player->GetHealth());

    if (mask & GROUP_UPDATE_FLAG_MAX_HP)
        *data << uint32(player->GetMaxHealth());

    if (mask & GROUP_UPDATE_FLAG_CUR_POWER)
        *data << uint16(player->GetPower(player->getPowerType()));

    if (mask & GROUP_UPDATE_FLAG_MAX_POWER)
        *data << uint16(player->GetMaxPower(player->getPowerType()));

    if (mask & GROUP_UPDATE_FLAG_LEVEL)
        *data << uint16(player->getLevel());

    if (mask & GROUP_UPDATE_FLAG_ZONE)
        *data << uint16(player->GetZoneId());

    if (mask & GROUP_UPDATE_FLAG_SPECIALIZATION)
        *data << uint16(player->GetSpecializationId(player->GetActiveSpec()));

    //if (mask & GROUP_UPDATE_FLAG_UNK100)
    //    *data << uint16(0);

    if (mask & GROUP_UPDATE_FLAG_POSITION)
        *data << uint16(player->GetPositionX()) << uint16(player->GetPositionY()) << uint16(player->GetPositionZ());

    if (mask & GROUP_UPDATE_FLAG_VEHICLE_SEAT)
    {
        if (Vehicle* veh = player->GetVehicle())
            *data << uint32(veh->GetVehicleInfo()->SeatID[player->m_movementInfo.transport.seat]);
        else
            *data << uint32(0);
    }

    if (mask & GROUP_UPDATE_FLAG_AURAS)
    {
        //data << uint8(0);
        uint64 auramask = player->GetAuraUpdateMaskForRaid();
        //data << uint64(auramask);
        size_t countPos = data->wpos();
        *data << uint32(MAX_AURAS); // count
        uint32 count = 0;

        for (uint32 i = 0; i < MAX_AURAS; ++i)
        {
            if (auramask & (uint64(1) << i))
            {
                ++count;
                AuraApplication const* aurApp = player->GetVisibleAura(i);
                if (!aurApp)
                {
                    *data << uint32(0);
                    *data << uint8(0);
                    *data << uint32(0);
                    *data << uint32(0);
                    continue;
                }

                *data << uint32(aurApp->GetBase()->GetId());
                *data << uint8(aurApp->GetFlags());
                *data << uint32(aurApp->GetEffectMask());

                size_t pos = data->wpos();

                uint8 count2 = 0;
                *data << uint32(count2);

                if (aurApp->GetFlags() & AFLAG_ANY_EFFECT_AMOUNT_SENT)
                {
                    for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                    {
                        if (AuraEffect const* eff = aurApp->GetBase()->GetEffect(i)) // NULL if effect flag not set
                        {
                            *data << float(eff->GetAmount());
                            ++count2;
                        }
                    }
                    data->put<uint32>(pos, count2);
                }
            }
        }
        data->put<uint32>(countPos, count);
    }

    if (mask & GROUP_UPDATE_PET)
    {
        Pet* pet = player->GetPet();

        data->WriteBit(mask & GROUP_UPDATE_FLAG_PET_GUID);
        data->WriteBit(mask & GROUP_UPDATE_FLAG_PET_NAME);
        data->WriteBit(mask & GROUP_UPDATE_FLAG_PET_MODEL_ID);
        data->WriteBit(mask & GROUP_UPDATE_FLAG_PET_CUR_HP);
        data->WriteBit(mask & GROUP_UPDATE_FLAG_PET_MAX_HP);
        data->WriteBit(mask & GROUP_UPDATE_FLAG_PET_AURAS);

        data->FlushBits();
        if (mask & GROUP_UPDATE_FLAG_PET_GUID)
        {
            if (pet)
                *data << pet->GetGUID();
            else
                *data << ObjectGuid::Empty;
        }

        if (mask & GROUP_UPDATE_FLAG_PET_NAME)
        {
            if (pet)
            {
                data->WriteBits(strlen(pet->GetName()), 8);
                data->WriteString(pet->GetName());
            }
            else
                *data << uint8(0);
        }

        if (mask & GROUP_UPDATE_FLAG_PET_MODEL_ID)
        {
            if (pet)
                *data << uint16(pet->GetDisplayId());
            else
                *data << uint16(0);
        }

        if (mask & GROUP_UPDATE_FLAG_PET_CUR_HP)
        {
            if (pet)
                *data << uint32(pet->GetHealth());
            else
                *data << uint32(0);
        }

        if (mask & GROUP_UPDATE_FLAG_PET_MAX_HP)
        {
            if (pet)
                *data << uint32(pet->GetMaxHealth());
            else
                *data << uint32(0);
        }

        /*if (mask & GROUP_UPDATE_FLAG_PET_POWER_TYPE)
        {
            if (pet)
                *data << uint8(pet->getPowerType());
            else
                *data << uint8(0);
        }

        if (mask & GROUP_UPDATE_FLAG_PET_CUR_POWER)
        {
            if (pet)
                *data << uint16(pet->GetPower(pet->getPowerType()));
            else
                *data << uint16(0);
        }

        if (mask & GROUP_UPDATE_FLAG_PET_MAX_POWER)
        {
            if (pet)
                *data << uint16(pet->GetMaxPower(pet->getPowerType()));
            else
                *data << uint16(0);
        }*/

        if (mask & GROUP_UPDATE_FLAG_PET_AURAS)
        {
            if (pet)
            {
                //*data << uint8(0);
                uint64 auramask = pet->GetAuraUpdateMaskForRaid();
                //*data << uint64(auramask);
                size_t countPos = data->wpos();
                uint32 count = 0;
                *data << uint32(MAX_AURAS); // count

                for (uint32 i = 0; i < MAX_AURAS; ++i)
                {
                    if (auramask & (uint64(1) << i))
                    {
                        ++count;
                        AuraApplication const* aurApp = pet->GetVisibleAura(i);
                        if (!aurApp)
                        {
                            *data << uint32(0);
                            *data << uint8(0);
                            *data << uint32(0);
                            *data << uint32(0);
                            continue;
                        }

                        *data << uint32(aurApp->GetBase()->GetId());
                        *data << uint8(aurApp->GetFlags());
                        *data << uint32(aurApp->GetEffectMask());

                        size_t pos = data->wpos();
                        uint8 count2 = 0;
                        *data << uint32(count2);

                        if (aurApp->GetFlags() & AFLAG_ANY_EFFECT_AMOUNT_SENT)
                        {
                            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                            {
                                if (AuraEffect const* eff = aurApp->GetBase()->GetEffect(i)) // NULL if effect flag not set
                                {
                                    *data << float(eff->GetAmount());
                                    ++count2;
                                }
                            }
                            data->put<uint32>(pos, count2);
                        }
                    }
                }
                data->put<uint32>(countPos, count);
            }
            else
            {
                *data << uint32(0);
            }
        }
    }

    if (mask & GROUP_UPDATE_FLAG_PHASE)
    {
        *data << uint32(0); // PhaseShiftFlags
        *data << uint32(0); // PhaseCount
        *data << ObjectGuid::Empty;
        // for (count) *data << uint16(phaseId)
    }
}

//! 6.0.3
/*this procedure handles clients CMSG_REQUEST_PARTY_MEMBER_STATS request*/
void WorldSession::HandleRequestPartyMemberStatsOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_REQUEST_PARTY_MEMBER_STATS");
    ObjectGuid Guid;
    recvData.read_skip<uint8>();
    recvData >> Guid;

    Player* player = HashMapHolder<Player>::Find(Guid);
    if (!player)
    {
        //! 6.0.3
        WorldPacket data(SMSG_PARTY_MEMBER_STATS, 3+4+2);
        data.WriteBit(0);                                   // only for SMSG_PARTY_MEMBER_STATS_FULL, probably arena/bg related
        data.WriteBit(1);                                   // full

        data.WriteBit(0);                              //bit761
        data.WriteBit(1/*mask & GROUP_UPDATE_FLAG_STATUS*/);
        data.WriteBit(0/*mask & GROUP_UPDATE_FLAG_POWER_TYPE*/);
        data.WriteBit(0);                              //HasUnk322
        data.WriteBit(0/*mask & GROUP_UPDATE_FLAG_CUR_HP*/);
        data.WriteBit(0/*mask & GROUP_UPDATE_FLAG_MAX_HP*/);
        data.WriteBit(0/*mask & GROUP_UPDATE_FLAG_CUR_POWER*/);
        data.WriteBit(0/*mask & GROUP_UPDATE_FLAG_MAX_POWER*/);
        data.WriteBit(0/*mask & GROUP_UPDATE_FLAG_LEVEL*/);
        data.WriteBit(0);                              //HasUnk326
        data.WriteBit(0/*mask & GROUP_UPDATE_FLAG_ZONE)*/);
        data.WriteBit(0/*mask & GROUP_UPDATE_FLAG_SPECIALIZATION*/);
        data.WriteBit(0);                              //HasUnk776
        data.WriteBit(0/*mask & GROUP_UPDATE_FLAG_POSITION*/);
        data.WriteBit(0/*mask & GROUP_UPDATE_FLAG_VEHICLE_SEAT*/);
        data.WriteBit(0/*mask & GROUP_UPDATE_FLAG_AURAS*/);
        data.WriteBit(0/*mask & GROUP_UPDATE_PET*/);
        data.WriteBit(0/*mask & GROUP_UPDATE_FLAG_PHASE*/);

        data.FlushBits();

        data << Guid;

        //if (mask & GROUP_UPDATE_FLAG_STATUS)
        data << (uint16) MEMBER_STATUS_OFFLINE;

        SendPacket(&data);
        return;
    }

    WorldPacket data;
    player->GetSession()->BuildPartyMemberStatsChangedPacket(player, &data, true);
    SendPacket(&data);

    // Send group target icons too
    if (Group* group = player->GetGroup())
        group->SendTargetIconList(this);
}

/*!*/void WorldSession::HandleRequestRaidInfoOpcode(WorldPacket & /*recvData*/)
{
    // every time the player checks the character screen
    _player->SendRaidInfo();
}

/*void WorldSession::HandleGroupCancelOpcode(WorldPacket & recvData)
{
sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: got CMSG_GROUP_CANCEL.");
}*/

//! 5.4.1
void WorldSession::HandleOptOutOfLootOpcode(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_OPT_OUT_OF_LOOT");

    bool passOnLoot;
    recvData >> passOnLoot; // 1 always pass, 0 do not pass

    // ignore if player not loaded
    if (!GetPlayer())                                        // needed because STATUS_AUTHED
    {
        if (passOnLoot)
            sLog->outError(LOG_FILTER_NETWORKIO, "CMSG_OPT_OUT_OF_LOOT value<>0 for not-loaded character!");
        return;
    }

    GetPlayer()->SetPassOnGroupLoot(passOnLoot);
}

void WorldSession::HandleRolePollBegin(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_ROLE_POLL_BEGIN");

    uint8 unk;
    recvData >> unk;

    if (!GetPlayer()->GetGroup())
        return;

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    if (!group->IsLeader(GetPlayer()->GetGUID()) && !group->IsAssistant(GetPlayer()->GetGUID()) && !(group->GetGroupType() & GROUPTYPE_EVERYONE_IS_ASSISTANT))
    {
        SendPartyResult(PARTY_OP_INVITE, "", ERR_NOT_LEADER);
        return;
    }

    ObjectGuid guid = group->GetGUID();

    //! 5.4.1
    WorldPacket data(SMSG_ROLE_POLL_BEGIN, (8+4+4));
    //data.WriteGuidMask<0, 1, 7, 2, 3, 5, 4, 6>(guid);
    //data.WriteGuidBytes<0, 7>(guid);
    data << unk;
    //data.WriteGuidBytes<1, 2, 4, 3, 5, 6>(guid);

    group->BroadcastPacket(&data, false);
}

void WorldSession::HandleClearRaidMarkerOpcode(WorldPacket& recv_data)
{
    int8 id;
    recv_data >> id;

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_CLEAR_RAID_MARKER from %s (%u) id: %i",
        GetPlayerName().c_str(), GetAccountId(), id);

    Group* group = _player->GetGroup();
    if (!group)
        return;

    if (group->IsAssistant(_player->GetGUID()) || group->IsLeader(_player->GetGUID()))
        group->SetRaidMarker(id, _player, ObjectGuid::Empty);
}
