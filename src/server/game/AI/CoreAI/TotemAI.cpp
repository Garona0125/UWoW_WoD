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

#include "TotemAI.h"
#include "Totem.h"
#include "Creature.h"
#include "DBCStores.h"
#include "ObjectAccessor.h"
#include "SpellMgr.h"

#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"

int TotemAI::Permissible(Creature const* creature)
{
    if (creature->isTotem())
        return PERMIT_BASE_PROACTIVE;

    return PERMIT_BASE_NO;
}

TotemAI::TotemAI(Creature* c) : CreatureAI(c)
{
    ASSERT(c->isTotem());
}

void TotemAI::InitializeAI()
{
    CreatureAI::InitializeAI();

    if(PetStats const* pStats = sObjectMgr->GetPetStats(me->GetEntry()))
        if(pStats->state)
        {
            me->SetReactState(ReactStates(pStats->state));
            if(Unit* victim = me->GetTargetUnit())
                me->Attack(victim, !me->GetCasterPet());
        }

    i_victimGuid.Clear();
}

void TotemAI::MoveInLineOfSight(Unit* /*who*/)
{
}

void TotemAI::EnterEvadeMode()
{
    me->CombatStop(true);
}

void TotemAI::UpdateAI(uint32 /*diff*/)
{
    if (me->ToTotem()->GetTotemType() != TOTEM_ACTIVE)
        return;

    if (!me->isAlive())
        return;

    // Search spell
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(me->ToTotem()->GetSpell());
    if (!spellInfo)
        return;

    Unit* owner = me->GetCharmerOrOwner();
    Unit* victim = i_victimGuid ? ObjectAccessor::GetUnit(*me, i_victimGuid) : NULL;
    if (!owner)
        return;

    Unit* targetOwner = owner->getAttackerForHelper();
    if(targetOwner != NULL && targetOwner != victim && me->IsWithinDistInMap(targetOwner, spellInfo->GetMaxRange(false)))
    {
        victim = targetOwner;
        i_victimGuid = victim->GetGUID();
    }

    if (me->IsNonMeleeSpellCasted(false))
    {
        if (victim && victim->HasCrowdControlAura())
            victim = NULL;
        else
            return;
    }

    // If have target
    if (victim)
    {
        if (!owner->isInCombat())
            owner->SetInCombatWith(victim);
        // attack
        me->SetInFront(victim);                         // client change orientation by self
        me->CastSpell(victim, spellInfo, false);
    }
}

void TotemAI::AttackStart(Unit* /*victim*/)
{
    // Sentry totem sends ping on attack
    if (me->GetEntry() == SENTRY_TOTEM_ENTRY && me->GetOwner()->GetTypeId() == TYPEID_PLAYER)
    {
        //! 6.0.3
        WorldPacket data(SMSG_MINIMAP_PING, (8+4+4));
        data << me->GetGUID();;
        data << me->GetPositionX();
        data << me->GetPositionY();
        ((Player*)me->GetOwner())->GetSession()->SendPacket(&data);
        
    }
}
