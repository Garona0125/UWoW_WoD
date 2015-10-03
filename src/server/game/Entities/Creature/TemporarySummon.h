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

#ifndef TEMPSUMMON_H
#define TEMPSUMMON_H

#include "Creature.h"

enum SummonActionType
{
    SUMMON_ACTION_TYPE_DEFAULT               = 0,
    SUMMON_ACTION_TYPE_ROUND_HOME_POS        = 1,
    SUMMON_ACTION_TYPE_ROUND_SUMMONER        = 2,
};

/// Stores data for temp summons
struct TempSummonData
{
    uint32 entry;           ///< Entry of summoned creature
    Position pos;           ///< Position, where should be creature spawned
    TempSummonType sumType; ///< Summon type, see TempSummonType for available types
    uint8 count;            ///< Summon count  for non default action
    uint8 actionType;       ///< Summon action type, option for any summon options
    uint32 time;            ///< Despawn time, usable only with certain temp summon types
    float distance;         ///< Distance from caster for non default action
};

class TempSummon : public Creature
{
    public:
        explicit TempSummon(SummonPropertiesEntry const* properties, Unit* owner, bool isWorldObject);
        virtual ~TempSummon() {}
        void Update(uint32 time);
        virtual void InitStats(uint32 lifetime);
        virtual void InitSummon();
        virtual void UnSummon(uint32 msTime = 0);
        void RemoveFromWorld();
        bool InitBaseStat(uint32 creatureId, bool& damageSet);
        void SetTempSummonType(TempSummonType type);
        void SaveToDB(uint32 /*mapid*/, uint32 /*spawnMask*/, uint32 /*phaseMask*/) {}
        Unit* GetSummoner() const;
        ObjectGuid GetSummonerGUID() const { return m_summonerGUID; }
        TempSummonType const& GetSummonType() { return m_type; }
        uint32 GetTimer() { return m_timer; }
        void CastPetAuras(bool current, uint32 spellId = 0);
        bool addSpell(uint32 spellId, ActiveStates active = ACT_DECIDE, PetSpellState state = PETSPELL_NEW, PetSpellType type = PETSPELL_NORMAL);
        bool removeSpell(uint32 spell_id);
        void LearnPetPassives();
        void InitLevelupSpellsForLevel();

        bool learnSpell(uint32 spell_id);
        bool unlearnSpell(uint32 spell_id);
        void ToggleAutocast(SpellInfo const* spellInfo, bool apply);

        PetType getPetType() const { return m_petType; }
        void setPetType(PetType type) { m_petType = type; }

        const SummonPropertiesEntry* const m_Properties;
        bool    m_loading;
        Unit*   m_owner;

    private:
        TempSummonType m_type;
        uint32 m_timer;
        uint32 m_lifetime;
        ObjectGuid m_summonerGUID;
        bool onUnload;
        PetType m_petType;
};

class Minion : public TempSummon
{
    public:
        Minion(SummonPropertiesEntry const* properties, Unit* owner, bool isWorldObject);
        void InitStats(uint32 duration);
        void RemoveFromWorld();
        Unit* GetOwner() { return m_owner; }
        bool IsPetGhoul() const {return GetEntry() == 26125;} // Ghoul may be guardian or pet
        bool IsPetGargoyle() const { return GetEntry() == 27829; }
        bool IsWarlockPet() const;
        bool IsGuardianPet() const;
};

class Guardian : public Minion
{
    public:
        Guardian(SummonPropertiesEntry const* properties, Unit* owner, bool isWorldObject);
        void InitStats(uint32 duration);
        bool InitStatsForLevel(uint8 level);
        void InitSummon();

        bool UpdateStats(Stats stat);
        bool UpdateAllStats();
        void UpdateResistances(uint32 school);
        void UpdateArmor();
        void UpdateMaxHealth();
        void UpdateMaxPower(Powers power);
        void UpdateAttackPowerAndDamage(bool ranged = false);
        void UpdateDamagePhysical(WeaponAttackType attType);

        int32 GetBonusDamage() { return m_bonusSpellDamage; }
        void SetBonusDamage(int32 damage);

    protected:
        int32   m_bonusSpellDamage;
        float   m_statFromOwner[MAX_STATS];
};

class Puppet : public Minion
{
    public:
        Puppet(SummonPropertiesEntry const* properties, Unit* owner);
        void InitStats(uint32 duration);
        void InitSummon();
        void Update(uint32 time);
        void RemoveFromWorld();
    protected:
};

class ForcedUnsummonDelayEvent : public BasicEvent
{
public:
    ForcedUnsummonDelayEvent(TempSummon& owner) : BasicEvent(), m_owner(owner) { }
    bool Execute(uint64 e_time, uint32 p_time);

private:
    TempSummon& m_owner;
};
#endif
