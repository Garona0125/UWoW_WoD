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
#include "Language.h"
#include "DatabaseEnv.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Opcodes.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Player.h"
#include "GossipDef.h"
#include "UpdateMask.h"
#include "ObjectAccessor.h"
#include "Creature.h"
#include "Pet.h"
#include "BattlegroundMgr.h"
#include "Battleground.h"
#include "ScriptMgr.h"
#include "CreatureAI.h"
#include "SpellInfo.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "NPCPackets.h"

enum StableResultCode
{
    STABLE_ERR_NONE         = 0x00,                         // does nothing, just resets stable states
    STABLE_ERR_MONEY        = 0x01,                         // "you don't have enough money"
    STABLE_ERR_INVALID_SLOT = 0x03,
    STABLE_ERR_STABLE       = 0x06,                         // currently used in most fail cases
    STABLE_SUCCESS_STABLE   = 0x08,                         // stable success
    STABLE_SUCCESS_UNSTABLE = 0x09,                         // unstable/swap success
    STABLE_SUCCESS_BUY_SLOT = 0x0A,                         // buy slot success
    STABLE_ERR_EXOTIC       = 0x0B,                         // "you are unable to control exotic creatures"
    STABLE_ERR_INTERNAL     = 0x0C,
};

void WorldSession::HandleTabardVendorActivateOpcode(WorldPacket & recvData)
{
    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_TABARDDESIGNER);
    if (!unit)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleTabardVendorActivateOpcode - Unit (GUID: %u) not found or you can not interact with him.", uint32(guid.GetCounter()));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    SendTabardVendorActivate(guid);
}

void WorldSession::SendTabardVendorActivate(ObjectGuid guid)
{
    WorldPacket data(SMSG_TABARDVENDOR_ACTIVATE, 8 + 1);
    data << guid;
    SendPacket(&data);
}

void WorldSession::HandleBankerActivateOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_BANKER_ACTIVATE");

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_BANKER);
    if (!unit)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleBankerActivateOpcode - Unit (GUID: %u) not found or you can not interact with him.", uint32(guid.GetCounter()));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    SendShowBank(guid);
}

void WorldSession::SendShowBank(ObjectGuid guid)
{
    WorldPacket data(SMSG_SHOW_BANK, 8 + 1);
    data << guid;
    SendPacket(&data);
}

void WorldSession::HandleTrainerListOpcode(WorldPacket & recvData)
{
    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    SendTrainerList(guid);
}

void WorldSession::SendTrainerList(ObjectGuid guid)
{
    std::string str = GetTrinityString(LANG_NPC_TAINER_HELLO);
    SendTrainerList(guid, str);
}

void WorldSession::SendTrainerList(ObjectGuid guid, const std::string& strTitle)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: SendTrainerList");

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_TRAINER);
    if (!unit)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: SendTrainerList - Unit (GUID: %u) not found or you can not interact with him.", uint32(guid.GetCounter()));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // trainer list loaded at check;
    if (!unit->isCanTrainingOf(_player, true))
        return;

    CreatureTemplate const* ci = unit->GetCreatureTemplate();

    if (!ci)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: SendTrainerList - (GUID: %u) NO CREATUREINFO!", guid.GetCounter());
        return;
    }

    TrainerSpellData const* trainer_spells = unit->GetTrainerSpells();
    if (!trainer_spells)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: SendTrainerList - Training spells not found for creature (GUID: %u Entry: %u)",
            guid.GetCounter(), unit->GetEntry());
        return;
    }

    WorldPackets::NPC::TrainerList packet;
    packet.TrainerGUID = guid;
    packet.TrainerType = trainer_spells->trainerType;
    packet.Greeting = strTitle;

    // reputation discount
    float fDiscountMod = _player->GetReputationPriceDiscount(unit);
    bool can_learn_primary_prof = GetPlayer()->GetFreePrimaryProfessionPoints() > 0;

    packet.Spells.resize(trainer_spells->spellList.size());
    uint32 count = 0;
    for (TrainerSpellMap::const_iterator itr = trainer_spells->spellList.begin(); itr != trainer_spells->spellList.end(); ++itr)
    {
        TrainerSpell const* tSpell = &itr->second;

        bool valid = true;
        bool primary_prof_first_rank = false;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (!tSpell->learnedSpell[i])
                continue;
            if (!_player->IsSpellFitByClassAndRace(tSpell->learnedSpell[i]))
            {
                valid = false;
                break;
            }
            SpellInfo const* learnedSpellInfo = sSpellMgr->GetSpellInfo(tSpell->learnedSpell[i]);
            if (learnedSpellInfo && learnedSpellInfo->IsPrimaryProfessionFirstRank())
                primary_prof_first_rank = true;
        }
        if (!valid)
            continue;

        if(SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(tSpell->spell))
        {
            if(spellInfo->AttributesEx7 & SPELL_ATTR7_HORDE_ONLY && GetPlayer()->GetTeam() != HORDE)
                continue;
            if(spellInfo->AttributesEx7 & SPELL_ATTR7_ALLIANCE_ONLY && GetPlayer()->GetTeam() != ALLIANCE)
                continue;
        }

        TrainerSpellState state = _player->GetTrainerSpellState(tSpell);

        WorldPackets::NPC::TrainerListSpell& spell = packet.Spells[count];
        spell.SpellID = tSpell->spell;
        spell.MoneyCost = floor(tSpell->spellCost * fDiscountMod);
        spell.ReqSkillLine = tSpell->reqSkill;
        spell.ReqSkillRank = tSpell->reqSkillValue;
        spell.ReqLevel = tSpell->reqLevel;
        spell.Usable = (state == TRAINER_SPELL_GREEN_DISABLED ? TRAINER_SPELL_GREEN : state);

        uint8 maxReq = 0;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (!tSpell->learnedSpell[i])
                continue;

            if (maxReq > MAX_TRAINERSPELL_ABILITY_REQS)
                break;

            if (uint32 prevSpellId = sSpellMgr->GetPrevSpellInChain(tSpell->learnedSpell[i]))
            {
                spell.ReqAbility[maxReq++] = prevSpellId;
                continue;
            }
            SpellsRequiringSpellMapBounds spellsRequired = sSpellMgr->GetSpellsRequiredForSpellBounds(tSpell->learnedSpell[i]);
            for (SpellsRequiringSpellMap::const_iterator itr2 = spellsRequired.first; itr2 != spellsRequired.second; ++itr2)
            {
                spell.ReqAbility[maxReq++] = itr2->second;
                continue;
            }
        }

        ++count;
    }

    // Shrink to actual data size
    packet.Spells.resize(count);

    SendPacket(packet.Write());
}

void WorldSession::HandleTrainerBuySpellOpcode(WorldPacket & recvData)
{
    ObjectGuid guid;
    uint32 spellId;
    int32 trainerId;

    recvData >> guid >> trainerId >> spellId;

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_TRAINER_BUY_SPELL NpcGUID=%u, learn spell id is: %u", uint32(guid.GetCounter()), spellId);

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_TRAINER);
    if (!unit)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleTrainerBuySpellOpcode - Unit (GUID: %u) not found or you can not interact with him.", uint32(guid.GetCounter()));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    if (!unit->isCanTrainingOf(_player, true))
    { 
        SendTrainerService(guid, spellId, 0);
        return;
    }

    // check present spell in trainer spell list
    TrainerSpellData const* trainer_spells = unit->GetTrainerSpells();
    if (!trainer_spells)
    {
        SendTrainerService(guid, spellId, 0);
        return;
    }

    // not found, cheat?
    TrainerSpell const* trainer_spell = trainer_spells->Find(spellId);
    if (!trainer_spell)
    { 
        SendTrainerService(guid, spellId, 0);
        return;
    }

    // can't be learn, cheat? Or double learn with lags...
    if (_player->GetTrainerSpellState(trainer_spell) != TRAINER_SPELL_GREEN)
    { 
        SendTrainerService(guid, spellId, 0);
        return;
    }

    // apply reputation discount
    uint32 nSpellCost = uint32(floor(trainer_spell->spellCost * _player->GetReputationPriceDiscount(unit)));

    // check money requirement
    if (!_player->HasEnoughMoney(uint64(nSpellCost)))
    { 
        SendTrainerService(guid, spellId, 1);
        return;
    }

    _player->ModifyMoney(-int64(nSpellCost));

    unit->SendPlaySpellVisualKit(179, 0);       // 53 SpellCastDirected
    _player->SendPlaySpellVisualKit(362, 1);    // 113 EmoteSalute

    // learn explicitly or cast explicitly
    if (trainer_spell->IsCastable())
        _player->CastSpell(_player, trainer_spell->spell, true);
    else
        _player->learnSpell(spellId, false);

    SendTrainerService(guid, spellId, 2);
}

void WorldSession::SendTrainerService(ObjectGuid guid, uint32 spellId, uint32 result)
{ 
    WorldPacket data(SMSG_TRAINER_SERVICE, 16);
    data << guid;
    data << uint32(spellId);        // should be same as in packet from client
    data << uint32(result);         // 2 == Success. 1 == "Not enough money for trainer service." 0 == "Trainer service %d unavailable."
    SendPacket(&data);
}

void WorldSession::HandleGossipHelloOpcode(WorldPackets::NPC::Hello& packet)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GOSSIP_HELLO");

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(packet.Unit, UNIT_NPC_FLAG_NONE);
    if (!unit)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleGossipHelloOpcode - Unit (GUID: %s) not found or you can not interact with him.", packet.Unit.ToString().c_str());
        return;
    }

    // set faction visible if needed
    if (FactionTemplateEntry const* factionTemplateEntry = sFactionTemplateStore.LookupEntry(unit->getFaction()))
        _player->GetReputationMgr().SetVisible(factionTemplateEntry);

    GetPlayer()->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TALK);
    // remove fake death
    //if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
    //    GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    if (unit->isArmorer() || unit->isCivilian() || unit->isQuestGiver() || unit->isServiceProvider() || unit->isGuard())
        unit->StopMoving();

    // If spiritguide, no need for gossip menu, just put player into resurrect queue
    if (unit->isSpiritGuide())
    {
        Battleground* bg = _player->GetBattleground();
        if (bg)
        {
            bg->AddPlayerToResurrectQueue(unit->GetGUID(), _player->GetGUID());
            sBattlegroundMgr->SendAreaSpiritHealerQueryOpcode(_player, bg, unit->GetGUID());
            return;
        }
    }

    if (!sScriptMgr->OnGossipHello(_player, unit))
    {
//        _player->TalkedToCreature(unit->GetEntry(), unit->GetGUID());
        _player->PrepareGossipMenu(unit, unit->GetCreatureTemplate()->GossipMenuId, true);
        _player->SendPreparedGossip(unit);
    }

    unit->AI()->sGossipHello(_player);
}

/*void WorldSession::HandleGossipSelectOptionOpcode(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_PACKETIO, "WORLD: CMSG_GOSSIP_SELECT_OPTION");

    uint32 option;
    uint32 unk;
    uint64 guid;
    std::string code = "";

    recvData >> guid >> unk >> option;

    if (_player->PlayerTalkClass->GossipOptionCoded(option))
    {
        sLog->outDebug(LOG_FILTER_PACKETIO, "reading string");
        recvData >> code;
        sLog->outDebug(LOG_FILTER_PACKETIO, "string read: %s", code.c_str());
    }

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
    if (!unit)
    {
        sLog->outDebug(LOG_FILTER_PACKETIO, "WORLD: HandleGossipSelectOptionOpcode - Unit (GUID: %u) not found or you can't interact with him.", uint32(guid.GetCounter()));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    if (!code.empty())
    {
        if (!Script->GossipSelectWithCode(_player, unit, _player->PlayerTalkClass->GossipOptionSender (option), _player->PlayerTalkClass->GossipOptionAction(option), code.c_str()))
            unit->OnGossipSelect (_player, option);
    }
    else
    {
        if (!Script->OnGossipSelect (_player, unit, _player->PlayerTalkClass->GossipOptionSender (option), _player->PlayerTalkClass->GossipOptionAction (option)))
           unit->OnGossipSelect (_player, option);
    }
}*/

void WorldSession::HandleSpiritHealerActivateOpcode(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_SPIRIT_HEALER_ACTIVATE");

    ObjectGuid guid;
    recvData >> guid;

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_SPIRITHEALER);
    if (!unit)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleSpiritHealerActivateOpcode - Unit (GUID: %u) not found or you can not interact with him.", uint32(guid.GetCounter()));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    SendSpiritResurrect();
}

void WorldSession::SendSpiritResurrect()
{
    _player->ResurrectPlayer(0.5f, true);

    _player->DurabilityLossAll(0.25f, true, false);

    // get corpse nearest graveyard
    WorldSafeLocsEntry const* corpseGrave = NULL;
    Corpse* corpse = _player->GetCorpse();
    if (corpse)
        corpseGrave = sObjectMgr->GetClosestGraveYard(
            corpse->GetPositionX(), corpse->GetPositionY(), corpse->GetPositionZ(), corpse->GetMapId(), _player->GetTeam());

    // now can spawn bones
    _player->SpawnCorpseBones();

    // teleport to nearest from corpse graveyard, if different from nearest to player ghost
    if (corpseGrave)
    {
        WorldSafeLocsEntry const* ghostGrave = sObjectMgr->GetClosestGraveYard(
            _player->GetPositionX(), _player->GetPositionY(), _player->GetPositionZ(), _player->GetMapId(), _player->GetTeam());

        if (corpseGrave != ghostGrave)
            _player->TeleportTo(corpseGrave->MapID, corpseGrave->Loc.X, corpseGrave->Loc.Y, corpseGrave->Loc.Z, _player->GetOrientation());
        // or update at original position
        else
            _player->UpdateObjectVisibility();
    }
    // or update at original position
    else
        _player->UpdateObjectVisibility();
}

void WorldSession::HandleBinderActivateOpcode(WorldPacket& recvData)
{
    ObjectGuid npcGUID;
    recvData >> npcGUID;

    if (!GetPlayer()->IsInWorld() || !GetPlayer()->isAlive())
        return;

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(npcGUID, UNIT_NPC_FLAG_INNKEEPER);
    if (!unit)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleBinderActivateOpcode - Unit (GUID: %u) not found or you can not interact with him.", npcGUID.GetCounter());
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    SendBindPoint(unit);
}

void WorldSession::SendBindPoint(Creature* npc)
{
    // prevent set homebind to instances in any case
    if (GetPlayer()->GetMap()->Instanceable())
        return;

    uint32 bindspell = 3286;

    // update sql homebind
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_PLAYER_HOMEBIND);
    stmt->setUInt16(0, _player->GetMapId());
    stmt->setUInt16(1, _player->GetAreaId());
    stmt->setFloat (2, _player->GetPositionX());
    stmt->setFloat (3, _player->GetPositionY());
    stmt->setFloat (4, _player->GetPositionZ());
    stmt->setUInt64(5, _player->GetGUID().GetCounter());
    CharacterDatabase.Execute(stmt);

    _player->m_homebindMapId = _player->GetMapId();
    _player->m_homebindAreaId = _player->GetAreaId();
    _player->m_homebindX = _player->GetPositionX();
    _player->m_homebindY = _player->GetPositionY();
    _player->m_homebindZ = _player->GetPositionZ();

    // send spell for homebinding (3286)
    _player->CastSpell(_player, bindspell, true);

    SendTrainerService(npc->GetGUID(), bindspell, 2);
    _player->PlayerTalkClass->SendCloseGossip();
}

void WorldSession::HandleListStabledPetsOpcode(WorldPacket & recvData)
{
    time_t now = time(NULL);
    if (now - timeAddIgnoreOpcode < 1)
    {
        SendStableResult(STABLE_ERR_STABLE);
        recvData.rfinish();
        return;
    }
    else
       timeAddIgnoreOpcode = now;

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recv CMSG_LIST_STABLED_PETS");

    ObjectGuid npcGUID;
    recvData >> npcGUID;

    if (!CheckStableMaster(npcGUID))
    {
        SendStableResult(STABLE_ERR_STABLE);
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // remove mounts this fix bug where getting pet from stable while mounted deletes pet.
    if (GetPlayer()->IsMounted())
        GetPlayer()->RemoveAurasByType(SPELL_AURA_MOUNTED);

    SendStablePet(npcGUID);
}

void WorldSession::SendStablePet(ObjectGuid guid)
{
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_PET_DETAIL);

    stmt->setUInt64(0, _player->GetGUID().GetCounter());

    _sendStabledPetCallback.SetParam(guid);
    _sendStabledPetCallback.SetFutureResult(CharacterDatabase.AsyncQuery(stmt));
}

void WorldSession::SendStablePetCallback(PreparedQueryResult result, ObjectGuid guid)
{
    if (!GetPlayer())
        return;

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Send SMSG_LIST_STABLED_PETS.");

    uint32 num = 0;
    WorldPacket data(SMSG_LIST_STABLED_PETS, 200);
    data << guid;

    size_t writePos = data.wpos();
    data << num;

    std::set<uint32> stableNumber;
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 petNumber = fields[0].GetUInt32();
            PetSlot petSlot = GetPlayer()->GetSlotForPetId(petNumber);
            stableNumber.insert(petNumber);

            if (petSlot > PET_SLOT_STABLE_LAST)
                continue;

            //Find free slot and move pet there
            if (petSlot == PET_SLOT_FULL_LIST)
                petSlot = (PetSlot)GetPlayer()->SetOnAnyFreeSlot(petNumber);

            if (petSlot >= PET_SLOT_HUNTER_FIRST &&  petSlot < PET_SLOT_STABLE_LAST)
            {
                std::string name = fields[3].GetString();
                data << uint32(petSlot);                        // 4.x petSlot
                data << uint32(petNumber);                      // petnumber
                data << uint32(fields[1].GetUInt32());          // creature entry
                data << uint32(fields[4].GetUInt32());          // model id
                data << uint32(fields[2].GetUInt16());          // level
                data << uint8(petSlot < PET_SLOT_STABLE_FIRST ? 1 : 3);     // 1 = current, 2/3 = in stable (any from 4, 5, ... create problems with proper show)

                data << uint8(name.size());
                data.WriteString(name);              
                

                ++num;
            }
        }
        while (result->NextRow());
    }
    data.put<uint32>(writePos, num);

    //send only for hunter
    if (GetPlayer()->getClass() == CLASS_HUNTER)
    {
        SendPacket(&data);
        SendStableResult(STABLE_ERR_NONE);
    }

    // Cleaner. As this send at first login in any way. no need do it at playerLoading.
    const PlayerPetSlotList &petSlots = GetPlayer()->GetPetSlotList();
    for (uint32 i = uint32(PET_SLOT_HUNTER_FIRST); i < uint32(PET_SLOT_STABLE_LAST); ++i)
    {
        if (!petSlots[i])
            continue;

        std::set<uint32>::iterator find = stableNumber.find(petSlots[i]);
        if (find == stableNumber.end())
        {
            //where is no pet. need clear data.
            GetPlayer()->cleanPetSlotForMove(PetSlot(i), petSlots[i]);
        }
    }
}

void WorldSession::SendStableResult(uint8 res)
{
    WorldPacket data(SMSG_STABLE_RESULT, 1);
    data << uint8(res);
    SendPacket(&data);
}

//! 5.4.1
void WorldSession::HandleStableChangeSlot(WorldPacket & recv_data)
{
    time_t now = time(NULL);
    if (now - timeAddIgnoreOpcode < 1)
    {
        SendStableResult(STABLE_ERR_STABLE);
        recv_data.rfinish();
        return;
    }
    else
       timeAddIgnoreOpcode = now;

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recv CMSG_SET_PET_SLOT.");
    uint32 pet_number;
    ObjectGuid npcGUID;
    uint8 new_slot;

    recv_data >> pet_number >> new_slot;

    ////recv_data.ReadGuidMask<4, 1, 5, 3, 0, 6, 7, 2>(npcGUID);
    ////recv_data.ReadGuidBytes<5, 1, 4, 6, 3, 7, 2, 0>(npcGUID);

    if (!CheckStableMaster(npcGUID))
    {
        SendStableResult(STABLE_ERR_STABLE);
        return;
    }

    if (new_slot > MAX_PET_STABLES)
    {
        SendStableResult(STABLE_ERR_STABLE);
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    Pet* pet = _player->GetPet();

    //If we move the pet already summoned...
    if (pet && pet->GetCharmInfo() && pet->GetCharmInfo()->GetPetNumber() == pet_number)
        _player->RemovePet(pet, PET_SLOT_ACTUAL_PET_SLOT);

    //If we move to the pet already summoned...
    PetSlot curentSlot = GetPlayer()->GetSlotForPetId(GetPlayer()->m_currentPetNumber);
    if (pet && curentSlot == new_slot)
        _player->RemovePet(pet, PET_SLOT_ACTUAL_PET_SLOT);


    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_PET_BY_ID);

    stmt->setUInt64(0, _player->GetGUID().GetCounter());
    stmt->setUInt32(1, pet_number);

    _stableChangeSlotCallback.SetParam(new_slot);
    _stableChangeSlotCallback.SetFutureResult(CharacterDatabase.AsyncQuery(stmt));
}

void WorldSession::HandleStableChangeSlotCallback(PreparedQueryResult result, uint8 new_slot)
{
    if (!GetPlayer())
        return;

    if (!result)
    {
        SendStableResult(STABLE_ERR_STABLE);
        return;
    }

    Field *fields = result->Fetch();

    uint32 pet_entry = fields[0].GetUInt32();
    uint32 pet_number  = fields[1].GetUInt32();
    bool isHunter = fields[2].GetUInt8() == HUNTER_PET;

    PetSlot slot = GetPlayer()->GetSlotForPetId(pet_number);

    if (!pet_entry)
    {
        SendStableResult(STABLE_ERR_STABLE);
        return;
    }

    CreatureTemplate const* creatureInfo = sObjectMgr->GetCreatureTemplate(pet_entry);
    if (!creatureInfo || !creatureInfo->isTameable(_player->CanTameExoticPets()))
    {
        // if problem in exotic pet
        if (creatureInfo && creatureInfo->isTameable(true))
            SendStableResult(STABLE_ERR_EXOTIC);
        else
            SendStableResult(STABLE_ERR_STABLE);
        return;
    }

    // Update if its a Hunter pet
    if (new_slot != 100)
    {
        // We need to remove and add the new pet to there diffrent slots
        GetPlayer()->cleanPetSlotForMove((PetSlot)slot, pet_number);
        GetPlayer()->setPetSlotWithStableMoveOrRealDelete((PetSlot)new_slot, pet_number, isHunter);
        timeAddIgnoreOpcode = 0;
    }

    SendStableResult(STABLE_SUCCESS_STABLE);
}

void WorldSession::HandleStableRevivePet(WorldPacket & recvData )
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "HandleStableRevivePet: Not implemented");
}

void WorldSession::HandleRepairItemOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_REPAIR_ITEM");

    ObjectGuid npcGUID, itemGUID;
    bool guildBank;                                         // new in 2.3.2, bool that means from guild bank money

    //recvData.ReadGuidMask<4>(npcGUID);
    //recvData.ReadGuidMask<0>(itemGUID);
    //recvData.ReadGuidMask<7>(npcGUID);
    //recvData.ReadGuidMask<2, 1>(itemGUID);
    //recvData.ReadGuidMask<5>(npcGUID);
    //recvData.ReadGuidMask<3>(itemGUID);
    //recvData.ReadGuidMask<2>(npcGUID);
    guildBank = recvData.ReadBit();
    //recvData.ReadGuidMask<3, 0>(npcGUID);
    //recvData.ReadGuidMask<7, 4>(itemGUID);
    //recvData.ReadGuidMask<6, 1>(npcGUID);
    //recvData.ReadGuidMask<6, 5>(itemGUID);

    //recvData.ReadGuidBytes<2>(npcGUID);
    //recvData.ReadGuidBytes<6, 5, 2>(itemGUID);
    //recvData.ReadGuidBytes<1, 7>(npcGUID);
    //recvData.ReadGuidBytes<4, 7>(itemGUID);
    //recvData.ReadGuidBytes<5, 4>(npcGUID);
    //recvData.ReadGuidBytes<1>(itemGUID);
    //recvData.ReadGuidBytes<3, 6, 0>(npcGUID);
    //recvData.ReadGuidBytes<0, 3>(itemGUID);

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(npcGUID, UNIT_NPC_FLAG_REPAIR);
    if (!unit)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleRepairItemOpcode - Unit (GUID: %u) not found or you can not interact with him.", npcGUID.GetCounter());
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // reputation discount
    float discountMod = _player->GetReputationPriceDiscount(unit);

    if (itemGUID)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "ITEM: Repair item, itemGUID = %u, npcGUID = %u", itemGUID.GetCounter(), npcGUID.GetCounter());

        Item* item = _player->GetItemByGuid(itemGUID);
        if (item)
            _player->DurabilityRepair(item->GetPos(), true, discountMod, guildBank != 0);
    }
    else
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "ITEM: Repair all items, npcGUID = %u", npcGUID.GetCounter());
        _player->DurabilityRepairAll(true, discountMod, guildBank != 0);
    }
}
