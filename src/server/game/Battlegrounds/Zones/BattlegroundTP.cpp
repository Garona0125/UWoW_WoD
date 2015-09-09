/*
 * Copyright (C) 2008-2013 TrinityCore <http://www.trinitycore.org/>
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

#include "Battleground.h"
#include "BattlegroundTP.h"
#include "Creature.h"
#include "GameObject.h"
#include "Language.h"
#include "Object.h"
#include "ObjectMgr.h"
#include "BattlegroundMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldPacket.h"

BattlegroundTP::BattlegroundTP()
{
    /// Resize BG miscellaneous
    BgObjects.resize(BG_TP_OBJECT_MAX);
    BgCreatures.resize(BG_CREATURES_MAX_TP);

    /// Start Messages Initialization
    StartMessageIds[BG_STARTING_EVENT_FIRST]  = LANG_BG_TP_START_TWO_MINUTES;
    StartMessageIds[BG_STARTING_EVENT_SECOND] = LANG_BG_TP_START_ONE_MINUTE;
    StartMessageIds[BG_STARTING_EVENT_THIRD]  = LANG_BG_TP_START_HALF_MINUTE;
    StartMessageIds[BG_STARTING_EVENT_FOURTH] = LANG_BG_TP_HAS_BEGUN;

    /// Misc Stuff - See declaration for more info
    _minutesElapsed = 0;
    _flagSpellForceTimer = 0;
    _bothFlagsKept = false;
    _flagDebuffState = 0;
}

BattlegroundTP::~BattlegroundTP()
{
}

void BattlegroundTP::PostUpdateImpl(uint32 diff)
{
    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        /// Total time it's supposed to be 20 (game) + 2 (when the battle begins and doors are closed) minutes
        if (GetElapsedTime() >= 22 * MINUTE * IN_MILLISECONDS) ///< End of game - Verify score
        {
            if (m_TeamScores[TEAM_ALLIANCE] == 0)
            {
                if (m_TeamScores[TEAM_HORDE] == 0)  ///< No one scored
                    EndBattleground(WINNER_NONE);
                else                                ///< Horde has more points (Alliance has 0) - Horde Wins
                    EndBattleground(HORDE);
            }
            else if (m_TeamScores[TEAM_HORDE] == 0) ///< Alliance has more than 0, Horde has 0 so Alliance Wins
                EndBattleground(ALLIANCE);
            else if (m_TeamScores[TEAM_HORDE] == m_TeamScores[TEAM_ALLIANCE]) ///< Team Score equal and both scored at least once so winer is the last team witch captured the flag
                EndBattleground(_lastFlagCaptureTeam);
            else if (m_TeamScores[TEAM_HORDE] > m_TeamScores[TEAM_ALLIANCE])  // Last but not least, check who has the higher score
                EndBattleground(HORDE);
            else
                EndBattleground(ALLIANCE);
        }
        // First Update is needed only after 1 minute in battle
        else if (GetElapsedTime() > uint32((_minutesElapsed + 1) * MINUTE * IN_MILLISECONDS) +  2 * MINUTE * IN_MILLISECONDS)
        {
            ++_minutesElapsed;
            UpdateWorldState(BG_TP_STATE_TIMER, 20 - _minutesElapsed); //< Time remaining showed on top of the screen via world state
        }

        /// Flags state update:
        for (uint8 team = TEAM_ALLIANCE; team <= TEAM_HORDE; ++team)
        {
            switch(_flagState[team])
            {
                case BG_TP_FLAG_STATE_WAIT_RESPAWN: ///< Flag is nowhere - Is in this situation if a team captured the flag
                {
                    _flagsTimer -= diff; ///< Update Flag Timer with the time between the last server update and now
                    if (_flagsTimer <= 0)
                    {
                        _flagsTimer = 0;
                        RespawnFlag(team, true);
                        UpdateWorldState(BG_TP_FLAG_UNKNOWN, 0); ///< From sniffs
                    }
                    break;
                }
                case BG_TP_FLAG_STATE_ON_GROUND: ///< Flag is on the ground waiting someone to click
                {
                    _flagsDropTimer[team] -= diff; ///< Update Flag Timer with the time between the last server update and now
                    if (_flagsDropTimer[team] < 0)
                    {
                        _flagsDropTimer[team] = 0;

                        /// Respawn flag
                        RespawnFlag(team, false);

                        /// Delete flag from ground
                        if (GameObject* obj = GetBgMap()->GetGameObject(_droppedFlagGUID[team]))
                                obj->Delete();
                            else
                                sLog->outError(LOG_FILTER_BATTLEGROUND, "BattlegroundTP: An error has occurred in PostUpdateImpl: Unknown dropped flag GUID: %u", _droppedFlagGUID[team].GetCounter());

                        _droppedFlagGUID[team].Clear();

                        /// If both flags are kept and 1 of cariers dies and no one clicked on flag set _bothflagskept = false
                        if (_bothFlagsKept)
                        {
                            _bothFlagsKept = false;
                            _flagDebuffState = 0;
                            _flagSpellForceTimer = 0;
                        }
                    }
                    break;
                }

            }
        }


        /**
        * If both flags are kept, for more than 3 minutes, both cariers should recive an course (Focused Assault).
        * Every minute afterward, an additional stack will be applied.
        * After 7 minutes, Brutal Assault will be applied in place of Focused Assault.
        */
        if (_bothFlagsKept)
        {
            _flagSpellForceTimer += diff; /// Update the timer

            if (_flagSpellForceTimer >= (1 + _flagDebuffState) * MINUTE* IN_MILLISECONDS && _flagDebuffState < 10)///< More than 1 minutes and amount of stacks from debuff lower than 10
            {
                _flagDebuffState = _flagSpellForceTimer / MINUTE / IN_MILLISECONDS;
                _flagSpellForceTimer += 30 * IN_MILLISECONDS;

                if (_flagDebuffState <= 5) ///< Cast on player Focused Assaultupposed to maintain number of stacks for debuff
                {
                    for (uint8 team = TEAM_ALLIANCE; team <= TEAM_HORDE; ++team)
                    {
                        if (Player* player = ObjectAccessor::FindPlayer(_flagKeepers[team]))
                            player->CastSpell(player, TP_SPELL_FOCUSED_ASSAULT, true);
                        else
                            sLog->outError(LOG_FILTER_BATTLEGROUND, "BattlegroundTP: Can't find flag keeper with GUID: %u, in team: %s", _flagKeepers[team], team == TEAM_ALLIANCE ? "ALLIANCE" : "HORDE");
                    }
                }
                else if (_flagDebuffState == 6) ///< Moment when you change the stacks
                {
                    for (uint8 team = TEAM_ALLIANCE; team <= TEAM_HORDE; ++team)
                    {
                        if (Player* player = ObjectAccessor::FindPlayer(_flagKeepers[team]))
                        {
                            player->RemoveAurasDueToSpell(TP_SPELL_FOCUSED_ASSAULT); ///< Remove aura from Focused Assault
                            player->CastCustomSpell(TP_SPELL_BRUTAL_ASSAULT, SPELLVALUE_AURA_STACK, _flagDebuffState, player);
                        }
                        else
                            sLog->outError(LOG_FILTER_BATTLEGROUND, "BattlegroundTP: Can't find flag keeper with GUID: %u, in team: %s", _flagKeepers[team], team == TEAM_ALLIANCE ? "ALLIANCE" : "HORDE");
                    }
                }
                else //< Continue to cast brutal Assault on target
                {
                    for (uint8 team = TEAM_ALLIANCE; team <= TEAM_HORDE; ++team)
                    {
                        if (Player* player = ObjectAccessor::FindPlayer(_flagKeepers[team]))
                            player->CastSpell(player, TP_SPELL_BRUTAL_ASSAULT, true);
                        else
                            sLog->outError(LOG_FILTER_BATTLEGROUND, "BattlegroundTP: Can't find flag keeper with GUID: %u, in team: %s", _flagKeepers[team], team == TEAM_ALLIANCE ? "ALLIANCE" : "HORDE");
                    }
                }
            }
        }
    }
}

void BattlegroundTP::AddPlayer(Player* player)
{
    ///Create score for player
    AddPlayerScore(player->GetGUID(), new BattlegroundTPScore);
    Battleground::AddPlayer(player);
}


void BattlegroundTP::StartingEventCloseDoors()
{
    for (uint32 i = BG_TP_OBJECT_DOOR_A_1; i <= BG_TP_OBJECT_DOOR_H_3; ++i)
    {
        DoorClose(i);
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
    }
    for (uint32 i = BG_TP_OBJECT_A_FLAG; i <= BG_TP_OBJECT_BERSERKBUFF_2; ++i)
        SpawnBGObject(i, RESPAWN_ONE_DAY);
}

void BattlegroundTP::StartingEventOpenDoors()
{
    for (uint32 i = BG_TP_OBJECT_DOOR_A_1; i <= BG_TP_OBJECT_DOOR_A_3; ++i)
        DoorOpen(i);
    for (uint32 i = BG_TP_OBJECT_DOOR_H_1; i <= BG_TP_OBJECT_DOOR_H_3; ++i)
        DoorOpen(i);

    for (uint32 i = BG_TP_OBJECT_A_FLAG; i <= BG_TP_OBJECT_BERSERKBUFF_2; ++i)
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);

    // players joining later are not eligibles
    StartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, TP_EVENT_START_BATTLE);
    StartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT2, TP_EVENT_START_BATTLE);

    UpdateWorldState(BG_TP_STATE_TIMER_ACTIVE, 1);
    UpdateWorldState(BG_TP_STATE_TIMER, 20);
}

bool BattlegroundTP::SetupBattleground()
{
    if (!AddObject(BG_TP_OBJECT_A_FLAG, BG_OBJECT_A_FLAG_TP_ENTRY, 2117.637f, 191.6823f, 44.05199f, 6.021387f, 0, 0, 0.9996573f, 0.02617699f, BG_TP_FLAG_RESPAWN_TIME/1000)
        || !AddObject(BG_TP_OBJECT_H_FLAG, BG_OBJECT_H_FLAG_TP_ENTRY, 1578.337f, 344.0451f, 2.418409f, 2.792518f, 0, 0, 0.008726535f, 0.9999619f, BG_TP_FLAG_RESPAWN_TIME/1000)
        /// Buffs
        || !AddObject(BG_TP_OBJECT_SPEEDBUFF_1, BG_OBJECTID_SPEEDBUFF_ENTRY, 1544.55f, 303.852f, 0.692371f, 6.265733f, 0, 0, 0.7313537f, -0.6819983f, BUFF_RESPAWN_TIME) // id 179899 in sniff
        || !AddObject(BG_TP_OBJECT_SPEEDBUFF_2, BG_OBJECTID_SPEEDBUFF_ENTRY, 2175.866f, 226.6215f, 43.76288f, 2.600535f, 0, 0, 0.7313537f, 0.6819984f, BUFF_RESPAWN_TIME)
        || !AddObject(BG_TP_OBJECT_REGENBUFF_1, BG_OBJECTID_REGENBUFF_ENTRY, 1754.163f, 242.125f, -14.13157f, 1.151916f, 0, 0, 0.1305263f, -0.9914448f, BUFF_RESPAWN_TIME) // id 179906 in sniff
        || !AddObject(BG_TP_OBJECT_REGENBUFF_2, BG_OBJECTID_REGENBUFF_ENTRY, 1951.18f, 383.795f, -10.5257f, 4.06662f, 0, 0, 0.333807f, -0.9426414f, BUFF_RESPAWN_TIME)
        || !AddObject(BG_TP_OBJECT_BERSERKBUFF_1, BG_OBJECTID_BERSERKERBUFF_ENTRY, 1932.832f, 226.7917f, -17.05979f, 2.44346f, 0, 0, 0.5591929f, 0.8290376f, BUFF_RESPAWN_TIME)
        || !AddObject(BG_TP_OBJECT_BERSERKBUFF_2, BG_OBJECTID_BERSERKERBUFF_ENTRY, 1737.566f, 435.8455f, -8.086342f, 5.515242f, 0, 0, 0.9396926f, -0.3420201f, BUFF_RESPAWN_TIME) // id 179907 in sniff
        /// Alliance gates
        || !AddObject(BG_TP_OBJECT_DOOR_A_1, BG_OBJECT_DOOR_A_1_TP_ENTRY, 2135.525f, 218.926f, 43.60946f, 5.750861f, 0, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_TP_OBJECT_DOOR_A_2, BG_OBJECT_DOOR_A_2_TP_ENTRY, 2156.0f, 219.2059f, 43.6256f, 2.609261f, 0, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_TP_OBJECT_DOOR_A_3, BG_OBJECT_DOOR_A_3_TP_ENTRY, 2118.088f, 154.6754f, 43.57089f, 2.609261f, 0, 0, 0, 0, RESPAWN_IMMEDIATELY)
        /// Horde gates
        || !AddObject(BG_TP_OBJECT_DOOR_H_1, BG_OBJECT_DOOR_H_1_TP_ENTRY, 1556.656f, 314.7127f, 1.589001f, 6.178466f, 0, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_TP_OBJECT_DOOR_H_2, BG_OBJECT_DOOR_H_2_TP_ENTRY, 1574.605f, 321.2421f, 1.58989f, 6.178466f, 0, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_TP_OBJECT_DOOR_H_3, BG_OBJECT_DOOR_H_3_TP_ENTRY, 1558.088f, 372.7654f, 1.723727f, 6.178466f, 0, 0, 0, 0, RESPAWN_IMMEDIATELY)
       )
    {
        sLog->outError(LOG_FILTER_GENERAL, "BattegroundTP: Failed to spawn some objects. Battleground not created!");
        return false;
    }

    /// Load spirit guides
    for (uint32 i = TP_GRAVEYARD_START_ALLIANCE; i < TP_MAX_GRAVEYARDS; ++i)
    {
        WorldSafeLocsEntry const* grave = sWorldSafeLocsStore.LookupEntry(BG_TP_GraveyardIds[i]);

        if (grave)
        {
            uint8 team = i % 2; ///< If 0 team == TEAM_ALLIANCE else TEAM_HORDE
            if (!AddSpiritGuide(team == TEAM_ALLIANCE ? TP_SPIRIT_ALLIANCE : TP_SPIRIT_HORDE, grave->Loc.X, grave->Loc.Y, grave->Loc.Z, team == TEAM_ALLIANCE ? M_PI : 0, team == TEAM_ALLIANCE ? ALLIANCE : HORDE))
            {
                sLog->outError(LOG_FILTER_GENERAL, "BatteGroundTP: Failed to spawn spirit guide id: %u. Battleground not created!", grave->ID);
                return false;
            }
        }
        else
        {
                sLog->outError(LOG_FILTER_GENERAL, "BatteGroundTP: Failed to load spirit guide. Battleground not created!");
                return false;
        }
    }

    return true;
}

void BattlegroundTP::Reset()
{
    /// Call Main Battleground reset
    Battleground::Reset();

    /// Start reset for Battleground specific miscelanouse
    for (uint8 team = TEAM_ALLIANCE; team <= TEAM_HORDE; ++team)
    {
        /// Unbind all flag stuff
        _flagKeepers[team].Clear();
        _droppedFlagGUID[team].Clear();
        _flagsDropTimer[team] = 0;
        _flagState[team] = BG_TP_FLAG_STATE_ON_BASE;

        /// Reset score
        m_TeamScores[team] = 0;
    }

    _flagsTimer = 0;
}

void BattlegroundTP::FillInitialWorldStates(WorldPacket& data)
{
    /// Show how many flags had been captured
    FillInitialWorldState(data, BG_TP_FLAG_CAPTURES_ALLIANCE, m_TeamScores[TEAM_ALLIANCE]);
    FillInitialWorldState(data, BG_TP_FLAG_CAPTURES_HORDE, m_TeamScores[TEAM_HORDE]);

    /// Show MAX number of flags (x/3)
    FillInitialWorldState(data, BG_TP_FLAG_CAPTURES_MAX, BG_TP_MAX_TEAM_SCORE);

    /// Next Stuff showed only if BG is in progress
    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        /// Show Flag state - if flag is on player / ground / in base
        for (uint8 team = TEAM_ALLIANCE; team <= TEAM_HORDE; ++team)
        {
            switch(_flagState[team])
            {
                case BG_TP_FLAG_STATE_ON_GROUND:
                    FillInitialWorldState(data, BG_TP_FLAG_UNK_ALLIANCE + team, -1);
                    FillInitialWorldState(data, BG_TP_FLAG_STATE_HORDE + team, 3); ///< Show if team's flag is carried
                    break;
                case BG_TP_FLAG_STATE_ON_PLAYER:
                    FillInitialWorldState(data, BG_TP_FLAG_UNK_ALLIANCE + team, 1);
                    FillInitialWorldState(data, BG_TP_FLAG_STATE_HORDE + team, 2); ///< Show if team's flag is carried
                    break;
                default: ///< In Base
                    FillInitialWorldState(data, BG_TP_FLAG_UNK_ALLIANCE + team, 0);
                    FillInitialWorldState(data, BG_TP_FLAG_STATE_HORDE + team, 1); ///< Show if team's flag is carried
                    break;
            }
        }

        /// Show Timer
        FillInitialWorldState(data, BG_TP_STATE_TIMER_ACTIVE, 1);
        FillInitialWorldState(data, BG_TP_STATE_TIMER, 20 - _minutesElapsed);
    }
    else
    {
        /// No timer for begining
        FillInitialWorldState(data, BG_TP_STATE_TIMER_ACTIVE, 0);

        /// Just show the maxscore and actual score (0)
        FillInitialWorldState(data, BG_TP_FLAG_UNK_ALLIANCE, 0);
        FillInitialWorldState(data, BG_TP_FLAG_UNK_HORDE, 0);
        FillInitialWorldState(data, BG_TP_FLAG_STATE_HORDE, 1);
        FillInitialWorldState(data, BG_TP_FLAG_STATE_ALLIANCE, 1);
    }
}

void BattlegroundTP::EndBattleground(uint32 winner)
{
    /// If BG ends with equal flag captured (draw) and both flags are kept the debuffs + flag aura stays on player, and it shouldn't
    for (uint8 team = TEAM_ALLIANCE; team <= TEAM_HORDE; ++team)
    {
        if (Player* player = ObjectAccessor::FindPlayer(_flagKeepers[team]))
        {
            player->RemoveAurasDueToSpell(TP_SPELL_FOCUSED_ASSAULT);
            player->RemoveAurasDueToSpell(TP_SPELL_BRUTAL_ASSAULT);
        }
    }

    /// Update a lot of world states
    UpdateWorldState(BG_TP_FLAG_UNK_ALLIANCE, 0);
    UpdateWorldState(BG_TP_FLAG_UNK_HORDE, 0);
    UpdateWorldState(BG_TP_FLAG_STATE_ALLIANCE, 1);
    UpdateWorldState(BG_TP_FLAG_STATE_HORDE, 1);
    UpdateWorldState(BG_TP_STATE_TIMER_ACTIVE, 0);

    uint32 realWinner = WINNER_NONE;
    if (winner == TEAM_ALLIANCE)
        realWinner = ALLIANCE;
    else if (winner == TEAM_HORDE)
        realWinner = HORDE;
    else if (winner > WINNER_NONE)
        realWinner = winner;

    Battleground::EndBattleground(realWinner);
}

void BattlegroundTP::HandleKillPlayer(Player* player, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    /// Call event player droped flag
    if(player->HasAura(BG_TP_SPELL_HORDE_FLAG) || player->HasAura(BG_TP_SPELL_ALLIANCE_FLAG))
        EventPlayerDroppedFlag(player);

    Battleground::HandleKillPlayer(player, killer);
}

void BattlegroundTP::HandleAreaTrigger(Player* player, uint32 trigger)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    switch (trigger)
    {
        case 5904:                                          // Alliance Flag spawn
            if (_flagState[TEAM_HORDE] && !_flagState[TEAM_ALLIANCE])
                if (_flagKeepers[TEAM_HORDE] == player->GetGUID())
                    EventPlayerCapturedFlag(player);
            break;
        case 5905:                                          // Horde Flag spawn
            if (_flagState[TEAM_ALLIANCE] && !_flagState[TEAM_HORDE])
                if (_flagKeepers[TEAM_ALLIANCE] == player->GetGUID())
                    EventPlayerCapturedFlag(player);
            break;
        case 5908:                                          // Horde Tower
        case 5909:                                          // Twin Peak House big
        case 5910:                                          // Horde House
        case 5911:                                          // Twin Peak House small
        case 5914:                                          // Alliance Start right
        case 5916:                                          // Alliance Start
        case 5917:                                          // Alliance Start left
        case 5918:                                          // Horde Start
        case 5920:                                          // Horde Start Front entrance
        case 5921:                                          // Horde Start left Water channel
            break;
        default:
            Battleground::HandleAreaTrigger(player, trigger);
            break;
    }
}

void BattlegroundTP::UpdatePlayerScore(Player* player, uint32 type, uint32 value, bool doAddHonor)
{
    /// Find player in map
    BattlegroundScoreMap::iterator itr = PlayerScores.find(player->GetGUID());
    if (itr == PlayerScores.end()) ///< Player not found
        return;

    /// Update Achievements + scores
    switch (type)
    {
        case SCORE_FLAG_CAPTURES:
            ((BattlegroundTPScore*)itr->second)->FlagCaptures += value;
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE, TP_OBJECTIVE_CAPTURE_FLAG, 1);
            break;
        case SCORE_FLAG_RETURNS:
            ((BattlegroundTPScore*)itr->second)->FlagReturns += value;
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE, TP_OBJECTIVE_RETURN_FLAG, 1);
            break;
        default: ///< else only count another kill
            Battleground::UpdatePlayerScore(player, type, value, doAddHonor);
            break;
    }
}

void BattlegroundTP::UpdateTeamScore(uint32 team)
{
    ///< After a capture update score
    if (team == TEAM_ALLIANCE)
        UpdateWorldState(BG_TP_FLAG_CAPTURES_ALLIANCE, m_TeamScores[team]);
    else
        UpdateWorldState(BG_TP_FLAG_CAPTURES_HORDE, m_TeamScores[team]);
}

/**
* Next method finds the right graveyard for player to resurect
* As 4.1: The right graveyard is not the nearest one
* So:
* Players will now only spawn at their base graveyard when they die in the enemy base.
* Defending players will respawn at the middle graveyard.
* Midfield players will respawn at the middle graveyard.
* Attacking players will respawn at their base graveyard.
* It's not the best "name" for this method
*/
WorldSafeLocsEntry const* BattlegroundTP::GetClosestGraveYard(Player* player)
{
    if (!player)
        return NULL;

    uint8 team = player->GetTeamId();

    if (GetStatus() != STATUS_IN_PROGRESS) ///< If battle didn't start yet and player is death (unprobably) revive in flagroom
        return sWorldSafeLocsStore.LookupEntry(BG_TP_GraveyardIds[TP_GRAVEYARD_FLAGROOM_ALLIANCE + team]);

    /// Check if player if is closer to the enemy base than the center
    WorldSafeLocsEntry const* grave_enemy_base = sWorldSafeLocsStore.LookupEntry(BG_TP_GraveyardIds[TP_GRAVEYARD_FLAGROOM_ALLIANCE + (team ^ 1)]);
    WorldSafeLocsEntry const* grave_enemy_middle = sWorldSafeLocsStore.LookupEntry(BG_TP_GraveyardIds[TP_GRAVEYARD_MIDDLE_ALLIANCE + (team ^ 1)]);

    if (player->GetDistance2d(grave_enemy_base->Loc.X, grave_enemy_base->Loc.Y) < player->GetDistance2d(grave_enemy_middle->Loc.X, grave_enemy_middle->Loc.Y))
        return sWorldSafeLocsStore.LookupEntry(BG_TP_GraveyardIds[TP_GRAVEYARD_START_ALLIANCE + team]);

    return sWorldSafeLocsStore.LookupEntry(BG_TP_GraveyardIds[TP_GRAVEYARD_MIDDLE_ALLIANCE + team]);
}


void BattlegroundTP::EventPlayerDroppedFlag(Player* Source)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
    {
        // if not running, do not cast things at the dropper player (prevent spawning the "dropped" flag), neither send unnecessary messages
        // just take off the aura
        if (Source->GetTeam() == ALLIANCE)
        {
            if (!this->IsHordeFlagPickedup())
                return;

            if (GetFlagPickerGUID(TEAM_HORDE) == Source->GetGUID())
            {
                SetHordeFlagPicker(ObjectGuid::Empty);
                Source->RemoveAurasDueToSpell(BG_TP_SPELL_HORDE_FLAG);
            }
        }
        else
        {
            if (!this->IsAllianceFlagPickedup())
                return;

            if (GetFlagPickerGUID(TEAM_ALLIANCE) == Source->GetGUID())
            {
                SetAllianceFlagPicker(ObjectGuid::Empty);
                Source->RemoveAurasDueToSpell(BG_TP_SPELL_ALLIANCE_FLAG);
            }
        }
        return;
    }

    bool set = false;

    if (Source->GetTeam() == ALLIANCE)
    {
        if (!IsHordeFlagPickedup())
            return;

        if (GetFlagPickerGUID(TEAM_HORDE) == Source->GetGUID())
        {
            SetHordeFlagPicker(ObjectGuid::Empty);
            Source->RemoveAurasDueToSpell(BG_TP_SPELL_HORDE_FLAG);
            _flagState[TEAM_HORDE] = BG_TP_FLAG_STATE_ON_GROUND;
            Source->CastSpell(Source, BG_TP_SPELL_HORDE_FLAG_DROPPED, true);
            set = true;
        }
    }
    else
    {
        if (!IsAllianceFlagPickedup())
            return;
        if (GetFlagPickerGUID(TEAM_ALLIANCE) == Source->GetGUID())
        {
            SetAllianceFlagPicker(ObjectGuid::Empty);
            Source->RemoveAurasDueToSpell(BG_TP_SPELL_ALLIANCE_FLAG);
            _flagState[TEAM_ALLIANCE] = BG_TP_FLAG_STATE_ON_GROUND;
            Source->CastSpell(Source, BG_TP_SPELL_ALLIANCE_FLAG_DROPPED, true);
            set = true;
        }
    }

    if (set)
    {
        Source->CastSpell(Source, SPELL_RECENTLY_DROPPED_FLAG, true);
        UpdateFlagState(Source->GetTeam(), 1);

        if (Source->GetTeam() == ALLIANCE)
        {
            SendMessageToAll(LANG_BG_TP_DROPPED_HF, CHAT_MSG_BG_SYSTEM_HORDE, Source);
            UpdateWorldState(BG_TP_FLAG_UNK_HORDE, uint32(-1));
        }
        else
        {
            SendMessageToAll(LANG_BG_TP_DROPPED_AF, CHAT_MSG_BG_SYSTEM_ALLIANCE, Source);
            UpdateWorldState(BG_TP_FLAG_UNK_ALLIANCE, uint32(-1));
        }

        _flagsDropTimer[GetTeamIndexByTeamId(Source->GetTeam()) ? 0 : 1] = BG_TP_FLAG_DROP_TIME;
    }
}

void BattlegroundTP::EventPlayerClickedOnFlag(Player* source, GameObject* target_obj)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    uint8 team = source->GetTeamId();

    if (source->IsWithinDistInMap(target_obj, 10)) ///< Check if target is in distance with flag
    {
        switch (_flagState[team ^ 1]) ///< Check opposite team faction state
        {
            case BG_TP_FLAG_STATE_ON_BASE: ///< If opposite team has flag in base
            {
                if (BgObjects[BG_TP_OBJECT_A_FLAG + (team ^ 1)] == target_obj->GetGUID()) ///< If clicked object is what we want
                {
                    ///Dispawn BGObject
                    SpawnBGObject(team == TEAM_ALLIANCE ? BG_TP_OBJECT_H_FLAG : BG_TP_OBJECT_A_FLAG, RESPAWN_ONE_DAY);

                    /// Update flag state + world state
                    UpdateFlagState(team ^ 1, BG_TP_FLAG_STATE_ON_PLAYER, source->GetGUID());

                    /// "Pick up the flag"
                    source->CastSpell(source, team == TEAM_ALLIANCE ? BG_TP_SPELL_HORDE_FLAG : BG_TP_SPELL_ALLIANCE_FLAG , true);

                    /// Start achievement criteria
                    source->GetAchievementMgr().StartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_SPELL_TARGET2, team == TEAM_ALLIANCE ? BG_TP_SPELL_HORDE_FLAG_PICKED : BG_TP_SPELL_ALLIANCE_FLAG_PICKED);

                    /// Verify if both flag are carried
                    if (!_flagKeepers[team].IsEmpty())
                        _bothFlagsKept = true;

                    /// Send message to all players + Play sound
                    SendMessageToAll(team == TEAM_ALLIANCE ? LANG_BG_TP_PICKEDUP_HF : LANG_BG_TP_PICKEDUP_AF, team == TEAM_ALLIANCE ? CHAT_MSG_BG_SYSTEM_ALLIANCE : CHAT_MSG_BG_SYSTEM_HORDE, source);
                    PlaySoundToAll(team == TEAM_ALLIANCE ? BG_TP_SOUND_HORDE_FLAG_PICKED_UP : BG_TP_SOUND_ALLIANCE_FLAG_PICKED_UP);
                }
                break;
            }
            case BG_TP_FLAG_STATE_ON_GROUND:
            {
                if (_droppedFlagGUID[team ^ 1] == target_obj->GetGUID()) ///< Check opposite team faction flag
                {
                    /// Reapply flag on target
                    source->CastSpell(source, team == TEAM_ALLIANCE? BG_TP_SPELL_HORDE_FLAG : BG_TP_SPELL_ALLIANCE_FLAG, true);

                    /// Update flag state
                    UpdateFlagState(team ^ 1, BG_TP_FLAG_STATE_ON_PLAYER, source->GetGUID());

                    /// Reapply debuff
                    if (_flagDebuffState  && _flagDebuffState < 6)
                        source->CastCustomSpell(TP_SPELL_FOCUSED_ASSAULT, SPELLVALUE_AURA_STACK, _flagDebuffState, source);
                    else if (_flagDebuffState >= 6)
                        source->CastCustomSpell(TP_SPELL_BRUTAL_ASSAULT, SPELLVALUE_AURA_STACK, _flagDebuffState, source);

                    /// Reset flag on the ground counter
                    _flagsDropTimer[team ^ 1] = 0;

                    /// Announce players
                    PlaySoundToAll(team == TEAM_ALLIANCE ? BG_TP_SOUND_HORDE_FLAG_PICKED_UP : BG_TP_SOUND_ALLIANCE_FLAG_PICKED_UP);
                    SendMessageToAll(team == TEAM_ALLIANCE ? LANG_BG_TP_PICKEDUP_HF : LANG_BG_TP_PICKEDUP_AF, team == TEAM_ALLIANCE ? CHAT_MSG_BG_SYSTEM_ALLIANCE : CHAT_MSG_BG_SYSTEM_HORDE, source);
                }
                break;
            }
        }

        /// If player from same side click on the flag that stand on ground return to base
        if (_flagState[team] == BG_TP_FLAG_STATE_ON_GROUND &&
            _droppedFlagGUID[team] == target_obj->GetGUID())
        {
            /// Update world states
            UpdateFlagState(team, BG_TP_FLAG_STATE_WAIT_RESPAWN);
            UpdatePlayerScore(source, SCORE_FLAG_RETURNS, 1);

            /// Respawn Flag
            RespawnFlag(team, false);

            /// Announce players
            PlaySoundToAll(BG_TP_SOUND_FLAG_RETURNED);
            SendMessageToAll(team == TEAM_ALLIANCE ? LANG_BG_TP_RETURNED_AF: LANG_BG_TP_RETURNED_HF, team == TEAM_ALLIANCE ? CHAT_MSG_BG_SYSTEM_ALLIANCE : CHAT_MSG_BG_SYSTEM_HORDE, source);

            /// Reset both flags things
            _bothFlagsKept = false;
            _flagSpellForceTimer = 0;
        }
    }

   source->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);
   SendFlagsPositionsUpdate(FLAGS_UPDATE);
}

void BattlegroundTP::EventPlayerCapturedFlag(Player* source)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;


    uint8 team = source->GetTeamId();

    source->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);
    _flagDebuffState = 0;

    /// Dispawn Flags
    SpawnBGObject(BG_TP_OBJECT_A_FLAG, RESPAWN_ONE_DAY);
    SpawnBGObject(BG_TP_OBJECT_H_FLAG, RESPAWN_ONE_DAY);

    /// Unaura assaults debuff (if there are)
        source->RemoveAurasDueToSpell(TP_SPELL_FOCUSED_ASSAULT);
        source->RemoveAurasDueToSpell(TP_SPELL_BRUTAL_ASSAULT);

    /// Reward flag capture with 2x honorable kills
    RewardHonorToTeam(GetBonusHonorFromKill(2), source->GetTeam());

    AddPoint(team == TEAM_ALLIANCE ? ALLIANCE : HORDE);

    /// Update scores + flag state + count flag caputured to carrier
    UpdateTeamScore(team);
    UpdateWorldState(BG_TP_FLAG_UNKNOWN, -1); ///< From sniffs
    UpdateFlagState(team ^ 1, BG_TP_FLAG_STATE_WAIT_RESPAWN);
    UpdatePlayerScore(source, SCORE_FLAG_CAPTURES, 1);
    UpdateWorldState(BG_TP_STATE_UNKNOWN, 1); ///< From sniffs

    /// Set last team that captured flag
    _lastFlagCaptureTeam = source->GetTeam();

    /// Play sound + Send message to all
    PlaySoundToAll(team == TEAM_ALLIANCE ? BG_TP_SOUND_FLAG_CAPTURED_ALLIANCE : BG_TP_SOUND_FLAG_CAPTURED_HORDE);
    SendMessageToAll(team == TEAM_ALLIANCE ? LANG_BG_TP_CAPTURED_HF : LANG_BG_TP_CAPTURED_AF, team == TEAM_ALLIANCE ? CHAT_MSG_BG_SYSTEM_ALLIANCE : CHAT_MSG_BG_SYSTEM_HORDE, source);

    /// IMPORTANT: Do not remove aura before flag update! Makes the flag spawn like is dropped
    source->RemoveAurasDueToSpell(team == TEAM_ALLIANCE ? BG_TP_SPELL_HORDE_FLAG : BG_TP_SPELL_ALLIANCE_FLAG);

    /// Last but not least verify if score is max to end
    if (m_TeamScores[team] == BG_TP_MAX_TEAM_SCORE)
        EndBattleground(team);
    else
        _flagsTimer = BG_TP_FLAG_RESPAWN_TIME;

    SendFlagsPositionsUpdate(FLAGS_UPDATE);
}

void BattlegroundTP::RemovePlayer(Player* player, ObjectGuid guid, uint32 /* team */)
{
    if (!player)
        return;

    uint8 team = player->GetTeamId();

    if (_flagKeepers[team ^ 1] == guid)
    {
        /// First of all respawn flag and second unaura player, if is vice versa player will drop the flag and spawn it on ground.
        RespawnFlag(team ^ 1, false);
        player->RemoveAurasDueToSpell(team == TEAM_ALLIANCE? BG_TP_SPELL_HORDE_FLAG : BG_TP_SPELL_ALLIANCE_FLAG);

        if (_bothFlagsKept)
        {
            /// Remove aura from both of them
            for (uint8 teamTemp = TEAM_ALLIANCE; teamTemp <= TEAM_HORDE; ++teamTemp)
            {
                if (Player* pl = ObjectAccessor::FindPlayer(_flagKeepers[team]))
                {
                    if (_flagDebuffState && _flagDebuffState < 6)
                        pl->RemoveAurasDueToSpell(TP_SPELL_FOCUSED_ASSAULT);
                    else if (_flagDebuffState >= 6)
                        pl->RemoveAurasDueToSpell(TP_SPELL_BRUTAL_ASSAULT);
                }
                else
                    sLog->outError(LOG_FILTER_BATTLEGROUND, "BattlegroundTP: An error has occurred in RemovePlayer: player with GUID: %u haven't been found. (_bothflagsKept is TRUE).", _flagKeepers[team]);
            }

            /// Reset both flags things
            _bothFlagsKept = false;
            _flagDebuffState = 0;
            _flagSpellForceTimer = 0;
        }
    }
}

void BattlegroundTP::RespawnFlag(uint32 team, bool captured)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    if (captured)
    {
        SpawnBGObject(BG_TP_OBJECT_H_FLAG, RESPAWN_IMMEDIATELY);
        SpawnBGObject(BG_TP_OBJECT_A_FLAG, RESPAWN_IMMEDIATELY);
        SendMessageToAll(LANG_BG_TP_F_PLACED, CHAT_MSG_BG_SYSTEM_NEUTRAL);
    }
    else
    {
        SpawnBGObject(BG_TP_OBJECT_A_FLAG + team, RESPAWN_IMMEDIATELY);
        SendMessageToAll(LANG_BG_TP_ALLIANCE_FLAG_RESPAWNED + team, CHAT_MSG_BG_SYSTEM_NEUTRAL);
    }

    PlaySoundToAll(BG_TP_SOUND_FLAGS_RESPAWNED);

    UpdateFlagState(team, BG_TP_FLAG_STATE_ON_BASE);
}

void BattlegroundTP::UpdateFlagState(uint32 team, uint32 value, ObjectGuid flagKeeperGUID)
{
    switch (value)
    {
        /// Values from sniffs
        case BG_TP_FLAG_STATE_WAIT_RESPAWN:
            UpdateWorldState(team == TEAM_ALLIANCE ? BG_TP_FLAG_UNK_ALLIANCE : BG_TP_FLAG_UNK_HORDE, 0);
            UpdateWorldState(team == TEAM_ALLIANCE ? BG_TP_FLAG_STATE_HORDE : BG_TP_FLAG_STATE_ALLIANCE, 1);
            break;
        case BG_TP_FLAG_STATE_ON_BASE:
            UpdateWorldState(team == TEAM_ALLIANCE ? BG_TP_FLAG_UNK_ALLIANCE : BG_TP_FLAG_UNK_HORDE, 0);
            UpdateWorldState(team == TEAM_ALLIANCE ? BG_TP_FLAG_STATE_HORDE : BG_TP_FLAG_STATE_ALLIANCE, 1);
            break;
        case BG_TP_FLAG_STATE_ON_GROUND:
            UpdateWorldState(team == TEAM_ALLIANCE ? BG_TP_FLAG_UNK_ALLIANCE : BG_TP_FLAG_UNK_HORDE, -1);
            UpdateWorldState(team == TEAM_ALLIANCE ? BG_TP_FLAG_STATE_HORDE : BG_TP_FLAG_STATE_ALLIANCE, BG_TP_FLAG_STATE_ON_GROUND);
            break;
        case BG_TP_FLAG_STATE_ON_PLAYER:
            UpdateWorldState(team == TEAM_ALLIANCE ? BG_TP_FLAG_UNK_ALLIANCE : BG_TP_FLAG_UNK_HORDE, 1);
            UpdateWorldState(team == TEAM_ALLIANCE ? BG_TP_FLAG_STATE_HORDE : BG_TP_FLAG_STATE_ALLIANCE, 2);
            break;
        default:
            break;
    }

    _flagState[team] = value;
    _flagKeepers[team] = flagKeeperGUID;
}
