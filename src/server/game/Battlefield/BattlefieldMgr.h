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

#ifndef BATTLEFIELD_MGR_H_
#define BATTLEFIELD_MGR_H_

#include "Battlefield.h"

class Player;
class GameObject;
class Creature;
class ZoneScript;
struct GossipMenuItems;

// class to handle player enter / leave / areatrigger / GO use events
class BattlefieldMgr
{
  private:
    // ctor
    BattlefieldMgr();
  public:

    // dtor
    ~BattlefieldMgr();

    // create battlefield events
    void InitBattlefield();
    // called when a player enters an battlefield area
    void HandlePlayerEnterZone(Player * player, uint32 areaflag);
    // called when player leaves an battlefield area
    void HandlePlayerLeaveZone(Player * player, uint32 areaflag);
    // called when player resurrects
    void HandlePlayerResurrects(Player * player, uint32 areaflag);
    // remove player from all queues.
    void EventPlayerLoggedOut(Player * player);
    // return assigned battlefield
    Battlefield *GetBattlefieldToZoneId(uint32 zoneid);
    Battlefield *GetBattlefieldByBattleId(uint32 battleid);
    Battlefield *GetBattlefieldByGUID(uint64 const& guid);

    static BattlefieldMgr* instance()
    {
        static BattlefieldMgr instance;
        return &instance;
    }

    ZoneScript *GetZoneScript(uint32 zoneId);

    void AddZone(uint32 zoneid, Battlefield * handle);

    void Update(uint32 diff);

    void HandleGossipOption(Player * player, uint64 guid, uint32 gossipid);

    bool CanTalkTo(Player * player, Creature * creature, GossipMenuItems gso);

    void HandleDropFlag(Player * player, uint32 spellId);

    typedef std::vector < Battlefield * >BattlefieldSet;
    typedef std::map < uint32 /* zoneid */ , Battlefield * >BattlefieldMap;
  private:
    // contains all initiated battlefield events
    // used when initing / cleaning up
      BattlefieldSet m_BattlefieldSet;
    // maps the zone ids to an battlefield event
    // used in player event handling
    BattlefieldMap m_BattlefieldMap;
    // update interval
    uint32 m_UpdateTimer;
};

#define sBattlefieldMgr BattlefieldMgr::instance()

#endif
