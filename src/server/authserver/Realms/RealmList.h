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

#ifndef _REALMLIST_H
#define _REALMLIST_H

#include "Common.h"
#include "Realm/Realm.h"

// Storage object for a realm
struct RealmAuth
{
    std::string address;
    std::string name;
    uint8 icon;
    RealmFlags flag;
    uint8 timezone;
    uint32 m_ID;
    AccountTypes allowedSecurityLevel;
    float populationLevel;
    uint32 gamebuild;
};

/// Storage object for the list of realms on the server
class RealmList
{
public:
    typedef std::map<std::string, RealmAuth> RealmMap;
    typedef std::vector<std::string> FirewallFarms;

    static RealmList* instance()
    {
        static RealmList instance;
        return &instance;
    }

    void Initialize(uint32 updateInterval);

    void UpdateIfNeed();

    void AddRealm(RealmAuth NewRealm) { m_realms[NewRealm.name] = NewRealm; }

    RealmMap::const_iterator begin() const { return m_realms.begin(); }
    RealmMap::const_iterator end() const { return m_realms.end(); }
    uint32 size() const { return m_realms.size(); }

    uint32 firewallSize() const { return m_firewallFarms.size(); }
    std::string GetRandomFirewall() { return m_firewallFarms[rand() % firewallSize()]; }

private:
    RealmList();

    void UpdateRealms(bool init = false);
    void UpdateRealm(uint32 id, const std::string& name, const std::string& address, uint16 port, uint8 icon, RealmFlags flag, uint8 timezone, AccountTypes allowedSecurityLevel, float popu, uint32 build);

    RealmMap m_realms;
    FirewallFarms m_firewallFarms;
    uint32   m_UpdateInterval;
    time_t   m_NextUpdateTime;
};

#define sRealmList RealmList::instance()
#endif
