/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
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

#ifndef TRINITY_CALENDARMGR_H
#define TRINITY_CALENDARMGR_H

#include "Calendar.h"

class CalendarMgr
{
    private:
        CalendarMgr();
        ~CalendarMgr();

    public:
        static CalendarMgr* instance()
        {
            static CalendarMgr* instance = new CalendarMgr();
            return instance;
        }

        void LoadFromDB();

        CalendarInvite* GetInvite(uint64 inviteId);
        CalendarEvent* GetEvent(uint64 eventId);

        CalendarInviteIdList const& GetPlayerInvites(uint64 guid);
        CalendarEventIdList const& GetPlayerEvents(uint64 guid);

        uint32 GetPlayerNumPending(uint64 guid);
        uint64 GetFreeEventId();
        uint64 GetFreeInviteId();

        void AddAction(CalendarAction const& action, uint64 EventID);

        void SendCalendarEvent(CalendarEvent const& calendarEvent, CalendarSendEventType type);
        void SendCalendarEventInvite(CalendarInvite const& invite, bool pending);
        void SendCalendarEventInviteAlert(CalendarEvent const& calendarEvent, CalendarInvite const& invite);
        void SendCalendarEventInviteRemove(uint64 guid, CalendarInvite const& invite, uint32 flags);
        void SendCalendarEventInviteRemoveAlert(uint64 guid, CalendarEvent const& calendarEvent, CalendarInviteStatus status);
        void SendCalendarEventUpdateAlert(uint64 guid, CalendarEvent const& calendarEvent, CalendarSendEventType type);
        void SendCalendarEventStatus(uint64 guid, CalendarEvent const& calendarEvent, CalendarInvite const& invite);
        void SendCalendarEventRemovedAlert(uint64 guid, CalendarEvent const& calendarEvent);
        void SendCalendarEventModeratorStatusAlert(CalendarInvite const& invite);

    private:
        CalendarEvent* CheckPermisions(uint64 eventId, Player* player, CalendarModerationRank minRank);
        CalendarInvite* GetInviterFromEvent(uint64 plrGuid, CalendarEvent* calendarEvent);

        bool AddEvent(CalendarEvent const& calendarEvent);
        bool RemoveEvent(uint64 eventId);
        bool AddPlayerEvent(uint64 guid, uint64 eventId);
        bool RemovePlayerEvent(uint64 guid, uint64 eventId);

        bool AddInvite(CalendarInvite const& invite);
        bool AddInvite(CalendarInveteMap const& inviteMap);
        uint64 RemoveInvite(uint64 inviteId);
        bool AddPlayerInvite(uint64 guid, uint64 inviteId);
        bool RemovePlayerInvite(uint64 guid, uint64 inviteId);

        CalendarEventMap _events;
        CalendarInviteMap _invites;
        CalendarPlayerInviteIdMap _playerInvites;
        CalendarPlayerEventIdMap _playerEvents;

        uint64 _eventNum;
        uint64 _inviteNum;
};

#define sCalendarMgr CalendarMgr::instance()

#endif
