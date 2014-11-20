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

#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "AuctionHouseMgr.h"
#include "Log.h"
#include "Opcodes.h"
#include "UpdateMask.h"
#include "Util.h"
#include "AccountMgr.h"

//please DO NOT use iterator++, because it is slower than ++iterator!!!
//post-incrementation is always slower than pre-incrementation !

//void called when player click on auctioneer npc
void WorldSession::HandleAuctionHelloOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;                                        //NPC guid
    recvData.ReadGuidMask<1, 0, 2, 6, 5, 4, 7, 3>(guid);
    recvData.ReadGuidBytes<2, 4, 7, 3, 6, 5, 1, 0>(guid);

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_AUCTIONEER);
    if (!unit)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleAuctionHelloOpcode - Unit (GUID: %u) not found or you can't interact with him.", uint32(guid.GetCounter()));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    SendAuctionHello(guid, unit);
}

//this void causes that auction window is opened
void WorldSession::SendAuctionHello(uint64 guid, Creature* unit)
{
    if (GetPlayer()->getLevel() < sWorld->getIntConfig(CONFIG_AUCTION_LEVEL_REQ))
    {
        SendNotification(GetTrinityString(LANG_AUCTION_REQ), sWorld->getIntConfig(CONFIG_AUCTION_LEVEL_REQ));
        return;
    }

    AuctionHouseEntry const* ahEntry = AuctionHouseMgr::GetAuctionHouseEntry(unit->getFaction());
    if (!ahEntry)
        return;

    WorldPacket data(SMSG_AUCTION_HELLO, 12);
    //data.WriteGuidMask<2, 1>(guid);
    data.WriteBit(1);                                   // 3.3.3: 1 - AH enabled, 0 - AH disabled
    //data.WriteGuidMask<4, 6, 0, 5, 3, 7>(guid);
    //data.WriteGuidBytes<2, 0, 7, 6, 5, 1, 3, 4>(guid);
    data << uint32(ahEntry->houseId);
    
    SendPacket(&data);
}

//call this method when player bids, creates, or deletes auction
void WorldSession::SendAuctionCommandResult(AuctionEntry* auction, uint32 action, uint32 errorCode, uint32 bidError)
{
    ObjectGuid bidderGUID = auction ? auction->bidder : 0;

    WorldPacket data(SMSG_AUCTION_COMMAND_RESULT);
    data << uint32(40);                                   // inventoryErrorCode???, if exists, default 40
    data << uint32(action);
    data << uint32(auction ? auction->Id : 0);
    data << uint32(errorCode);

    data.WriteBit(1);                                     // bit_1, related to auction error or action
    data.WriteBit(1);                                     // bit_2, related to auction error or action

    //data.WriteGuidMask<7, 0, 1, 6, 3, 4, 5, 2>(bidderGUID);
    data.WriteBit(1);                                     // bit_3, related to auction error or action
    //data.WriteGuidBytes<2, 7, 3, 1, 5, 0, 6, 4>(bidderGUID);

    SendPacket(&data);
}

//this function sends notification, if bidder is online
void WorldSession::SendAuctionBidderNotification(uint32 location, uint32 auctionId, ObjectGuid bidder, uint32 bidSum, uint32 diff, uint32 itemEntry)
{
    /*WorldPacket data(SMSG_AUCTION_BIDDER_NOTIFICATION);
    //data.WriteGuidMask<4, 5, 0, 7, 2, 3, 1, 6>(bidder);
    //data.WriteGuidBytes<2, 7, 5, 0>(bidder);
    data << uint32(0);
    //data.WriteGuidBytes<3>(bidder);
    data << uint32(location);
    data << uint32(auctionId);
    data << uint32(itemEntry);
    data << uint32(0);
    //data.WriteGuidBytes<1, 4, 6>(bidder);
    SendPacket(&data);*/
}

// this void causes on client to display: "Your auction sold"
void WorldSession::SendAuctionOwnerNotification(AuctionEntry* auction)
{
    WorldPacket data(SMSG_AUCTION_OWNER_NOTIFICATION, 40);
    data.WriteBit(0);
    data << uint64(auction ? auction->bid : 0);
    data << uint32(auction ? auction->Id : 0);                           // unk
    data << uint32(0);                                     // unk
    data << float(0);                                      // unk
    data << uint32(auction ? auction->itemEntry : 0);
    data << uint32(0);                                     // unk
    SendPacket(&data);
}

void WorldSession::SendAuctionRemovedNotification(uint32 auctionId, uint32 itemEntry, int32 randomPropertyId)
{
    WorldPacket data(SMSG_AUCTION_REMOVED_NOTIFICATION, (4+4+4));
    data << uint32(auctionId);
    data << uint32(itemEntry);
    data << uint32(randomPropertyId);
    SendPacket(&data);
}

//this void creates new auction and adds auction to some auctionhouse
void WorldSession::HandleAuctionSellItem(WorldPacket & recvData)
{
    ObjectGuid auctioneerGUID;
    uint64 bid, buyout;
    uint32 itemsCount, etime;

    recvData >> buyout;
    recvData >> bid;
    recvData >> etime;

    if (!bid || !etime)
        return;

    recvData.ReadGuidMask<5, 0, 1, 4, 3, 6>(auctioneerGUID);
    itemsCount = recvData.ReadBits(5);

    ObjectGuid itemGUIDs[MAX_AUCTION_ITEMS]; // 160 slot = 4x 36 slot bag + backpack 16 slot
    uint32 stackCount[MAX_AUCTION_ITEMS];

    if (itemsCount > MAX_AUCTION_ITEMS)
    {
        SendAuctionCommandResult(NULL, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
        return;
    }

    for (uint32 i = 0; i < itemsCount; ++i)
        recvData.ReadGuidMask<7, 0, 5, 1, 4, 6, 3, 2>(itemGUIDs[i]);

    recvData.ReadGuidMask<2, 7>(auctioneerGUID);
    recvData.ReadGuidBytes<6>(auctioneerGUID);

    for (uint32 i = 0; i < itemsCount; ++i)
    {
        recvData.ReadGuidBytes<0, 5, 6, 3, 1, 7>(itemGUIDs[i]);
        recvData >> stackCount[i];
        recvData.ReadGuidBytes<2, 4>(itemGUIDs[i]);

        if (!itemGUIDs[i] || !stackCount[i] || stackCount[i] > 1000 )
            return;
    }
    
    recvData.ReadGuidBytes<7, 5, 3, 4, 0, 2, 1>(auctioneerGUID);

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(auctioneerGUID, UNIT_NPC_FLAG_AUCTIONEER);
    if (!creature)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleAuctionSellItem - Unit (GUID: %u) not found or you can't interact with him.", GUID_LOPART(auctioneerGUID));
        return;
    }

    AuctionHouseEntry const* auctionHouseEntry = AuctionHouseMgr::GetAuctionHouseEntry(creature->getFaction());
    if (!auctionHouseEntry)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleAuctionSellItem - Unit (GUID: %u) has wrong faction.", GUID_LOPART(auctioneerGUID));
        return;
    }

    etime *= MINUTE;

    switch (etime)
    {
        case 1*MIN_AUCTION_TIME:
        case 2*MIN_AUCTION_TIME:
        case 4*MIN_AUCTION_TIME:
            break;
        default:
            return;
    }

    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    Item* items[MAX_AUCTION_ITEMS];

    uint32 finalCount = 0;

    for (uint32 i = 0; i < itemsCount; ++i)
    {
        Item* item = _player->GetItemByGuid(itemGUIDs[i]);

        if (!item)
        {
            SendAuctionCommandResult(NULL, AUCTION_SELL_ITEM, ERR_AUCTION_ITEM_NOT_FOUND);
            return;
        }

        if (sAuctionMgr->GetAItem(item->GetGUID().GetCounter()) || !item->CanBeTraded() || item->IsNotEmptyBag() ||
            item->GetTemplate()->Flags & ITEM_PROTO_FLAG_CONJURED || item->GetUInt32Value(ITEM_FIELD_DURATION) ||
            item->GetCount() < stackCount[i] || _player->GetItemCount(item->GetEntry(), false) < stackCount[i])
        {
            SendAuctionCommandResult(NULL, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
            return;
        }

        if(i != 0 && (items[0]->GetEntry() != item->GetEntry() || items[0]->GetGUID().GetCounter() == item->GetGUID().GetCounter()))
        {
            sWorld->BanAccount(BAN_CHARACTER, _player->GetName(), "1282400845", "Dupe Auction mop","System");
            return;
        }

        items[i] = item;
        finalCount += stackCount[i];
    }

    if (!finalCount)
    {
        SendAuctionCommandResult(NULL, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
        return;
    }

    for (uint32 i = 0; i < itemsCount; ++i)
    {
        Item* item = items[i];

        if (item->GetMaxStackCount() < finalCount)
        {
            SendAuctionCommandResult(NULL, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
            return;
        }
    }

    for (uint32 i = 0; i < itemsCount; ++i)
    {
        Item* item = items[i];

        uint32 auctionTime = uint32(etime * sWorld->getRate(RATE_AUCTION_TIME));
        AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(creature->getFaction());

        uint32 deposit = sAuctionMgr->GetAuctionDeposit(auctionHouseEntry, etime, item, finalCount);
        if (!_player->HasEnoughMoney((uint64)deposit))
        {
            SendAuctionCommandResult(NULL, AUCTION_SELL_ITEM, ERR_AUCTION_NOT_ENOUGHT_MONEY);
            return;
        }

        _player->ModifyMoney(-int32(deposit));

        AuctionEntry* AH = new AuctionEntry;
        AH->Id = sObjectMgr->GenerateAuctionID();

        if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
            AH->auctioneer = 174444;
        else
            AH->auctioneer = GUID_LOPART(auctioneerGUID);

        ASSERT(sObjectMgr->GetCreatureData(AH->auctioneer)); // Tentative de vendre un item a un pnj qui n'existe pas, mieux vaut crash ici sinon l'item en question risque de disparaitre tout simplement

        // Required stack size of auction matches to current item stack size, just move item to auctionhouse
        if (itemsCount == 1 && item->GetCount() == stackCount[i])
        {
            if (GetSecurity() > SEC_PLAYER && sWorld->getBoolConfig(CONFIG_GM_LOG_TRADE))
            {
                sLog->outCommand(GetAccountId(), "GM %s (Account: %u) create auction: %s (Entry: %u Count: %u)",
                    GetPlayerName().c_str(), GetAccountId(), item->GetTemplate()->Name1.c_str(), item->GetEntry(), item->GetCount());
            }

            AH->itemGUIDLow = item->GetGUID().GetCounter();
            AH->itemEntry = item->GetEntry();
            AH->itemCount = item->GetCount();
            AH->owner = _player->GetGUID().GetCounter();
            AH->startbid = bid;
            AH->bidder = 0;
            AH->bid = 0;
            AH->buyout = buyout;
            AH->expire_time = time(NULL) + auctionTime;
            AH->deposit = deposit;
            AH->auctionHouseEntry = auctionHouseEntry;

            sLog->outInfo(LOG_FILTER_NETWORKIO, "CMSG_AUCTION_SELL_ITEM: Player %s (guid %d) is selling item %s entry %u (guid %d) to auctioneer %u with count %u with initial bid %u with buyout %u and with time %u (in sec) in auctionhouse %u", _player->GetName(), _player->GetGUID().GetCounter(), item->GetTemplate()->Name1.c_str(), item->GetEntry(), item->GetGUID().GetCounter(), AH->auctioneer, item->GetCount(), bid, buyout, auctionTime, AH->GetHouseId());
            sAuctionMgr->AddAItem(item);
            auctionHouse->AddAuction(AH);

            _player->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);

            SQLTransaction trans = CharacterDatabase.BeginTransaction();
            item->DeleteFromInventoryDB(trans);
            item->SaveToDB(trans);
            AH->SaveToDB(trans);
            _player->SaveInventoryAndGoldToDB(trans);
            CharacterDatabase.CommitTransaction(trans);

            SendAuctionCommandResult(AH, AUCTION_SELL_ITEM, ERR_AUCTION_OK);

            GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_CREATE_AUCTION, 1);
            return;
        }
        else // Required stack size of auction does not match to current item stack size, clone item and set correct stack size
        {
            Item* newItem = item->CloneItem(finalCount, _player);
            if (!newItem)
            {
                sLog->outError(LOG_FILTER_NETWORKIO, "CMSG_AUCTION_SELL_ITEM: Could not create clone of item %u", item->GetEntry());
                SendAuctionCommandResult(NULL, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
                return;
            }

            if (GetSecurity() > SEC_PLAYER && sWorld->getBoolConfig(CONFIG_GM_LOG_TRADE))
            {
                sLog->outCommand(GetAccountId(), "GM %s (Account: %u) create auction: %s (Entry: %u Count: %u)",
                    GetPlayerName().c_str(), GetAccountId(), newItem->GetTemplate()->Name1.c_str(), newItem->GetEntry(), newItem->GetCount());
            }

            AH->itemGUIDLow = newItem->GetGUID().GetCounter();
            AH->itemEntry = newItem->GetEntry();
            AH->itemCount = newItem->GetCount();
            AH->owner = _player->GetGUID().GetCounter();
            AH->startbid = bid;
            AH->bidder = 0;
            AH->bid = 0;
            AH->buyout = buyout;
            AH->expire_time = time(NULL) + auctionTime;
            AH->deposit = deposit;
            AH->auctionHouseEntry = auctionHouseEntry;

            sLog->outInfo(LOG_FILTER_NETWORKIO, "CMSG_AUCTION_SELL_ITEM: Player %s (guid %d) is selling item %s entry %u (guid %d) to auctioneer %u with count %u with initial bid %u with buyout %u and with time %u (in sec) in auctionhouse %u", _player->GetName(), _player->GetGUID().GetCounter(), newItem->GetTemplate()->Name1.c_str(), newItem->GetEntry(), newItem->GetGUID().GetCounter(), AH->auctioneer, newItem->GetCount(), bid, buyout, auctionTime, AH->GetHouseId());
            sAuctionMgr->AddAItem(newItem);
            auctionHouse->AddAuction(AH);

            SQLTransaction trans = CharacterDatabase.BeginTransaction();

            for (uint32 j = 0; j < itemsCount; ++j)
            {
                Item* item2 = items[j];

                // Item stack count equals required count, ready to delete item - cloned item will be used for auction
                if (item2->GetCount() == stackCount[j])
                {
                    _player->MoveItemFromInventory(item2->GetBagSlot(), item2->GetSlot(), true);
                    item2->DeleteFromInventoryDB(trans);
                    item2->DeleteFromDB(trans);
                    delete item2;
                }
                else // Item stack count is bigger than required count, update item stack count and save to database - cloned item will be used for auction
                {
                    item2->SetCount(item2->GetCount() - stackCount[j]);
                    item2->SetState(ITEM_CHANGED, _player);
                    _player->ItemRemovedQuestCheck(item2->GetEntry(), stackCount[j]);
                    item2->SendUpdateToPlayer(_player);
                    item2->SaveToDB(trans);
                }
            }

            newItem->SaveToDB(trans);
            AH->SaveToDB(trans);
            _player->SaveInventoryAndGoldToDB(trans);
            CharacterDatabase.CommitTransaction(trans);

            SendAuctionCommandResult(AH, AUCTION_SELL_ITEM, ERR_AUCTION_OK);

            GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_CREATE_AUCTION, 1);
            return;
        }
    }
}

// this function is called when client bids or buys out auction
void WorldSession::HandleAuctionPlaceBid(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_AUCTION_PLACE_BID");

    ObjectGuid auctioneerGUID;
    uint32 auctionId;
    uint64 price;
    recvData >> price;
    recvData >> auctionId;
    recvData.ReadGuidMask<2, 1, 0, 7, 5, 4, 3, 6>(auctioneerGUID);
    recvData.ReadGuidBytes<0, 5, 4, 6, 1, 2, 7, 3>(auctioneerGUID);

    if (!auctionId || !price)
        return;                                             // check for cheaters

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(auctioneerGUID, UNIT_NPC_FLAG_AUCTIONEER);
    if (!creature)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleAuctionPlaceBid - Unit (GUID: %u) not found or you can't interact with him.", uint32(GUID_LOPART(auctioneerGUID)));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(creature->getFaction());

    AuctionEntry* auction = auctionHouse->GetAuction(auctionId);
    Player* player = GetPlayer();

    if (!auction || auction->owner == player->GetGUID().GetCounter())
    {
        //you cannot bid your own auction:
        SendAuctionCommandResult(NULL, AUCTION_PLACE_BID, ERR_AUCTION_BID_OWN);
        return;
    }

    // impossible have online own another character (use this for speedup check in case online owner)
    /*Player* auction_owner = ObjectAccessor::FindPlayer(MAKE_NEW_GUID(auction->owner, 0, HIGHGUID_PLAYER));
    if (!auction_owner && sObjectMgr->GetPlayerAccountIdByGUID(MAKE_NEW_GUID(auction->owner, 0, HIGHGUID_PLAYER)) == player->GetSession()->GetAccountId())
    {
        //you cannot bid your another character auction:
        SendAuctionCommandResult(NULL, AUCTION_PLACE_BID, ERR_AUCTION_BID_OWN);
        return;
    }*/

    // cheating
    if (price <= auction->bid || price < auction->startbid)
        return;

    // price too low for next bid if not buyout
    if ((price < auction->buyout || auction->buyout == 0) &&
        price < auction->bid + auction->GetAuctionOutBid())
    {
        // client already test it but just in case ...
        SendAuctionCommandResult(auction, AUCTION_PLACE_BID, ERR_AUCTION_HIGHER_BID);
        return;
    }

    if (!player->HasEnoughMoney(price))
    {
        // client already test it but just in case ...
        SendAuctionCommandResult(auction, AUCTION_PLACE_BID, ERR_AUCTION_NOT_ENOUGHT_MONEY);
        return;
    }

    SQLTransaction trans = CharacterDatabase.BeginTransaction();

    if (price < auction->buyout || auction->buyout == 0)
    {
        if (auction->bidder > 0)
        {
            if (auction->bidder == player->GetGUID().GetCounter())
                player->ModifyMoney(-int64(price - auction->bid));
            else
            {
                // mail to last bidder and return money
                sAuctionMgr->SendAuctionOutbiddedMail(auction, price, GetPlayer(), trans);
                player->ModifyMoney(-int64(price));
            }
        }
        else
            player->ModifyMoney(-int64(price));

        auction->bidder = player->GetGUID().GetCounter();
        auction->bid = price;
        GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_BID, price);

        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_AUCTION_BID);
        stmt->setUInt32(0, auction->bidder);
        stmt->setUInt32(1, auction->bid);
        stmt->setUInt32(2, auction->Id);
        trans->Append(stmt);

        SendAuctionCommandResult(auction, AUCTION_PLACE_BID, ERR_AUCTION_OK);
    }
    else
    {
        //buyout:
        if (player->GetGUID().GetCounter() == auction->bidder)
            player->ModifyMoney(-int64(auction->buyout - auction->bid));
        else
        {
            player->ModifyMoney(-int64(auction->buyout));
            if (auction->bidder)                          //buyout for bidded auction ..
                sAuctionMgr->SendAuctionOutbiddedMail(auction, auction->buyout, GetPlayer(), trans);
        }
        auction->bidder = player->GetGUID().GetCounter();
        auction->bid = auction->buyout;
        GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_BID, auction->buyout);

        SendAuctionCommandResult(auction, AUCTION_PLACE_BID, ERR_AUCTION_OK);

        //- Mails must be under transaction control too to prevent data loss
        sAuctionMgr->SendAuctionSalePendingMail(auction, trans);
        sAuctionMgr->SendAuctionSuccessfulMail(auction, trans);
        sAuctionMgr->SendAuctionWonMail(auction, trans);

        auction->DeleteFromDB(trans);

        uint32 itemEntry = auction->itemEntry;
        sAuctionMgr->RemoveAItem(auction->itemGUIDLow);
        auctionHouse->RemoveAuction(auction, itemEntry);
    }
    player->SaveInventoryAndGoldToDB(trans);
    CharacterDatabase.CommitTransaction(trans);
}

//this void is called when auction_owner cancels his auction
void WorldSession::HandleAuctionRemoveItem(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_AUCTION_REMOVE_ITEM");

    ObjectGuid auctioneerGUID;
    uint32 auctionId;
    recvData >> auctionId;
    recvData.ReadGuidMask<0, 1, 4, 7, 2, 3, 5, 6>(auctioneerGUID);
    recvData.ReadGuidBytes<1, 5, 3, 4, 2, 6, 7, 0>(auctioneerGUID);

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(auctioneerGUID, UNIT_NPC_FLAG_AUCTIONEER);
    if (!creature)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleAuctionRemoveItem - Unit (GUID: %u) not found or you can't interact with him.", uint32(GUID_LOPART(auctioneerGUID)));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(creature->getFaction());

    AuctionEntry* auction = auctionHouse->GetAuction(auctionId);
    Player* player = GetPlayer();

    SQLTransaction trans = CharacterDatabase.BeginTransaction();
    if (auction && auction->owner == player->GetGUID().GetCounter())
    {
        Item* pItem = sAuctionMgr->GetAItem(auction->itemGUIDLow);
        if (pItem)
        {
            if (auction->bidder > 0)                        // If we have a bidder, we have to send him the money he paid
            {
                uint32 auctionCut = auction->GetAuctionCut();
                if (!player->HasEnoughMoney((uint64)auctionCut))          //player doesn't have enough money, maybe message needed
                    return;
                sAuctionMgr->SendAuctionCancelledToBidderMail(auction, trans, pItem);
                player->ModifyMoney(-int64(auctionCut));
            }

            // item will deleted or added to received mail list
            MailDraft(auction->BuildAuctionMailSubject(AUCTION_CANCELED), AuctionEntry::BuildAuctionMailBody(0, 0, auction->buyout, auction->deposit, 0))
                .AddItem(pItem)
                .SendMailTo(trans, player, auction, MAIL_CHECK_MASK_COPIED);
        }
        else
        {
            sLog->outError(LOG_FILTER_NETWORKIO, "Auction id: %u got non existing item (item guid : %u)!", auction->Id, auction->itemGUIDLow);
            SendAuctionCommandResult(NULL, AUCTION_CANCEL, ERR_AUCTION_DATABASE_ERROR);
            return;
        }
    }
    else
    {
        SendAuctionCommandResult(NULL, AUCTION_CANCEL, ERR_AUCTION_DATABASE_ERROR);
        //this code isn't possible ... maybe there should be assert
        sLog->outError(LOG_FILTER_NETWORKIO, "CHEATER: %u tried to cancel auction (id: %u) of another player or auction is NULL", player->GetGUID().GetCounter(), auctionId);
        return;
    }

    //inform player, that auction is removed
    SendAuctionCommandResult(auction, AUCTION_CANCEL, ERR_AUCTION_OK);

    // Now remove the auction

    player->SaveInventoryAndGoldToDB(trans);
    auction->DeleteFromDB(trans);
    CharacterDatabase.CommitTransaction(trans);

    uint32 itemEntry = auction->itemEntry;
    sAuctionMgr->RemoveAItem(auction->itemGUIDLow);
    auctionHouse->RemoveAuction(auction, itemEntry);
}

//called when player lists his bids
void WorldSession::HandleAuctionListBidderItems(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_AUCTION_LIST_BIDDER_ITEMS");

    ObjectGuid auctioneerGUID;
    uint32 page;                                            // page of auctions
    uint32 outbiddedCount;                                  // count of outbidded auctions

    recvData >> page;                                       // not used in fact (this list not have page control in client)
    recvData.ReadGuidMask<6, 2, 1, 0, 3, 4>(auctioneerGUID);
    outbiddedCount = recvData.ReadBits(7);
    recvData.ReadGuidMask<7, 5>(auctioneerGUID);

    std::vector<uint32> outbiddedAuctionIds;
    for (uint32 i = 0; i < outbiddedCount; i++)
    {
        uint32 auctionID;
        recvData >> auctionID;
        outbiddedAuctionIds.push_back(auctionID);
    }

    recvData.ReadGuidBytes<0, 4, 7, 3, 5, 6, 2, 1>(auctioneerGUID);

    /*if (recvData.size() != (16 + outbiddedCount * 4))
    {
        sLog->outError(LOG_FILTER_NETWORKIO, "Client sent bad opcode!!! with count: %u and size : %lu (must be: %u)", outbiddedCount, (unsigned long)recvData.size(), (16 + outbiddedCount * 4));
        outbiddedCount = 0;
    }*/

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(auctioneerGUID, UNIT_NPC_FLAG_AUCTIONEER);
    if (!creature)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleAuctionListBidderItems - Unit (GUID: %u) not found or you can't interact with him.", uint32(GUID_LOPART(auctioneerGUID)));
        recvData.rfinish();
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(creature->getFaction());

    WorldPacket data(SMSG_AUCTION_BIDDER_LIST_RESULT, (4+4+4));
    Player* player = GetPlayer();
    data << uint32(0);                                     //add 0 as count
    uint32 count = 0;
    uint32 totalcount = 0;

    for (uint32 i = 0; i < outbiddedCount; ++i)
    {
        AuctionEntry* auction = auctionHouse->GetAuction(outbiddedAuctionIds[i]);

        if (auction && auction->BuildAuctionInfo(data))
        {
            ++totalcount;
            ++count;
        }
    }

    auctionHouse->BuildListBidderItems(data, _player, count, totalcount);

    data.put<uint32>(0, count);                           // add count to placeholder
    data << uint32(totalcount);                           // CGAuctionHouse__m_numTotalBid
    data << uint32(300);                                  // CGAuctionHouse__m_desiredDelayTime
    SendPacket(&data);
}

//this void sends player info about his auctions
void WorldSession::HandleAuctionListOwnerItems(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_AUCTION_LIST_OWNER_ITEMS");

    ObjectGuid guid;
    uint32 listfrom;

    recvData >> listfrom;                                  // not used in fact (this list not have page control in client)

    recvData.ReadGuidMask<6, 0, 4, 3, 2, 5, 7, 1>(guid);
    recvData.ReadGuidBytes<2, 4, 6, 0, 5, 7, 3, 1>(guid);

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_AUCTIONEER);
    if (!creature)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleAuctionListOwnerItems - Unit (GUID: %u) not found or you can't interact with him.", uint32(guid.GetCounter()));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(creature->getFaction());

    WorldPacket data(SMSG_AUCTION_OWNER_LIST_RESULT, (4+4+4));
    data << uint32(0);                                     // amount place holder

    uint32 count = 0;
    uint32 totalcount = 0;

    auctionHouse->BuildListOwnerItems(data, _player, count, totalcount);

    data.put<uint32>(0, count);                  // add count to placeholder
    data << uint32(totalcount);
    data << uint32(300);                         // CGAuctionHouse__m_desiredDelayTime
    SendPacket(&data);
}

//this void is called when player clicks on search button
void WorldSession::HandleAuctionListItems(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_AUCTION_LIST_ITEMS");

    ObjectGuid guid;
    std::string searchedname;
    uint8 levelmin, levelmax, canUse, auctionId;
    uint32 page, classIndex, subClassIndex, invTypeIndex, quality;

    recvData >> page;
    recvData >> auctionId;
    recvData >> subClassIndex;
    recvData >> levelmin;
    recvData >> quality;
    recvData >> classIndex;
    recvData >> levelmax;
    recvData >> invTypeIndex;
    uint32 strLen;
    recvData >> strLen;
    std::string unkStr1 = recvData.ReadString(strLen);
    recvData.ReadGuidMask<4, 0, 1, 5, 3, 6>(guid);
    uint32 searchedname_length = recvData.ReadBits(8);
    recvData.ReadGuidMask<2>(guid);
    bool unk = recvData.ReadBit();
    recvData.ReadGuidMask<7>(guid);
    canUse = recvData.ReadBit();
    recvData.ReadGuidBytes<0, 7, 3, 1, 4>(guid);
    searchedname = recvData.ReadString(searchedname_length);
    recvData.ReadGuidBytes<5, 6, 2>(guid);

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_AUCTIONEER);
    if (!creature)
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleAuctionListItems - Unit (GUID: %u) not found or you can't interact with him.", uint32(guid.GetCounter()));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(creature->getFaction());

    //sLog->outDebug(LOG_FILTER_GENERAL, "Auctionhouse search (GUID: %u TypeId: %u)",, list from: %u, searchedname: %s, levelmin: %u, levelmax: %u, auctionSlotID: %u, auctionMainCategory: %u, auctionSubCategory: %u, quality: %u, usable: %u",
    //  guid.GetCounter(), GuidHigh2TypeId(GUID_HIPART(guid)), listfrom, searchedname.c_str(), levelmin, levelmax, auctionSlotID, auctionMainCategory, auctionSubCategory, quality, usable);

    WorldPacket data(SMSG_AUCTION_LIST_RESULT, (4+4+4));
    uint32 count = 0;
    uint32 totalcount = 0;
    data << uint32(0);

    // converting string that we try to find to lower case
    std::wstring wsearchedname;
    if (!Utf8toWStr(searchedname, wsearchedname))
        return;

    wstrToLower(wsearchedname);

    auctionHouse->BuildListAuctionItems(data, _player,
        wsearchedname, page, levelmin, levelmax, canUse,
        invTypeIndex, classIndex, subClassIndex, quality,
        count, totalcount);

    data.put<uint32>(0, count);
    data << totalcount;                                   // CGAuctionHouse__m_numTotalBid
    data << uint32(300);                                  // CGAuctionHouse__m_desiredDelayTime
    SendPacket(&data);
}

void WorldSession::HandleAuctionListPendingSales(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_AUCTION_LIST_PENDING_SALES");
    uint32 count = 0;

    WorldPacket data(SMSG_AUCTION_LIST_PENDING_SALES, 4);
    data << uint32(count);                                  // count
    /*for (uint32 i = 0; i < count; ++i)
    {
        data << "";                                         // string
        data << "";                                         // string
        data << uint64(0);
        data << uint32(0);
        data << float(0);
    }*/
    SendPacket(&data);
}
