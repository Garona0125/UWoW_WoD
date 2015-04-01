//UWoWCore
//World boss

#include "NewScriptPCH.h"

enum eSpells
{
    SPELL_ARC_NOVA           = 136338,
    SPELL_STATIC_SHIELD      = 136341,
    SPELL_STATIC_SHIELD_DMG  = 136343,
    SPELL_LIGHTNING_TETHER   = 136339,
    SPELL_STORMCLOUD         = 136340,
};

enum eEvents
{
    EVENT_ARC_NOVA           = 1,
    EVENT_LIGHTNING_TETHER   = 2,
    EVENT_STORMCLOUD         = 3,
    EVENT_RE_ATTACK          = 4,
};

//69099
class boss_nalak : public CreatureScript
{
public:
    boss_nalak() : CreatureScript("boss_nalak") { }

    struct boss_nalakAI : public ScriptedAI
    {
        boss_nalakAI(Creature* creature) : ScriptedAI(creature)
        {
            me->SetCanFly(true);
            me->SetDisableGravity(true);
            me->SetByteFlag(UNIT_FIELD_BYTES_1, 3, UNIT_BYTE1_FLAG_ALWAYS_STAND | UNIT_BYTE1_FLAG_HOVER);
        }

        EventMap events;
        bool attack_ready;

        void Reset()
        {
            events.Reset();
            me->SetReactState(REACT_DEFENSIVE);
            attack_ready = true;
        }

        void EnterCombat(Unit* unit)
        {
            DoZoneInCombat(me, 75.0f);
            DoCast(me, SPELL_STATIC_SHIELD, true);             
            events.ScheduleEvent(EVENT_ARC_NOVA,         42000);
          //events.ScheduleEvent(EVENT_LIGHTNING_TETHER, 35000); not work
            events.ScheduleEvent(EVENT_STORMCLOUD,       24000);
        }

        void UpdateAI(uint32 diff)
        {
            if (!UpdateVictim() || me->HasUnitState(UNIT_STATE_CASTING))
                return;

            /*if (me->getVictim() && attack_ready)
                if (!me->IsWithinMeleeRange(me->getVictim()))
                    me->GetMotionMaster()->MoveCharge(me->getVictim()->GetPositionX(), me->getVictim()->GetPositionY(), me->getVictim()->GetPositionZ() + 18.0f, 15.0f);*/

            events.Update(diff);

            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                case EVENT_ARC_NOVA:
                    attack_ready = false;
                    me->AttackStop();
                    me->SetReactState(REACT_PASSIVE);
                    DoCastAOE(SPELL_ARC_NOVA);
                    events.DelayEvents(3300);
                    events.ScheduleEvent(EVENT_RE_ATTACK, 3000);
                    events.ScheduleEvent(EVENT_ARC_NOVA, 42000);
                    break;
                case EVENT_LIGHTNING_TETHER:
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 40.0f, true))
                        DoCast(target, SPELL_LIGHTNING_TETHER);
                    events.ScheduleEvent(EVENT_LIGHTNING_TETHER, 35000);
                    break;
                case EVENT_STORMCLOUD:
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 40.0f, true))
                        DoCast(target, SPELL_STORMCLOUD);
                    events.ScheduleEvent(EVENT_STORMCLOUD,  24000);
                    break;
                case EVENT_RE_ATTACK:
                    me->SetReactState(REACT_AGGRESSIVE);
                    DoZoneInCombat(me, 75.0f);
                    attack_ready = true;
                    break;
                }
            }
            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const
    {
        return new boss_nalakAI(creature);
    }
};

//136341
class spell_static_shield : public SpellScriptLoader
{
    public:
        spell_static_shield() : SpellScriptLoader("spell_static_shield") { }

        class spell_static_shield_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_static_shield_AuraScript);

            void HandleTriggerSpell(AuraEffect const* aurEff)
            {
                if (GetCaster() && GetCaster()->isInCombat())
                {
                    std::list<HostileReference*> ThreatList = GetCaster()->getThreatManager().getThreatList();
                    if (!ThreatList.empty() && ThreatList.size() > 1)
                    {
                        uint8 targetcount = 0;
                        for (std::list<HostileReference*>::const_iterator pitr = ThreatList.begin(); pitr != ThreatList.end(); pitr++)
                        {
                            std::list<HostileReference*>::iterator itr = ThreatList.begin();
                            std::advance(itr, urand(1, ThreatList.size() - 1));
                            if (Player* pl = GetCaster()->GetPlayer(*GetCaster(), (*itr)->getUnitGuid()))
                            {
                                GetCaster()->CastSpell(pl, SPELL_STATIC_SHIELD_DMG, true);
                                targetcount++;
                                
                                if (targetcount == 3)
                                    break;
                            }
                        }
                    }
                }
            }

            void Register()
            {
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_static_shield_AuraScript::HandleTriggerSpell, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
            }
        };

        AuraScript* GetAuraScript() const
        {
            return new spell_static_shield_AuraScript();
        }
};

void AddSC_boss_nalak()
{
    new boss_nalak();
    new spell_static_shield();
}
