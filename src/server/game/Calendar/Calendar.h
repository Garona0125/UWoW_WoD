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

#ifndef TRINITY_CALENDAR_H
#define TRINITY_CALENDAR_H

#include "Errors.h"
#include "SharedDefines.h"
#include <map>

class CalendarInvite
{
    public:
        CalendarInvite() : _inviteId(0) { Init(); }
        explicit CalendarInvite(uint64 inviteId) : _inviteId(inviteId) { Init(); }

        ~CalendarInvite() { }

        bool operator < (const CalendarInvite& i) const { return _inviteId > i.GetInviteId(); }
        bool operator == (const CalendarInvite& i) const { return _inviteId == i.GetInviteId(); }

        void SetInviteId(uint64 inviteId) { _inviteId = inviteId; }
        uint64 GetInviteId() const { return _inviteId; }

        void SetEventId(uint64 eventId) { _eventId = eventId; }
        uint64 GetEventId() const { return _eventId; }

        void SetSenderGUID(ObjectGuid guid) { _senderGUID = guid; }
        ObjectGuid GetSenderGUID() const { return _senderGUID; }

        void SetInvitee(ObjectGuid guid) { _invitee = guid; }
        ObjectGuid GetInvitee() const { return _invitee; }

        void SetResponseTime(uint32 responseTime) { _responseTime = responseTime; }
        uint32 GetResponseTime() const { return _responseTime; }

        void SetText(std::string text) { _text = text; }
        std::string GetText() const { return _text; }

        void SetStatus(CalendarInviteStatus status) { _status = status; }
        CalendarInviteStatus GetStatus() const { return _status; }

        void SetRank(CalendarModerationRank rank) { _rank = rank; }
        CalendarModerationRank GetRank() const { return _rank; }

        std::string GetDebugString() const;

    private:
        void Init();

        uint64 _inviteId;
        uint64 _eventId;
        ObjectGuid _invitee;
        ObjectGuid _senderGUID;
        uint32 _responseTime;
        CalendarInviteStatus _status;
        CalendarModerationRank _rank;
        std::string _text;
};

typedef std::set<uint64> CalendarInviteIdList;

class CalendarEvent
{
    public:
        CalendarEvent() : _eventId(0) { Init(); }
        explicit CalendarEvent(uint64 eventId) : _eventId(eventId) { Init(); }

        ~CalendarEvent() { }

        void SetEventId(uint64 eventId) { _eventId = eventId; }
        uint64 GetEventId() const { return _eventId; }

        void SetCreatorGUID(ObjectGuid guid) { _creatorGUID = guid; }
        ObjectGuid GetCreatorGUID() const { return _creatorGUID; }

        void SetGuildId(uint32 guildId) { _guildId = guildId; }
        uint32 GetGuildId() const { return _guildId; }

        void SetTitle(std::string title) { _title = title; }
        std::string GetTitle() const { return _title; }

        void SetDescription(std::string description) { _description = description; }
        std::string GetDescription() const { return _description; }

        void SetType(CalendarEventType type) { _type = type; }
        CalendarEventType GetType() const { return _type; }

        void SetMaxInvites(uint32 limit) { _maxInvites = limit; }
        uint32 GetMaxInvites() const { return _maxInvites; }

        void SetDungeonId(int32 dungeonId) { _dungeonId = dungeonId; }
        int32 GetDungeonId() const { return _dungeonId; }

        void SetTime(uint32 eventTime) { _eventTime = eventTime; }
        uint32 GetTime() const { return _eventTime; }

        void SetFlags(uint32 flags) { _flags = flags; }
        uint32 GetFlags() const { return _flags; }

        void SetRepeatable(bool repeatable) { _repeatable = repeatable; }
        bool GetRepeatable() const { return _repeatable; }

        void SetTimeZoneTime(uint32 timezoneTime) { _timezoneTime = timezoneTime; }
        uint32 GetTimeZoneTime() const { return _timezoneTime; }

        void AddInvite(uint64 inviteId)
        {
            if (inviteId)
                _invites.insert(inviteId);
        }

        void RemoveInvite(uint64 inviteId) { _invites.erase(inviteId); }
        bool HasInvite(uint64 inviteId) const { return _invites.find(inviteId) != _invites.end(); }
        CalendarInviteIdList const& GetInviteIdList() const { return _invites; }
        void SetInviteIdList(CalendarInviteIdList const& list) { _invites = list; }
        void ClearInviteIdList() { _invites.clear(); }

        std::string GetDebugString() const;

    private:
        void Init();

        uint64 _eventId;
        ObjectGuid _creatorGUID;
        uint32 _guildId;
        CalendarEventType _type;
        int32 _dungeonId;
        uint32 _maxInvites;
        uint32 _eventTime;
        uint32 _flags;
        bool _repeatable;
        uint32 _timezoneTime;
        std::string _title;
        std::string _description;
        CalendarInviteIdList _invites;
};

typedef std::set<uint64> CalendarEventIdList;
typedef std::map<ObjectGuid, CalendarInviteIdList> CalendarPlayerInviteIdMap;
typedef std::map<ObjectGuid, CalendarEventIdList> CalendarPlayerEventIdMap;
typedef std::map<uint64, CalendarInvite> CalendarInviteMap;
typedef std::map<uint64, CalendarEvent> CalendarEventMap;
typedef std::set<CalendarInvite> CalendarInveteMap;
class Player;

struct CalendarAction
{
        CalendarAction(): _action(CALENDAR_ACTION_NONE), _player(NULL), _data(0), _inviteId(0)
        {
        }

        void SetActionInviteId(uint64 inviteId) { _inviteId = inviteId; }
        uint64 GetActionInviteId() const { return _inviteId; }

        void SetAction(CalendarActionData data) { _action = data; }
        CalendarActionData GetAction() const { return _action; }

        void SetPlayer(Player* player) { ASSERT(player); _player = player; }
        Player* GetPlayer() const { return _player; }

        void SetExtraData(uint32 data) { _data = data; }
        uint32 GetExtraData() const { return _data; }

        CalendarInveteMap const& GetInviteMap() const { return InveteMap; }
        void SetInvite(CalendarInvite invite) { InveteMap.insert(invite); }

        CalendarEvent Event;
        //CalendarInvite Invite;
        CalendarInveteMap InveteMap;
        std::string GetDebugString() const;

    private:
        uint64 _inviteId;
        CalendarActionData _action;
        Player* _player;
        uint32 _data;
};

#endif
