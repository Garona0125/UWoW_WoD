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

#include "ObjectAccessor.h"
#include "Unit.h"
#include "SpellInfo.h"
#include "Log.h"
#include "AreaTrigger.h"
#include "GridNotifiers.h"
#include "Chat.h"

AreaTrigger::AreaTrigger() : WorldObject(false), _duration(0), _activationDelay(0), _updateDelay(0), _on_unload(false), _caster(NULL),
    _radius(1.0f), atInfo(), _on_despawn(false), m_spellInfo(NULL), _moveSpeed(0.0f), _moveTime(0), _realEntry(0), _hitCount(1), _areaTriggerCylinder(false)
{
    m_objectType |= TYPEMASK_AREATRIGGER;
    m_objectTypeId = TYPEID_AREATRIGGER;

    m_updateFlag = UPDATEFLAG_STATIONARY_POSITION | UPDATEFLAG_AREA_TRIGGER;

    m_valuesCount = AREATRIGGER_END;
    _dynamicValuesCount = AREATRIGGER_DYNAMIC_END;
}

AreaTrigger::~AreaTrigger()
{
}

void AreaTrigger::AddToWorld()
{
    ///- Register the AreaTrigger for guid lookup and for caster
    if (!IsInWorld())
    {
        sObjectAccessor->AddObject(this);
        WorldObject::AddToWorld();
        BindToCaster();
    }
}

void AreaTrigger::RemoveFromWorld()
{
    ///- Remove the AreaTrigger from the accessor and from all lists of objects in world
    if (IsInWorld())
    {
        UnbindFromCaster();
        WorldObject::RemoveFromWorld();
        sObjectAccessor->RemoveObject(this);
    }
}

bool AreaTrigger::CreateAreaTrigger(ObjectGuid::LowType guidlow, uint32 triggerEntry, Unit* caster, SpellInfo const* info, Position const& pos, Position const& posMove, Spell* spell /*=NULL*/, ObjectGuid targetGuid /*=0*/)
{
    m_spellInfo = info;

    if (!info)
    {
        sLog->outError(LOG_FILTER_GENERAL, "AreaTrigger (entry %u) caster %s no spellInfo", triggerEntry, caster->ToString().c_str());
        return false;
    }

    // Caster not in world, might be spell triggered from aura removal
    if (!caster->IsInWorld())
        return false;

    if (!caster->isAlive())
    {
        sLog->outError(LOG_FILTER_GENERAL, "AreaTrigger (spell %u) caster %s is dead ", info->Id, caster->ToString().c_str());
        return false;
    }

    SetMap(caster->GetMap());
    Relocate(pos);
    if (!IsPositionValid())
    {
        sLog->outError(LOG_FILTER_GENERAL, "AreaTrigger (spell %u) not created. Invalid coordinates (X: %f Y: %f)", info->Id, GetPositionX(), GetPositionY());
        return false;
    }

    if (AreaTriggerInfo const* infoAt = sObjectMgr->GetAreaTriggerInfo(triggerEntry))
    {
        atInfo = *infoAt;
        _activationDelay = atInfo.activationDelay;
        _updateDelay = atInfo.updateDelay;

        for (AreaTriggerActionList::const_iterator itr = atInfo.actions.begin(); itr != atInfo.actions.end(); ++itr)
            _actionInfo[itr->id] = ActionInfo(&*itr);
    }

    Object::_Create(ObjectGuid::Create<HighGuid::AreaTrigger>(caster->GetMapId(), 0, guidlow));
    SetPhaseMask(caster->GetPhaseMask(), false);
    SetPhaseId(caster->GetPhases(), false);

    _realEntry = triggerEntry;
    SetEntry(_realEntry);
    uint32 duration = info->GetDuration();

    Player* modOwner = caster->GetSpellModOwner();
    if (duration != -1 && modOwner)
        modOwner->ApplySpellMod(info->Id, SPELLMOD_DURATION, duration);

    SetDuration(duration);
    SetObjectScale(1);

    // on some spells radius set on dummy aura, not on create effect.
    // overwrite by radius from spell if exist.
    bool find = false;
    for (uint32 j = 0; j < MAX_SPELL_EFFECTS; ++j)
    {
        if (float r = info->Effects[j].CalcRadius(caster))
        {
            _radius = r * (spell ? spell->m_spellValue->RadiusMod : 1.0f);
            find = true;
        }
    }

    if (!find && atInfo.sphereScale)
        _radius = atInfo.sphereScale;

    if (atInfo.Height)
        _areaTriggerCylinder = true;

    SetGuidValue(AREATRIGGER_FIELD_CASTER, caster->GetGUID());
    SetUInt32Value(AREATRIGGER_FIELD_SPELL_ID, info->Id);
    SetUInt32Value(AREATRIGGER_FIELD_SPELL_VISUAL_ID, info->SpellVisual[0] ? info->SpellVisual[0] : info->SpellVisual[1]);
    SetUInt32Value(AREATRIGGER_FIELD_DURATION, duration);
    SetFloatValue(AREATRIGGER_FIELD_EXPLICIT_SCALE, 1.0);
    SetTargetGuid(targetGuid);

    // culculate destination point
    if (isMoving())
    {
        _startPosition.Relocate(pos);
        if (atInfo.moveType)
        {
            _destPosition.Relocate(posMove);
            SetDuration(int32(pos.GetExactDist2d(&posMove) * 100));
        }
        else
            pos.SimplePosXYRelocationByAngle(_destPosition, GetSpellInfo()->GetMaxRange(), 0.0f);

        if (atInfo.speed)
            _moveSpeed = atInfo.speed;
        else
            _moveSpeed = GetSpellInfo()->GetMaxRange() / duration;
    }

    FillCustomData(caster);

    setActive(true);

    if (!GetMap()->AddToMap(this))
        return false;

    #ifdef WIN32
    sLog->outDebug(LOG_FILTER_SPELLS_AURAS, "AreaTrigger::Create AreaTrigger caster %s spellID %u spell rage %f dist %f dest - X:%f,Y:%f,Z:%f", caster->ToString().c_str(), info->Id, _radius, GetSpellInfo()->GetMaxRange(), _destPosition.GetPositionX(), _destPosition.GetPositionY(), _destPosition.GetPositionZ());
    #endif

    if (atInfo.maxCount)
    {
        std::list<AreaTrigger*> oldTriggers;
        caster->GetAreaObjectList(oldTriggers, info->Id);
        oldTriggers.sort(Trinity::GuidValueSorterPred());
        while (oldTriggers.size() > atInfo.maxCount)
        {
            AreaTrigger* at = oldTriggers.front();
            oldTriggers.remove(at);
            if (at->GetCasterGUID() == caster->GetGUID())
                at->Remove(false);
        }
    }
    UpdateAffectedList(0, AT_ACTION_MOMENT_SPAWN);

    return true;
}

void AreaTrigger::FillCustomData(Unit* caster)
{
    //custom visual case.
    if (GetCustomVisualId())
        SetUInt32Value(AREATRIGGER_FIELD_SPELL_VISUAL_ID, GetCustomVisualId());

    if (GetCustomEntry())
        SetEntry(GetCustomEntry());

    switch(GetSpellId())
    {
        case 143961:    //OO: Defiled Ground
            //ToDo: should cast only 1/4 of circle
            SetSpellId(143960);
            SetDuration(-1);
            _radius = 8.0f;
            //infrontof
            break;
        case 166539:    //WOD: Q34392
        {
            m_movementInfo.transport.pos.Relocate(0, 0, 0);
            m_movementInfo.transport.time = 0;
            m_movementInfo.transport.seat = 64;
            m_movementInfo.transport.guid = caster->GetGUID();
            m_movementInfo.transport.vehicleId = 0;

            caster->SetAIAnimKitId(6591);
            break;
        }
        default:
            break;
    }
}

void AreaTrigger::UpdateAffectedList(uint32 p_time, AreaTriggerActionMoment actionM)
{
    if (atInfo.actions.empty())
        return;

    WorldObject const* searcher = this;
    if(ObjectGuid targetGuid = GetTargetGuid())
        if(Unit* target = ObjectAccessor::GetUnit(*this, targetGuid))
            if(_caster->GetMap() == target->GetMap())
                searcher = target;

    if (actionM & AT_ACTION_MOMENT_ENTER)
    {
        for (GuidList::iterator itr = affectedPlayers.begin(), next; itr != affectedPlayers.end(); itr = next)
        {
            next = itr;
            ++next;

            Unit* unit = ObjectAccessor::GetUnit(*this, *itr);
            if (!unit)
            {
                affectedPlayers.erase(itr);
                continue;
            }
            
            if (!unit->IsWithinDistInMap(searcher, GetRadius()) ||
                (isMoving() && _HasActionsWithCharges(AT_ACTION_MOMENT_ON_THE_WAY) && !unit->IsInBetween(this, _destPosition.GetPositionX(), _destPosition.GetPositionY())))
            {
                affectedPlayers.erase(itr);
                AffectUnit(unit, AT_ACTION_MOMENT_LEAVE);
                continue;
            }

            UpdateOnUnit(unit, p_time);
        }

        std::list<Unit*> unitList;
        searcher->GetAttackableUnitListInRange(unitList, GetRadius());
        for (std::list<Unit*>::iterator itr = unitList.begin(); itr != unitList.end(); ++itr)
        {
            if (!IsUnitAffected((*itr)->GetGUID()))
            {
                //No 
                if (isMoving() && _HasActionsWithCharges(AT_ACTION_MOMENT_ON_THE_WAY) && !(*itr)->IsInBetween(this, _destPosition.GetPositionX(), _destPosition.GetPositionY()))
                    continue;
                affectedPlayers.push_back((*itr)->GetGUID());
                AffectUnit(*itr, actionM);
            }
        }
    }
    else
    {
        for (GuidList::iterator itr = affectedPlayers.begin(), next; itr != affectedPlayers.end(); itr = next)
        {
            next = itr;
            ++next;

            Unit* unit = ObjectAccessor::GetUnit(*this, *itr);
            if (!unit)
            {
                affectedPlayers.erase(itr);
                continue;
            }

            AffectUnit(unit, actionM);
            affectedPlayers.erase(itr);
        }
        AffectOwner(actionM);
    }
}

void AreaTrigger::UpdateActionCharges(uint32 p_time)
{
    for (ActionInfoMap::iterator itr = _actionInfo.begin(); itr != _actionInfo.end(); ++itr)
    {
        ActionInfo& info = itr->second;
        if (!info.charges || !info.action->chargeRecoveryTime)
            continue;
        if (info.charges >= info.action->maxCharges)
            continue;

        info.recoveryTime += p_time;
        if (info.recoveryTime >= info.action->chargeRecoveryTime)
        {
            info.recoveryTime -= info.action->chargeRecoveryTime;
            ++info.charges;
            if (info.charges == info.action->maxCharges)
                info.recoveryTime = 0;
        }
    }
}

void AreaTrigger::Update(uint32 p_time)
{
    //TMP. For debug info.
    uint32 spell = GetSpellId();

    if (GetDuration() != -1)
    {
        if (GetDuration() > int32(p_time))
        {
            _duration -= p_time;

            if (_activationDelay >= p_time)
                _activationDelay -= p_time;
            else
                _activationDelay = 0;
        }else
        {
            Remove(!_on_despawn); // expired
            return;
        }
    }

    UpdateActionCharges(p_time);
    UpdateMovement(p_time);

    if (!_activationDelay)
        UpdateAffectedList(p_time, AT_ACTION_MOMENT_ENTER);

    //??
    //WorldObject::Update(p_time);
}

bool AreaTrigger::IsUnitAffected(ObjectGuid guid) const
{
    return std::find(affectedPlayers.begin(), affectedPlayers.end(), guid) != affectedPlayers.end();
}

void AreaTrigger::AffectUnit(Unit* unit, AreaTriggerActionMoment actionM)
{
    for (ActionInfoMap::iterator itr =_actionInfo.begin(); itr != _actionInfo.end(); ++itr)
    {
        ActionInfo& info = itr->second;
        if (!(info.action->moment & actionM))
            continue;

        DoAction(unit, info);
        // if(unit != _caster)
            // AffectOwner(actionM);//action if action on area trigger
    }
}

void AreaTrigger::AffectOwner(AreaTriggerActionMoment actionM)
{
    for (ActionInfoMap::iterator itr =_actionInfo.begin(); itr != _actionInfo.end(); ++itr)
    {
        ActionInfo& info = itr->second;
        if (!(info.action->targetFlags & AT_TARGET_FLAG_ALWAYS_TRIGGER))
            continue;
        if (!(info.action->moment & actionM))
            continue;

        DoAction(_caster, info);
    }
}

void AreaTrigger::UpdateOnUnit(Unit* unit, uint32 p_time)
{
    if (atInfo.updateDelay)
    {
        if (_updateDelay > p_time)
        {
            _updateDelay -= p_time;
            return;
        }
        else
            _updateDelay = atInfo.updateDelay;
    }

    for (ActionInfoMap::iterator itr =_actionInfo.begin(); itr != _actionInfo.end(); ++itr)
    {
        ActionInfo& info = itr->second;
        if (!(info.action->moment & AT_ACTION_MOMENT_UPDATE))
            continue;

        DoAction(unit, info);
    }
}

bool AreaTrigger::_HasActionsWithCharges(AreaTriggerActionMoment action /*= AT_ACTION_MOMENT_ENTER*/)
{
    for (ActionInfoMap::iterator itr =_actionInfo.begin(); itr != _actionInfo.end(); ++itr)
    {
        ActionInfo& info = itr->second;
        if (info.action->moment & action)
        {
            if (info.charges || !info.action->maxCharges)
                return true;
        }
    }
    return false;
}

void AreaTrigger::DoAction(Unit* unit, ActionInfo& action)
{
    // do not process depleted actions
    if (!action.charges && action.action->maxCharges)
        return;

    Unit* caster = _caster;

    //sLog->outDebug(LOG_FILTER_SPELLS_AURAS, "AreaTrigger::DoAction caster %s unit %s type %u spellID %u, moment %u, targetFlags %u",
    //caster->ToString().c_str(), unit->ToString().c_str(), action.action->actionType, action.action->spellId, action.action->moment, action.action->targetFlags);

    if (action.action->targetFlags & AT_TARGET_FLAG_FRIENDLY)
        if (!caster || !caster->IsFriendlyTo(unit))
            return;
    if (action.action->targetFlags & AT_TARGET_FLAG_HOSTILE)
        if (!caster || !caster->IsHostileTo(unit))
            return;
    if (action.action->targetFlags & AT_TARGET_FLAG_VALIDATTACK)
        if (!caster || !caster->IsValidAttackTarget(unit))
            return;
    if (action.action->targetFlags & AT_TARGET_FLAG_OWNER)
    if (unit->GetGUID() != GetCasterGUID())
            return;
    if (action.action->targetFlags & AT_TARGET_FLAG_PLAYER)
        if (!unit->ToPlayer())
            return;
    if (action.action->targetFlags & AT_TARGET_FLAG_NOT_PET)
        if (unit->isPet())
            return;
    if (action.action->targetFlags & AT_TARGET_FLAG_NOT_FULL_HP)
        if (unit->IsFullHealth())
            return;
    if (action.action->targetFlags & AT_TARGET_FLAG_GROUP_OR_RAID)
        if (!unit->IsInRaidWith(caster))
            return;

    if (action.action->targetFlags & AT_TARGET_FLAT_IN_FRONT)
        if (!HasInArc(static_cast<float>(M_PI), unit))
            return;

    if (action.action->aura > 0 && !unit->HasAura(action.action->aura))
        return;
    else if (action.action->aura < 0 && unit->HasAura(abs(action.action->aura)))
        return;

    if (action.action->hasspell > 0 && !unit->HasSpell(action.action->hasspell))
        return;
    else if (action.action->hasspell < 0 && unit->HasSpell(abs(action.action->hasspell)))
        return;

    if (!CheckActionConditions(*action.action, unit))
        return;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(action.action->spellId);
    if (!spellInfo)
        return;

    if (action.action->targetFlags & AT_TARGET_FLAG_NOT_FULL_ENERGY)
    {
        Powers energeType = POWER_NULL;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if(spellInfo->Effects[i].Effect == SPELL_EFFECT_ENERGIZE)
                energeType = Powers(spellInfo->Effects[i].MiscValue);
        }

        if (energeType == POWER_NULL || unit->GetMaxPower(energeType) == 0 || unit->GetMaxPower(energeType) == unit->GetPower(energeType))
            return;
    }

    // should cast on self.
    if (spellInfo->Effects[EFFECT_0].TargetA.GetTarget() == TARGET_UNIT_CASTER
        || action.action->targetFlags & AT_TARGET_FLAG_CASTER_IS_TARGET)
        caster = unit;

    if(action.action->hitMaxCount && (int32)action.hitCount >= action.action->hitMaxCount)
        return;

    switch (action.action->actionType)
    {
        case AT_ACTION_TYPE_CAST_SPELL:
        {
            if (caster)
            {
                if (action.action->targetFlags & AT_TARGET_FLAG_CAST_AT_SRC)
                    caster->CastSpell(GetPositionX(), GetPositionY(), GetPositionZ(), action.action->spellId, TriggerCastFlags(TRIGGERED_FULL_MASK | TRIGGERED_CASTED_BY_AREATRIGGER));
                else
                    caster->CastSpell(unit, action.action->spellId, TriggerCastFlags(TRIGGERED_FULL_MASK | TRIGGERED_CASTED_BY_AREATRIGGER));
            }
            break;
        }
        case AT_ACTION_TYPE_REMOVE_AURA:
        {
            unit->RemoveAura(action.action->spellId);       //only one aura should be removed.
            break;
        }
        case AT_ACTION_TYPE_ADD_STACK:
        {
            if(Aura* aura = unit->GetAura(action.action->spellId))
                aura->ModStackAmount(1);
            break;
        }
        case AT_ACTION_TYPE_REMOVE_STACK:
        {
            if(Aura* aura = unit->GetAura(action.action->spellId))
                aura->ModStackAmount(-1);
            break;
        }
        case AT_ACTION_TYPE_CHANGE_SCALE: //limit scale by hit
        {
            float scale = GetFloatValue(AREATRIGGER_FIELD_EXPLICIT_SCALE) + action.action->scale;
            SetFloatValue(AREATRIGGER_FIELD_EXPLICIT_SCALE, scale);
            break;
        }
        case AT_ACTION_TYPE_SHARE_DAMAGE:
        {
            if (caster)
            {
                int32 bp0 = spellInfo->GetEffect(EFFECT_0, caster->GetSpawnMode()).BasePoints / _hitCount;
                int32 bp1 = spellInfo->GetEffect(EFFECT_1, caster->GetSpawnMode()).BasePoints / _hitCount;
                int32 bp2 = spellInfo->GetEffect(EFFECT_2, caster->GetSpawnMode()).BasePoints / _hitCount;

                if (action.action->targetFlags & AT_TARGET_FLAG_CAST_AT_SRC)
                {
                    SpellCastTargets targets;
                    targets.SetDst(GetPositionX(), GetPositionY(), GetPositionZ(), GetOrientation());

                    CustomSpellValues values;
                    if (bp0)
                        values.AddSpellMod(SPELLVALUE_BASE_POINT0, bp0);
                    if (bp1)
                        values.AddSpellMod(SPELLVALUE_BASE_POINT1, bp1);
                    if (bp2)
                        values.AddSpellMod(SPELLVALUE_BASE_POINT2, bp2);
                    caster->CastSpell(targets, spellInfo, &values, TriggerCastFlags(TRIGGERED_FULL_MASK | TRIGGERED_CASTED_BY_AREATRIGGER));
                }
                else
                    caster->CastCustomSpell(unit, action.action->spellId, &bp0, &bp1, &bp2, true);
            }
            break;
        }
    }

    action.hitCount++;
    if (atInfo.hitType & (1 << action.action->actionType))
        _hitCount++;

    //sLog->outDebug(LOG_FILTER_SPELLS_AURAS, "AreaTrigger::DoAction action _hitCount %i hitCount %i hitMaxCount %i hitType %i actionType %i", _hitCount, action.hitCount, action.action->hitMaxCount, atInfo.hitType, action.action->actionType);

    if (action.charges > 0)
    {
        --action.charges;
        //unload at next update.
        if (!action.charges && !_HasActionsWithCharges()) //_noActionsWithCharges check any action at enter.
        {
            _on_despawn = true;
            SetDuration(0);
        }
    }
}

void AreaTrigger::Remove(bool duration)
{
    if (_on_unload)
        return;
    _on_unload = true;

    if (IsInWorld())
    {
        UpdateAffectedList(0, AT_ACTION_MOMENT_REMOVE);//any remove from world

        if(duration)
            UpdateAffectedList(0, AT_ACTION_MOMENT_DESPAWN);//remove from world with time
        else
            UpdateAffectedList(0, AT_ACTION_MOMENT_LEAVE);//remove from world in action

        // Possibly this?
        if (!IsInWorld())
            return;

        SendObjectDeSpawnAnim(GetGUID());
        RemoveFromWorld();
        AddObjectToRemoveList();
    }
}

float AreaTrigger::GetVisualScale(bool max /*=false*/) const
{
    if (max) return atInfo.sphereScaleMax;
    return atInfo.sphereScale;
}

Unit* AreaTrigger::GetCaster() const
{
    return ObjectAccessor::GetUnit(*this, GetCasterGUID());
}

bool AreaTrigger::CheckActionConditions(AreaTriggerAction const& action, Unit* unit)
{
    Unit* caster = GetCaster();
    if (!caster)
        return false;

    ConditionSourceInfo srcInfo = ConditionSourceInfo(caster, unit);
    return sConditionMgr->IsObjectMeetToConditions(srcInfo, sConditionMgr->GetConditionsForAreaTriggerAction(GetEntry(), action.id));
}

void AreaTrigger::BindToCaster()
{
    ASSERT(!_caster);
    _caster = ObjectAccessor::GetUnit(*this, GetCasterGUID());
    ASSERT(_caster);
    ASSERT(_caster->GetMap() == GetMap());
    _caster->_RegisterAreaObject(this);
}

void AreaTrigger::UnbindFromCaster()
{
    ASSERT(_caster);
    _caster->_UnregisterAreaObject(this);
    _caster = NULL;
}

uint32 AreaTrigger::GetObjectMovementParts() const
{
    //now only source and destination points send.
    //ToDo: find interval calculation. On some spels only 2 points send (each 2 times)
    return 4;
}

void AreaTrigger::PutObjectUpdateMovement(ByteBuffer* data) const
{
    *data << uint32(0);                             //TimeToTarget
    *data << uint32(0);                             //ElapsedTimeForMovement
    *data << uint32(GetObjectMovementParts());      //VerticesCount

    //Source position 2 times.
    *data << float(GetPositionX());
    *data << float(GetPositionZ());
    *data << float(GetPositionY());
            
    *data << float(GetPositionX());
    *data << float(GetPositionZ());
    *data << float(GetPositionY());

    //Dest position 2 times.
    *data << float(_destPosition.GetPositionX());
    *data << float(_destPosition.GetPositionZ());
    *data << float(_destPosition.GetPositionY());

    *data << float(_destPosition.GetPositionX());
    *data << float(_destPosition.GetPositionZ());
    *data << float(_destPosition.GetPositionY());
}

void AreaTrigger::UpdateMovement(uint32 diff)
{
    if (!isMoving())
        return;

    float angle = _startPosition.GetAngle(_destPosition.GetPositionX(), _destPosition.GetPositionY());
    //sLog->outDebug(LOG_FILTER_SPELLS_AURAS, "AreaTrigger::UpdateMovement %f %f %f %f %i angle %f", GetPositionX(), GetPositionY(), GetPositionZ(), getMoveSpeed(), _moveTime, angle);

    _moveTime += diff;
    _startPosition.SimplePosXYRelocationByAngle(*this, getMoveSpeed() * _moveTime, angle, true);
}