/*
 * Copyright (C) 2008-2010 TrinityCore <http://www.trinitycore.org/>
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

#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Map.h"
#include "MapManager.h"
#include "Group.h"
#include "WorldPacket.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "CreatureTextMgr.h"
#include "GroupMgr.h"
#include "Chat.h"
#include "Language.h"

Battlefield::Battlefield()
{
    m_Timer = 0;
    m_IsEnabled = true;
    m_isActive = false;
    m_DefenderTeam = TEAM_NEUTRAL;

    m_TypeId = 0;
    m_BattleId = 0;
    m_ZoneId = 0;
    m_MapId = 0;
    m_MaxPlayer = 0;
    m_MinPlayer = 0;
    m_BattleTime = 0;
    m_NoWarBattleTime = 0;
    m_TimeForAcceptInvite = 20;
    m_uiKickDontAcceptTimer = 1000;

    m_uiKickAfkPlayersTimer = 1000;

    m_LastResurectTimer = 30 * IN_MILLISECONDS;
    m_StartGroupingTimer = 0;
    m_StartGrouping = false;
    StalkerGuid.Clear();
}

Battlefield::~Battlefield()
{
    for (BfCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        delete itr->second;

    for (GraveyardVect::const_iterator itr = m_GraveyardList.begin(); itr != m_GraveyardList.end(); ++itr)
        delete *itr;

    m_capturePoints.clear();
}

// Called when a player enters the zone
void Battlefield::HandlePlayerEnterZone(Player* player, uint32 /*zone*/)
{
    if (!player || player->GetTeamId() >= 2)
        return;

    // If battle is started,
    // If not full of players > invite player to join the war
    // If full of players > announce to player that BF is full and kick him after a few second if he desn't leave
    if (IsWarTime())
    {
        if (m_PlayersInWar[player->GetTeamId()].size() + m_InvitedPlayers[player->GetTeamId()].size() < m_MaxPlayer) // Vacant spaces
            InvitePlayerToWar(player);
        else // No more vacant places
        {
            // TODO: Send a packet to announce it to player
            //ChatHandler(player->GetSession()).PSendSysMessage(LANG_BG_WG_LEAVE_MAX_PLR);
            m_PlayersWillBeKick[player->GetTeamId()][player->GetGUID()] = time(NULL) + 10;
            InvitePlayerToQueue(player);
        }
    }
    else
    {
        // If time left is < 15 minutes invite player to join queue
        if (m_Timer <= m_StartGroupingTimer)
            InvitePlayerToQueue(player);
    }

    // Add player in the list of player in zone
    m_players[player->GetTeamId()].insert(player->GetGUID());
    OnPlayerEnterZone(player);
}

// Called when a player leave the zone
void Battlefield::HandlePlayerLeaveZone(Player* player, uint32 /*zone*/)
{
    if (IsWarTime())
    {
        // If the player is participating to the battle
        if (player->GetTeamId() < 2)
        {
            if (m_PlayersInWar[player->GetTeamId()].find(player->GetGUID()) != m_PlayersInWar[player->GetTeamId()].end())
            {
                m_PlayersInWar[player->GetTeamId()].erase(player->GetGUID());
                player->GetSession()->SendBfLeaveMessage(m_Guid);
                if (Group* group = player->GetGroup()) // Remove the player from the raid group
                    group->RemoveMember(player->GetGUID());

                OnPlayerLeaveWar(player);
            }
        }
    }

    for (BfCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        itr->second->HandlePlayerLeave(player);

    if (player->GetTeamId() < 2)
    {
        m_InvitedPlayers[player->GetTeamId()].erase(player->GetGUID());
        m_PlayersWillBeKick[player->GetTeamId()].erase(player->GetGUID());
        m_players[player->GetTeamId()].erase(player->GetGUID());
    }
    SendRemoveWorldStates(player);
    RemovePlayerFromResurrectQueue(player->GetGUID());
    OnPlayerLeaveZone(player);
}

bool Battlefield::Update(uint32 diff)
{
    if (m_Timer <= diff)
    {
        // Battlefield ends on time
        if (IsWarTime())
            EndBattle(true);
        else // Time to start a new battle!
            StartBattle();
    }
    else
        m_Timer -= diff;

    // Invite players a few minutes before the battle's beginning
    if (!IsWarTime() && !m_StartGrouping && m_Timer <= m_StartGroupingTimer)
    {
        m_StartGrouping = true;
        InvitePlayersInZoneToQueue();
        OnStartGrouping();
    }

    bool objective_changed = false;
    if (IsWarTime())
    {
        if (m_uiKickAfkPlayersTimer <= diff)
        {
            m_uiKickAfkPlayersTimer = 1000;
            KickAfkPlayers();
        }
        else
            m_uiKickAfkPlayersTimer -= diff;

        // Kick players who chose not to accept invitation to the battle
        if (m_uiKickDontAcceptTimer <= diff)
        {
            for (int team = 0; team < 2; team++)
                for (PlayerTimerMap::iterator itr = m_InvitedPlayers[team].begin(), next; itr != m_InvitedPlayers[team].end(); itr = next)
                {
                    next = itr;
                    ++next;

                    if ((*itr).second <= time(NULL))
                        KickPlayerFromBattlefield((*itr).first);
                }

            InvitePlayersInZoneToWar();
            for (int team = 0; team < 2; team++)
                for (PlayerTimerMap::iterator itr = m_PlayersWillBeKick[team].begin(), next; itr != m_PlayersWillBeKick[team].end(); itr = next)
                {
                    next = itr;
                    ++next;
                    if ((*itr).second <= time(NULL))
                        KickPlayerFromBattlefield((*itr).first);
                }

            m_uiKickDontAcceptTimer = 1000;
        }
        else
            m_uiKickDontAcceptTimer -= diff;

        for (BfCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
            if (itr->second->Update(diff))
                objective_changed = true;
    }


    if (m_LastResurectTimer <= diff)
    {
        for (GraveyardVect::iterator itr = m_GraveyardList.begin(); itr != m_GraveyardList.end(); ++itr)
            if (*itr)
               (*itr)->Resurrect();
        m_LastResurectTimer = RESURRECTION_INTERVAL;
    }
    else
        m_LastResurectTimer -= diff;

    return objective_changed;
}

void Battlefield::InvitePlayersInZoneToQueue()
{
    for (uint8 team = 0; team < 2; ++team)
        for (GuidSet::const_iterator itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                InvitePlayerToQueue(player);
}

void Battlefield::InvitePlayerToQueue(Player* player)
{
    if (player->GetTeamId() >= 2)
        return;

    if (m_PlayersInQueue[player->GetTeamId()].count(player->GetGUID()))
        return;

    if (m_PlayersInQueue[player->GetTeamId()].size() <= m_MinPlayer || m_PlayersInQueue[GetOtherTeam(player->GetTeamId())].size() >= m_MinPlayer)
        player->GetSession()->SendBfInvitePlayerToQueue(m_Guid);
}

void Battlefield::InvitePlayersInQueueToWar()
{
    for (uint8 team = 0; team < BG_TEAMS_COUNT; ++team)
    {
        for (GuidSet::const_iterator itr = m_PlayersInQueue[team].begin(); itr != m_PlayersInQueue[team].end(); ++itr)
        {
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
            {
                if (player->GetTeamId() < 2)
                {
                    if (m_PlayersInWar[player->GetTeamId()].size() + m_InvitedPlayers[player->GetTeamId()].size() < m_MaxPlayer)
                        InvitePlayerToWar(player);
                }
            }
        }
        m_PlayersInQueue[team].clear();
    }
}

void Battlefield::InvitePlayersInZoneToWar()
{
    for (uint8 team = 0; team < BG_TEAMS_COUNT; ++team)
        for (GuidSet::const_iterator itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
        {
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
            {
                if (player->GetTeamId() >= 2)
                    continue;
                if (m_PlayersInWar[player->GetTeamId()].count(player->GetGUID()) || m_InvitedPlayers[player->GetTeamId()].count(player->GetGUID()))
                    continue;
                if (m_PlayersInWar[player->GetTeamId()].size() + m_InvitedPlayers[player->GetTeamId()].size() < m_MaxPlayer)
                    InvitePlayerToWar(player);
                else // Battlefield is full of players
                    m_PlayersWillBeKick[player->GetTeamId()][player->GetGUID()] = time(NULL) + 10;
            }
        }
}

void Battlefield::InvitePlayerToWar(Player* player)
{
    if (!player)
        return;
    
    if (player->GetTeamId() >= 2)
        return;

    // TODO : needed ?
    if (player->isInFlight())
        return;

    if (player->InArena() || player->GetBattleground())
    {
        m_PlayersInQueue[player->GetTeamId()].erase(player->GetGUID());
        return;
    }

    // If the player does not match minimal level requirements for the battlefield, kick him
    if (player->getLevel() < m_MinLevel)
    {
        if (m_PlayersWillBeKick[player->GetTeamId()].count(player->GetGUID()) == 0)
            m_PlayersWillBeKick[player->GetTeamId()][player->GetGUID()] = time(NULL) + 10;
        return;
    }

    // Check if player is not already in war
    if (m_PlayersInWar[player->GetTeamId()].count(player->GetGUID()) || m_InvitedPlayers[player->GetTeamId()].count(player->GetGUID()))
        return;

    m_PlayersWillBeKick[player->GetTeamId()].erase(player->GetGUID());
    m_InvitedPlayers[player->GetTeamId()][player->GetGUID()] = time(NULL) + m_TimeForAcceptInvite;
    player->GetSession()->SendBfInvitePlayerToWar(m_Guid, m_ZoneId, m_TimeForAcceptInvite);
}

void Battlefield::InitStalker(uint32 entry, float x, float y, float z, float o)
{
    if (Creature* creature = SpawnCreature(entry, x, y, z, o, TEAM_NEUTRAL))
        StalkerGuid = creature->GetGUID();
    else
        sLog->outError(LOG_FILTER_BATTLEFIELD, "Battlefield::InitStalker: could not spawn Stalker (Creature entry %u), zone messeges will be un-available", entry);
}

void Battlefield::KickAfkPlayers()
{
    for (uint8 team = 0; team < BG_TEAMS_COUNT; ++team)
        for (GuidSet::const_iterator itr = m_PlayersInWar[team].begin(), next; itr != m_PlayersInWar[team].end(); itr = next)
        {
            next = itr;
            ++next;
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                if (player->isAFK())
                    KickPlayerFromBattlefield(*itr);
        }
}

void Battlefield::KickPlayerFromBattlefield(ObjectGuid guid)
{
    if (Player* player = ObjectAccessor::FindPlayer(guid))
    {
        if (player->GetZoneId() == GetZoneId())
        {
            player->TeleportToHomeBind();
            //player->DelayTeleportToGomeBind();
            //player->TeleportTo(KickPosition);
        }

        if (player->GetTeamId() >= 2)
            return;

        m_InvitedPlayers[player->GetTeamId()].erase(player->GetGUID());
        m_PlayersInWar[player->GetTeamId()].erase(player->GetGUID());

    }
}

void Battlefield::StartBattle()
{
    if (m_isActive)
        return;

    for (int team = 0; team < BG_TEAMS_COUNT; team++)
    {
        m_PlayersInWar[team].clear();
        m_Groups[team].clear();
    }

    m_Timer = m_BattleTime;
    m_isActive = true;

    InvitePlayersInZoneToWar();
    InvitePlayersInQueueToWar();

    DoPlaySoundToAll(BF_START);

    OnBattleStart();
}

void Battlefield::EndBattle(bool endByTimer)
{
    if (!m_isActive)
        return;

    m_isActive = false;

    m_StartGrouping = false;

    if (!endByTimer)
        SetDefenderTeam(GetAttackerTeam());

    if (GetDefenderTeam() == TEAM_ALLIANCE)
        DoPlaySoundToAll(BF_ALLIANCE_WINS);
    else
        DoPlaySoundToAll(BF_HORDE_WINS);

    OnBattleEnd(endByTimer);

    // Reset battlefield timer
    m_Timer = m_NoWarBattleTime;
    SendInitWorldStatesToAll();
}

//! 5.4.1
void Battlefield::DoPlaySoundToAll(uint32 SoundID)
{
    ObjectGuid guid = ObjectGuid::Empty;
    WorldPacket data(SMSG_PLAY_SOUND, 12);
    /*data.WriteGuidMask<0, 2, 4, 7, 6, 5, 1, 3>(guid);
    //data.WriteGuidBytes<3, 4, 2, 6, 1, 5, 0>(guid);
    data << uint32(SoundID);
    //data.WriteGuidBytes<7>(guid);*/

    for (int team = 0; team < BG_TEAMS_COUNT; team++)
        for (GuidSet::const_iterator itr = m_PlayersInWar[team].begin(); itr != m_PlayersInWar[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->GetSession()->SendPacket(&data);
}

bool Battlefield::HasPlayer(Player* player) const
{
    if (player->GetTeamId() >= 2)
        return false;

    return m_players[player->GetTeamId()].find(player->GetGUID()) != m_players[player->GetTeamId()].end();
}

// Called in WorldSession::HandleBfQueueInviteResponse
void Battlefield::PlayerAcceptInviteToQueue(Player* player)
{
    // Add player in queue
    if (player->GetTeamId() < 2)
        m_PlayersInQueue[player->GetTeamId()].insert(player->GetGUID());
    // Send notification
    player->GetSession()->SendBfQueueInviteResponse(m_Guid, m_ZoneId);
}

// Called in WorldSession::HandleBfExitQueueRequest
void Battlefield::AskToLeaveQueue(Player* player)
{
    // Remove player from queue
    if (player->GetTeamId() < 2)
        m_PlayersInQueue[player->GetTeamId()].erase(player->GetGUID());
}

// Called in WorldSession::HandleBfEntryInviteResponse
void Battlefield::PlayerAcceptInviteToWar(Player* player)
{
    if (!IsWarTime())
        return;

    if (AddOrSetPlayerToCorrectBfGroup(player))
    {
        player->GetSession()->SendBfEntered(m_Guid);
        
        if (player->GetTeamId() < 2)
        {
            m_PlayersInWar[player->GetTeamId()].insert(player->GetGUID());
            m_InvitedPlayers[player->GetTeamId()].erase(player->GetGUID());
        }

        if (player->isAFK())
            player->ToggleAFK();

        player->SetPvP(true);
        OnPlayerJoinWar(player);                               //for scripting
    }
}

void Battlefield::TeamCastSpell(TeamId team, int32 spellId)
{
    if (spellId > 0)
        for (GuidSet::const_iterator itr = m_PlayersInWar[team].begin(); itr != m_PlayersInWar[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->CastSpell(player, uint32(spellId), true);
    else
        for (GuidSet::const_iterator itr = m_PlayersInWar[team].begin(); itr != m_PlayersInWar[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->RemoveAuraFromStack(uint32(-spellId));
}

void Battlefield::BroadcastPacketToZone(WorldPacket& data) const
{
    for (uint8 team = 0; team < BG_TEAMS_COUNT; ++team)
        for (GuidSet::const_iterator itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->GetSession()->SendPacket(&data);
}

void Battlefield::BroadcastPacketToQueue(WorldPacket& data) const
{
    for (uint8 team = 0; team < BG_TEAMS_COUNT; ++team)
        for (GuidSet::const_iterator itr = m_PlayersInQueue[team].begin(); itr != m_PlayersInQueue[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->GetSession()->SendPacket(&data);
}

void Battlefield::BroadcastPacketToWar(WorldPacket& data) const
{
    for (uint8 team = 0; team < BG_TEAMS_COUNT; ++team)
        for (GuidSet::const_iterator itr = m_PlayersInWar[team].begin(); itr != m_PlayersInWar[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->GetSession()->SendPacket(&data);
}

const WorldPacket * Battlefield::BuildWarningAnnPacket(std::string msg)
{
    Trinity::ChatData c;
    c.message = msg;
    c.chatType = CHAT_MSG_RAID_BOSS_EMOTE;

    return Trinity::BuildChatPacket(c);
}

void Battlefield::SendWarningToAllInZone(uint32 entry)
{
    if (Unit* unit = ObjectAccessor::FindUnit(StalkerGuid))
        if (Creature* stalker = unit->ToCreature())
            // FIXME: replaced CHAT_TYPE_END with CHAT_MSG_BG_SYSTEM_NEUTRAL to fix compile, it's a guessed change :/
            sCreatureTextMgr->SendChat(stalker, (uint8)entry, ObjectGuid::Empty, CHAT_MSG_BG_SYSTEM_NEUTRAL, LANG_ADDON, TEXT_RANGE_ZONE);
}

/*void Battlefield::SendWarningToAllInWar(int32 entry,...)
{
    const char *format = sObjectMgr->GetTrinityStringForDBCLocale(entry);
    va_list ap;
    char str [1024];
    va_start(ap, entry);
    vsnprintf(str,1024,format, ap);
    va_end(ap);
    std::string msg = (std::string)str;

    WorldPacket data = BuildWarningAnnPacket(msg);
    BroadcastPacketWar(data);
}*/

void Battlefield::SendWarningToPlayer(Player* player, uint32 entry)
{
    if (player)
        if (Unit* unit = ObjectAccessor::FindUnit(StalkerGuid))
            if (Creature* stalker = unit->ToCreature())
                sCreatureTextMgr->SendChat(stalker, (uint8)entry, player->GetGUID());
}

void Battlefield::SendUpdateWorldState(uint32 field, uint32 value)
{
    for (uint8 i = 0; i < BG_TEAMS_COUNT; ++i)
        for (GuidSet::iterator itr = m_players[i].begin(); itr != m_players[i].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->SendUpdateWorldState(field, value);
}

void Battlefield::RegisterZone(uint32 zoneId)
{
    sBattlefieldMgr->AddZone(zoneId, this);
}

void Battlefield::HideNpc(Creature* creature)
{
    creature->CombatStop();
    creature->SetReactState(REACT_PASSIVE);
    creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
    creature->SetPhaseMask(2, true);
    creature->DisappearAndDie();
    creature->SetVisible(false);
}

void Battlefield::ShowNpc(Creature* creature, bool aggressive)
{
    creature->SetPhaseMask(305, true);
    creature->SetVisible(true);
    creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
    if (!creature->isAlive())
        creature->Respawn(true);
    if (aggressive)
        creature->SetReactState(REACT_AGGRESSIVE);
    else
    {
        creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
        creature->SetReactState(REACT_PASSIVE);
    }
}

// ****************************************************
// ******************* Group System *******************
// ****************************************************
Group* Battlefield::GetFreeBfRaid(TeamId TeamId)
{
    for (GuidSet::const_iterator itr = m_Groups[TeamId].begin(); itr != m_Groups[TeamId].end(); ++itr)
        if (Group* group = sGroupMgr->GetGroupByGUID(*itr))
            if (!group->IsFull())
                return group;

    return NULL;
}

Group* Battlefield::GetGroupPlayer(ObjectGuid guid, TeamId TeamId)
{
    for (GuidSet::const_iterator itr = m_Groups[TeamId].begin(); itr != m_Groups[TeamId].end(); ++itr)
        if (Group* group = sGroupMgr->GetGroupByGUID(*itr))
            if (group->IsMember(guid))
                return group;

    return NULL;
}

bool Battlefield::AddOrSetPlayerToCorrectBfGroup(Player* player)
{
    if (!player->IsInWorld())
        return false;

    if (Group* group = player->GetGroup())
        group->RemoveMember(player->GetGUID());

    Group* group = GetFreeBfRaid(player->GetTeamId());
    if (!group)
    {
        group = new Group;
        group->SetBattlefieldGroup(this);
        group->Create(player);
        sGroupMgr->AddGroup(group);

        if (player->GetTeamId() < 2)
            m_Groups[player->GetTeamId()].insert(group->GetGUID());
    }
    else if (group->IsMember(player->GetGUID()))
    {
        uint8 subgroup = group->GetMemberGroup(player->GetGUID());
        player->SetBattlegroundOrBattlefieldRaid(group, subgroup);
    }
    else
        group->AddMember(player);

    return true;
}

void Battlefield::OnPlayerLeaveWar(Player* player)
{
    if (Group *group = GetGroupPlayer(player->GetGUID(), player->GetTeamId()))
    {
        ObjectGuid gGUID = group->GetGUID();
        if (!group->RemoveMember(player->GetGUID()))                // group was disbanded
            m_Groups[player->GetTeamId()].erase(gGUID);
    }
}

//***************End of Group System*******************

//*****************************************************
//***************Spirit Guide System*******************
//*****************************************************

//--------------------
//-Battlefield Method-
//--------------------
BfGraveyard* Battlefield::GetGraveyardById(uint32 id)
{
    for (GraveyardVect::iterator itr = m_GraveyardList.begin(); itr != m_GraveyardList.end(); ++itr)
        if ((*itr) && (*itr)->GetTypeId() == id)
            return *itr;
    
    sLog->outError(LOG_FILTER_BATTLEFIELD, "Battlefield::GetGraveyardById Id:%u cant be found", id);
    return NULL;
}

WorldSafeLocsEntry const * Battlefield::GetClosestGraveYard(Player* player)
{
    BfGraveyard* closestGY = NULL;
    float maxdist = -1;
    for (GraveyardVect::iterator itr = m_GraveyardList.begin(); itr != m_GraveyardList.end(); ++itr)
    {
        if (*itr)
        {
            if ((*itr)->GetControlTeamId() != player->GetTeamId())
                continue;

            float dist = (*itr)->GetDistance(player);
            if (dist < maxdist || maxdist < 0)
            {
                closestGY = *itr;
                maxdist = dist;
            }
        }
    }

    if (closestGY)
        return sWorldSafeLocsStore.LookupEntry(closestGY->GetGraveyardId());

    return NULL;
}

void Battlefield::AddPlayerToResurrectQueue(ObjectGuid npcGuid, ObjectGuid playerGuid)
{
    for (GraveyardVect::iterator itr = m_GraveyardList.begin(); itr != m_GraveyardList.end(); ++itr)
    {
        if (!(*itr))
            continue;

        if (!npcGuid || (*itr)->HasNpc(npcGuid))
        {
            (*itr)->AddPlayer(playerGuid);
            break;
        }
    }
}

void Battlefield::RemovePlayerFromResurrectQueue(ObjectGuid playerGuid)
{
    for (GraveyardVect::iterator itr = m_GraveyardList.begin(); itr != m_GraveyardList.end(); ++itr)
    {
        if (!(*itr))
            continue;

        if ((*itr)->HasPlayer(playerGuid))
        {
            (*itr)->RemovePlayer(playerGuid);
            break;
        }
    }
}

void Battlefield::SendAreaSpiritHealerQueryOpcode(Player* player, const ObjectGuid &guid)
{
    WorldPacket data(SMSG_AREA_SPIRIT_HEALER_TIME, 12);
    uint32 time = m_LastResurectTimer;  // resurrect every 30 seconds

    /*data.WriteGuidMask<6, 5, 4, 2, 7, 0, 3, 1>(guid);
    //data.WriteGuidBytes<4, 5, 7, 3, 1>(guid);
    data << uint32(time);
    //data.WriteGuidBytes<0, 2, 6>(guid);*/

    ASSERT(player && player->GetSession());
    player->GetSession()->SendPacket(&data);
}

// ----------------------
// - BfGraveyard Method -
// ----------------------
BfGraveyard::BfGraveyard(Battlefield* battlefield)
{
    m_Bf = battlefield;
    m_GraveyardId = 0;
    m_ControlTeam = TEAM_NEUTRAL;
    m_SpiritGuide[0].Clear();
    m_SpiritGuide[1].Clear();
    m_ResurrectQueue.clear();
}

void BfGraveyard::Initialize(TeamId startControl, uint32 graveyardId, uint32 tp)
{
    m_ControlTeam = startControl;
    m_GraveyardId = graveyardId;
    m_TypeId      = tp;
}

void BfGraveyard::SetSpirit(Creature* spirit, TeamId team)
{
    if (!spirit)
    {
        sLog->outError(LOG_FILTER_BATTLEFIELD, "BfGraveyard::SetSpirit: Invalid Spirit.");
        return;
    }

    m_SpiritGuide[team] = spirit->GetGUID();
    spirit->SetReactState(REACT_PASSIVE);
}

float BfGraveyard::GetDistance(Player* player)
{
    const WorldSafeLocsEntry* safeLoc = sWorldSafeLocsStore.LookupEntry(m_GraveyardId);
    return player->GetDistance2d(safeLoc->Loc.X, safeLoc->Loc.Y);
}

void BfGraveyard::AddPlayer(ObjectGuid playerGuid)
{
    if (!m_ResurrectQueue.count(playerGuid))
    {
        m_ResurrectQueue.insert(playerGuid);

        if (Player* player = ObjectAccessor::FindPlayer(playerGuid))
            player->CastSpell(player, SPELL_WAITING_FOR_RESURRECT, true);
    }
}

void BfGraveyard::RemovePlayer(ObjectGuid playerGuid)
{
    m_ResurrectQueue.erase(m_ResurrectQueue.find(playerGuid));

    if (Player* player = ObjectAccessor::FindPlayer(playerGuid))
        player->RemoveAurasDueToSpell(SPELL_WAITING_FOR_RESURRECT);
}

void BfGraveyard::Resurrect()
{
    if (m_ResurrectQueue.empty())
        return;

    for (GuidSet::const_iterator itr = m_ResurrectQueue.begin(); itr != m_ResurrectQueue.end();)
    {
        // Get player object from his guid
        Player* player = ObjectAccessor::FindPlayer(*itr++);
        if (!player)
            continue;

        // Check  if the player is in world and on the good graveyard
        if (player->IsInWorld())
            if (Unit* spirit = ObjectAccessor::FindUnit(m_SpiritGuide[m_ControlTeam]))
                spirit->CastSpell(spirit, SPELL_SPIRIT_HEAL, true);

        // Resurect player
        player->CastSpell(player, SPELL_RESURRECTION_VISUAL, true);
        player->ResurrectPlayer(1.0f);
        player->CastSpell(player, 6962, true);
        player->CastSpell(player, SPELL_SPIRIT_HEAL_MANA, true);
        player->RemoveAurasDueToSpell(SPELL_WAITING_FOR_RESURRECT);

        sObjectAccessor->ConvertCorpseForPlayer(player->GetGUID());
    }

    m_ResurrectQueue.clear();
}

// For changing graveyard control
void BfGraveyard::GiveControlTo(TeamId team)
{
    // Guide switching
    // Note: Visiblity changes are made by phasing
    /*if (m_SpiritGuide[1 - team])
        m_SpiritGuide[1 - team]->SetVisible(false);
    if (m_SpiritGuide[team])
        m_SpiritGuide[team]->SetVisible(true);*/

    m_ControlTeam = team;
    // Teleport to other graveyard, player witch were on this graveyard
    RelocateDeadPlayers();
}

void BfGraveyard::RelocateDeadPlayers()
{
    WorldSafeLocsEntry const* closestGrave = NULL;
    for (GuidSet::const_iterator itr = m_ResurrectQueue.begin(); itr != m_ResurrectQueue.end(); ++itr)
    {
        Player* player = ObjectAccessor::FindPlayer(*itr);
        if (!player)
            continue;

        if (closestGrave)
            player->TeleportTo(player->GetMapId(), closestGrave->Loc.X, closestGrave->Loc.Y, closestGrave->Loc.Z, player->GetOrientation());
        else
        {
            closestGrave = m_Bf->GetClosestGraveYard(player);
            if (closestGrave)
                player->TeleportTo(player->GetMapId(), closestGrave->Loc.X, closestGrave->Loc.Y, closestGrave->Loc.Z, player->GetOrientation());
        }
    }
}

// *******************************************************
// *************** End Spirit Guide system ***************
// *******************************************************
// ********************** Misc ***************************
// *******************************************************

Creature* Battlefield::SpawnCreature(uint32 entry, Position pos, TeamId team)
{
    return SpawnCreature(entry, pos.m_positionX, pos.m_positionY, pos.m_positionZ, pos.m_orientation, team);
}

Creature* Battlefield::SpawnCreature(uint32 entry, float x, float y, float z, float o, TeamId team)
{
    //Get map object
    Map* map = const_cast < Map * >(sMapMgr->CreateBaseMap(m_MapId));
    if (!map)
    {
        sLog->outError(LOG_FILTER_BATTLEFIELD, "Battlefield::SpawnCreature: Can't create creature entry: %u map not found", entry);
        return 0;
    }

    Creature* creature = new Creature;
    if (!creature->Create(sObjectMgr->GetGenerator<HighGuid::Creature>()->Generate(), map, PHASEMASK_NORMAL, entry, 0, team, x, y, z, o))
    {
        sLog->outError(LOG_FILTER_BATTLEFIELD, "Battlefield::SpawnCreature: Can't create creature entry: %u", entry);
        delete creature;
        return NULL;
    }

    creature->SetHomePosition(x, y, z, o);

    CreatureTemplate const* cinfo = sObjectMgr->GetCreatureTemplate(entry);
    if (!cinfo)
    {
        sLog->outError(LOG_FILTER_BATTLEFIELD, "Battlefield::SpawnCreature: entry %u does not exist.", entry);
        return NULL;
    }
    // force using DB speeds -- do we really need this?
    creature->SetSpeed(MOVE_WALK,   cinfo->speed_walk);
    creature->SetSpeed(MOVE_RUN,    cinfo->speed_run);
    creature->SetSpeed(MOVE_FLIGHT, cinfo->speed_fly);

    // Set creature in world
    map->AddToMap(creature);
    creature->setActive(true);

    return creature;
}

// Method for spawning gameobject on map
GameObject* Battlefield::SpawnGameObject(uint32 entry, float x, float y, float z, float o)
{
    // Get map object
    Map* map = const_cast<Map*>(sMapMgr->CreateBaseMap(m_MapId)); // *vomits*
    if (!map)
        return 0;

    // Create gameobject
    GameObject* go = new GameObject;
    if (!go->Create(sObjectMgr->GetGenerator<HighGuid::GameObject>()->Generate(), entry, map, PHASEMASK_NORMAL, x, y, z, o, 0, 0, 0, 0, 100, GO_STATE_READY))
    {
        sLog->outError(LOG_FILTER_BATTLEFIELD, "Battlefield::SpawnGameObject: Gameobject template %u not found in database! Battlefield not created!", entry);
        sLog->outError(LOG_FILTER_BATTLEFIELD, "Battlefield::SpawnGameObject: Cannot create gameobject template %u! Battlefield not created!", entry);
        delete go;
        return NULL;
    }

    // Add to world
    go->SetPhaseMask(49, false);
    map->AddToMap(go);
    go->setActive(true);

    return go;
}

void Battlefield::SendInitWorldStatesToAll()
{
    for (uint8 team = 0; team < 2; team++)
        for (GuidSet::iterator itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
            if (Player* player = sObjectAccessor->FindPlayer(*itr))
                player->SendInitWorldStates(m_ZoneId, m_ZoneId);
}

bool Battlefield::IncrementQuest(Player *player, uint32 quest, bool complete)
{
    if (!player)
        return false;

    Quest const* pQuest = sObjectMgr->GetQuestTemplate(quest);

    if (!pQuest || player->GetQuestStatus(quest) == QUEST_STATUS_NONE)
        return false;

    if (complete)
    {
        player->CompleteQuest(quest);
        return true;
    }
    else
    {
        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 creature = pQuest->RequiredNpcOrGo[i];
            if (uint32 spell_id = pQuest->GetRequiredSpell())
            {
                player->CastedCreatureOrGO(creature, ObjectGuid::Empty, spell_id);
                return true;
            }
            else if (creature > 0)
            {
                player->KilledMonsterCredit(creature);
                return true;
            }
            else if (creature < 0)
            {
                player->CastedCreatureOrGO(creature, ObjectGuid::Empty, 0);
                return true;
            }
            return true;
        }
    }
    return false;
}

void Battlefield::InitGUID()
{ 
    m_Guid = ObjectGuid::Create<HighGuid::PVPQueueGroup>(m_TypeId);
}

// *******************************************************
// ******************* CapturePoint **********************
// *******************************************************

BfCapturePoint::BfCapturePoint(Battlefield* battlefield) : m_Bf(battlefield), m_capturePoint(NULL)
{
    m_team = TEAM_NEUTRAL;
    m_value = 0.0f;
    m_maxValue = 0.0f;
    m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL;
    m_OldState = BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL;
    m_capturePointEntry = 0;
    m_neutralValuePct = 0;
    m_maxSpeed = 0;
}

bool BfCapturePoint::HandlePlayerEnter(Player* player)
{
    if (m_capturePoint)
    {
        player->SendUpdateWorldState(m_capturePoint->GetGOInfo()->controlZone.worldState1, 1);
        player->SendUpdateWorldState(m_capturePoint->GetGOInfo()->controlZone.worldstate2, uint32(ceil((m_value + m_maxValue) / (2 * m_maxValue) * 100.0f)));
        player->SendUpdateWorldState(m_capturePoint->GetGOInfo()->controlZone.worldstate3, m_neutralValuePct);
    }
    
    if (player->GetTeamId() >= 2)
        return false;

    return m_activePlayers[player->GetTeamId()].insert(player->GetGUID()).second;
}

GuidSet::iterator BfCapturePoint::HandlePlayerLeave(Player* player)
{
    /*if (!player || player->GetTeamId() >= 2)
        return;*/

    if (m_capturePoint)
        player->SendUpdateWorldState(m_capturePoint->GetGOInfo()->controlZone.worldState1, 0);

    GuidSet::iterator current = m_activePlayers[player->GetTeamId()].find(player->GetGUID());

    if (current == m_activePlayers[player->GetTeamId()].end())
        return current; // return end()

    m_activePlayers[player->GetTeamId()].erase(current++);
    return current;
}

void BfCapturePoint::SendChangePhase()
{
    if (!m_capturePoint)
        return;

    // send this too, sometimes the slider disappears, dunno why :(
    SendUpdateWorldState(m_capturePoint->GetGOInfo()->controlZone.worldState1, 1);
    // send these updates to only the ones in this objective
    SendUpdateWorldState(m_capturePoint->GetGOInfo()->controlZone.worldstate2, (uint32) ceil((m_value + m_maxValue) / (2 * m_maxValue) * 100.0f));
    // send this too, sometimes it resets :S
    SendUpdateWorldState(m_capturePoint->GetGOInfo()->controlZone.worldstate3, m_neutralValuePct);
}

bool BfCapturePoint::SetCapturePointData(GameObject* capturePoint)
{
    ASSERT(capturePoint);

    sLog->outDebug(LOG_FILTER_BATTLEFIELD, "Creating capture point %u", capturePoint->GetEntry());

    m_capturePoint = capturePoint;

    // check info existence
    GameObjectTemplate const* goinfo = capturePoint->GetGOInfo();
    if (goinfo->type != GAMEOBJECT_TYPE_CAPTURE_POINT)
    {
        sLog->outError(LOG_FILTER_GENERAL, "OutdoorPvP: GO %u is not capture point!", capturePoint->GetEntry());
        return false;
    }

    // get the needed values from goinfo
    m_maxValue = goinfo->controlZone.maxTime;
    m_maxSpeed = m_maxValue / (goinfo->controlZone.minTime ? goinfo->controlZone.minTime : 60);
    m_neutralValuePct = goinfo->controlZone.neutralPercent;
    m_minValue = m_maxValue * goinfo->controlZone.neutralPercent / 100;
    m_capturePointEntry = capturePoint->GetEntry();
    if (m_team == TEAM_ALLIANCE)
    {
        m_value = m_maxValue;
        m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE;
    }
    else
    {
        m_value = -m_maxValue;
        m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE;
    }

    return true;
}

bool BfCapturePoint::DelCapturePoint()
{
    if (m_capturePoint)
    {
        m_capturePoint->SetRespawnTime(0);                  // not save respawn time
        m_capturePoint->Delete();
        m_capturePoint = NULL;
    }

    return true;
}

bool BfCapturePoint::Update(uint32 diff)
{
    if (!m_capturePoint)
        return false;

    float radius = m_capturePoint->GetGOInfo()->controlZone.radius;
    for (uint8 team = 0; team < 2; ++team)
    {
        for (GuidSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end();)
        {
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
            {
                if (!m_capturePoint->IsWithinDistInMap(player, radius) || !player->IsOutdoorPvPActive())
                    itr = HandlePlayerLeave(player);
                else
                    ++itr;
            }
            else
                ++itr;
        }
    }

    std::list<Player*> players;
    Trinity::AnyPlayerInObjectRangeCheck checker(m_capturePoint, radius);
    Trinity::PlayerListSearcher<Trinity::AnyPlayerInObjectRangeCheck> searcher(m_capturePoint, players, checker);
    m_capturePoint->VisitNearbyWorldObject(radius, searcher);

    for (std::list<Player*>::iterator itr = players.begin(); itr != players.end(); ++itr)
        if ((*itr)->IsOutdoorPvPActive())
            if (m_activePlayers[(*itr)->GetTeamId()].insert((*itr)->GetGUID()).second)
                HandlePlayerEnter(*itr);

    // get the difference of numbers
    float fact_diff = ((float) m_activePlayers[0].size() - (float) m_activePlayers[1].size()) * diff / BATTLEFIELD_OBJECTIVE_UPDATE_INTERVAL;
    if (!fact_diff)
        return false;

    uint32 Challenger = 0;
    float maxDiff = m_maxSpeed * diff;

    if (fact_diff < 0)
    {
        // horde is in majority, but it's already horde-controlled -> no change
        if (m_State == BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE && m_value <= -m_maxValue)
            return false;

        if (fact_diff < -maxDiff)
            fact_diff = -maxDiff;

        Challenger = HORDE;
    }
    else
    {
        // ally is in majority, but it's already ally-controlled -> no change
        if (m_State == BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE && m_value >= m_maxValue)
            return false;

        if (fact_diff > maxDiff)
            fact_diff = maxDiff;

        Challenger = ALLIANCE;
    }

    float oldValue = m_value;
    TeamId oldTeam = m_team;

    m_OldState = m_State;

    m_value += fact_diff;

    if (m_value < -m_minValue)                              // red
    {
        if (m_value < -m_maxValue)
            m_value = -m_maxValue;
        m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE;
        m_team = TEAM_HORDE;
    }
    else if (m_value > m_minValue)                          // blue
    {
        if (m_value > m_maxValue)
            m_value = m_maxValue;
        m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE;
        m_team = TEAM_ALLIANCE;
    }
    else if (oldValue * m_value <= 0)                       // grey, go through mid point
    {
        // if challenger is ally, then n->a challenge
        if (Challenger == ALLIANCE)
            m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE;
        // if challenger is horde, then n->h challenge
        else if (Challenger == HORDE)
            m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE;
        m_team = TEAM_NEUTRAL;
    }
    else                                                    // grey, did not go through mid point
    {
        // old phase and current are on the same side, so one team challenges the other
        if (Challenger == ALLIANCE && (m_OldState == BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE || m_OldState == BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE))
            m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE;
        else if (Challenger == HORDE && (m_OldState == BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE || m_OldState == BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE))
            m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE;
        m_team = TEAM_NEUTRAL;
    }

    if (m_value != oldValue)
        SendChangePhase();

    if (m_OldState != m_State)
    {
        //sLog->outError(LOG_FILTER_BATTLEFIELD, "%u->%u", m_OldState, m_State);
        if (oldTeam != m_team)
        {
            ChangeTeam(oldTeam);

            for (uint8 team = 0; team < 2; ++team)
                for (GuidSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end(); ++itr)
                    if (Player* player = ObjectAccessor::FindPlayer(*itr))
                        player->UpdateAreaDependentAuras(player->getCurrentUpdateAreaID());
        }
        return true;
    }

    return false;
}

void BfCapturePoint::SendUpdateWorldState(uint32 field, uint32 value)
{
    for (uint8 team = 0; team < 2; ++team)
        for (GuidSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end(); ++itr)  // send to all players present in the area
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->SendUpdateWorldState(field, value);
}

void BfCapturePoint::SendObjectiveComplete(uint32 id, ObjectGuid guid)
{
    uint8 team;
    switch (m_State)
    {
        case BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE:
            team = 0;
            break;
        case BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE:
            team = 1;
            break;
        default:
            return;
    }

    // send to all players present in the area
    for (GuidSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end(); ++itr)
        if (Player* player = ObjectAccessor::FindPlayer(*itr))
            player->KilledMonsterCredit(id, guid);
}

bool BfCapturePoint::IsInsideObjective(Player* player) const
{
    if (!player || player->GetTeamId() >= 2)
        return false;

    return m_activePlayers[player->GetTeamId()].find(player->GetGUID()) != m_activePlayers[player->GetTeamId()].end();
}
