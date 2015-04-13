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
#include "DBCStores.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "ObjectMgr.h"
#include "GuildMgr.h"
#include "SpellMgr.h"
#include "Log.h"
#include "Opcodes.h"
#include "Spell.h"
#include "Totem.h"
#include "TemporarySummon.h"
#include "SpellAuras.h"
#include "CreatureAI.h"
#include "ScriptMgr.h"
#include "GameObjectAI.h"
#include "SpellAuraEffects.h"
#include "SpellPackets.h"

void WorldSession::HandleUseItemOpcode(WorldPacket& recvPacket)
{
    // TODO: add targets.read() check
    Player* pUser = _player;

    // ignore for remote control state
    if (pUser->m_mover != pUser)
        return;

    uint8 bagIndex, slot;
    ObjectGuid itemGUID;

    recvPacket >> bagIndex >> slot >> itemGUID;

    WorldPackets::Spells::CastSpell cast(std::move(recvPacket));
    cast.Read();

    // client provided targets
    SpellCastTargets targets(pUser, cast.Cast.Target);

    if (cast.Cast.Misc >= MAX_GLYPH_SLOT_INDEX)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, NULL, NULL);
        return;
    }

    Item* pItem = pUser->GetUseableItemByPos(bagIndex, slot);
    if (!pItem)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, NULL, NULL);
        return;
    }

    if (pItem->GetGUID() != itemGUID)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, NULL, NULL);
        return;
    }

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_USE_ITEM packet, bagIndex: %u, slot: %u, castCount: %u, spellId: %u, Item: %u, glyphIndex: %u, data length = %i", bagIndex, slot, cast.Cast.CastID, cast.Cast.SpellID, pItem->GetEntry(), cast.Cast.Misc, (uint32)recvPacket.size());

    ItemTemplate const* proto = pItem->GetTemplate();
    if (!proto)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pItem, NULL);
        return;
    }

    // some item classes can be used only in equipped state
    if (proto->GetInventoryType() != INVTYPE_NON_EQUIP && !pItem->IsEquipped())
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pItem, NULL);
        return;
    }

    InventoryResult msg = pUser->CanUseItem(pItem);
    if (msg != EQUIP_ERR_OK)
    {
        pUser->SendEquipError(msg, pItem, NULL);
        return;
    }

    // only allow conjured consumable, bandage, poisons (all should have the 2^21 item flag set in DB)
    if (proto->Class == ITEM_CLASS_CONSUMABLE && !(proto->Flags & ITEM_PROTO_FLAG_USEABLE_IN_ARENA) && pUser->InArena())
    {
        pUser->SendEquipError(EQUIP_ERR_NOT_DURING_ARENA_MATCH, pItem, NULL);
        return;
    }

    //Citron-infused bandages, hack fix
    if (pItem->GetEntry() == 82787 && pUser->GetMap()->IsDungeon())
    {
        pUser->SendEquipError(EQUIP_ERR_CLIENT_LOCKED_OUT, pItem, NULL);
        return;
    }

    // don't allow items banned in arena
    if (proto->Flags & ITEM_PROTO_FLAG_NOT_USEABLE_IN_ARENA && pUser->InArena())
    {
        pUser->SendEquipError(EQUIP_ERR_NOT_DURING_ARENA_MATCH, pItem, NULL);
        return;
    }

    if (pUser->isInCombat())
    {
        for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(proto->Spells[i].SpellId))
            {
                if (!spellInfo->CanBeUsedInCombat())
                {
                    pUser->SendEquipError(EQUIP_ERR_NOT_IN_COMBAT, pItem, NULL);
                    return;
                }
            }
        }
    }

    // check also  BIND_WHEN_PICKED_UP and BIND_QUEST_ITEM for .additem or .additemset case by GM (not binded at adding to inventory)
    if (pItem->GetTemplate()->Bonding == BIND_WHEN_USE || pItem->GetTemplate()->Bonding == BIND_WHEN_PICKED_UP || pItem->GetTemplate()->Bonding == BIND_QUEST_ITEM)
    {
        if (!pItem->IsSoulBound())
        {
            pItem->SetState(ITEM_CHANGED, pUser);
            pItem->SetBinding(true);
        }
    }

    // Note: If script stop casting it must send appropriate data to client to prevent stuck item in gray state.
    if (!sScriptMgr->OnItemUse(pUser, pItem, targets))
    {
        // no script or script not process request by self
        pUser->CastItemUseSpell(pItem, targets, cast.Cast.CastID, cast.Cast.Misc);
    }
}

void WorldSession::HandleGameObjectUseOpcode(WorldPacket & recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_GAMEOBJ_USE Message [guid=%u]", guid.GetCounter());

    // ignore for remote control state
    if (_player->m_mover != _player)
        return;

    if (GameObject* obj = GetPlayer()->GetMap()->GetGameObject(guid))
        obj->Use(_player);
}

void WorldSession::HandleGameobjectReportUse(WorldPacket& recvPacket)
{
    ObjectGuid guid;
    recvPacket >> guid;

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_GAMEOBJ_REPORT_USE Message [in game guid: %u]", guid.GetCounter());

    if (GameObject* go = GetPlayer()->GetMap()->GetGameObject(guid))
    {
        // ignore for remote control state
        if (_player->m_mover != _player)
            if (!(_player->IsOnVehicle(_player->m_mover) || _player->IsMounted()) && !go->GetGOInfo()->IsUsableMounted())
                return;

        if (!go->IsWithinDistInMap(_player, INTERACTION_DISTANCE))
            return;

        if (go->GetEntry() == 193905 || go->GetEntry() == 193967 || //Chest Alexstrasza's Gift
            go->GetEntry() == 194158 || go->GetEntry() == 194159)   //Chest Heart of Magic
            _player->CastSpell(go, 6247, true);
        else
            go->AI()->GossipHello(_player);

        _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_USE_GAMEOBJECT, go->GetEntry());
    }
}

void WorldSession::HandleCastSpellOpcode(WorldPackets::Spells::CastSpell& cast)
{
    bool replaced = false;
    // ignore for remote control state (for player case)
    Unit* mover = _player->m_mover;
    if (mover != _player && mover->GetTypeId() == TYPEID_PLAYER)
    {
        sLog->outError(LOG_FILTER_NETWORKIO, "WORLD: mover != _player id %u", cast.Cast.SpellID);
        return;
    }

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: got cast spell packet, castCount: %u, spellId: %u, glyphIndex %u", cast.Cast.CastID, cast.Cast.SpellID, cast.Cast.Misc);

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(cast.Cast.SpellID);
    if (!spellInfo)
    {
        sLog->outError(LOG_FILTER_NETWORKIO, "WORLD: unknown spell id %u", cast.Cast.SpellID);
        return;
    }

    if (spellInfo->IsPassive())
        return;

    if (mover->GetTypeId() == TYPEID_PLAYER)
    {
        // not have spell in spellbook or spell passive and not casted by client
        if ((!mover->ToPlayer()->HasActiveSpell(cast.Cast.SpellID) || spellInfo->IsPassive()) && !spellInfo->ResearchProject && cast.Cast.SpellID != 101054 && !spellInfo->HasEffect(SPELL_EFFECT_OPEN_LOCK) && !spellInfo->HasEffect(SPELL_EFFECT_LOOT_BONUS) &&
            !(spellInfo->AttributesEx8 & SPELL_ATTR8_RAID_MARKER))
        {
            if (cast.Cast.SpellID == 101603)
            {
                mover->RemoveAurasDueToSpell(107837);
                mover->RemoveAurasDueToSpell(101601);
            }
            if (cast.Cast.SpellID == 119393)
            {
                mover->RemoveAurasDueToSpell(119388);
                mover->RemoveAurasDueToSpell(119386);
            }
            else
            {
                //cheater? kick? ban?
                sLog->outError(LOG_FILTER_NETWORKIO, "WORLD: cheater? kick? ban? TYPEID_PLAYER spell id %u", cast.Cast.SpellID);
                return;
            }
        }
    }
    else
    {
        // spell passive and not casted by client
        if (spellInfo->IsPassive())
        {
            sLog->outError(LOG_FILTER_NETWORKIO, "WORLD: spell passive and not casted by client id %u", cast.Cast.SpellID);
            return;
        }
        // not have spell in spellbook or spell passive and not casted by client
        if ((mover->GetTypeId() == TYPEID_UNIT && !mover->ToCreature()->HasSpell(cast.Cast.SpellID)) || spellInfo->IsPassive())
            if (mover->GetTypeId() == TYPEID_UNIT && !mover->ToCreature()->HasSpell(cast.Cast.SpellID))
        {
            if (_player->HasActiveSpell(cast.Cast.SpellID))
                mover = (Unit*)_player;
            else
            {
                //cheater? kick? ban?
                sLog->outError(LOG_FILTER_NETWORKIO, "WORLD: not have spell in spellbook id %u", cast.Cast.SpellID);
                return;
            }
        }
    }

    // process spells overriden by SpecializationSpells.dbc
    for (std::set<SpecializationSpellEntry const*>::const_iterator itr = spellInfo->SpecializationOverrideSpellList.begin(); itr != spellInfo->SpecializationOverrideSpellList.end(); ++itr)
    {
        if (_player->HasSpell((*itr)->LearnSpell))
        {
            if (SpellInfo const* overrideSpellInfo = sSpellMgr->GetSpellInfo((*itr)->LearnSpell))
            {
                spellInfo = overrideSpellInfo;
                cast.Cast.SpellID = overrideSpellInfo->Id;
                replaced = true;
            }
            break;
        }
    }

    // process spellOverride column replacements of Talent.dbc
    if (Player* plMover = mover->ToPlayer())
    {
        PlayerTalentMap const* talents = plMover->GetTalentMap(plMover->GetActiveSpec());
        for (PlayerTalentMap::const_iterator itr = talents->begin(); itr != talents->end(); ++itr)
        {
            if (itr->second->state == PLAYERSPELL_REMOVED)
                continue;

            if (itr->second->talentEntry->spellOverride == cast.Cast.SpellID)
            {
                if (SpellInfo const* newInfo = sSpellMgr->GetSpellInfo(itr->second->talentEntry->spellId))
                {
                    spellInfo = newInfo;
                    cast.Cast.SpellID = newInfo->Id;
                replaced = true;
                }
                break;
            }
        }
    }

    Unit::AuraEffectList swaps = mover->GetAuraEffectsByType(SPELL_AURA_OVERRIDE_ACTIONBAR_SPELLS);
    Unit::AuraEffectList const& swaps2 = mover->GetAuraEffectsByType(SPELL_AURA_OVERRIDE_ACTIONBAR_SPELLS_2);
    if (!swaps2.empty())
        swaps.insert(swaps.end(), swaps2.begin(), swaps2.end());

    if (!swaps.empty())
    {
        for (Unit::AuraEffectList::const_iterator itr = swaps.begin(); itr != swaps.end(); ++itr)
        {
            if ((*itr)->IsAffectingSpell(spellInfo))
            {
                if (SpellInfo const* newInfo = sSpellMgr->GetSpellInfo((*itr)->GetAmount()))
                {
                    _player->SwapSpellUncategoryCharges(cast.Cast.SpellID, newInfo->Id);
                    spellInfo = newInfo;
                    cast.Cast.SpellID = newInfo->Id;
                    replaced = true;
                }
                break;
            }
        }
    }

    // Client is resending autoshot cast opcode when other spell is casted during shoot rotation
    // Skip it to prevent "interrupt" message
    if (spellInfo->IsAutoRepeatRangedSpell() && _player->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL)
        && _player->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL)->m_spellInfo == spellInfo)
        return;

    // can't use our own spells when we're in possession of another unit,
    if (_player->isPossessing())
    {
        sLog->outError(LOG_FILTER_NETWORKIO, "WORLD: can't use our own spells when we're in possession id %u", cast.Cast.SpellID);
        return;
    }

    // Check possible spell cast overrides
    //603 TODO
    //spellInfo = caster->GetCastSpellInfo(spellInfo);

    // client provided targets
    SpellCastTargets targets(mover, cast.Cast.Target);
    // auto-selection buff level base at target level (in spellInfo)
    if (targets.GetUnitTarget())
    {
        SpellInfo const* actualSpellInfo = spellInfo->GetAuraRankForLevel(targets.GetUnitTarget()->getLevel());

        // if rank not found then function return NULL but in explicit cast case original spell can be casted and later failed with appropriate error message
        if (actualSpellInfo)
            spellInfo = actualSpellInfo;
    }

    // Custom spell overrides
    // are most of these still needed?
    switch (spellInfo->Id)
    {
//         case 15407: // Mind Flay
//         {
//             if (Unit * target = targets.GetUnitTarget())
//             {
//                 if (_player->HasAura(139139) && target->HasAura(2944, _player->GetGUID()))
//                 {
//                     if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(129197))
//                     {
//                         spellInfo = newSpellInfo;
//                         spellId = newSpellInfo->Id;
//                     }
//                 }
//             }
//             break;
//         }
        case 686: //Shadow Bolt
        {
            if (_player->HasSpell(112092)) //Shadow Bolt (Glyphed)
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(112092))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 105174: //Hand of Gul'dan
        {
            if (_player->HasSpell(123194)) //Glyph of Hand of Gul'dan
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(123194))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 18540: //Summon Terrorguard
        {
            if (_player->HasSpell(112927))
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(112927))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 1122: //Summon Abyssal
        {
            if (_player->HasSpell(112921))
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(112921))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 30146: //Summon Wrathguard
        {
            if (_player->HasSpell(112870))
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(112870))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 712: //Summon Shivarra
        {
            if (_player->HasSpell(112868))
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(112868))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 691: //Summon Observer
        {
            if (_player->HasSpell(112869))
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(112869))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 697: //Summon Voidlord
        {
            if (_player->HasSpell(112867))
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(112867))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 688: //Summon Fel Imp
        {
            if (_player->HasSpell(112866))
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(112866))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 19434:         // Aimed Shot - 19434 and Aimed Shot (for Master Marksman) - 82928
        {
            if (_player->HasAura(82926))
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(82928))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 113860:        // Fix Dark Soul for Destruction warlocks
        {
            if (_player->GetSpecializationId(_player->GetActiveSpec()) == SPEC_WARLOCK_DESTRUCTION)
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(113858))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 123273:        // Surging Mist - 123273 and Surging Mist - 116995
        case 116694:        // Surging Mist - 116694 and Surging Mist - 116995
        {
            // Surging Mist is instantly casted if player is channeling Soothing Mist
            if (_player->GetCurrentSpell(CURRENT_CHANNELED_SPELL) && _player->GetCurrentSpell(CURRENT_CHANNELED_SPELL)->GetSpellInfo()->Id == 115175)
            {
                _player->CastSpell(targets.GetUnitTarget(), 116995, true);
                _player->EnergizeBySpell(_player, 116995, 1, POWER_CHI);
                int32 powerCost = spellInfo->CalcPowerCost(_player, spellInfo->GetSchoolMask());
                _player->ModifyPower(POWER_MANA, -powerCost, true);
                return;
            }
            break;
        }
        case 120517:         // Halo - 120517 and Halo - 120644 (shadow form)
        {
            if (_player->HasAura(15473))
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(120644))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 121135:        // Cascade (shadow) - 127632 and Cascade - 121135
        {
            if (_player->HasAura(15473))
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(127632))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 124682:        // Enveloping Mist - 124682 and Enveloping Mist - 132120
        {
            // Enveloping Mist is instantly casted if player is channeling Soothing Mist
            if (_player->GetCurrentSpell(CURRENT_CHANNELED_SPELL) && _player->GetCurrentSpell(CURRENT_CHANNELED_SPELL)->GetSpellInfo()->Id == 115175)
            {
                _player->CastSpell(targets.GetUnitTarget(), 132120, true);
                int32 powerCost = spellInfo->CalcPowerCost(_player, spellInfo->GetSchoolMask());
                _player->ModifyPower(POWER_CHI, -powerCost, true);
                return;
            }
            break;
        }
        case 126892:        // Zen Pilgrimage - 126892 and Zen Pilgrimage : Return - 126895
        {
            if (_player->HasAura(126896))
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(126895))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 127538:        // Savage Roar
        {
            if (_player->GetComboPoints())
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(52610))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
        case 129250:        // Power Word : Solace - 129250 and Power Word : Insanity - 129249
        {
            if (_player->GetShapeshiftForm() == FORM_SHADOW)
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(129249))
                {
                    spellInfo = newSpellInfo;
                    cast.Cast.SpellID = newSpellInfo->Id;
                    replaced = true;
                }
            }
            break;
        }
    }

    targets.m_weights.resize(cast.Cast.Weight.size());
    for (uint8 i = 0; i < cast.Cast.Weight.size(); ++i)
    {
        targets.m_weights[i].type = cast.Cast.Weight[i].Type;
        switch (targets.m_weights[i].type)
        {
            case WEIGHT_KEYSTONE:
                targets.m_weights[i].keystone.itemId = cast.Cast.Weight[i].ID;
                targets.m_weights[i].keystone.itemCount = cast.Cast.Weight[i].Quantity;
                break;
            case WEIGHT_FRAGMENT:
                targets.m_weights[i].fragment.currencyId = cast.Cast.Weight[i].ID;
                targets.m_weights[i].fragment.currencyCount = cast.Cast.Weight[i].Quantity;
                break;
            default:
                targets.m_weights[i].raw.id = cast.Cast.Weight[i].ID;
                targets.m_weights[i].raw.count = cast.Cast.Weight[i].Quantity;
                break;
        }
    }

    Spell* spell = new Spell(mover, spellInfo, TRIGGERED_NONE, ObjectGuid::Empty, false, replaced);
    spell->m_cast_count = cast.Cast.CastID;                         // set count of casts
    spell->m_misc.Data = cast.Cast.Misc;                            // 6.x Misc is just a guess
    spell->prepare(&targets);
}

void WorldSession::HandleCancelCastOpcode(WorldPacket& recvPacket)
{
    uint32 spellId;

    bool hasSpell = !recvPacket.ReadBit();
    bool hasCastCount = !recvPacket.ReadBit();
    if (hasCastCount)
        recvPacket.read_skip<uint8>();                      // counter, increments with every CANCEL packet, don't use for now
    if (hasSpell)
        recvPacket >> spellId;

    if (!spellId)
        return;

    if (_player->IsNonMeleeSpellCasted(false))
        _player->InterruptNonMeleeSpells(false, spellId, false);
}

void WorldSession::HandleCancelAuraOpcode(WorldPacket& recvPacket)
{
    ObjectGuid guid;
    uint32 spellId;
    recvPacket >> spellId;

    recvPacket.ReadBit();   // guid marker
    //recvPacket.ReadGuidMask<0, 4, 7, 1, 6, 2, 3, 5>(guid);
    //recvPacket.ReadGuidBytes<4, 2, 7, 1, 5, 0, 3, 6>(guid);

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return;

    // not allow remove spells with attr SPELL_ATTR0_CANT_CANCEL
    if (spellInfo->Attributes & SPELL_ATTR0_CANT_CANCEL)
        return;

    // channeled spell case (it currently casted then)
    if (spellInfo->IsChanneled())
    {
        if (Spell* curSpell = _player->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            if (curSpell->m_spellInfo->Id == spellId)
                _player->InterruptSpell(CURRENT_CHANNELED_SPELL);
        return;
    }

    // non channeled case:
    // don't allow remove non positive spells
    // don't allow cancelling passive auras (some of them are visible)
    if (!spellInfo->IsPositive() || spellInfo->IsPassive())
        return;

    // maybe should only remove one buff when there are multiple?
    _player->RemoveOwnedAura(spellId, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
}

//! 5.4.1
void WorldSession::HandlePetCancelAuraOpcode(WorldPacket& recvPacket)
{
    ObjectGuid guid;
    uint32 spellId;

    recvPacket >> spellId;

    //recvPacket.WriteGuidMask<7, 2, 6, 4, 1, 5, 0, 3>(guid);
    //recvPacket.WriteGuidBytes<0, 2, 3, 7, 4, 1, 6, 5>(guid);

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
    {
        sLog->outError(LOG_FILTER_NETWORKIO, "WORLD: unknown PET spell id %u", spellId);
        return;
    }

    Creature* pet=ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, guid);

    if (!pet)
    {
        sLog->outError(LOG_FILTER_NETWORKIO, "HandlePetCancelAura: Attempt to cancel an aura for non-existant pet %u by player '%s'", uint32(guid.GetCounter()), GetPlayer()->GetName());
        return;
    }

    if (pet != GetPlayer()->GetGuardianPet() && pet != GetPlayer()->GetCharm())
    {
        sLog->outError(LOG_FILTER_NETWORKIO, "HandlePetCancelAura: Pet %u is not a pet of player '%s'", uint32(guid.GetCounter()), GetPlayer()->GetName());
        return;
    }

    if (!pet->isAlive())
    {
        pet->SendPetActionFeedback(FEEDBACK_PET_DEAD);
        return;
    }

    pet->RemoveOwnedAura(spellId, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);

    pet->AddCreatureSpellCooldown(spellId);
}

void WorldSession::HandleCancelGrowthAuraOpcode(WorldPacket& /*recvPacket*/)
{
}

void WorldSession::HandleCancelAutoRepeatSpellOpcode(WorldPacket& /*recvPacket*/)
{
    // may be better send SMSG_CANCEL_AUTO_REPEAT?
    // cancel and prepare for deleting
    _player->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
}

void WorldSession::HandleCancelChanneling(WorldPacket& recvData)
{
    recvData.read_skip<uint32>();                          // spellid, not used

    // ignore for remote control state (for player case)
    Unit* mover = _player->m_mover;
    if (mover != _player && mover->GetTypeId() == TYPEID_PLAYER)
        return;

    mover->InterruptSpell(CURRENT_CHANNELED_SPELL);
}

void WorldSession::HandleTotemDestroyed(WorldPacket& recvPacket)
{
    // ignore for remote control state
    if (_player->m_mover != _player)
        return;

    uint8 slotId;
    ObjectGuid guid;

    recvPacket >> slotId;
    //recvPacket.ReadGuidMask<3, 5, 2, 0, 4, 1, 6, 7>(guid);
    //recvPacket.ReadGuidBytes<4, 1, 5, 2, 3, 6, 7, 0>(guid);
    ObjectGuid::LowType logGuid = guid.GetCounter();

    ++slotId;

    if (slotId >= MAX_TOTEM_SLOT)
        return;

    if (!_player->m_SummonSlot[slotId])
        return;

    if(Creature* summon = GetPlayer()->GetMap()->GetCreature(_player->m_SummonSlot[slotId]))
    {
        if(uint32 spellId = summon->GetUInt32Value(UNIT_FIELD_CREATED_BY_SPELL))
            if(AreaTrigger* arTrigger = _player->GetAreaObject(spellId))
                arTrigger->SetDuration(0);
        summon->DespawnOrUnsummon();
    }
}

void WorldSession::HandleSelfResOpcode(WorldPacket& /*recvData*/)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_SELF_RES");                  // empty opcode

    if (_player->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION))
        return; // silent return, client should display error by itself and not send this opcode

    if (_player->GetUInt32Value(PLAYER_FIELD_SELF_RES_SPELL))
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(_player->GetUInt32Value(PLAYER_FIELD_SELF_RES_SPELL));
        if (spellInfo)
            _player->CastSpell(_player, spellInfo, false, 0);

        _player->SetUInt32Value(PLAYER_FIELD_SELF_RES_SPELL, 0);
    }
}

void WorldSession::HandleSpellClick(WorldPacket& recvData)
{
    time_t now = time(NULL);
    if (now - timeLastHandleSpellClick < 2)
    {
        recvData.rfinish();
        return;
    }
    else
       timeLastHandleSpellClick = now;

    ObjectGuid guid;
    //recvData.ReadGuidMask<1, 7, 2, 5, 0, 6>(guid);
    recvData.ReadBit();
    //recvData.ReadGuidMask<3, 4>(guid);

    //recvData.ReadGuidBytes<2, 3, 6, 5, 4, 1, 0, 7>(guid);

    // this will get something not in world. crash
    Creature* unit = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, guid);

    if (!unit)
        return;

    // TODO: Unit::SetCharmedBy: 28782 is not in world but 0 is trying to charm it! -> crash
    if (!unit->IsInWorld())
        return;

    // flags in Deepwind Gorge
    if (unit->GetEntry() == 53194)
    {
        _player->CastSpell(unit, unit->GetInt32Value(UNIT_FIELD_INTERACT_SPELL_ID));
        return;
    }

    unit->HandleSpellClick(_player);
}

void WorldSession::HandleMirrorImageDataRequest(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_GET_MIRRORIMAGE_DATA");
    ObjectGuid guid;
    uint32 displayId;

    recvData >> displayId;
    //recvData.ReadGuidMask<0, 2, 5, 6, 4, 3, 1, 7>(guid);
    //recvData.ReadGuidBytes<2, 0, 1, 3, 7, 6, 5, 4>(guid);

    // Get unit for which data is needed by client
    Unit* unit = ObjectAccessor::GetObjectInWorld(guid, (Unit*)NULL);
    if (!unit)
        return;

    if (!unit->HasAuraType(SPELL_AURA_CLONE_CASTER))
        return;

    // Get creator of the unit (SPELL_AURA_CLONE_CASTER does not stack)
    Unit* creator = unit->GetAuraEffectsByType(SPELL_AURA_CLONE_CASTER).front()->GetCaster();
    if (!creator)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_GET_MIRRORIMAGE_DATA displayId %u, creator not found", displayId);
        creator = unit;
    }

    Player* player = creator->ToPlayer();
    ObjectGuid guildGuid;
    if (uint32 guildId = player ? player->GetGuildId() : 0)
        if (Guild* guild = sGuildMgr->GetGuildById(guildId))
            guildGuid = guild->GetGUID();

    WorldPacket data(SMSG_MIRRORIMAGE_DATA, 80);
    //data.WriteGuidMask<5>(guildGuid);
    //data.WriteGuidMask<0, 5>(guid);
    //data.WriteGuidMask<4>(guildGuid);
    //data.WriteGuidMask<7, 1, 4>(guid);
    //data.WriteGuidMask<3, 7, 2>(guildGuid);
    //data.WriteGuidMask<6>(guid);
    //data.WriteGuidMask<1, 6>(guildGuid);
    //data.WriteGuidMask<3>(guid);

    uint32 slotCount = 0;
    uint32 bitpos = data.bitwpos();
    data.WriteBits(slotCount, 22);

    //data.WriteGuidMask<2>(guid);
    //data.WriteGuidMask<0>(guildGuid);

    data << uint8(player ? player->GetByteValue(PLAYER_FIELD_BYTES, 1) : 0);   // face
    //data.WriteGuidBytes<2, 7>(guid);
    //data.WriteGuidBytes<6, 1>(guildGuid);
    data << uint8(creator->getGender());
    data << uint8(creator->getClass());
    //data.WriteGuidBytes<1>(guid);
    //data.WriteGuidBytes<0>(guildGuid);
    data << uint8(player ? player->GetByteValue(PLAYER_FIELD_BYTES, 2) : 0);   // hair
    //data.WriteGuidBytes<5, 0>(guid);
    //data.WriteGuidBytes<4, 2>(guildGuid);

    static EquipmentSlots const itemSlots[] =
    {
        EQUIPMENT_SLOT_HEAD,
        EQUIPMENT_SLOT_SHOULDERS,
        EQUIPMENT_SLOT_BODY,
        EQUIPMENT_SLOT_CHEST,
        EQUIPMENT_SLOT_WAIST,
        EQUIPMENT_SLOT_LEGS,
        EQUIPMENT_SLOT_FEET,
        EQUIPMENT_SLOT_WRISTS,
        EQUIPMENT_SLOT_HANDS,
        EQUIPMENT_SLOT_TABARD,
        EQUIPMENT_SLOT_BACK,
        EQUIPMENT_SLOT_END
    };

    // Display items in visible slots
    for (EquipmentSlots const* itr = &itemSlots[0]; *itr != EQUIPMENT_SLOT_END; ++itr)
    {
        if (!player)
            data << uint32(0);
        else if (*itr == EQUIPMENT_SLOT_HEAD && player->HasFlag(PLAYER_FIELD_PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM))
            data << uint32(0);
        else if (*itr == EQUIPMENT_SLOT_BACK && player->HasFlag(PLAYER_FIELD_PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK))
            data << uint32(0);
        else if (Item const* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, *itr))
            data << uint32(item->GetDisplayId());
        else
            data << uint32(0);
        ++slotCount;
    }

    data << uint8(creator->getRace());
    //data.WriteGuidBytes<4>(guid);
    //data.WriteGuidBytes<7>(guildGuid);
    data << uint8(player ? player->GetByteValue(PLAYER_FIELD_BYTES_2, 0) : 0); // facialhair
    //data.WriteGuidBytes<6>(guid);
    data << uint32(creator->GetDisplayId());
    data << uint8(player ? player->GetByteValue(PLAYER_FIELD_BYTES, 0) : 0);   // skin
    //data.WriteGuidBytes<3>(guid);
    //data.WriteGuidBytes<3>(guildGuid);
    data << uint8(player ? player->GetByteValue(PLAYER_FIELD_BYTES, 3) : 0);   // haircolor
    //data.WriteGuidBytes<5>(guildGuid);

    data.PutBits<uint32>(bitpos, slotCount, 22);

    SendPacket(&data);
}

void WorldSession::HandleUpdateProjectilePosition(WorldPacket& recvPacket)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_UPDATE_PROJECTILE_POSITION");

    ObjectGuid casterGuid;
    uint32 spellId;
    uint8 castCount;
    float x, y, z;    // Position of missile hit

    recvPacket >> casterGuid;
    recvPacket >> spellId;
    recvPacket >> castCount;
    recvPacket >> x;
    recvPacket >> y;
    recvPacket >> z;

    Unit* caster = ObjectAccessor::GetUnit(*_player, casterGuid);
    if (!caster)
        return;

    Spell* spell = caster->FindCurrentSpellBySpellId(spellId);
    if (!spell || !spell->m_targets.HasDst())
        return;

    Position pos = *spell->m_targets.GetDstPos();
    pos.Relocate(x, y, z);
    spell->m_targets.ModDst(pos);

    WorldPacket data(SMSG_SET_PROJECTILE_POSITION, 21);
    data << casterGuid;
    data << uint8(castCount);
    data << float(x);
    data << float(y);
    data << float(z);
    caster->SendMessageToSet(&data, true);
}

void WorldSession::HandleUpdateMissileTrajectory(WorldPacket& recvPacket)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_UPDATE_MISSILE_TRAJECTORY");

    ObjectGuid guid;
    uint32 spellId;
    float elevation, speed;
    float curX, curY, curZ;
    float targetX, targetY, targetZ;
    uint8 moveStop;

    recvPacket >> guid >> spellId >> elevation >> speed;
    recvPacket >> curX >> curY >> curZ;
    recvPacket >> targetX >> targetY >> targetZ;
    recvPacket >> moveStop;

    Unit* caster = ObjectAccessor::GetUnit(*_player, guid);
    Spell* spell = caster ? caster->GetCurrentSpell(CURRENT_GENERIC_SPELL) : NULL;
    if (!spell || spell->m_spellInfo->Id != spellId || !spell->m_targets.HasDst() || !spell->m_targets.HasSrc())
    {
        recvPacket.rfinish();
        return;
    }

    Position pos = *spell->m_targets.GetSrcPos();
    pos.Relocate(curX, curY, curZ);
    spell->m_targets.ModSrc(pos);

    pos = *spell->m_targets.GetDstPos();
    pos.Relocate(targetX, targetY, targetZ);
    spell->m_targets.ModDst(pos);

    spell->m_targets.SetElevation(elevation);
    spell->m_targets.SetSpeed(speed);

    if (moveStop)
    {
        uint32 opcode;
        recvPacket >> opcode;
        recvPacket.SetOpcode(CMSG_MOVE_STOP); // always set to MSG_MOVE_STOP in client SetOpcode
        //HandleMovementOpcodes(recvPacket);
    }
}

void WorldSession::HandlerCategoryCooldownOpocde(WorldPacket& recvPacket)
{
    _player->SendCategoryCooldownMods();
}
