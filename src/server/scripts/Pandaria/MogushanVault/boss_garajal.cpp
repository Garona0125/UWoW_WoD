/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2006-2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
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

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "mogu_shan_vault.h"

enum eSpells
{
    SPELL_BANISHMENT            = 116272,
    SPELL_SOUL_CUT_SUICIDE      = 116278,
    SPELL_SOUL_CUT_DAMAGE       = 117337,

    SPELL_VOODOO_DOLL_VISUAL    = 122151,
    SPELL_VOODOO_DOLL_SHARE     = 116000,
    SPELL_SUMMON_SPIRIT_TOTEM   = 116174,

    // attaques ombreuses
    SPELL_RIFHT_CROSS           = 117215,
    SPELL_LEFT_HOOK             = 117218,
    SPELL_HAMMER_FIST           = 117219,
    SPELL_SWEEPING_KICK         = 117222,

    SPELL_FRENESIE              = 117752,

    // Shadowy Minion
    SPELL_SHADOW_BOLT           = 122118,
    SPELL_SPIRITUAL_GRASP       = 118421,

    // Misc
    SPELL_CLONE                 = 119051,
    SPELL_CLONE_VISUAL          = 119053,
    SPELL_LIFE_FRAGILE_THREAD   = 116227,
    SPELL_CROSSED_OVER          = 116161, // Todo : virer le summon

    SPELL_FRAIL_SOUL            = 117723,
};

enum eEvents
{
    EVENT_SECONDARY_ATTACK      = 1,
    EVENT_SUMMON_TOTEM          = 2,
    EVENT_VOODOO_DOLL           = 3,
    EVENT_BANISHMENT            = 4,

    // Shadowy Minion
    EVENT_SHADOW_BOLT           = 5,
    EVENT_SPIRITUAL_GRASP       = 6,
};

class boss_garajal : public CreatureScript
{
    public:
        boss_garajal() : CreatureScript("boss_garajal") {}

        struct boss_garajalAI : public BossAI
        {
            boss_garajalAI(Creature* creature) : BossAI(creature, DATA_GARAJAL)
            {
                pInstance = creature->GetInstanceScript();
            }

            InstanceScript* pInstance;
            uint64 voodooTargets[3];

            void Reset()
            {
                _Reset();

                events.ScheduleEvent(EVENT_SECONDARY_ATTACK, urand(5000, 10000));
                events.ScheduleEvent(EVENT_SUMMON_TOTEM,     urand(27500, 32500));
                events.ScheduleEvent(EVENT_VOODOO_DOLL,      2500);
            }

            void JustSummoned(Creature* summon)
            {
                summons.Summon(summon);
            }

            void SummonedCreatureDespawn(Creature* summon)
            {
                summons.Despawn(summon);
            }

            void DamageTaken(Unit* attacker, uint32& damage)
            {
                if (!pInstance)
                    return;

                if (!me->HasAura(SPELL_FRENESIE))
                    if (me->HealthBelowPctDamaged(20, damage))
                        me->CastSpell(me, SPELL_FRENESIE, true);
            }

            void UpdateAI(const uint32 diff)
            {
                if (!UpdateVictim())
                    return;

                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                events.Update(diff);

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch(eventId)
                    {
                        case EVENT_SECONDARY_ATTACK:
                        {
                            if (Unit* target = SelectTarget(SELECT_TARGET_TOPAGGRO))
                            {
                                uint32 spellId = RAND(SPELL_RIFHT_CROSS, SPELL_LEFT_HOOK, SPELL_HAMMER_FIST, SPELL_SWEEPING_KICK);
                                me->CastSpell(target, spellId, true);
                            }
                            events.ScheduleEvent(EVENT_SECONDARY_ATTACK, urand(5000, 10000));
                            break;
                        }
                        case EVENT_SUMMON_TOTEM:
                        {
                            float x = 0.0f, y = 0.0f;
                            GetRandPosFromCenterInDist(4277.08f, 1341.35f, frand(0.0f, 30.0f), x, y);
                            me->CastSpell(x, y, 454.55f, SPELL_SUMMON_SPIRIT_TOTEM, true);
                            events.ScheduleEvent(EVENT_SUMMON_TOTEM,     urand(27500, 32500));
                            break;
                        }
                        case EVENT_VOODOO_DOLL:
                        {
                            pInstance->DoRemoveAurasDueToSpellOnPlayers(SPELL_VOODOO_DOLL_VISUAL);
                            pInstance->DoRemoveAurasDueToSpellOnPlayers(SPELL_VOODOO_DOLL_SHARE);

                            uint8 mobCount = Is25ManRaid() ? 4: 3;

                            for (uint8 i = 0; i < mobCount; ++i)
                            {
                                if (Unit* target = SelectTarget(i == 0 ? SELECT_TARGET_TOPAGGRO:SELECT_TARGET_RANDOM, 0, 0, true, -SPELL_VOODOO_DOLL_VISUAL))
                                {
                                    voodooTargets[i] = target->GetGUID();
                                    target->AddAura(SPELL_VOODOO_DOLL_VISUAL, target);
                                }
                            }

                            for (uint8 i = 0; i < 3; ++i)
                                if (Player* caster = sObjectAccessor->GetPlayer(*me, voodooTargets[i]))
                                    for (uint8 j = 0; j < 3; ++j)
                                        if (j != i)
                                            if (Player* target = sObjectAccessor->GetPlayer(*me, voodooTargets[j]))
                                                caster->CastSpell(target, SPELL_VOODOO_DOLL_SHARE, true);
                            break;
                        }
                        case EVENT_BANISHMENT:
                        {
                            if (Unit* target = SelectTarget(SELECT_TARGET_TOPAGGRO))
                            {
                                me->AddAura(SPELL_BANISHMENT,       target);
                                me->AddAura(SPELL_SOUL_CUT_SUICIDE, target);
                                me->AddAura(SPELL_SOUL_CUT_DAMAGE,  target);

                                Difficulty difficulty = me->GetMap()->GetDifficulty();
                                uint64 viewerGuid = difficulty != RAID_TOOL_DIFFICULTY ? target->GetGUID(): 0;
                                uint8  mobCount   = IsHeroic() ? 3: 1;

                                for (uint8 i = 0; i < mobCount; ++i)
                                    if (Creature* soulCutter = me->SummonCreature(NPC_SOUL_CUTTER, target->GetPositionX() + 2.0f, target->GetPositionY() + 2.0f, target->GetPositionZ(), 0.0f, TEMPSUMMON_TIMED_DESPAWN, 30000, i == 0 ? viewerGuid: 0))
                                    {
                                        soulCutter->AI()->AttackStart(target);
                                        soulCutter->getThreatManager().addThreat(target, 1000000000.0f);
                                    }

                                me->getThreatManager().resetAllAggro();
                            }

                            pInstance->DoRemoveAurasDueToSpellOnPlayers(SPELL_VOODOO_DOLL_VISUAL);
                            pInstance->DoRemoveAurasDueToSpellOnPlayers(SPELL_VOODOO_DOLL_SHARE);

                            events.ScheduleEvent(EVENT_VOODOO_DOLL, 5000);
                            events.ScheduleEvent(EVENT_BANISHMENT, 90000);
                            break;
                        }
                        default:
                            break;
                    }
                }

                DoMeleeAttackIfReady();
            }
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new boss_garajalAI(creature);
        }
};

class mob_spirit_totem : public CreatureScript
{
    public:
        mob_spirit_totem() : CreatureScript("mob_spirit_totem") {}

        struct mob_spirit_totemAI : public ScriptedAI
        {
            mob_spirit_totemAI(Creature* creature) : ScriptedAI(creature)
            {}

            void Reset()
            {}

            void JustDied(Unit* attacker)
            {
                std::list<Player*> playerList;
                GetPlayerListInGrid(playerList, me, 3.0f);

                uint8 count = 0;
                for (auto player: playerList)
                {
                    if (player->HasAura(SPELL_VOODOO_DOLL_VISUAL) || player->HasAura(SPELL_FRAIL_SOUL))
                        continue;

                    if (++count > 3)
                        break;

                    if (Creature* clone = me->SummonCreature(56405, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation()))
                    {
                        player->CastSpell(player, SPELL_CLONE_VISUAL, true);
                        player->CastSpell(player, SPELL_CROSSED_OVER, true);

                        player->CastSpell(clone,  SPELL_CLONE, true);

                        clone->CastSpell(clone, SPELL_LIFE_FRAGILE_THREAD, true);
                        clone->GetMotionMaster()->MoveTakeoff(1, clone->GetPositionX(), clone->GetPositionY(), clone->GetPositionZ() + 10.0f);

                        player->AddAura(SPELL_LIFE_FRAGILE_THREAD, player);
                    }
                }
            }
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new mob_spirit_totemAI(creature);
        }
};

class mob_shadowy_minion : public CreatureScript
{
    public:
        mob_shadowy_minion() : CreatureScript("mob_shadowy_minion") {}

        struct mob_shadowy_minionAI : public Scripted_NoMovementAI
        {
            mob_shadowy_minionAI(Creature* creature) : Scripted_NoMovementAI(creature)
            {}

            uint64 spiritGuid;
            EventMap events;

            void Reset()
            {
                events.Reset();
                spiritGuid = 0;

                if (me->GetEntry() == NPC_SHADOWY_MINION_REAL)
                {
                    if (Creature* spirit = me->SummonCreature(NPC_SHADOWY_MINION_SPIRIT, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation(), TEMPSUMMON_DEAD_DESPAWN))
                    {
                        spiritGuid = spirit->GetGUID();
                        spirit->SetPhaseMask(2, true);
                    }

                    events.ScheduleEvent(EVENT_SPIRITUAL_GRASP, urand(2000, 5000));
                }
                else
                    events.ScheduleEvent(EVENT_SHADOW_BOLT, urand(2000, 5000));

                DoZoneInCombat();
            }

            void SummonedCreatureDespawn(Creature* summon)
            {
                if (summon->GetEntry() == NPC_SHADOWY_MINION_SPIRIT)
                    me->DespawnOrUnsummon();
            }

            void UpdateAI(const uint32 diff)
            {
                events.Update(diff);

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        // Spirit World
                        case EVENT_SHADOW_BOLT:
                        {
                            if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM))
                                me->CastSpell(target, SPELL_SHADOW_BOLT, false);

                            events.ScheduleEvent(EVENT_SHADOW_BOLT, urand(2000, 5000));
                            break;
                        }
                        // Real World
                        case EVENT_SPIRITUAL_GRASP:
                        {
                            if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM))
                                me->CastSpell(target, SPELL_SPIRITUAL_GRASP, false);

                            events.ScheduleEvent(EVENT_SPIRITUAL_GRASP, urand(5000, 8000));
                            break;
                        }
                    }
                }
            }
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new mob_shadowy_minionAI(creature);
        }
};

class mob_soul_cutter : public CreatureScript
{
    public:
        mob_soul_cutter() : CreatureScript("mob_soul_cutter") {}

        struct mob_soul_cutterAI : public ScriptedAI
        {
            mob_soul_cutterAI(Creature* creature) : ScriptedAI(creature)
            {}

            void Reset()
            {}

            void JustDied(Unit* attacker)
            {
                if (Unit* target = SelectTarget(SELECT_TARGET_TOPAGGRO, 0, 0, true, SPELL_SOUL_CUT_SUICIDE))
                {
                    target->RemoveAurasDueToSpell(SPELL_SOUL_CUT_SUICIDE);
                    target->RemoveAurasDueToSpell(SPELL_SOUL_CUT_DAMAGE);
                }
            }
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new mob_soul_cutterAI(creature);
        }
};

// Soul Back - 120715
class spell_soul_back : public SpellScriptLoader
{
    public:
        spell_soul_back() : SpellScriptLoader("spell_soul_back") { }

        class spell_soul_back_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_soul_back_SpellScript);

            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                {
                    // SPELL_LIFE_FRAGILE_THREAD removed by default effect
                    target->RemoveAurasDueToSpell(SPELL_CLONE_VISUAL);
                    target->RemoveAurasDueToSpell(SPELL_CROSSED_OVER);
                    target->AddAura(SPELL_FRAIL_SOUL, target);
                    target->SetHealth(target->GetMaxHealth() * 0.3);

                    // Todo : Jump le joueur l� ou �tait son corps
                }
            }

            void Register()
            {
                OnEffectHitTarget += SpellEffectFn(spell_soul_back_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_REMOVE_AURA);
            }
        };

        SpellScript* GetSpellScript() const
        {
            return new spell_soul_back_SpellScript();
        }
};

void AddSC_boss_garajal()
{
    new boss_garajal();
    new mob_spirit_totem();
    new mob_shadowy_minion();
    new mob_soul_cutter();
    new spell_soul_back();
}