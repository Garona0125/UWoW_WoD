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

#ifndef TRINITYCORE_AREATRIGGER_H
#define TRINITYCORE_AREATRIGGER_H

#include "Object.h"

class Unit;
class SpellInfo;
class Spell;

enum AreaTriggerActionMoment
{
    AT_ACTION_MOMENT_ENTER        = 0x001,                // when unit enters areatrigger
    AT_ACTION_MOMENT_LEAVE        = 0x002,                // when unit exits areatrigger
    AT_ACTION_MOMENT_UPDATE       = 0x004,                // on areatrigger update
    AT_ACTION_MOMENT_DESPAWN      = 0x008,                // when areatrigger despawn
    AT_ACTION_MOMENT_SPAWN        = 0x010,                // when areatrigger spawn
    AT_ACTION_MOMENT_REMOVE       = 0x020,                // when areatrigger remove
    //range should be = distance.
    AT_ACTION_MOMENT_ON_THE_WAY   = 0x040,                // when target is betwin source and dest points. For movement only. WARN! Should add AT_ACTION_MOMENT_ENTER flag too
    AT_ACTION_MOMENT_ON_STOP_MOVE = 0x080,                // when target is betwin source and dest points. For movement only. WARN! Should add AT_ACTION_MOMENT_ENTER flag too
};

enum AreaTriggerActionType
{
    AT_ACTION_TYPE_CAST_SPELL   = 0, // hit mask 0x001
    AT_ACTION_TYPE_REMOVE_AURA  = 1, // hit mask 0x002
    AT_ACTION_TYPE_ADD_STACK    = 2, // hit mask 0x004
    AT_ACTION_TYPE_REMOVE_STACK = 3, // hit mask 0x008
    AT_ACTION_TYPE_CHANGE_SCALE = 4, // hit mask 0x010
    AT_ACTION_TYPE_SHARE_DAMAGE = 5, // hit mask 0x020
    AT_ACTION_TYPE_MAX          = 6,
};

enum AreaTriggerTargetFlags
{
    AT_TARGET_FLAG_FRIENDLY          = 0x0001,             // casted on targets that are friendly to areatrigger owner
    AT_TARGET_FLAG_VALIDATTACK       = 0x0002,             // casted on targets that are valid attcak to areatrigger owner
    AT_TARGET_FLAG_OWNER             = 0x0004,             // casted only on areatrigger owner
    AT_TARGET_FLAG_PLAYER            = 0x0008,             // casted only on players
    AT_TARGET_FLAG_NOT_PET           = 0x0010,             // casted on everyone except pets
    AT_TARGET_FLAG_CAST_AT_SRC       = 0x0020,             // casted on areatrigger position as dest
    AT_TARGET_FLAG_CASTER_IS_TARGET  = 0x0040,             // casted on areatrigger position as dest
    AT_TARGET_FLAG_NOT_FULL_HP       = 0x0080,             // casted on targets if not full hp
    AT_TARGET_FLAG_ALWAYS_TRIGGER    = 0x0100,             // casted always at any action on owner
    AT_TARGET_FLAT_IN_FRONT          = 0x0200,             // WARNING! If target come from back he not get cast. ToDo it..
    AT_TARGET_FLAG_NOT_FULL_ENERGY   = 0x0400,             // casted on targets if not full enegy
    AT_TARGET_FLAG_GROUP_OR_RAID     = 0x0800,             // casted on targets that in group to areatrigger owner
    AT_TARGET_FLAG_HOSTILE           = 0x1000,             // casted on targets that are hostile to areatrigger owner

    AT_TARGET_MASK_REQUIRE_TARGET = 
        AT_TARGET_FLAG_FRIENDLY | AT_TARGET_FLAG_HOSTILE | AT_TARGET_FLAG_OWNER | AT_TARGET_FLAG_PLAYER |
        AT_TARGET_FLAG_NOT_PET  | AT_TARGET_FLAG_CASTER_IS_TARGET | AT_TARGET_FLAG_NOT_FULL_HP | AT_TARGET_FLAG_ALWAYS_TRIGGER | AT_TARGET_FLAT_IN_FRONT,
};

struct AreaTriggerAction
{
    uint32 id;
    AreaTriggerActionMoment moment;
    AreaTriggerActionType actionType;
    AreaTriggerTargetFlags targetFlags;
    uint32 spellId;
    int8 maxCharges;
    uint32 chargeRecoveryTime;
    int32 aura;
    int32 hasspell;
    float scale;
    int32 hitMaxCount;
};

typedef std::list<AreaTriggerAction> AreaTriggerActionList;

struct AreaTriggerInfo
{
    AreaTriggerInfo() : sphereScale(1.0f), sphereScaleMax(1.0f), activationDelay(0), updateDelay(0), maxCount(0), 
        visualId(1), customEntry(0), isMoving(false), speed(0.0f), moveType(0), hitType(0),
        Height(0.0f), RadiusTarget(0.0f), Float5(0.0f), Float4(0.0f), Radius(0.0f), HeightTarget(0.0f),
        MoveCurveID(0), ElapsedTime(0) {}

    bool isMoving;
    float sphereScale;
    float sphereScaleMax;
    uint32 visualId;    //unk520 on 5.4.8 parse at SMSG_UPDATE_OBJECT
    uint32 activationDelay;
    uint32 updateDelay;
    uint32 customEntry;
    uint8 maxCount;
    AreaTriggerActionList actions;
    float speed;
    uint32 moveType;
    uint32 hitType;
    float Height;
    float RadiusTarget;
    float Float5;
    float Float4;
    float Radius;
    float HeightTarget;
    uint32 MoveCurveID;
    uint32 ElapsedTime;
};

class AreaTrigger : public WorldObject, public GridObject<AreaTrigger>
{
    private:
        struct ActionInfo
        {
            ActionInfo() : charges(0), recoveryTime(0), action(NULL), hitCount(0) { }
            ActionInfo(AreaTriggerAction const* _action) :
                charges(_action->maxCharges), recoveryTime(0), action(_action), hitCount(0) { }

            uint32 hitCount;
            uint8 charges;
            uint32 recoveryTime;
            AreaTriggerAction const* action;
        };
        typedef std::map<uint8, ActionInfo> ActionInfoMap;

    public:

        AreaTrigger();
        ~AreaTrigger();

        void AddToWorld();
        void RemoveFromWorld();

        bool CreateAreaTrigger(ObjectGuid::LowType guidlow, uint32 triggerEntry, Unit* caster, SpellInfo const* info, Position const& pos, Position const& posMove, Spell* spell = NULL, ObjectGuid targetGuid = ObjectGuid::Empty);
        void Update(uint32 p_time);
        void UpdateAffectedList(uint32 p_time, AreaTriggerActionMoment actionM);
        void Remove(bool duration = true);
        uint32 GetSpellId() const { return GetUInt32Value(AREATRIGGER_FIELD_SPELL_ID); }
        void SetSpellId(uint32 spell) { return SetUInt32Value(AREATRIGGER_FIELD_SPELL_ID, spell); }
        ObjectGuid GetCasterGUID() const { return GetGuidValue(AREATRIGGER_FIELD_CASTER); }
        Unit* GetCaster() const;
        void SetTargetGuid(ObjectGuid targetGuid) { _targetGuid = targetGuid; }
        ObjectGuid GetTargetGuid() { return _targetGuid; }
        int32 GetDuration() const { return _duration; }
        void SetDuration(int32 newDuration) { _duration = newDuration; }
        void Delay(int32 delaytime) { SetDuration(GetDuration() - delaytime); }
        float GetVisualScale(bool max = false) const;
        float GetRadius() const { return _radius; }
        float GetCustomVisualId() const { return atInfo.visualId; }
        uint32 GetCustomEntry() const { return atInfo.customEntry; }
        uint32 GetRealEntry() const { return _realEntry; }
        bool IsUnitAffected(ObjectGuid guid) const;
        void AffectUnit(Unit* unit, AreaTriggerActionMoment actionM);
        void AffectOwner(AreaTriggerActionMoment actionM);
        void UpdateOnUnit(Unit* unit, uint32 p_time);
        void DoAction(Unit* unit, ActionInfo& action);
        bool CheckActionConditions(AreaTriggerAction const& action, Unit* unit);
        void UpdateActionCharges(uint32 p_time);
        bool GetAreaTriggerCylinder() const { return _areaTriggerCylinder; }
        AreaTriggerInfo GetAreaTriggerInfo() const { return atInfo; }

        void BindToCaster();
        void UnbindFromCaster();

        SpellInfo const*  GetSpellInfo() { return m_spellInfo; }

        //movement
        void UpdateMovement(uint32 diff);
        bool isMoving() const { return atInfo.isMoving; }
        float getMoveSpeed() const { return _moveSpeed; }
        uint32 GetObjectMovementParts() const;
        void PutObjectUpdateMovement(ByteBuffer* data) const;

    private:
        bool _HasActionsWithCharges(AreaTriggerActionMoment action = AT_ACTION_MOMENT_ENTER);
        void FillCustomData(Unit* caster);

    protected:
        Unit* _caster;
        ObjectGuid _targetGuid;
        int32 _duration;
        uint32 _activationDelay;
        uint32 _updateDelay;
        GuidList affectedPlayers;
        float _radius;
        AreaTriggerInfo atInfo;
        ActionInfoMap _actionInfo;
        Position _destPosition;
        Position _startPosition;
        uint32 _realEntry;
        float _moveSpeed;
        uint32 _moveTime;
        bool _on_unload;
        bool _on_despawn;
        uint32 _hitCount;
        bool _areaTriggerCylinder;

        SpellInfo const* m_spellInfo;
};
#endif
