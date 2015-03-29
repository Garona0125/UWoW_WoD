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
#include "WorldPacket.h"
#include "WorldSession.h"
#include "World.h"
#include "ObjectMgr.h"
#include "GuildMgr.h"
#include "Log.h"
#include "Opcodes.h"
#include "Guild.h"
#include "GossipDef.h"
#include "SocialMgr.h"

// Helper for getting guild object of session's player.
// If guild does not exist, sends error (if necessary).
inline Guild* _GetPlayerGuild(WorldSession* session, bool sendError = false)
{
    if (uint32 guildId = session->GetPlayer()->GetGuildId())    // If guild id = 0, player is not in guild
        if (Guild* guild = sGuildMgr->GetGuildById(guildId))   // Find guild by id
            return guild;
    if (sendError)
        Guild::SendCommandResult(session, GUILD_CREATE_S, ERR_GUILD_PLAYER_NOT_IN_GUILD);
    return NULL;
}

//! 6.0.3
void WorldSession::HandleGuildQueryOpcode(WorldPacket& recvPacket)
{
    //sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_QUERY");

    ObjectGuid guildGuid, playerGuid;
    recvPacket >> guildGuid >> playerGuid;

    // If guild doesn't exist or player is not part of the guild send error
    if (Guild* guild = sGuildMgr->GetGuildByGuid(guildGuid))
        //if (guild->IsMember(playerGuid))
        {
            guild->HandleQuery(this);
            return;
        }
    else
    {
        WorldPacket data(SMSG_GUILD_QUERY_RESPONSE, 8 + 1 + 1);
        data << guildGuid;
        data.WriteBit(0);   // no data
        SendPacket(&data);
    }

    Guild::SendCommandResult(this, GUILD_CREATE_S, ERR_GUILD_PLAYER_NOT_IN_GUILD);
}

//! 6.0.3
void WorldSession::HandleGuildInviteOpcode(WorldPacket& recvPacket)
{
    time_t now = time(NULL);
    if (now - timeLastGuildInviteCommand < 5)
        return;
    else
       timeLastGuildInviteCommand = now;

    //sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_INVITE_BY_NAME");
    std::string invitedName = recvPacket.ReadString(recvPacket.ReadBits(9));

    if (normalizePlayerName(invitedName))
        if (Guild* guild = _GetPlayerGuild(this, true))
            guild->HandleInviteMember(this, invitedName);
}

//! 6.0.3
void WorldSession::HandleGuildRemoveOpcode(WorldPacket& recvPacket)
{
    //sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_OFFICER_REMOVE_MEMBER");

    ObjectGuid playerGuid;
    recvPacket >> playerGuid;

    if (Guild* guild = _GetPlayerGuild(this, true))
        guild->HandleRemoveMember(this, playerGuid);
}

//! 6.0.3
void WorldSession::HandleGuildAcceptOpcode(WorldPacket& /*recvPacket*/)
{
    //sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received /*CMSG_ACCEPT_GUILD_INVITE*/");
    // Player cannot be in guild
    if (!GetPlayer()->GetGuildId())
        // Guild where player was invited must exist
        if (Guild* guild = sGuildMgr->GetGuildById(GetPlayer()->GetGuildIdInvited()))
            guild->HandleAcceptMember(this);
}

//! 6.0.3
void WorldSession::HandleGuildDeclineOpcode(WorldPacket& recvPacket)
{
    //sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_DECLINE_INVITATION");

    if (Player* inviter = ObjectAccessor::FindPlayer(GetPlayer()->GetGuildInviterGuid()))
    {
        std::string name = GetPlayer()->GetName();
        WorldPacket data(SMSG_GUILD_INVITE_DECLINED, 1 + name.size() + 4);
        data.WriteBits(name.size(), 6);
        data.WriteBit(recvPacket.GetOpcode() == CMSG_GUILD_AUTO_DECLINE_INVITATION);
        data << uint32(realmHandle.Index);
        data.WriteString(name);
        inviter->SendDirectMessage(&data);
    }

    GetPlayer()->SetGuildIdInvited(UI64LIT(0));
    GetPlayer()->SetInGuild(UI64LIT(0));
}

//! 6.0.3
void WorldSession::HandleGuildRosterOpcode(WorldPacket& /*recvPacket*/)
{
    //sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_GET_ROSTER");

    if (Guild* guild = _GetPlayerGuild(this, true))
        guild->HandleRoster(this);
}

//! 6.0.3
void WorldSession::HandleGuildPromoteOpcode(WorldPacket& recvPacket)
{
    //sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_PROMOTE_MEMBER");

    ObjectGuid targetGuid;
    recvPacket >> targetGuid;

    if (Guild* guild = _GetPlayerGuild(this, true))
        guild->HandleUpdateMemberRank(this, targetGuid, false);
}

//! 6.0.3
void WorldSession::HandleGuildDemoteOpcode(WorldPacket& recvPacket)
{
    //sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_DEMOTE_MEMBER");

    ObjectGuid targetGuid;
    recvPacket >> targetGuid;

    if (Guild* guild = _GetPlayerGuild(this, true))
        guild->HandleUpdateMemberRank(this, targetGuid, true);
}

//! 6.0.3
void WorldSession::HandleGuildAssignRankOpcode(WorldPacket& recvPacket)
{
    //sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_ASSIGN_MEMBER_RANK");

    ObjectGuid targetGuid;
    uint32 rankId;
    recvPacket >> targetGuid >> rankId;

    if (Guild* guild = _GetPlayerGuild(this, true))
        guild->HandleSetMemberRank(this, targetGuid, GetPlayer()->GetGUID(), rankId);
}

void WorldSession::HandleGuildLeaveOpcode(WorldPacket& /*recvPacket*/)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_LEAVE");

    if (Guild* guild = _GetPlayerGuild(this, true))
        guild->HandleLeaveMember(this);
}

void WorldSession::HandleGuildDisbandOpcode(WorldPacket& /*recvPacket*/)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_DISBAND");

    if (Guild* guild = _GetPlayerGuild(this, true))
        guild->HandleDisband(this);
}

void WorldSession::HandleGuildLeaderOpcode(WorldPacket& recvPacket)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_LEADER");

    std::string name = recvPacket.ReadString(recvPacket.ReadBits(9));

    if (normalizePlayerName(name))
        if (Guild* guild = _GetPlayerGuild(this, true))
            guild->HandleSetLeader(this, name);
}

void WorldSession::HandleGuildMOTDOpcode(WorldPacket& recvPacket)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_MOTD");

    std::string motd = recvPacket.ReadString(recvPacket.ReadBits(10));

    if (Guild* guild = _GetPlayerGuild(this, true))
        guild->HandleSetMOTD(this, motd);
}

void WorldSession::HandleSwapRanks(WorldPacket& recvPacket)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_SWITCH_RANK");

    uint32 id;

    recvPacket >> id;
    bool up = recvPacket.ReadBit();
    if (Guild* guild = _GetPlayerGuild(this, true))
        guild->HandleSwapRanks(this, id, up);
}

void WorldSession::HandleGuildSetNoteOpcode(WorldPacket& recvPacket)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_SET_NOTE");

    ObjectGuid playerGuid;

    //recvPacket.ReadGuidMask<3, 7, 1>(playerGuid);
    bool type = recvPacket.ReadBit();      // 0 == Officer, 1 == Public
    //recvPacket.ReadGuidMask<6, 0, 4, 2>(playerGuid);
    uint32 noteLength = recvPacket.ReadBits(8);
    //recvPacket.ReadGuidMask<5>(playerGuid);

    //recvPacket.ReadGuidBytes<1, 2, 4, 7, 0, 5, 3, 6>(playerGuid);
    std::string note = recvPacket.ReadString(noteLength);

    if (Guild* guild = _GetPlayerGuild(this, true))
        guild->HandleSetMemberNote(this, note, playerGuid, type);

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: end CMSG_GUILD_SET_NOTE");
}

void WorldSession::HandleGuildQueryRanksOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_QUERY_RANKS");

    ObjectGuid guildGuid;
    //recvData.ReadGuidMask<5, 7, 2, 0, 4, 3, 6, 1>(guildGuid);
    //recvData.ReadGuidBytes<2, 3, 5, 7, 6, 4, 1, 0>(guildGuid);

    if (Guild* guild = sGuildMgr->GetGuildByGuid(guildGuid))
        if (guild->IsMember(_player->GetGUID()))
            guild->HandleGuildRanks(this);
}

void WorldSession::HandleGuildAddRankOpcode(WorldPacket& recvPacket)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_ADD_RANK");

    uint32 rankId;
    recvPacket >> rankId;

    std::string rankName = recvPacket.ReadString(recvPacket.ReadBits(7));

    if (Guild* guild = _GetPlayerGuild(this, true))
        guild->HandleAddNewRank(this, rankName); //, rankId);
}

void WorldSession::HandleGuildDelRankOpcode(WorldPacket& recvPacket)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_DEL_RANK");

    uint32 rankId;
    recvPacket >> rankId;

    if (Guild* guild = _GetPlayerGuild(this, true))
        guild->HandleRemoveRank(this, rankId);
}

void WorldSession::HandleGuildChangeInfoTextOpcode(WorldPacket& recvPacket)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_INFO_TEXT");

    uint32 length = recvPacket.ReadBits(11);
    std::string info = recvPacket.ReadString(length);

    if (Guild* guild = _GetPlayerGuild(this, true))
        guild->HandleSetInfo(this, info);
}

void WorldSession::HandleSaveGuildEmblemOpcode(WorldPacket& recvPacket)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_SAVE_GUILD_EMBLEM");

    ObjectGuid vendorGuid;
    EmblemInfo emblemInfo;

    emblemInfo.ReadPacket(recvPacket);

    //recvPacket.ReadGuidMask<2, 4, 1, 5, 7, 3, 6, 0>(vendorGuid);
    //recvPacket.ReadGuidBytes<2, 0, 3, 7, 1, 6, 4, 5>(vendorGuid);

    if (GetPlayer()->GetNPCIfCanInteractWith(vendorGuid, UNIT_NPC_FLAG_TABARDDESIGNER))
    {
        // Remove fake death
        if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
            GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

        if (Guild* guild = _GetPlayerGuild(this))
            guild->HandleSetEmblem(this, emblemInfo);
        else
            // "You are not part of a guild!";
            Guild::SendSaveEmblemResult(this, ERR_GUILDEMBLEM_NOGUILD);
    }
    else
    {
        // "That's not an emblem vendor!"
        Guild::SendSaveEmblemResult(this, ERR_GUILDEMBLEM_INVALIDVENDOR);
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleSaveGuildEmblemOpcode - Unit (GUID: %u) not found or you can't interact with him.", vendorGuid.GetCounter());
    }
}

void WorldSession::HandleGuildEventLogQueryOpcode(WorldPacket& /* recvPacket */)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received (CMSG_GUILD_EVENT_LOG_QUERY)");

    if (Guild* guild = _GetPlayerGuild(this))
        guild->SendEventLog(this);
}

void WorldSession::HandleGuildBankMoneyWithdrawn(WorldPacket & /* recvData */)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received (CMSG_GUILD_BANK_MONEY_WITHDRAWN_QUERY)");

    if (Guild* guild = _GetPlayerGuild(this))
        guild->SendMoneyInfo(this);
}

void WorldSession::HandleGuildPermissions(WorldPacket& /* recvData */)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received (CMSG_GUILD_PERMISSIONS)");

    if (Guild* guild = _GetPlayerGuild(this))
        guild->SendPermissions(this);
}

// Called when clicking on Guild bank gameobject
void WorldSession::HandleGuildBankerActivate(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received (CMSG_GUILD_BANKER_ACTIVATE)");

    ObjectGuid GoGuid;
    //recvData.ReadGuidMask<5, 2, 1, 0, 4>(GoGuid);
    bool fullSlotList = recvData.ReadBit();
    //recvData.ReadGuidMask<6, 3, 7>(GoGuid);

    //recvData.ReadGuidBytes<3, 6, 1, 7, 0, 5, 2, 4>(GoGuid);

    if (GetPlayer()->GetGameObjectIfCanInteractWith(GoGuid, GAMEOBJECT_TYPE_GUILD_BANK))
    {
        if (Guild* guild = _GetPlayerGuild(this))
            guild->SendBankList(this, 0, true, true);
        else
            Guild::SendCommandResult(this, GUILD_UNK1, ERR_GUILD_PLAYER_NOT_IN_GUILD);
    }
}

// Called when opening guild bank tab only (first one)
void WorldSession::HandleGuildBankQueryTab(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received (CMSG_GUILD_BANK_QUERY_TAB)");

    ObjectGuid GoGuid;
    uint8 tabId;

    recvData >> tabId;
    //recvData.ReadGuidMask<6>(GoGuid);
    bool fullSlotList = recvData.ReadBit(); // 0 = only slots updated in last operation are shown. 1 = all slots updated
    //recvData.ReadGuidMask<5, 2, 1, 0, 4, 7, 3>(GoGuid);

    //recvData.ReadGuidBytes<4, 6, 7, 3, 5, 0, 1, 2>(GoGuid);

    if (GetPlayer()->GetGameObjectIfCanInteractWith(GoGuid, GAMEOBJECT_TYPE_GUILD_BANK))
        if (Guild* guild = _GetPlayerGuild(this))
            guild->SendBankList(this, tabId, true, false);
}

void WorldSession::HandleGuildBankDepositMoney(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received (CMSG_GUILD_BANK_DEPOSIT_MONEY)");

    ObjectGuid goGuid;
    uint64 money;

    recvData >> money;
    //recvData.ReadGuidMask<0, 1, 4, 3, 7, 5, 2, 6>(goGuid);
    //recvData.ReadGuidBytes<1, 5, 4, 0, 3, 6, 7, 2>(goGuid);

    if (GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
        if (money && GetPlayer()->HasEnoughMoney(money))
            if (Guild* guild = _GetPlayerGuild(this))
                guild->HandleMemberDepositMoney(this, money);
}

void WorldSession::HandleGuildBankWithdrawMoney(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received (CMSG_GUILD_BANK_WITHDRAW_MONEY)");

    ObjectGuid GoGuid;
    uint64 money;

    recvData >> money;
    //recvData.ReadGuidMask<0, 6, 1, 3, 2, 5, 7, 4>(GoGuid);
    //recvData.ReadGuidBytes<1, 4, 6, 7, 0, 5, 2, 3>(GoGuid);

    if (money)
        if (GetPlayer()->GetGameObjectIfCanInteractWith(GoGuid, GAMEOBJECT_TYPE_GUILD_BANK))
            if (Guild* guild = _GetPlayerGuild(this))
                guild->HandleMemberWithdrawMoney(this, money);
}

void WorldSession::HandleGuildBankSwapItems(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received (CMSG_GUILD_BANK_SWAP_ITEMS)");

    Guild* guild = _GetPlayerGuild(this);
    if (!guild)
    {
        recvData.rfinish();                   // Prevent additional spam at rejected packet
        return;
    }

    ObjectGuid GoGuid;

    bool bankToBank, autoStore;
    uint8 tabId = 0, slotId = 0, toChar, destTabId, destSlotId, playerBag = NULL_BAG, playerSlotId = NULL_SLOT;
    uint32 itemEntry = 0, destItemEntry, splitedAmount;

    recvData >> toChar >> destTabId >> destItemEntry >> destSlotId >> splitedAmount;

    autoStore = recvData.ReadBit();
    bankToBank = recvData.ReadBit();
    //recvData.ReadGuidMask<2, 6, 0, 5, 7>(GoGuid);
    bool hasPlayerSlotId = !recvData.ReadBit();
    //recvData.ReadGuidMask<4, 1>(GoGuid);
    bool hasPlayerBag = !recvData.ReadBit();
    bool hasItemEntry = !recvData.ReadBit();
    bool hasTabId = !recvData.ReadBit();
    bool hasSlotId = !recvData.ReadBit();
    //recvData.ReadGuidMask<3>(GoGuid);
    bool hasAutoStoreCount = !recvData.ReadBit();

    //recvData.ReadGuidBytes<7, 5, 6, 1, 0, 4, 2, 3>(GoGuid);

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(GoGuid, GAMEOBJECT_TYPE_GUILD_BANK))
    {
        recvData.rfinish();                   // Prevent additional spam at rejected packet
        return;
    }

    if (hasAutoStoreCount)
        recvData.read_skip<uint32>();
    if (hasSlotId)
        recvData >> slotId;
    if (hasPlayerBag)
        recvData >> playerBag;
    if (hasItemEntry)
        recvData >> itemEntry;
    if (hasPlayerSlotId)
        recvData >> playerSlotId;
    if (hasTabId)
        recvData >> tabId;

    if (bankToBank)
        guild->SwapItems(GetPlayer(), tabId, slotId, destTabId, destSlotId, splitedAmount);
    else
    {
        tabId = destTabId;
        slotId = destSlotId;

        // Player <-> Bank
        // Allow to work with inventory only
        if (!Player::IsInventoryPos(playerBag, playerSlotId) && !(playerBag == NULL_BAG && playerSlotId == NULL_SLOT))
            GetPlayer()->SendEquipError(EQUIP_ERR_INTERNAL_BAG_ERROR, NULL);
        else
            guild->SwapItemsWithInventory(GetPlayer(), toChar != 0, tabId, slotId, playerBag, playerSlotId, splitedAmount);
    }
}

void WorldSession::HandleGuildBankBuyTab(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received (CMSG_GUILD_BANK_BUY_TAB)");

    ObjectGuid GoGuid;
    uint8 tabId;
    recvData >> tabId;
    //recvData.ReadGuidMask<7, 3, 4, 0, 2, 1, 5, 6>(GoGuid);
    //recvData.ReadGuidBytes<5, 0, 1, 4, 7, 6, 3, 2>(GoGuid);

    if (!GoGuid || GetPlayer()->GetGameObjectIfCanInteractWith(GoGuid, GAMEOBJECT_TYPE_GUILD_BANK))
        if (Guild* guild = _GetPlayerGuild(this))
            guild->HandleBuyBankTab(this, tabId);
}

void WorldSession::HandleGuildBankUpdateTab(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received (CMSG_GUILD_BANK_UPDATE_TAB)");

    ObjectGuid GoGuid;
    uint8 tabId;
    recvData >> tabId;
    uint32 nameLen = recvData.ReadBits(7);
    //recvData.ReadGuidMask<6, 5, 7>(GoGuid);
    uint32 iconLen = recvData.ReadBits(9);
    //recvData.ReadGuidMask<3, 2, 1, 4, 0>(GoGuid);

    //recvData.ReadGuidBytes<4, 0, 7, 3, 2>(GoGuid);
    std::string name = recvData.ReadString(nameLen);
    //recvData.ReadGuidBytes<1, 5>(GoGuid);
    std::string icon = recvData.ReadString(iconLen);
    //recvData.ReadGuidBytes<6>(GoGuid);

    if (!name.empty() && !icon.empty())
        if (GetPlayer()->GetGameObjectIfCanInteractWith(GoGuid, GAMEOBJECT_TYPE_GUILD_BANK))
            if (Guild* guild = _GetPlayerGuild(this))
                guild->HandleSetBankTabInfo(this, tabId, name, icon);
}

void WorldSession::HandleGuildBankLogQuery(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received (MSG_GUILD_BANK_LOG_QUERY)");

    uint32 tabId;
    recvData >> tabId;

    if (Guild* guild = _GetPlayerGuild(this))
        guild->SendBankLog(this, tabId);
}

void WorldSession::HandleQueryGuildBankTabText(WorldPacket &recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_BANK_QUERY_TEXT");

    uint32 tabId;
    recvData >> tabId;

    if (Guild* guild = _GetPlayerGuild(this))
        guild->SendBankTabText(this, tabId);
}

void WorldSession::HandleSetGuildBankTabText(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_SET_GUILD_BANK_TEXT");

    uint32 tabId;
    recvData >> tabId;

    std::string text = recvData.ReadString(recvData.ReadBits(14));

    if (Guild* guild = _GetPlayerGuild(this))
        guild->SetBankTabText(tabId, text);
}

void WorldSession::HandleGuildQueryXPOpcode(WorldPacket& recvPacket)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_QUERY_GUILD_XP");

    ObjectGuid guildGuid;
    //recvPacket.ReadGuidMask<7, 0, 6, 3, 2, 1, 4, 5>(guildGuid);
    //recvPacket.ReadGuidBytes<6, 5, 2, 0, 7, 1, 3, 4>(guildGuid);

    if (Guild* guild = sGuildMgr->GetGuildByGuid(guildGuid))
        if (guild->IsMember(_player->GetGUID()))
            guild->SendGuildXP(this);
}

void WorldSession::HandleGuildSetRankPermissionsOpcode(WorldPacket& recvPacket)
{
    Guild* guild = _GetPlayerGuild(this, true);
    if (!guild)
    {
        recvPacket.rfinish();
        return;
    }

    uint32 oldRankId;
    uint32 rankId;
    uint32 oldRights;
    uint32 newRights;
    uint32 moneyPerDay;

    recvPacket >> oldRankId;
    recvPacket >> oldRights;
    recvPacket >> moneyPerDay;
    recvPacket >> newRights;
    recvPacket >> rankId;

    GuildBankRightsAndSlotsVec rightsAndSlots(GUILD_BANK_MAX_TABS);
    for (uint8 tabId = 0; tabId < GUILD_BANK_MAX_TABS; ++tabId)
    {
        uint32 bankRights;
        uint32 slots;

        recvPacket >> bankRights;
        recvPacket >> slots;

        rightsAndSlots[tabId] = GuildBankRightsAndSlots(tabId, uint8(bankRights), slots);
    }

    std::string rankName = recvPacket.ReadString(recvPacket.ReadBits(7));

    guild->HandleSetRankInfo(this, rankId, rankName, newRights, moneyPerDay * 10000, rightsAndSlots);

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_SET_RANK_PERMISSIONS moneyPerDay %u, rankId %u, newRights %u"
    , moneyPerDay, rankId, newRights);
}

void WorldSession::HandleGuildRequestPartyState(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GUILD_REQUEST_PARTY_STATE");

    ObjectGuid guildGuid;
    //recvData.ReadGuidMask<0, 6, 5, 3, 4, 7, 2, 1>(guildGuid);
    //recvData.ReadGuidBytes<1, 2, 3, 5, 4, 0, 6, 7>(guildGuid);

    if (Guild* guild = sGuildMgr->GetGuildByGuid(guildGuid))
        guild->HandleGuildPartyRequest(this);
}

void WorldSession::HandleGuildRequestMaxDailyXP(WorldPacket& recvPacket)
{
    ObjectGuid guildGuid;

    uint8 bitOrder[8] = {2, 5, 3, 7, 4, 1, 0, 6};
    //recvPacket.ReadBitInOrder(guildGuid, bitOrder);

    uint8 byteOrder[8] = {7, 3, 2, 1, 0, 5, 6, 4};
    //recvPacket.ReadBytesSeq(guildGuid, byteOrder);

    if (Guild* guild = sGuildMgr->GetGuildByGuid(guildGuid))
    {
        if (guild->IsMember(_player->GetGUID()))
        {
            WorldPacket data(SMSG_GUILD_MAX_DAILY_XP, 8);
            data << uint64(sWorld->getIntConfig(CONFIG_GUILD_DAILY_XP_CAP));
            SendPacket(&data);
        }
    }
}

void WorldSession::HandleAutoDeclineGuildInvites(WorldPacket& recvPacket)
{
    bool enable = recvPacket.ReadBit();

    GetPlayer()->ApplyModFlag(PLAYER_FIELD_PLAYER_FLAGS, PLAYER_FLAGS_AUTO_DECLINE_GUILD, enable);
}

void WorldSession::HandleGuildRewardsQueryOpcode(WorldPacket& recvPacket)
{
    recvPacket.read_skip<uint32>();         // counter?

    if (Guild* guild = sGuildMgr->GetGuildById(_player->GetGuildId()))
    {
        std::vector<GuildReward> const& rewards = sGuildMgr->GetGuildRewards();

        WorldPacket data(SMSG_GUILD_REWARDS_LIST, (3 + rewards.size() * (4 + 4 + 4 + 8 + 4 + 4)));
        data.WriteBits(rewards.size(), 19);
        for (uint32 i = 0; i < rewards.size(); ++i)
            data.WriteBits(0, 22);          // conditions count?

        for (uint32 i = 0; i < rewards.size(); ++i)
        {
            data << uint32(rewards[i].Entry);
            data << uint64(rewards[i].Price);
            data << int32(rewards[i].Racemask);
            data << uint32(rewards[i].AchievementId);
            data << uint32(rewards[i].Standing);
        }
        data << uint32(time(NULL));         // counter?
        SendPacket(&data);
    }
}

void WorldSession::HandleGuildQueryNewsOpcode(WorldPacket& recvPacket)
{
    recvPacket.rfinish();   // guild guid

    if (Guild* guild = sGuildMgr->GetGuildById(_player->GetGuildId()))
    {
        WorldPacket data;
        guild->GetNewsLog().BuildNewsData(data);
        SendPacket(&data);
    }
}

void WorldSession::HandleGuildNewsUpdateStickyOpcode(WorldPacket& recvPacket)
{
    uint32 newsId;
    ObjectGuid guid;

    recvPacket >> newsId;
    //recvPacket.ReadGuidMask<6, 7>(guid);
    bool sticky = recvPacket.ReadBit();
    //recvPacket.ReadGuidMask<1, 5, 2, 4, 3, 0>(guid);
    //recvPacket.ReadGuidBytes<7, 6, 0, 1, 4, 3, 2, 5>(guid);

    if (Guild* guild = sGuildMgr->GetGuildById(_player->GetGuildId()))
    {
        if (GuildNewsEntry* newsEntry = guild->GetNewsLog().GetNewsById(newsId))
        {
            if (sticky)
                newsEntry->Flags |= 1;
            else
                newsEntry->Flags &= ~1;
            WorldPacket data;
            guild->GetNewsLog().BuildNewsData(newsId, *newsEntry, data);
            SendPacket(&data);
        }
    }
}

void WorldSession::HandleGuildQueryGuildRecipesOpcode(WorldPacket& recvPacket)
{
    ObjectGuid guildGuid;
    //recvPacket.ReadGuidMask<6, 7, 1, 2, 5, 0, 3, 4>(guildGuid);
    //recvPacket.ReadGuidBytes<1, 6, 5, 4, 0, 7, 3, 2>(guildGuid);

    Guild* guild = _player->GetGuild();
    if (!guild)
        return;

    Guild::KnownRecipesMap const& recipesMap = guild->GetGuildRecipes();

    WorldPacket* data = new WorldPacket(SMSG_GUILD_RECIPES, 2 + recipesMap.size() * (300 + 4));
    uint32 bitpos = data->bitwpos();
    uint32 count = 0;
    data->WriteBits(count, 15);

    for (Guild::KnownRecipesMap::const_iterator itr = recipesMap.begin(); itr != recipesMap.end(); ++itr)
    {
        if (itr->second.IsEmpty())
            continue;

        *data << uint32(itr->first);
        data->append(itr->second.recipesMask, KNOW_RECIPES_MASK_SIZE);
        ++count;
    }

    data->FlushBits();
    data->PutBits(bitpos, count, 15);

    _player->ScheduleMessageSend(data, 500);
}

void WorldSession::HandleGuildQueryGuildMembersForRecipe(WorldPacket& recvPacket)
{
    uint32 skillId, spellId;
    ObjectGuid guildGuid;

    recvPacket.read_skip<uint32>(); // unk
    recvPacket >> skillId;
    recvPacket >> spellId;

    //recvPacket.ReadGuidMask<1, 4, 3, 7, 2, 0, 5, 6>(guildGuid);
    //recvPacket.ReadGuidBytes<4, 1, 6, 2, 3, 0, 5, 7>(guildGuid);

    if (Guild* guild = _player->GetGuild())
        guild->SendGuildMembersForRecipeResponse(this, skillId, spellId);
}

void WorldSession::HandleGuildQueryGuildMembersRecipes(WorldPacket& recvPacket)
{
    ObjectGuid playerGuid, guildGuid;
    uint32 skillId;

    recvPacket >> skillId;
    //recvPacket.ReadGuidMask<0, 4>(guildGuid);
    //recvPacket.ReadGuidMask<4, 0>(playerGuid);
    //recvPacket.ReadGuidMask<6, 5, 1>(guildGuid);
    //recvPacket.ReadGuidMask<5, 1, 3, 2>(playerGuid);
    //recvPacket.ReadGuidMask<2, 7, 3>(guildGuid);
    //recvPacket.ReadGuidMask<7, 6>(playerGuid);

    //recvPacket.ReadGuidBytes<2>(guildGuid);
    //recvPacket.ReadGuidBytes<0>(playerGuid);
    //recvPacket.ReadGuidBytes<1>(guildGuid);
    //recvPacket.ReadGuidBytes<7, 5>(playerGuid);
    //recvPacket.ReadGuidBytes<4, 5, 7, 3>(guildGuid);
    //recvPacket.ReadGuidBytes<4>(playerGuid);
    //recvPacket.ReadGuidBytes<6>(guildGuid);
    //recvPacket.ReadGuidBytes<3>(playerGuid);
    //recvPacket.ReadGuidBytes<0>(guildGuid);
    //recvPacket.ReadGuidBytes<2, 6, 1>(playerGuid);

    Guild* guild = _player->GetGuild();
    if (!guild || !guild->IsMember(playerGuid))
        return;

    guild->SendGuildMemberRecipesResponse(this, playerGuid, skillId);
}
