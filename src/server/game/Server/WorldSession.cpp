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

/** \file
    \ingroup u2w
*/

#include <zlib.h>
#include "WorldSocket.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Player.h"
#include "Vehicle.h"
#include "ObjectMgr.h"
#include "GuildMgr.h"
#include "Group.h"
#include "Guild.h"
#include "World.h"
#include "ObjectAccessor.h"
#include "BattlegroundMgr.h"
#include "OutdoorPvPMgr.h"
#include "MapManager.h"
#include "SocialMgr.h"
#include "zlib.h"
#include "ScriptMgr.h"
#include "Transport.h"
#include "WardenWin.h"
#include "WardenMac.h"
#include "BattlenetServerManager.h"
#include "CharacterPackets.h"
#include "ClientConfigPackets.h"
#include "MiscPackets.h"
#include "SystemPackets.h"

bool MapSessionFilter::Process(WorldPacket* packet)
{
    ClientOpcodeHandler const* opHandle = opcodeTable[static_cast<OpcodeClient>(packet->GetOpcode())];

    //let's check if our opcode can be really processed in Map::Update()
    if (opHandle->ProcessingPlace == PROCESS_INPLACE)
        return true;

    //we do not process thread-unsafe packets
    if (opHandle->ProcessingPlace == PROCESS_THREADUNSAFE)
        return false;

    Player* player = m_pSession->GetPlayer();
    if (!player)
        return false;

    //in Map::Update() we do not process packets where player is not in world!
    return player->IsInWorld();
}

//we should process ALL packets when player is not in world/logged in
//OR packet handler is not thread-safe!
bool WorldSessionFilter::Process(WorldPacket* packet)
{
    ClientOpcodeHandler const* opHandle = opcodeTable[static_cast<OpcodeClient>(packet->GetOpcode())];
    //check if packet handler is supposed to be safe
    if (opHandle->ProcessingPlace == PROCESS_INPLACE)
        return true;

    //thread-unsafe packets should be processed in World::UpdateSessions()
    if (opHandle->ProcessingPlace == PROCESS_THREADUNSAFE)
        return true;

    //no player attached? -> our client! ^^
    Player* player = m_pSession->GetPlayer();
    if (!player)
        return true;

    //lets process all packets for non-in-the-world player
    return (player->IsInWorld() == false);
}

/// WorldSession constructor
WorldSession::WorldSession(uint32 id, uint32 battlenetAccountId, std::shared_ptr<WorldSocket> sock, AccountTypes sec, uint8 expansion, time_t mute_time, LocaleConstant locale, uint32 recruiter, bool isARecruiter) :
m_muteTime(mute_time), m_timeOutTime(0), _player(NULL), m_Socket(sock),
_security(sec), _accountId(id), _battlenetAccountId(battlenetAccountId), m_expansion(expansion), _logoutTime(0),
m_inQueue(false), m_playerLoading(false), m_playerLogout(false),
m_playerRecentlyLogout(false), m_playerSave(false),
m_sessionDbcLocale(sWorld->GetAvailableDbcLocale(locale)),
m_sessionDbLocaleIndex(locale),
m_latency(0), m_TutorialsChanged(false), recruiterId(recruiter),
isRecruiter(isARecruiter), timeLastWhoCommand(0),
timeLastChannelInviteCommand(0), timeLastGroupInviteCommand(0), timeLastGuildInviteCommand(0), timeLastChannelPassCommand(0),
timeLastChannelMuteCommand(0), timeLastChannelBanCommand(0), timeLastChannelUnbanCommand(0), timeLastChannelAnnounceCommand(0),
timeLastChannelModerCommand(0), timeLastChannelOwnerCommand(0),
timeLastChannelSetownerCommand(0),
timeLastChannelUnmoderCommand(0),
timeLastChannelUnmuteCommand(0),
timeLastChannelKickCommand(0), timeLastHandleSendMail(0), timeLastHandleSellItem(0), timeLastHandlePlayerLogin(0), timeLastHandleSpellClick(0),
timeCharEnumOpcode(0), timeAddIgnoreOpcode(0), timeMoveTeleportAck(0),
playerLoginCounter(0), m_currentBankerGUID()
{
    _warden = NULL;
    _pakagepersecond = 0;
    _counttokick = 0;
    _second = 1000;
    _filterAddonMessages = false;

    if (sock)
    {
        m_Address = sock->GetRemoteIpAddress().to_string();
        ResetTimeOutTime();
        LoginDatabase.PExecute("UPDATE account SET online = 1 WHERE id = %u;", GetAccountId());     // One-time query
    }

    InitializeQueryCallbackParameters();
}

/// WorldSession destructor
WorldSession::~WorldSession()
{
    ///- unload player if not unloaded
    if (_player)
        LogoutPlayer (true);

    /// - If have unclosed socket, close it
    if (m_Socket)
    {
        m_Socket->CloseSocket();
        m_Socket = nullptr;
    }

    if (_warden)
        delete _warden;

    ///- empty incoming packet queue
    WorldPacket* packet = NULL;
    while (_recvQueue.next(packet))
        delete packet;

    LoginDatabase.PExecute("UPDATE account SET online = 0 WHERE id = %u;", GetAccountId());     // One-time query
}

/// Get the player name
std::string WorldSession::GetPlayerName(bool simple /* = true */) const
 {
    std::string name = "[Player: ";
    uint32 guidLow = 0;

    if (Player* player = GetPlayer())
    {
        name.append(player->GetName());
        guidLow = player->GetGUID().GetCounter();
    }
    else
        name.append("<none>");

    if (!simple)
    {
        std::ostringstream ss;
        ss << " (Guid: " << guidLow << ", Account: " << GetAccountId() << ")";
        name.append(ss.str());
    }

    name.append("]");
    return name;
}

/// Get player guid if available. Use for logging purposes only
uint32 WorldSession::GetGuidLow() const
{
    return GetPlayer() ? GetPlayer()->GetGUID().GetCounter() : 0;
}

/// Send a packet to the client
void WorldSession::SendPacket(WorldPacket const* packet, bool forced /*= false*/)
{
    if (!m_Socket)
        return;

    if (packet->GetOpcode() == NULL_OPCODE)
    {
        sLog->outError(LOG_FILTER_OPCODES, "Prevented sending of NULL_OPCODE to %s", GetPlayerName(false).c_str());
        return;
    }
    else if (packet->GetOpcode() == UNKNOWN_OPCODE)
    {
        sLog->outError(LOG_FILTER_OPCODES, "Prevented sending of UNKNOWN_OPCODE to %s", GetPlayerName(false).c_str());
        return;
    }

    if (!forced)
    {
        ServerOpcodeHandler const* handler = opcodeTable[static_cast<OpcodeServer>(packet->GetOpcode())];

        if (!handler || handler->Status == STATUS_UNHANDLED)
        {
            sLog->outError(LOG_FILTER_OPCODES, "Prevented sending disabled opcode %s to %s", GetOpcodeNameForLogging(static_cast<OpcodeServer>(packet->GetOpcode())).c_str(), GetPlayerName(false).c_str());
            return;
        }
    }

    const_cast<WorldPacket*>(packet)->FlushBits();

#ifdef TRINITY_DEBUG
    // Code for network use statistic
    static uint64 sendPacketCount = 0;
    static uint64 sendPacketBytes = 0;

    static time_t firstTime = time(NULL);
    static time_t lastTime = firstTime;                     // next 60 secs start time

    static uint64 sendLastPacketCount = 0;
    static uint64 sendLastPacketBytes = 0;

    time_t cur_time = time(NULL);

    if ((cur_time - lastTime) < 60)
    {
        sendPacketCount+=1;
        sendPacketBytes+=packet->size();

        sendLastPacketCount+=1;
        sendLastPacketBytes+=packet->size();
    }
    else
    {
        uint64 minTime = uint64(cur_time - lastTime);
        uint64 fullTime = uint64(lastTime - firstTime);
        sLog->outInfo(LOG_FILTER_GENERAL, "Send all time packets count: " UI64FMTD " bytes: " UI64FMTD " avr.count/sec: %f avr.bytes/sec: %f time: %u", sendPacketCount, sendPacketBytes, float(sendPacketCount)/fullTime, float(sendPacketBytes)/fullTime, uint32(fullTime));
        sLog->outInfo(LOG_FILTER_GENERAL, "Send last min packets count: " UI64FMTD " bytes: " UI64FMTD " avr.count/sec: %f avr.bytes/sec: %f", sendLastPacketCount, sendLastPacketBytes, float(sendLastPacketCount)/minTime, float(sendLastPacketBytes)/minTime);

        lastTime = cur_time;
        sendLastPacketCount = 1;
        sendLastPacketBytes = packet->wpos();               // wpos is real written size
    }
#endif                                                      // !TRINITY_DEBUG

    sScriptMgr->OnPacketSend(this, *packet);

    m_Socket->SendPacket(*const_cast<WorldPacket*>(packet));
}

/// Add an incoming packet to the queue
void WorldSession::QueuePacket(WorldPacket* new_packet)
{
    if(_counttokick < 4 && _pakagepersecond < 2000)
    {
        _pakagepersecond++;
        _recvQueue.add(new_packet);
    }
}

/// Logging helper for unexpected opcodes
void WorldSession::LogUnexpectedOpcode(WorldPacket* packet, const char* status, const char *reason)
{
    sLog->outError(LOG_FILTER_OPCODES, "Received unexpected opcode %s Status: %s Reason: %s from %s",
        GetOpcodeNameForLogging(static_cast<OpcodeClient>(packet->GetOpcode())).c_str(), status, reason, GetPlayerName(false).c_str());
}

/// Logging helper for unexpected opcodes
void WorldSession::LogUnprocessedTail(WorldPacket* packet)
{
    sLog->outError(LOG_FILTER_OPCODES, "Unprocessed tail data (read stop at %u from %u) Opcode %s from %s",
        uint32(packet->rpos()), uint32(packet->wpos()), GetOpcodeNameForLogging(static_cast<OpcodeClient>(packet->GetOpcode())).c_str(), GetPlayerName(false).c_str());
    packet->print_storage();
}

/// Update the WorldSession (triggered by World update)
bool WorldSession::Update(uint32 diff, PacketFilter& updater)
{
    /// Update Timeout timer.
    UpdateTimeOutTime(diff);

    ///- Before we process anything:
    /// If necessary, kick the player from the character select screen
    if (IsConnectionIdle())
        m_Socket->CloseSocket();

    //count pakage
    if(_second <= diff)
    {
        if(_pakagepersecond > 1200)
        {
            sLog->outSpamm("_pakagepersecond: %u, Account: %u: Player: %s.", _pakagepersecond, GetAccountId(), (_player) ? _player->GetName() : "None");
            _counttokick++;
        }
        else _counttokick = 0;

        if(_counttokick > 3)
        {
            sLog->outSpamm("Kick Player: %s. Reason: packets spam.", (_player) ? _player->GetName() : "None");
            KickPlayer();
        }

        _pakagepersecond = 0;
        _second = 1000;
    } else _second -= diff;

    ///- Retrieve packets from the receive queue and call the appropriate handlers
    /// not process packets if socket already closed
    WorldPacket* packet = NULL;
    //! Delete packet after processing by default
    bool deletePacket = true;
    //! To prevent infinite loop
    WorldPacket* firstDelayedPacket = NULL;
    //! If _recvQueue.peek() == firstDelayedPacket it means that in this Update call, we've processed all
    //! *properly timed* packets, and we're now at the part of the queue where we find
    //! delayed packets that were re-enqueued due to improper timing. To prevent an infinite
    //! loop caused by re-enqueueing the same packets over and over again, we stop updating this session
    //! and continue updating others. The re-enqueued packets will be handled in the next Update call for this session.

    while (m_Socket && !_recvQueue.empty() && _recvQueue.peek(true) != firstDelayedPacket && _recvQueue.next(packet, updater))
    {
        ClientOpcodeHandler const* opHandle = opcodeTable[static_cast<OpcodeClient>(packet->GetOpcode())];
        try
        {
            switch (opHandle->Status)
            {
                case STATUS_LOGGEDIN:
                    if (!_player)
                    {
                        // skip STATUS_LOGGEDIN opcode unexpected errors if player logout sometime ago - this can be network lag delayed packets
                        //! If player didn't log out a while ago, it means packets are being sent while the server does not recognize
                        //! the client to be in world yet. We will re-add the packets to the bottom of the queue and process them later.
                        if (!m_playerRecentlyLogout)
                        {
                            //! Prevent infinite loop
                            if (!firstDelayedPacket)
                                firstDelayedPacket = packet;
                            //! Because checking a bool is faster than reallocating memory
                            deletePacket = false;
                            QueuePacket(packet);
                            //! Log
                                sLog->outDebug(LOG_FILTER_NETWORKIO, "Re-enqueueing packet with opcode %s with with status STATUS_LOGGEDIN. "
                                    "Player is currently not in world yet.", GetOpcodeNameForLogging(static_cast<OpcodeClient>(packet->GetOpcode())).c_str());
                        }
                    }
                    else if (_player->IsInWorld())
                    {
                        sScriptMgr->OnPacketReceive(this, *packet);
                        opHandle->Call(this, *packet);
                        if (sLog->ShouldLog(LOG_FILTER_NETWORKIO, LOG_LEVEL_TRACE) && packet->rpos() < packet->wpos())
                            LogUnprocessedTail(packet);
                    }
                    // lag can cause STATUS_LOGGEDIN opcodes to arrive after the player started a transfer
                    break;
                case STATUS_LOGGEDIN_OR_RECENTLY_LOGGOUT:
                    if (!_player && !m_playerRecentlyLogout && !m_playerLogout) // There's a short delay between _player = null and m_playerRecentlyLogout = true during logout
                        LogUnexpectedOpcode(packet, "STATUS_LOGGEDIN_OR_RECENTLY_LOGGOUT",
                            "the player has not logged in yet and not recently logout");
                    else
                    {
                        // not expected _player or must checked in packet hanlder
                        sScriptMgr->OnPacketReceive(this, *packet);
                        opHandle->Call(this, *packet);
                        if (sLog->ShouldLog(LOG_FILTER_NETWORKIO, LOG_LEVEL_TRACE) && packet->rpos() < packet->wpos())
                            LogUnprocessedTail(packet);
                    }
                    break;
                case STATUS_TRANSFER:
                    if (!_player)
                        LogUnexpectedOpcode(packet, "STATUS_TRANSFER", "the player has not logged in yet");
                    else if (_player->IsInWorld())
                        LogUnexpectedOpcode(packet, "STATUS_TRANSFER", "the player is still in world");
                    else
                    {
                        sScriptMgr->OnPacketReceive(this, *packet);
                        opHandle->Call(this, *packet);
                        if (sLog->ShouldLog(LOG_FILTER_NETWORKIO, LOG_LEVEL_TRACE) && packet->rpos() < packet->wpos())
                            LogUnprocessedTail(packet);
                    }
                    break;
                case STATUS_AUTHED:
                    // prevent cheating with skip queue wait
                    if (m_inQueue)
                    {
                        LogUnexpectedOpcode(packet, "STATUS_AUTHED", "the player not pass queue yet");
                        break;
                    }

                    // some auth opcodes can be recieved before STATUS_LOGGEDIN_OR_RECENTLY_LOGGOUT opcodes
                    // however when we recieve CMSG_CHAR_ENUM we are surely no longer during the logout process.
                    if (packet->GetOpcode() == CMSG_CHAR_ENUM)
                        m_playerRecentlyLogout = false;

                    sScriptMgr->OnPacketReceive(this, *packet);
                    opHandle->Call(this, *packet);
                    if (sLog->ShouldLog(LOG_FILTER_NETWORKIO, LOG_LEVEL_TRACE) && packet->rpos() < packet->wpos())
                        LogUnprocessedTail(packet);
                    break;
                case STATUS_NEVER:
                    sLog->outError(LOG_FILTER_OPCODES, "Received not allowed opcode %s from %s", GetOpcodeNameForLogging(static_cast<OpcodeClient>(packet->GetOpcode())).c_str(), GetPlayerName(false).c_str());
                    break;
                case STATUS_UNHANDLED:
                    sLog->outError(LOG_FILTER_OPCODES, "Received not handled opcode %s from %s", GetOpcodeNameForLogging(static_cast<OpcodeClient>(packet->GetOpcode())).c_str(), GetPlayerName(false).c_str());
                    break;
            }
        }
        catch(ByteBufferException &)
        {
            sLog->outError(LOG_FILTER_NETWORKIO, "WorldSession::Update ByteBufferException occured while parsing a packet (opcode: %u) from client %s, accountid=%i. Skipped packet.",
                    packet->GetOpcode(), GetRemoteAddress().c_str(), GetAccountId());
            packet->hexlike();
        }

        if (deletePacket)
            delete packet;
    }

    if (m_Socket && m_Socket->IsOpen() && _warden)
        _warden->Update();

    ProcessQueryCallbacks();

    //check if we are safe to proceed with logout
    //logout procedure should happen only in World::UpdateSessions() method!!!
    if (updater.ProcessLogout())
    {
        time_t currTime = time(NULL);
        ///- If necessary, log the player out
        if (ShouldLogOut(currTime) && !m_playerLoading)
            LogoutPlayer(true);

        if (m_Socket && GetPlayer() && _warden)
            _warden->Update();

        ///- Cleanup socket pointer if need
        if (m_Socket && !m_Socket->IsOpen())
        {
            m_Socket = nullptr;
        }

        if (!m_Socket)
            return false;                                       //Will remove this session from the world session map
    }

    return true;
}

/// %Log the player out
void WorldSession::LogoutPlayer(bool Save)
{
    // finish pending transfers before starting the logout
    while (_player && _player->IsBeingTeleportedFar())
        HandleMoveWorldportAckOpcode();

    m_playerLogout = true;
    m_playerSave = Save;

    if (_player)
    {
        if (ObjectGuid lguid = _player->GetLootGUID())
            DoLootRelease(lguid);
        _player->ClearAoeLootList();

        ///- If the player just died before logging out, make him appear as a ghost
        //FIXME: logout must be delayed in case lost connection with client in time of combat
        if (_player->GetDeathTimer())
        {
            _player->getHostileRefManager().deleteReferences();
            _player->BuildPlayerRepop();
            _player->RepopAtGraveyard();
        }
        else if (!_player->getAttackers().empty())
        {
            bool _killer = false;
            // build set of player who attack _player or who have pet attacking of _player
            std::set<Player*> aset;
            for (Unit::AttackerSet::const_iterator itr = _player->getAttackers().begin(); itr != _player->getAttackers().end(); ++itr)
            {
                Unit* owner = (*itr)->GetOwner();           // including player controlled case
                if (owner && owner->GetTypeId() == TYPEID_PLAYER)
                    aset.insert(owner->ToPlayer());
                else if ((*itr)->GetTypeId() == TYPEID_PLAYER)
                    aset.insert((Player*)(*itr));

                if (!(*itr)->isTrainingDummy())
                    _killer = true;
            }
            // CombatStop() method is removing all attackers from the AttackerSet
            // That is why it must be AFTER building current set of attackers
            _player->CombatStop();
            _player->getHostileRefManager().setOnlineOfflineState(false);

            if (_killer)
            {
                _player->RemoveAllAurasOnDeath();
                _player->SetPvPDeath(!aset.empty());
                _player->KillPlayer();
                _player->BuildPlayerRepop();
                _player->RepopAtGraveyard();
            }

            // give honor to all attackers from set like group case
            for (std::set<Player*>::const_iterator itr = aset.begin(); itr != aset.end(); ++itr)
                (*itr)->RewardHonor(_player, aset.size());

            // give bg rewards and update counters like kill by first from attackers
            // this can't be called for all attackers.
            if (!aset.empty())
                if (Battleground* bg = _player->GetBattleground())
                    bg->HandleKillPlayer(_player, *aset.begin());
        }
        else if (_player->HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION))
        {
            // this will kill character by SPELL_AURA_SPIRIT_OF_REDEMPTION
            _player->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
            _player->KillPlayer();
            _player->BuildPlayerRepop();
            _player->RepopAtGraveyard();
        }
        else if (_player->HasPendingBind())
        {
            _player->RepopAtGraveyard();
            _player->SetPendingBind(0, 0);
        }

        sBattlefieldMgr->EventPlayerLoggedOut(_player);

        //drop a flag if player is carrying it
        if (Battleground* bg = _player->GetBattleground())
            bg->EventPlayerLoggedOut(_player);

        ///- Teleport to home if the player is in an invalid instance
        if (!_player->m_InstanceValid && !_player->isGameMaster())
            _player->TeleportTo(_player->m_homebindMapId, _player->m_homebindX, _player->m_homebindY, _player->m_homebindZ, _player->GetOrientation());

        sOutdoorPvPMgr->HandlePlayerLeaveZone(_player, _player->GetZoneId());

        for (int i=0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            if (BattlegroundQueueTypeId bgQueueTypeId = _player->GetBattlegroundQueueTypeId(i))
            {
                _player->RemoveBattlegroundQueueId(bgQueueTypeId);
                sBattlegroundMgr->m_BattlegroundQueues[ bgQueueTypeId ].RemovePlayer(_player->GetGUID(), true);
            }
        }

        // Repop at GraveYard or other player far teleport will prevent saving player because of not present map
        // Teleport player immediately for correct player save
        while (_player->IsBeingTeleportedFar())
            HandleMoveWorldportAckOpcode();

        ///- If the player is in a guild, update the guild roster and broadcast a logout message to other guild members
        if (Guild* guild = sGuildMgr->GetGuildById(_player->GetGuildId()))
            guild->HandleMemberLogout(this);

        ///- Remove pet
        if (_player->getClass() != CLASS_WARLOCK)
            _player->RemovePet(NULL);
        else
        {
            if (Pet* _pet = _player->GetPet())
                _pet->SavePetToDB();
        }

        ///- empty buyback items and save the player in the database
        // some save parts only correctly work in case player present in map/player_lists (pets, etc)
        if (Save)
        {
            uint32 eslot;
            for (int j = BUYBACK_SLOT_START; j < BUYBACK_SLOT_END; ++j)
            {
                eslot = j - BUYBACK_SLOT_START;
                _player->SetGuidValue(PLAYER_FIELD_INV_SLOTS + (j * 4), ObjectGuid::Empty);
                _player->SetUInt32Value(PLAYER_FIELD_BUYBACK_PRICE + eslot, 0);
                _player->SetUInt32Value(PLAYER_FIELD_BUYBACK_TIMESTAMP + eslot, 0);
            }
            _player->SaveToDB();
        }

        ///- Leave all channels before player delete...
        _player->CleanupChannels();

        ///- If the player is in a group (or invited), remove him. If the group if then only 1 person, disband the group.
        _player->UninviteFromGroup();

        // remove player from the group if he is:
        // a) in group; b) not in raid group; c) logging out normally (not being kicked or disconnected)
        if (_player->GetGroup() && !_player->GetGroup()->isRaidGroup() && m_Socket)
            _player->RemoveFromGroup();

        //! Send update to group and reset stored max enchanting level
        if (_player->GetGroup())
        {
            _player->GetGroup()->SendUpdate();
            _player->GetGroup()->ResetMaxEnchantingLevel();
        }

        //! Broadcast a logout message to the player's friends
        sSocialMgr->SendFriendStatus(_player, FRIEND_OFFLINE, _player->GetGUID(), true);
        sSocialMgr->RemovePlayerSocial(_player->GetGUID());

        //! Call script hook before deletion
        sScriptMgr->OnPlayerLogout(_player);

        //! Remove the player from the world
        // the player may not be in the world when logging out
        // e.g if he got disconnected during a transfer to another map
        // calls to GetMap in this case may cause crashes
        _player->CleanupsBeforeDelete();
        sLog->outInfo(LOG_FILTER_CHARACTER, "Account: %d (IP: %s) Logout Character:[%s] (GUID: %u) Level: %d", GetAccountId(), GetRemoteAddress().c_str(), _player->GetName(), _player->GetGUID().GetCounter(), _player->getLevel());

        sBattlenetServer.SendChangeToonOnlineState(GetBattlenetAccountId(), GetAccountId(), _player->GetGUID(), _player->GetName(), false);

        //! Send the 'logout complete' packet to the client
        //! Client will respond by sending 3x CMSG_CANCEL_TRADE, which we currently dont handle
        //! Send before delete. As need guid.
        WorldPacket data(SMSG_LOGOUT_COMPLETE);
        //data << _player->GetGUID();   //relogin
        SendPacket(&data);
        sLog->outDebug(LOG_FILTER_NETWORKIO, "SESSION: Sent SMSG_LOGOUT_COMPLETE Message");

        if (Map* _map = _player->FindMap())
            _map->RemovePlayerFromMap(_player, true);

        SetPlayer(NULL); //! Pointer already deleted during RemovePlayerFromMap

        //! Since each account can only have one online character at any given time, ensure all characters for active account are marked as offline
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ACCOUNT_ONLINE);
        stmt->setUInt32(0, GetAccountId());
        CharacterDatabase.Execute(stmt);
    }

    m_playerLogout = false;
    m_playerSave = false;
    m_playerRecentlyLogout = true;
    LogoutRequest(0);
}

/// Kick a player out of the World
void WorldSession::KickPlayer()
{
    if (m_Socket)
        m_Socket->CloseSocket();
}

void WorldSession::SendNotification(const char *format, ...)
{
    if (format)
    {
        va_list ap;
        char szStr[1024];
        szStr[0] = '\0';
        va_start(ap, format);
        vsnprintf(szStr, 1024, format, ap);
        va_end(ap);

        size_t len = strlen(szStr);

        //! 5.4.1
        WorldPacket data(SMSG_NOTIFICATION, 2 + len);
        data.WriteBits(len, 12);
        data.FlushBits();
        data.append(szStr, len);
        SendPacket(&data);
    }
}

void WorldSession::SendNotification(uint32 string_id, ...)
{
    char const* format = GetTrinityString(string_id);
    if (format)
    {
        va_list ap;
        char szStr[1024];
        szStr[0] = '\0';
        va_start(ap, string_id);
        vsnprintf(szStr, 1024, format, ap);
        va_end(ap);

        size_t len = strlen(szStr);
        WorldPacket data(SMSG_NOTIFICATION, 2 + len);
        data.WriteBits(len, 12);
        data.FlushBits();
        data.append(szStr, len);
        SendPacket(&data);
    }
}

const char *WorldSession::GetTrinityString(int32 entry) const
{
    return sObjectMgr->GetTrinityString(entry, GetSessionDbLocaleIndex());
}

void WorldSession::Handle_NULL(WorldPacket& recvPacket)
{
    sLog->outError(LOG_FILTER_OPCODES, "Received unhandled opcode %s from %s",
        GetOpcodeNameForLogging(static_cast<OpcodeClient>(recvPacket.GetOpcode())).c_str(), GetPlayerName(false).c_str());
}

void WorldSession::Handle_EarlyProccess(WorldPacket& recvPacket)
{
    sLog->outError(LOG_FILTER_OPCODES, "Received opcode %s that must be processed in WorldSocket::OnRead from %s",
        GetOpcodeNameForLogging(static_cast<OpcodeClient>(recvPacket.GetOpcode())).c_str(), GetPlayerName(false).c_str());
}

void WorldSession::Handle_ServerSide(WorldPacket& recvPacket)
{
    sLog->outError(LOG_FILTER_OPCODES, "Received server-side opcode %s from %s",
        GetOpcodeNameForLogging(static_cast<OpcodeClient>(recvPacket.GetOpcode())).c_str(), GetPlayerName(false).c_str());
}

void WorldSession::Handle_Deprecated(WorldPacket& recvPacket)
{
    sLog->outError(LOG_FILTER_OPCODES, "Received deprecated opcode %s from %s",
        GetOpcodeNameForLogging(static_cast<OpcodeClient>(recvPacket.GetOpcode())).c_str(), GetPlayerName(false).c_str());
}

void WorldSession::SendAuthWaitQue(uint32 position)
{
    if (position == 0)
        SendAuthResponse(AUTH_OK);
    else
        SendAuthResponse(AUTH_OK, true, true, position);
}

void WorldSession::LoadGlobalAccountData()
{
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_ACCOUNT_DATA);
    stmt->setUInt32(0, GetAccountId());
    LoadAccountData(CharacterDatabase.Query(stmt), GLOBAL_CACHE_MASK);
}

void WorldSession::LoadAccountData(PreparedQueryResult result, uint32 mask)
{
    for (uint32 i = 0; i < NUM_ACCOUNT_DATA_TYPES; ++i)
        if (mask & (1 << i))
            m_accountData[i] = AccountData();

    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        uint32 type = fields[0].GetUInt8();
        if (type >= NUM_ACCOUNT_DATA_TYPES)
        {
            sLog->outError(LOG_FILTER_GENERAL, "Table `%s` have invalid account data type (%u), ignore.",
                mask == GLOBAL_CACHE_MASK ? "account_data" : "character_account_data", type);
            continue;
        }

        if ((mask & (1 << type)) == 0)
        {
            sLog->outError(LOG_FILTER_GENERAL, "Table `%s` have non appropriate for table  account data type (%u), ignore.",
                mask == GLOBAL_CACHE_MASK ? "account_data" : "character_account_data", type);
            continue;
        }

        m_accountData[type].Time = time_t(fields[1].GetUInt32());
        m_accountData[type].Data = fields[2].GetString();
    }
    while (result->NextRow());
}

void WorldSession::SetAccountData(AccountDataType type, time_t tm, std::string data)
{
    if ((1 << type) & GLOBAL_CACHE_MASK)
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_ACCOUNT_DATA);
        stmt->setUInt32(0, GetAccountId());
        stmt->setUInt8(1, type);
        stmt->setUInt32(2, uint32(tm));
        stmt->setString(3, data);
        CharacterDatabase.Execute(stmt);
    }
    else
    {
        // _player can be NULL and packet received after logout but m_GUID still store correct guid
        if (!m_GUIDLow)
            return;

        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_PLAYER_ACCOUNT_DATA);
        stmt->setUInt64(0, m_GUIDLow);
        stmt->setUInt8(1, type);
        stmt->setUInt32(2, uint32(tm));
        stmt->setString(3, data);
        CharacterDatabase.Execute(stmt);
    }

    m_accountData[type].Time = tm;
    m_accountData[type].Data = data;
}

// ToDo: add confing. Are we need it?
void WorldSession::SendTimeZoneInformation()
{
    //Etc/UTC recomendation from skyfire
    WorldPackets::System::SetTimeZoneInformation packet;
    packet.ServerTimeTZ = "Etc/UTC";    //RTL: Europe/Paris
    packet.GameTimeTZ = "Etc/UTC";      //RTL: Europe/Paris

    SendPacket(packet.Write());
}

void WorldSession::LoadTutorialsData()
{
    memset(m_Tutorials, 0, sizeof(uint32) * MAX_ACCOUNT_TUTORIAL_VALUES);

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_TUTORIALS);
    stmt->setUInt32(0, GetAccountId());
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
        for (uint8 i = 0; i < MAX_ACCOUNT_TUTORIAL_VALUES; ++i)
            m_Tutorials[i] = (*result)[i].GetUInt32();

    m_TutorialsChanged = false;
}

void WorldSession::SendTutorialsData()
{
    WorldPackets::Misc::TutorialFlags packet;
    memcpy(packet.TutorialData, m_Tutorials, sizeof(m_Tutorials));
    SendPacket(packet.Write());
}

void WorldSession::SaveTutorialsData(SQLTransaction &trans)
{
    if (!m_TutorialsChanged)
        return;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_HAS_TUTORIALS);
    stmt->setUInt32(0, GetAccountId());
    bool hasTutorials = bool(CharacterDatabase.Query(stmt));
    // Modify data in DB
    stmt = CharacterDatabase.GetPreparedStatement(hasTutorials ? CHAR_UPD_TUTORIALS : CHAR_INS_TUTORIALS);
    for (uint8 i = 0; i < MAX_ACCOUNT_TUTORIAL_VALUES; ++i)
        stmt->setUInt32(i, m_Tutorials[i]);
    stmt->setUInt32(MAX_ACCOUNT_TUTORIAL_VALUES, GetAccountId());
    trans->Append(stmt);

    m_TutorialsChanged = false;
}

void WorldSession::ReadAddonsInfo(ByteBuffer &data)
{
    if (data.rpos() + 4 > data.size())
        return;

    uint32 size;
    data >> size;

    if (!size)
        return;

    if (size > 0xFFFFF)
    {
        sLog->outError(LOG_FILTER_GENERAL, "WorldSession::ReadAddonsInfo addon info too big, size %u", size);
        return;
    }

    uLongf uSize = size;
    uint32 pos = data.rpos();

    ByteBuffer addonInfo;
    addonInfo.resize(size);

    m_addonsList.clear();
    if (uncompress(const_cast<uint8*>(addonInfo.contents()), &uSize, const_cast<uint8*>(data.contents() + pos), data.size() - pos) == Z_OK)
    {
        uint32 addonsCount;
        addonInfo >> addonsCount;                         // addons count

        for (uint32 i = 0; i < addonsCount; ++i)
        {
            std::string addonName;
            uint8 hasPubKey;
            uint32 CRC, urlCRC;

            // check next addon data format correctness
            if (addonInfo.rpos() + 1 > addonInfo.size())
                return;

            addonInfo >> addonName;
            addonInfo >> hasPubKey >> CRC >> urlCRC;

            sLog->outInfo(LOG_FILTER_GENERAL, "ADDON: Name: %s, hasPubKey: 0x%x, CRC: 0x%x, urlCRC: 0x%x", addonName.c_str(), hasPubKey, CRC, urlCRC);

            AddonInfo addon(addonName, hasPubKey, CRC, 2, true);

            SavedAddon const* savedAddon = AddonMgr::GetAddonInfo(addonName);
            if (savedAddon)
            {
                bool match = true;

                if (addon.CRC != savedAddon->CRC)
                    match = false;

                if (!match)
                    sLog->outInfo(LOG_FILTER_GENERAL, "ADDON: %s was known, but didn't match known CRC (0x%x)!", addon.Name.c_str(), savedAddon->CRC);
                else
                    sLog->outInfo(LOG_FILTER_GENERAL, "ADDON: %s was known, CRC is correct (0x%x)", addon.Name.c_str(), savedAddon->CRC);
            }
            else
            {
                AddonMgr::SaveAddon(addon);
                sLog->outInfo(LOG_FILTER_GENERAL, "ADDON: %s (0x%x) was not known, saving...", addon.Name.c_str(), addon.CRC);
            }

            // TODO: Find out when to not use CRC/pubkey, and other possible states.
            m_addonsList.push_back(addon);
        }

        uint32 latestBannedAddonTimeStamp;
        addonInfo >> latestBannedAddonTimeStamp;
        sLog->outDebug(LOG_FILTER_NETWORKIO, "ADDON: latestBannedAddonTimeStamp: %u", latestBannedAddonTimeStamp);

        if (addonInfo.rpos() != addonInfo.size())
            sLog->outDebug(LOG_FILTER_NETWORKIO, "packet under-read!");
    }
    else
        sLog->outError(LOG_FILTER_GENERAL, "Addon packet uncompress error!");
}

void WorldSession::SendAddonsInfo()
{
    WorldPackets::ClientConfig::AddonInfo addonInfo;
    addonInfo.Addons = &m_addonsList;
    addonInfo.BannedAddons = &sBannedAddonDataMap;
    SendPacket(addonInfo.Write());
}

bool WorldSession::IsAddonRegistered(const std::string& prefix) const
{
    if (!_filterAddonMessages) // if we have hit the softcap (64) nothing should be filtered
        return true;

    if (_registeredAddonPrefixes.empty())
        return false;

    std::vector<std::string>::const_iterator itr = std::find(_registeredAddonPrefixes.begin(), _registeredAddonPrefixes.end(), prefix);
    return itr != _registeredAddonPrefixes.end();
}

void WorldSession::HandleUnregisterAddonPrefixesOpcode(WorldPacket& /*recvPacket*/) // empty packet
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_UNREGISTER_ALL_ADDON_PREFIXES");

    _registeredAddonPrefixes.clear();
}

void WorldSession::HandleAddonRegisteredPrefixesOpcode(WorldPacket& recvPacket)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_ADDON_REGISTERED_PREFIXES");

    // This is always sent after CMSG_UNREGISTER_ALL_ADDON_PREFIXES

    uint32 count = 0;
    recvPacket >> count;

    if (count > REGISTERED_ADDON_PREFIX_SOFTCAP)
    {
        // if we have hit the softcap (64) nothing should be filtered
        _filterAddonMessages = false;
        recvPacket.rfinish();
        return;
    }

    std::vector<uint8> lengths(count);
    for (uint32 i = 0; i < count; ++i)
    {
        lengths[i] = recvPacket.ReadBits(5);
        _registeredAddonPrefixes.push_back(recvPacket.ReadString(lengths[i]));
    }

    if (_registeredAddonPrefixes.size() > REGISTERED_ADDON_PREFIX_SOFTCAP) // shouldn't happen
    {
        _filterAddonMessages = false;
        return;
    }

    _filterAddonMessages = true;
}

void WorldSession::SetPlayer(Player* player)
{
    _player = player;

    // set m_GUID that can be used while player loggined and later until m_playerRecentlyLogout not reset
    if (_player)
        m_GUIDLow = _player->GetGUID().GetCounter();
}

void WorldSession::InitializeQueryCallbackParameters()
{
    // Callback parameters that have pointers in them should be properly
    // initialized to NULL here.
    _charCreateCallback.SetParam(NULL);
}

void WorldSession::ProcessQueryCallbacks()
{
    PreparedQueryResult result;

    //! HandleCharEnumOpcode
    if (_charEnumCallback.valid() && _charEnumCallback.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        result = _charEnumCallback.get();
        HandleCharEnum(result);
    }

    if (_charCreateCallback.IsReady())
    {
        _charCreateCallback.GetResult(result);
        HandleCharCreateCallback(result, _charCreateCallback.GetParam().get());
    }

    //! HandlePlayerLoginOpcode
    if (_charLoginCallback.valid() && _charLoginCallback.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        SQLQueryHolder* param = _charLoginCallback.get();
        HandlePlayerLogin((LoginQueryHolder*)param);
    }

    //! HandleAddFriendOpcode
    if (_addFriendCallback.IsReady())
    {
        std::string param = _addFriendCallback.GetParam();
        _addFriendCallback.GetResult(result);
        HandleAddFriendOpcodeCallBack(result, param);
        _addFriendCallback.FreeResult();
    }

    //- HandleCharRenameOpcode
    if (_charRenameCallback.IsReady())
    {
        std::string param = _charRenameCallback.GetParam();
        _charRenameCallback.GetResult(result);
        HandleChangePlayerNameOpcodeCallBack(result, param);
        _charRenameCallback.FreeResult();
    }

    //- HandleCharAddIgnoreOpcode
    if (_addIgnoreCallback.valid() && _addIgnoreCallback.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        result = _addIgnoreCallback.get();
        HandleAddIgnoreOpcodeCallBack(result);
    }

    //- SendStabledPet
    if (_sendStabledPetCallback.IsReady())
    {
        ObjectGuid param = _sendStabledPetCallback.GetParam();
        _sendStabledPetCallback.GetResult(result);
        SendStablePetCallback(result, param);
        _sendStabledPetCallback.FreeResult();
    }

    //- HandleStableChangeSlot
    if (_stableChangeSlotCallback.IsReady())
    {
        uint8 param = _stableChangeSlotCallback.GetParam();
        _stableChangeSlotCallback.GetResult(result);
        HandleStableChangeSlotCallback(result, param);
        _stableChangeSlotCallback.FreeResult();
    }
}

void WorldSession::InitWarden(BigNumber* k, std::string os)
{
    if (os == "Win")
    {
        _warden = new WardenWin();
        _warden->Init(this, k);
    }
    else if (os == "OSX")
    {
        // Disabled as it is causing the client to crash
        // _warden = new WardenMac();
        // _warden->Init(this, k);
    }
}
ObjectGuid WorldSession::GetAccountGUID() const
{ 
    return ObjectGuid::Create<HighGuid::WowAccount>(GetAccountId());
}

ObjectGuid  WorldSession::GetBattlenetAccountGUID() const
{ 
    return ObjectGuid::Create<HighGuid::BNetAccount>(GetBattlenetAccountId());
}

PacketSendEvent::~PacketSendEvent()
{
    delete m_packet;
}

bool PacketSendEvent::Execute(uint64 , uint32)
{
    m_owner->SendDirectMessage(m_packet);
    return true;
}

void PacketSendEvent::Schedule()
{
    m_owner->m_Events.AddEvent(this, m_owner->m_Events.CalculateTime(m_delay));
}
