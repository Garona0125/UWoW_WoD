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
#include "UpdateMask.h"
#include "Opcodes.h"
#include "World.h"
#include "ObjectAccessor.h"
#include "DatabaseEnv.h"
#include "GridNotifiers.h"
#include "CellImpl.h"
#include "GridNotifiersImpl.h"
#include "ScriptMgr.h"
#include "GroupMgr.h"
#include "UpdateFieldFlags.h"

DynamicObject::DynamicObject(bool isWorldObject) : WorldObject(isWorldObject),
    _aura(NULL), _removedAura(NULL), _caster(NULL), _duration(0), _isViewpoint(false)
{
    m_objectType |= TYPEMASK_DYNAMICOBJECT;
    m_objectTypeId = TYPEID_DYNAMICOBJECT;

    m_updateFlag = UPDATEFLAG_STATIONARY_POSITION;

    m_valuesCount = DYNAMICOBJECT_END;

    _fieldNotifyFlags = UF_FLAG_DYNAMIC;
}

DynamicObject::~DynamicObject()
{
    // make sure all references were properly removed
    ASSERT(!_aura);
    ASSERT(!_caster);
    ASSERT(!_isViewpoint);
}

void DynamicObject::AddToWorld()
{
    ///- Register the dynamicObject for guid lookup and for caster
    if (!IsInWorld())
    {
        sObjectAccessor->AddObject(this);
        WorldObject::AddToWorld();
        BindToCaster();
    }
}

void DynamicObject::RemoveFromWorld()
{
    ///- Remove the dynamicObject from the accessor and from all lists of objects in world
    if (IsInWorld())
    {
        if (_isViewpoint)
            RemoveCasterViewpoint();

        if (_aura)
            RemoveAura();

        // dynobj could get removed in Aura::RemoveAura
        if (!IsInWorld())
            return;

        UnbindFromCaster();
        WorldObject::RemoveFromWorld();
        sObjectAccessor->RemoveObject(this);
    }
}

bool DynamicObject::CreateDynamicObject(ObjectGuid::LowType guidlow, Unit* caster, uint32 spellId, Position const& pos, float radius, DynamicObjectType type)
{
    SetMap(caster->GetMap());
    Relocate(pos);
    if (!IsPositionValid())
    {
        sLog->outError(LOG_FILTER_GENERAL, "DynamicObject (spell %u) not created. Suggested coordinates isn't valid (X: %f Y: %f)", spellId, GetPositionX(), GetPositionY());
        return false;
    }

    WorldObject::_Create(ObjectGuid::Create<HighGuid::DynamicObject>(GetMapId(), spellId, guidlow));
    SetPhaseMask(caster->GetPhaseMask(), false);

    SetEntry(spellId);
    SetObjectScale(1.0f);
    if (type == DYNAMIC_OBJECT_RAID_MARKER)
    {
        ASSERT(caster->GetTypeId() == TYPEID_PLAYER && caster->ToPlayer()->GetGroup()
            && "DYNAMIC_OBJECT_RAID_MARKER must only be created by players that are in group.");
        SetGuidValue(DYNAMICOBJECT_FIELD_CASTER, caster->ToPlayer()->GetGroup()->GetGUID());

        AddPlayerInPersonnalVisibilityList(GetCasterGUID());
    }
    else
        SetGuidValue(DYNAMICOBJECT_FIELD_CASTER, caster->GetGUID());

    // The lower word of DYNAMIC_OBJECT_FIELD_BYTES must be 0x0001. This value means that the visual radius will be overriden
    // by client for most of the "ground patch" visual effect spells and a few "skyfall" ones like Hurricane.
    // If any other value is used, the client will _always_ use the radius provided in DYNAMIC_OBJECT_FIELD_RADIUS, but
    // precompensation is necessary (eg radius *= 2) for many spells. Anyway, blizz sends 0x0001 for all the spells
    // I saw sniffed...

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (spellInfo)
    {
        uint32 visual = spellInfo->SpellVisual[0] ? spellInfo->SpellVisual[0] : spellInfo->SpellVisual[1];
        SetUInt32Value(DYNAMICOBJECT_FIELD_BYTES, (type << 28) | visual);
    }

    SetUInt32Value(DYNAMICOBJECT_FIELD_SPELL_ID, spellId);
    SetFloatValue(DYNAMICOBJECT_FIELD_RADIUS, G3D::fuzzyEq(radius, 0.0f) ? 1.0f : radius);
    SetUInt32Value(DYNAMICOBJECT_FIELD_CAST_TIME, getMSTime());

    if (IsWorldObject())
        setActive(true);    //must before add to map to be put in world container

    if (!GetMap()->AddToMap(this))
        return false;

    return true;
}

void DynamicObject::Update(uint32 p_time)
{
    // caster has to be always available and in the same map
    ASSERT(GetType() == DYNAMIC_OBJECT_RAID_MARKER || _caster);
    ASSERT(GetType() == DYNAMIC_OBJECT_RAID_MARKER || _caster->GetMap() == GetMap());

    bool expired = false;

    if (_aura)
    {
        if (!_aura->IsRemoved())
            _aura->UpdateOwner(p_time, this);

        // _aura may be set to null in Aura::UpdateOwner call
        if (_aura && (_aura->IsRemoved() || _aura->IsExpired()))
            expired = true;
    }
    else
    {
        if (GetDuration() > int32(p_time))
            _duration -= p_time;
        else
            expired = true;

        if (GetType() == DYNAMIC_OBJECT_RAID_MARKER)
        {
            Group* group = sGroupMgr->GetGroupByGUID(GetCasterGUID());
            if (!group || !group->HasRaidMarker(GetGUID()))
                expired = true;
        }
    }

    if (expired)
    {
        if (GetType() == DYNAMIC_OBJECT_RAID_MARKER)
        {
            if (Group* group = sGroupMgr->GetGroupByGUID(GetCasterGUID()))
                group->ClearRaidMarker(GetGUID());
        }

        Remove();
    }
    else
        sScriptMgr->OnDynamicObjectUpdate(this, p_time);
}

void DynamicObject::Remove()
{
    if (IsInWorld())
    {
        SendObjectDeSpawnAnim(GetGUID());
        RemoveFromWorld();
        AddObjectToRemoveList();
    }
}

int32 DynamicObject::GetDuration() const
{
    if (!_aura)
        return _duration;
    else
        return _aura->GetDuration();
}

void DynamicObject::SetDuration(int32 newDuration)
{
    if (!_aura)
        _duration = newDuration;
    else
        _aura->SetDuration(newDuration);
}

void DynamicObject::Delay(int32 delaytime)
{
    SetDuration(GetDuration() - delaytime);
}

void DynamicObject::SetAura(Aura* aura)
{
    ASSERT(!_aura && aura);
    _aura = aura;
}

void DynamicObject::RemoveAura()
{
    ASSERT(_aura && !_removedAura);
    _removedAura = _aura;
    _aura = NULL;
    if (!_removedAura->IsRemoved())
        _removedAura->_Remove(AURA_REMOVE_BY_DEFAULT);
}

void DynamicObject::SetCasterViewpoint()
{
    if (Player* caster = _caster->ToPlayer())
    {
        caster->SetViewpoint(this, true);
        _isViewpoint = true;
    }
}

void DynamicObject::RemoveCasterViewpoint()
{
    if (Player* caster = _caster->ToPlayer())
    {
        caster->SetViewpoint(this, false);
        _isViewpoint = false;
    }
}

void DynamicObject::BindToCaster()
{
    if (GetType() == DYNAMIC_OBJECT_RAID_MARKER)
        return;

    ASSERT(!_caster);
    _caster = ObjectAccessor::GetUnit(*this, GetCasterGUID());
    ASSERT(_caster);
    ASSERT(_caster->GetMap() == GetMap());
    _caster->_RegisterDynObject(this);
}

void DynamicObject::UnbindFromCaster()
{
    if (GetType() == DYNAMIC_OBJECT_RAID_MARKER)
        return;

    ASSERT(_caster);
    _caster->_UnregisterDynObject(this);
    _caster = NULL;
}
