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

#include "DBCStores.h"
#include "Log.h"
#include "SharedDefines.h"
#include "SpellMgr.h"
#include "DBCfmt.h"
#include "ItemTemplate.h"
#include <iostream>
#include <fstream>

#include <map>

typedef std::map<uint16, uint32> AreaFlagByAreaID;
typedef std::map<uint32, uint32> AreaFlagByMapID;

struct WMOAreaTableTripple
{
    WMOAreaTableTripple(int32 r, int32 a, int32 g) :  groupId(g), rootId(r), adtId(a)
    {
    }

    bool operator <(const WMOAreaTableTripple& b) const
    {
        return memcmp(this, &b, sizeof(WMOAreaTableTripple))<0;
    }

    // ordered by entropy; that way memcmp will have a minimal medium runtime
    int32 groupId;
    int32 rootId;
    int32 adtId;
};

static UNORDERED_MAP<uint32, std::list<uint32> > sCriteriaTreeEntryList;
static UNORDERED_MAP<uint32, std::list<uint32> > sModifierTreeEntryList;
static UNORDERED_MAP<uint32, std::list<uint32> > sSpellProcsPerMinuteModEntryList;
static UNORDERED_MAP<uint32, uint32 > sAchievementEntryParentList;
static UNORDERED_MAP<uint32, std::list<uint32> > sItemSpecsList;
static UNORDERED_MAP<uint32, uint32 > sRevertLearnSpellList;

typedef std::map<WMOAreaTableTripple, WMOAreaTableEntry const*> WMOAreaInfoByTripple;

DBCStorage<AreaTableEntry> sAreaStore(AreaTableEntryfmt);
DBCStorage<AreaGroupEntry> sAreaGroupStore(AreaGroupEntryfmt);
DBCStorage<AreaPOIEntry> sAreaPOIStore(AreaPOIEntryfmt);
static AreaFlagByAreaID sAreaFlagByAreaID;
static AreaFlagByMapID sAreaFlagByMapID;                    // for instances without generated *.map files

static WMOAreaInfoByTripple sWMOAreaInfoByTripple;

DBCStorage<AchievementEntry> sAchievementStore(Achievementfmt);
//DBCStorage<AchievementCriteriaEntry> sAchievementCriteriaStore(AchievementCriteriafmt);
DBCStorage <CriteriaEntry> sCriteriaStore(Criteriafmt);
DBCStorage<CriteriaTreeEntry> sCriteriaTreeStore(CriteriaTreefmt);
DBCStorage<ModifierTreeEntry> sModifierTreeStore(ModifierTreefmt);
DBCStorage<AreaTriggerEntry> sAreaTriggerStore(AreaTriggerEntryfmt);
DBCStorage<ArmorLocationEntry> sArmorLocationStore(ArmorLocationfmt);
DBCStorage<AuctionHouseEntry> sAuctionHouseStore(AuctionHouseEntryfmt);
DBCStorage<BankBagSlotPricesEntry> sBankBagSlotPricesStore(BankBagSlotPricesEntryfmt);
DBCStorage <BannedAddOnsEntry> sBannedAddOnsStore(BannedAddOnsfmt);
DBCStorage<BattlemasterListEntry> sBattlemasterListStore(BattlemasterListEntryfmt);
DBCStorage<BarberShopStyleEntry> sBarberShopStyleStore(BarberShopStyleEntryfmt);
DBCStorage<CharStartOutfitEntry> sCharStartOutfitStore(CharStartOutfitEntryfmt);
DBCStorage<CharTitlesEntry> sCharTitlesStore(CharTitlesEntryfmt);
DBCStorage<ChatChannelsEntry> sChatChannelsStore(ChatChannelsEntryfmt);
DBCStorage<ChrClassesEntry> sChrClassesStore(ChrClassesEntryfmt);
DBCStorage<ChrRacesEntry> sChrRacesStore(ChrRacesEntryfmt);
DBCStorage<ChrPowerTypesEntry> sChrPowerTypesStore(ChrClassesXPowerTypesfmt);
DBCStorage<ChrSpecializationsEntry> sChrSpecializationsStore(ChrSpecializationsfmt);

DBCStorage<CinematicSequencesEntry> sCinematicSequencesStore(CinematicSequencesEntryfmt);
DBCStorage<CreatureDisplayInfoEntry> sCreatureDisplayInfoStore(CreatureDisplayInfofmt);
DBCStorage <CreatureDisplayInfoExtraEntry> sCreatureDisplayInfoExtraStore(CreatureDisplayInfoExtrafmt);
DBCStorage<CreatureFamilyEntry> sCreatureFamilyStore(CreatureFamilyfmt);
DBCStorage<CreatureModelDataEntry> sCreatureModelDataStore(CreatureModelDatafmt);
DBCStorage<CreatureSpellDataEntry> sCreatureSpellDataStore(CreatureSpellDatafmt);
DBCStorage<CreatureTypeEntry> sCreatureTypeStore(CreatureTypefmt);
DBCStorage<CurrencyTypesEntry> sCurrencyTypesStore(CurrencyTypesfmt);

DBCStorage<DestructibleModelDataEntry> sDestructibleModelDataStore(DestructibleModelDatafmt);
DBCStorage<DifficultyEntry> sDifficultyStore(Difficultyfmt);
DBCStorage<DungeonEncounterEntry> sDungeonEncounterStore(DungeonEncounterfmt);
DBCStorage<DurabilityQualityEntry> sDurabilityQualityStore(DurabilityQualityfmt);
DBCStorage<DurabilityCostsEntry> sDurabilityCostsStore(DurabilityCostsfmt);

DBCStorage<EmotesEntry> sEmotesStore(EmotesEntryfmt);
DBCStorage<EmotesTextEntry> sEmotesTextStore(EmotesTextEntryfmt);

typedef std::map<uint32, SimpleFactionsList> FactionTeamMap;
static FactionTeamMap sFactionTeamMap;
DBCStorage<FactionEntry> sFactionStore(FactionEntryfmt);
DBCStorage<FactionTemplateEntry> sFactionTemplateStore(FactionTemplateEntryfmt);

DBCStorage<GameObjectDisplayInfoEntry> sGameObjectDisplayInfoStore(GameObjectDisplayInfofmt);
DBCStorage<GemPropertiesEntry> sGemPropertiesStore(GemPropertiesEntryfmt);
DBCStorage<GlyphPropertiesEntry> sGlyphPropertiesStore(GlyphPropertiesfmt);
DBCStorage<GlyphSlotEntry> sGlyphSlotStore(GlyphSlotfmt);

DBCStorage<GtBarberShopCostBaseEntry>    sGtBarberShopCostBaseStore(GtBarberShopCostBasefmt);
DBCStorage<GtCombatRatingsEntry>         sGtCombatRatingsStore(GtCombatRatingsfmt);
DBCStorage<GtChanceToMeleeCritBaseEntry> sGtChanceToMeleeCritBaseStore(GtChanceToMeleeCritBasefmt);
DBCStorage<GtChanceToMeleeCritEntry>     sGtChanceToMeleeCritStore(GtChanceToMeleeCritfmt);
DBCStorage<GtChanceToSpellCritBaseEntry> sGtChanceToSpellCritBaseStore(GtChanceToSpellCritBasefmt);
DBCStorage<GtChanceToSpellCritEntry>     sGtChanceToSpellCritStore(GtChanceToSpellCritfmt);
DBCStorage<GtItemSocketCostPerLevelEntry> sGtItemSocketCostPerLevelStore(GtItemSocketCostPerLevelfmt);
DBCStorage<GtOCTClassCombatRatingScalarEntry> sGtOCTClassCombatRatingScalarStore(GtOCTClassCombatRatingScalarfmt);
DBCStorage<GtOCTRegenHPEntry>            sGtOCTRegenHPStore(GtOCTRegenHPfmt);
//DBCStorage<GtOCTRegenMPEntry>            sGtOCTRegenMPStore(GtOCTRegenMPfmt);  -- not used currently
DBCStorage<gtOCTHpPerStaminaEntry>       sGtOCTHpPerStaminaStore(GtOCTHpPerStaminafmt);
DBCStorage<GtRegenMPPerSptEntry>         sGtRegenMPPerSptStore(GtRegenMPPerSptfmt);
DBCStorage<GtSpellScalingEntry>          sGtSpellScalingStore(GtSpellScalingfmt);
DBCStorage<GtOCTBaseHPByClassEntry>      sGtOCTBaseHPByClassStore(GtOCTBaseHPByClassfmt);
DBCStorage<GtOCTBaseMPByClassEntry>      sGtOCTBaseMPByClassStore(GtOCTBaseMPByClassfmt);
DBCStorage <GtBattlePetTypeDamageModEntry>      sGtBattlePetTypeDamageModStore(GtBattlePetTypeDamageModfmt);
DBCStorage<GuildPerkSpellsEntry>         sGuildPerkSpellsStore(GuildPerkSpellsfmt);

DBCStorage<ImportPriceArmorEntry>        sImportPriceArmorStore(ImportPriceArmorfmt);
DBCStorage<ImportPriceQualityEntry>      sImportPriceQualityStore(ImportPriceQualityfmt);
DBCStorage<ImportPriceShieldEntry>       sImportPriceShieldStore(ImportPriceShieldfmt);
DBCStorage<ImportPriceWeaponEntry>       sImportPriceWeaponStore(ImportPriceWeaponfmt);
DBCStorage<ItemPriceBaseEntry>           sItemPriceBaseStore(ItemPriceBasefmt);
DBCStorage<ItemReforgeEntry>             sItemReforgeStore(ItemReforgefmt);
DBCStorage<ItemArmorQualityEntry>        sItemArmorQualityStore(ItemArmorQualityfmt);
DBCStorage<ItemArmorShieldEntry>         sItemArmorShieldStore(ItemArmorShieldfmt);
DBCStorage<ItemArmorTotalEntry>          sItemArmorTotalStore(ItemArmorTotalfmt);
DBCStorage<ItemClassEntry>               sItemClassStore(ItemClassfmt);
DBCStorage<ItemBagFamilyEntry>           sItemBagFamilyStore(ItemBagFamilyfmt);
DBCStorage<ItemDamageEntry>              sItemDamageAmmoStore(ItemDamagefmt);
DBCStorage<ItemDamageEntry>              sItemDamageOneHandStore(ItemDamagefmt);
DBCStorage<ItemDamageEntry>              sItemDamageOneHandCasterStore(ItemDamagefmt);
DBCStorage<ItemDamageEntry>              sItemDamageRangedStore(ItemDamagefmt);
DBCStorage<ItemDamageEntry>              sItemDamageThrownStore(ItemDamagefmt);
DBCStorage<ItemDamageEntry>              sItemDamageTwoHandStore(ItemDamagefmt);
DBCStorage<ItemDamageEntry>              sItemDamageTwoHandCasterStore(ItemDamagefmt);
DBCStorage<ItemDamageEntry>              sItemDamageWandStore(ItemDamagefmt);
DBCStorage<ItemDisenchantLootEntry>      sItemDisenchantLootStore(ItemDisenchantLootfmt);
//DBCStorage<ItemDisplayInfoEntry>       sItemDisplayInfoStore(ItemDisplayTemplateEntryfmt); -- not used currently
DBCStorage<ItemLimitCategoryEntry>       sItemLimitCategoryStore(ItemLimitCategoryEntryfmt);
DBCStorage<ItemRandomPropertiesEntry>    sItemRandomPropertiesStore(ItemRandomPropertiesfmt);
DBCStorage<ItemRandomSuffixEntry>        sItemRandomSuffixStore(ItemRandomSuffixfmt);
DBCStorage<ItemSetEntry>                 sItemSetStore(ItemSetEntryfmt);
DBCStorage <ItemSetSpellEntry>           sItemSetSpellStore(ItemSetSpellEntryfmt);
ItemSetSpellsStore                       sItemSetSpellsStore;
DBCStorage<ItemSpecEntry>                sItemSpecStore(ItemSpecEntryfmt);
//std::map<uint32, 

DBCStorage<LFGDungeonEntry> sLFGDungeonStore(LFGDungeonEntryfmt);
DBCStorage<LiquidTypeEntry> sLiquidTypeStore(LiquidTypefmt);
DBCStorage<LockEntry> sLockStore(LockEntryfmt);

DBCStorage<MailTemplateEntry> sMailTemplateStore(MailTemplateEntryfmt);
DBCStorage<MapEntry> sMapStore(MapEntryfmt);

// DBC used only for initialization sMapDifficultyMap at startup.
DBCStorage<MapDifficultyEntry> sMapDifficultyStore(MapDifficultyEntryfmt); // only for loading
MapDifficultyMap sMapDifficultyMap;

DBCStorage<MovieEntry> sMovieStore(MovieEntryfmt);
DBCStorage<MountCapabilityEntry> sMountCapabilityStore(MountCapabilityfmt);
DBCStorage<MountTypeEntry> sMountTypeStore(MountTypefmt);

DBCStorage<NameGenEntry> sNameGenStore(NameGenfmt);
NameGenVectorArraysMap sGenNameVectoArraysMap;

DBCStorage<PvPDifficultyEntry> sPvPDifficultyStore(PvPDifficultyfmt);

DBCStorage<QuestSortEntry> sQuestSortStore(QuestSortEntryfmt);
DBCStorage<QuestXPEntry>   sQuestXPStore(QuestXPfmt);
DBCStorage<QuestV2Entry> sQuestV2Store(QuestV2fmt);
DBCStorage<QuestFactionRewEntry>  sQuestFactionRewardStore(QuestFactionRewardfmt);
DBCStorage<RandomPropertiesPointsEntry> sRandomPropertiesPointsStore(RandomPropertiesPointsfmt);
DBCStorage<ResearchBranchEntry> sResearchBranchStore(ResearchBranchfmt);
DBCStorage<ResearchProjectEntry> sResearchProjectStore(ResearchProjectfmt);
DBCStorage<ResearchSiteEntry> sResearchSiteStore(ResearchSitefmt);
std::set<ResearchProjectEntry const*> sResearchProjectSet;
ResearchSiteDataMap sResearchSiteDataMap;
DBCStorage<QuestPOIBlobEntry> sQuestPOIBlobStore(QuestPOIBlobfmt);
DBCStorage<QuestPOIPointEntry> sQuestPOIPointStore(QuestPOIPointfmt);
DBCStorage<ScalingStatDistributionEntry> sScalingStatDistributionStore(ScalingStatDistributionfmt);

std::set<uint32> sScenarioCriteriaTreeStore;
DBCStorage <ScenarioEntry> sScenarioStore(Scenariofmt);
DBCStorage <ScenarioStepEntry> sScenarioStepStore(ScenarioStepfmt);

DBCStorage<SkillLineEntry> sSkillLineStore(SkillLinefmt);
DBCStorage<SkillLineAbilityEntry> sSkillLineAbilityStore(SkillLineAbilityfmt);

DBCStorage<SoundEntriesEntry> sSoundEntriesStore(SoundEntriesfmt);

DBCStorage<SpecializationSpellEntry> sSpecializationSpellStore(SpecializationSpellsfmt);

DBCStorage<SpellItemEnchantmentEntry> sSpellItemEnchantmentStore(SpellItemEnchantmentfmt);
DBCStorage<SpellItemEnchantmentConditionEntry> sSpellItemEnchantmentConditionStore(SpellItemEnchantmentConditionfmt);
DBCStorage<SpellEntry> sSpellStore(SpellEntryfmt);
SpellCategoryStore sSpellCategoryStore;
SpellSkillingList sSpellSkillingList;
PetFamilySpellsStore sPetFamilySpellsStore;

DBCStorage<SpellScalingEntry> sSpellScalingStore(SpellScalingEntryfmt);
DBCStorage<SpellTargetRestrictionsEntry> sSpellTargetRestrictionsStore(SpellTargetRestrictionsEntryfmt);
DBCStorage <PowerDisplayEntry> sPowerDisplayStore(PowerDisplayEntryfmt);
DBCStorage<SpellLevelsEntry> sSpellLevelsStore(SpellLevelsEntryfmt);
DBCStorage<SpellInterruptsEntry> sSpellInterruptsStore(SpellInterruptsEntryfmt);
DBCStorage<SpellEquippedItemsEntry> sSpellEquippedItemsStore(SpellEquippedItemsEntryfmt);
DBCStorage<SpellCooldownsEntry> sSpellCooldownsStore(SpellCooldownsEntryfmt);
DBCStorage<SpellAuraOptionsEntry> sSpellAuraOptionsStore(SpellAuraOptionsEntryfmt);
DBCStorage<SpellProcsPerMinuteEntry> sSpellProcsPerMinuteStore(SpellProcsPerMinuteEntryfmt);
DBCStorage<SpellProcsPerMinuteModEntry> sSpellProcsPerMinuteModStore(SpellProcsPerMinuteModEntryfmt);

SpellRestrictionDiffMap sSpellRestrictionDiffMap;

SpellEffectMap sSpellEffectMap;
SpellEffectDiffMap sSpellEffectDiffMap;

DBCStorage<SpellCastTimesEntry> sSpellCastTimesStore(SpellCastTimefmt);
DBCStorage<SpellCategoriesEntry> sSpellCategoriesStore(SpellCategoriesEntryfmt);
DBCStorage<SpellCategoryEntry> sSpellCategoryStores(SpellCategoryEntryfmt);
DBCStorage<SpellEffectEntry> sSpellEffectStore(SpellEffectEntryfmt);
DBCStorage<SpellEffectScalingEntry> sSpellEffectScalingStore(SpellEffectScalingEntryfmt);
DBCStorage<SpellDurationEntry> sSpellDurationStore(SpellDurationfmt);
DBCStorage<SpellFocusObjectEntry> sSpellFocusObjectStore(SpellFocusObjectfmt);
DBCStorage<SpellRadiusEntry> sSpellRadiusStore(SpellRadiusfmt);
DBCStorage<SpellRangeEntry> sSpellRangeStore(SpellRangefmt);
DBCStorage<SpellShapeshiftEntry> sSpellShapeshiftStore(SpellShapeshiftEntryfmt);
DBCStorage<SpellShapeshiftFormEntry> sSpellShapeshiftFormStore(SpellShapeshiftFormfmt);
DBCStorage<StableSlotPricesEntry> sStableSlotPricesStore(StableSlotPricesfmt);
DBCStorage<SummonPropertiesEntry> sSummonPropertiesStore(SummonPropertiesfmt);

DBCStorage<TalentEntry> sTalentStore(TalentEntryfmt);
TalentSpellList sTalentSpellList;

DBCStorage<TeamContributionPointsEntry> sTeamContributionPointsStore(TeamContributionPointsfmt);
DBCStorage<TotemCategoryEntry> sTotemCategoryStore(TotemCategoryEntryfmt);

TransportAnimationsByEntry sTransportAnimationsByEntry;
DBCStorage<TransportAnimationEntry> sTransportAnimationStore(TransportAnimationfmt);
DBCStorage<TransportRotationEntry> sTransportRotationStore(TransportRotationfmt);
DBCStorage<UnitPowerBarEntry> sUnitPowerBarStore(UnitPowerBarfmt);
DBCStorage<VehicleEntry> sVehicleStore(VehicleEntryfmt);
DBCStorage<VehicleSeatEntry> sVehicleSeatStore(VehicleSeatEntryfmt);
DBCStorage<WMOAreaTableEntry> sWMOAreaTableStore(WMOAreaTableEntryfmt);
DBCStorage<WorldMapAreaEntry> sWorldMapAreaStore(WorldMapAreaEntryfmt);
DBCStorage<WorldMapOverlayEntry> sWorldMapOverlayStore(WorldMapOverlayEntryfmt);
DBCStorage<WorldSafeLocsEntry> sWorldSafeLocsStore(WorldSafeLocsEntryfmt);
DBCStorage<PhaseEntry> sPhaseStores(PhaseEntryfmt);

typedef std::list<std::string> StoreProblemList;

uint32 DBCFileCount = 0;

static bool LoadDBC_assert_print(uint32 fsize, uint32 rsize, const std::string& filename)
{
    sLog->outError(LOG_FILTER_GENERAL, "Size of '%s' setted by format string (%u) not equal size of C++ structure (%u).", filename.c_str(), fsize, rsize);

    // ASSERT must fail after function call
    return false;
}

template<class T>
inline void LoadDBC(uint32& availableDbcLocales, StoreProblemList& errors, DBCStorage<T>& storage, std::string const& dbcPath, std::string const& filename, std::string const* customFormat = NULL, std::string const* customIndexName = NULL)
{
    // compatibility format and C++ structure sizes
    ASSERT(DBCFileLoader::GetFormatRecordSize(storage.GetFormat()) == sizeof(T) || LoadDBC_assert_print(DBCFileLoader::GetFormatRecordSize(storage.GetFormat()), sizeof(T), filename));

    ++DBCFileCount;
    std::string dbcFilename = dbcPath + filename;
    SqlDbc * sql = NULL;
    if (customFormat)
        sql = new SqlDbc(&filename, customFormat, customIndexName, storage.GetFormat());

    if (storage.Load(dbcFilename.c_str(), sql))
    {
        for (uint8 i = 0; i < TOTAL_LOCALES; ++i)
        {
            if (!(availableDbcLocales & (1 << i)))
                continue;

            std::string localizedName(dbcPath);
            localizedName.append(localeNames[i]);
            localizedName.push_back('/');
            localizedName.append(filename);

            if (!storage.LoadStringsFrom(localizedName.c_str()))
                availableDbcLocales &= ~(1<<i);             // mark as not available for speedup next checks
        }
    }
    else
    {
        // sort problematic dbc to (1) non compatible and (2) non-existed
        if (FILE* f = fopen(dbcFilename.c_str(), "rb"))
        {
            char buf[100];
            snprintf(buf, 100, " (exists, but has %u fields instead of %u) Possible wrong client version.", storage.GetFieldCount(), strlen(storage.GetFormat()));
            errors.push_back(dbcFilename + buf);
            fclose(f);
        }
        else
            errors.push_back(dbcFilename);
    }

    delete sql;
}

void LoadDBCStores(const std::string& dataPath)
{
    uint32 oldMSTime = getMSTime();

    std::string dbcPath = dataPath+"dbc/";

    StoreProblemList bad_dbc_files;
    uint32 availableDbcLocales = 0xFFFFFFFF;

    LoadDBC(availableDbcLocales, bad_dbc_files, sAreaStore,                   dbcPath, "AreaTable.dbc");
    LoadDBC(availableDbcLocales, bad_dbc_files, sAchievementStore,            dbcPath, "Achievement.dbc"/*, &CustomAchievementfmt, &CustomAchievementIndex*/);//14545
    //LoadDBC(availableDbcLocales, bad_dbc_files, sAchievementCriteriaStore,    dbcPath, "Achievement_Criteria.dbc", &CustomAchievementCriteriafmt, &CustomAchievementCriteriaIndex);//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sCriteriaStore,               dbcPath, "Criteria.dbc");//16048
    LoadDBC(availableDbcLocales, bad_dbc_files, sCriteriaTreeStore,           dbcPath, "CriteriaTree.dbc");//16048
    LoadDBC(availableDbcLocales, bad_dbc_files, sModifierTreeStore,           dbcPath, "ModifierTree.dbc");//16048
    LoadDBC(availableDbcLocales, bad_dbc_files, sAreaTriggerStore,            dbcPath, "AreaTrigger.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sAreaGroupStore,              dbcPath, "AreaGroup.dbc");//14545
    //LoadDBC(availableDbcLocales, bad_dbc_files, sAreaPOIStore,                dbcPath, "AreaPOI.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sAuctionHouseStore,           dbcPath, "AuctionHouse.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sArmorLocationStore,          dbcPath, "ArmorLocation.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sBankBagSlotPricesStore,      dbcPath, "BankBagSlotPrices.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sBannedAddOnsStore,           dbcPath, "BannedAddOns.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sBattlemasterListStore,       dbcPath, "BattleMasterList.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sBarberShopStyleStore,        dbcPath, "BarberShopStyle.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sCharStartOutfitStore,        dbcPath, "CharStartOutfit.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sCharTitlesStore,             dbcPath, "CharTitles.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sChatChannelsStore,           dbcPath, "ChatChannels.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sChrClassesStore,             dbcPath, "ChrClasses.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sChrRacesStore,               dbcPath, "ChrRaces.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sChrPowerTypesStore,          dbcPath, "ChrClassesXPowerTypes.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sChrSpecializationsStore,     dbcPath, "ChrSpecialization.dbc");//19342
    //LoadDBC(availableDbcLocales, bad_dbc_files, sCinematicSequencesStore,     dbcPath, "CinematicSequences.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sCreatureDisplayInfoStore,    dbcPath, "CreatureDisplayInfo.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sCreatureDisplayInfoExtraStore, dbcPath, "CreatureDisplayInfoExtra.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sCreatureFamilyStore,         dbcPath, "CreatureFamily.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sCreatureModelDataStore,      dbcPath, "CreatureModelData.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sCreatureSpellDataStore,      dbcPath, "CreatureSpellData.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sCreatureTypeStore,           dbcPath, "CreatureType.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sCurrencyTypesStore,          dbcPath, "CurrencyTypes.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sDestructibleModelDataStore,  dbcPath, "DestructibleModelData.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sDifficultyStore,             dbcPath, "Difficulty.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sDungeonEncounterStore,       dbcPath, "DungeonEncounter.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sDurabilityCostsStore,        dbcPath, "DurabilityCosts.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sDurabilityQualityStore,      dbcPath, "DurabilityQuality.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sEmotesStore,                 dbcPath, "Emotes.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sEmotesTextStore,             dbcPath, "EmotesText.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sFactionStore,                dbcPath, "Faction.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sFactionTemplateStore,        dbcPath, "FactionTemplate.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sGameObjectDisplayInfoStore,  dbcPath, "GameObjectDisplayInfo.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sGemPropertiesStore,          dbcPath, "GemProperties.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sGlyphPropertiesStore,        dbcPath, "GlyphProperties.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sGlyphSlotStore,              dbcPath, "GlyphSlot.dbc");//19243
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtBarberShopCostBaseStore,   dbcPath, "gtBarberShopCostBase.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtCombatRatingsStore,        dbcPath, "gtCombatRatings.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtChanceToMeleeCritBaseStore, dbcPath, "gtChanceToMeleeCritBase.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtChanceToMeleeCritStore,    dbcPath, "gtChanceToMeleeCrit.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtChanceToSpellCritBaseStore, dbcPath, "gtChanceToSpellCritBase.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtChanceToSpellCritStore,    dbcPath, "gtChanceToSpellCrit.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtItemSocketCostPerLevelStore,dbcPath,"gtItemSocketCostPerLevel.dbc");
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtOCTClassCombatRatingScalarStore,    dbcPath, "gtOCTClassCombatRatingScalar.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtOCTHpPerStaminaStore,      dbcPath, "gtOCTHpPerStamina.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtRegenMPPerSptStore,        dbcPath, "gtRegenMPPerSpt.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtSpellScalingStore,         dbcPath, "gtSpellScaling.dbc");//15595
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtOCTBaseHPByClassStore,     dbcPath, "gtOCTBaseHPByClass.dbc");//15595
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtOCTBaseMPByClassStore,     dbcPath, "gtOCTBaseMPByClass.dbc");//15595
    LoadDBC(availableDbcLocales, bad_dbc_files, sGtBattlePetTypeDamageModStore,        dbcPath, "gtBattlePetTypeDamageMod.dbc");//15595
    LoadDBC(availableDbcLocales, bad_dbc_files, sGuildPerkSpellsStore,        dbcPath, "GuildPerkSpells.dbc");//15595
    LoadDBC(availableDbcLocales, bad_dbc_files, sImportPriceArmorStore,       dbcPath, "ImportPriceArmor.dbc"); // 15595
    LoadDBC(availableDbcLocales, bad_dbc_files, sImportPriceQualityStore,     dbcPath, "ImportPriceQuality.dbc"); // 15595
    LoadDBC(availableDbcLocales, bad_dbc_files, sImportPriceShieldStore,      dbcPath, "ImportPriceShield.dbc"); // 15595
    LoadDBC(availableDbcLocales, bad_dbc_files, sImportPriceWeaponStore,      dbcPath, "ImportPriceWeapon.dbc"); // 15595
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemPriceBaseStore,          dbcPath, "ItemPriceBase.dbc"); // 15595
    //LoadDBC(availableDbcLocales, bad_dbc_files, sItemReforgeStore,            dbcPath, "ItemReforge.dbc"); // 15595
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemBagFamilyStore,          dbcPath, "ItemBagFamily.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemClassStore,              dbcPath, "ItemClass.dbc"); // 15595
    //LoadDBC(dbcCount, availableDbcLocales, bad_dbc_files, sItemDisplayInfoStore,        dbcPath, "ItemDisplayInfo.dbc");     -- not used currently
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemLimitCategoryStore,      dbcPath, "ItemLimitCategory.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemRandomPropertiesStore,   dbcPath, "ItemRandomProperties.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemRandomSuffixStore,       dbcPath, "ItemRandomSuffix.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemSetStore,                dbcPath, "ItemSet.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemSetSpellStore,           dbcPath, "ItemSetSpell.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemSpecStore,               dbcPath, "ItemSpecOverride.dbc", &CustomItemSpecEntryfmt, &CustomItemSpecEntryIndex);//17538
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemArmorQualityStore,       dbcPath, "ItemArmorQuality.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemArmorShieldStore,        dbcPath, "ItemArmorShield.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemArmorTotalStore,         dbcPath, "ItemArmorTotal.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemDamageAmmoStore,         dbcPath, "ItemDamageAmmo.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemDamageOneHandStore,      dbcPath, "ItemDamageOneHand.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemDamageOneHandCasterStore,dbcPath, "ItemDamageOneHandCaster.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemDamageRangedStore,       dbcPath, "ItemDamageRanged.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemDamageThrownStore,       dbcPath, "ItemDamageThrown.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemDamageTwoHandStore,      dbcPath, "ItemDamageTwoHand.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemDamageTwoHandCasterStore,dbcPath, "ItemDamageTwoHandCaster.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemDamageWandStore,         dbcPath, "ItemDamageWand.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sItemDisenchantLootStore,     dbcPath, "ItemDisenchantLoot.dbc", &CustomtemDisenchantLootEntryfmt, &CustomtemDisenchantLootEntryIndex);
    LoadDBC(availableDbcLocales, bad_dbc_files, sLFGDungeonStore,             dbcPath, "LfgDungeons.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sLiquidTypeStore,             dbcPath, "LiquidType.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sLockStore,                   dbcPath, "Lock.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sPhaseStores,                 dbcPath, "Phase.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sMailTemplateStore,           dbcPath, "MailTemplate.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sMapStore,                    dbcPath, "Map.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sMapDifficultyStore,          dbcPath, "MapDifficulty.dbc", &CustomMapDifficultyEntryfmt, &CustomMapDifficultyEntryIndex);//17538
    LoadDBC(availableDbcLocales, bad_dbc_files, sMountCapabilityStore,        dbcPath, "MountCapability.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sMountTypeStore,              dbcPath, "MountType.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sNameGenStore,                dbcPath, "NameGen.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sMovieStore,                  dbcPath, "Movie.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sPvPDifficultyStore,          dbcPath, "PvpDifficulty.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sQuestXPStore,                dbcPath, "QuestXP.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sQuestV2Store,                dbcPath, "QuestV2.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sQuestFactionRewardStore,     dbcPath, "QuestFactionReward.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sQuestPOIPointStore,          dbcPath, "QuestPOIPoint.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sQuestSortStore,              dbcPath, "QuestSort.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sRandomPropertiesPointsStore, dbcPath, "RandPropPoints.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sResearchBranchStore,         dbcPath, "ResearchBranch.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sResearchProjectStore,        dbcPath, "ResearchProject.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sResearchSiteStore,           dbcPath, "ResearchSite.dbc");//14545
    //LoadDBC(availableDbcLocales, bad_dbc_files, sQuestPOIBlobStore,           dbcPath, "QuestPOIBlob.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sScalingStatDistributionStore, dbcPath, "ScalingStatDistribution.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sSkillLineStore,              dbcPath, "SkillLine.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSkillLineAbilityStore,       dbcPath, "SkillLineAbility.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSoundEntriesStore,           dbcPath, "SoundEntries.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpecializationSpellStore,    dbcPath, "SpecializationSpells.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellStore,                  dbcPath, "Spell.dbc", &CustomSpellEntryfmt, &CustomSpellEntryIndex);//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellScalingStore,           dbcPath,"SpellScaling.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellTargetRestrictionsStore,dbcPath,"SpellTargetRestrictions.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellLevelsStore,            dbcPath,"SpellLevels.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellInterruptsStore,        dbcPath,"SpellInterrupts.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellEquippedItemsStore,     dbcPath,"SpellEquippedItems.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellCooldownsStore,         dbcPath,"SpellCooldowns.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellAuraOptionsStore,       dbcPath,"SpellAuraOptions.dbc", &CustomSpellAuraOptionsEntryfmt, &CustomSpellAuraOptionsEntryIndex);//17538
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellProcsPerMinuteStore,    dbcPath,"SpellProcsPerMinute.dbc");//17538
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellProcsPerMinuteModStore, dbcPath,"SpellProcsPerMinuteMod.dbc");//17538
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellCategoriesStore,        dbcPath,"SpellCategories.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellCategoryStores,         dbcPath,"SpellCategory.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellEffectStore,            dbcPath,"SpellEffect.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellEffectScalingStore,     dbcPath,"SpellEffectScaling.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellCastTimesStore,         dbcPath, "SpellCastTimes.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellDurationStore,          dbcPath, "SpellDuration.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellFocusObjectStore,       dbcPath, "SpellFocusObject.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellItemEnchantmentStore,   dbcPath, "SpellItemEnchantment.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellItemEnchantmentConditionStore, dbcPath, "SpellItemEnchantmentCondition.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellRadiusStore,            dbcPath, "SpellRadius.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellRangeStore,             dbcPath, "SpellRange.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellShapeshiftStore,        dbcPath, "SpellShapeshift.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sSpellShapeshiftFormStore,    dbcPath, "SpellShapeshiftForm.dbc");//14545
    //LoadDBC(availableDbcLocales, bad_dbc_files, sStableSlotPricesStore,       dbcPath, "StableSlotPrices.dbc");
    LoadDBC(availableDbcLocales, bad_dbc_files, sSummonPropertiesStore,       dbcPath, "SummonProperties.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sTalentStore,                 dbcPath, "Talent.dbc");//16135
    //LoadDBC(availableDbcLocales, bad_dbc_files, sTeamContributionPointsStore, dbcPath, "TeamContributionPoints.dbc");
    LoadDBC(availableDbcLocales, bad_dbc_files, sTotemCategoryStore,          dbcPath, "TotemCategory.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sTransportAnimationStore,     dbcPath, "TransportAnimation.dbc");//17538
    LoadDBC(availableDbcLocales, bad_dbc_files, sTransportRotationStore,      dbcPath, "TransportRotation.dbc");
    LoadDBC(availableDbcLocales, bad_dbc_files, sUnitPowerBarStore,           dbcPath, "UnitPowerBar.dbc");//15595
    LoadDBC(availableDbcLocales, bad_dbc_files, sVehicleStore,                dbcPath, "Vehicle.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sVehicleSeatStore,            dbcPath, "VehicleSeat.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sWMOAreaTableStore,           dbcPath, "WMOAreaTable.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sWorldMapAreaStore,           dbcPath, "WorldMapArea.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sWorldMapOverlayStore,        dbcPath, "WorldMapOverlay.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sWorldSafeLocsStore,          dbcPath, "WorldSafeLocs.dbc");//14545
    LoadDBC(availableDbcLocales, bad_dbc_files, sScenarioStore,               dbcPath, "Scenario.dbc");
    LoadDBC(availableDbcLocales, bad_dbc_files, sScenarioStepStore,           dbcPath, "ScenarioStep.dbc");//19342
    LoadDBC(availableDbcLocales, bad_dbc_files, sPowerDisplayStore,           dbcPath, "PowerDisplay.dbc");//17538

    // error checks
    if (bad_dbc_files.size() >= DBCFileCount)
    {
        sLog->outError(LOG_FILTER_GENERAL, "Incorrect DataDir value in worldserver.conf or ALL required *.dbc files (%d) not found by path: %sdbc", DBCFileCount, dataPath.c_str());
        exit(1);
    }
    else if (!bad_dbc_files.empty())
    {
        std::string str;
        for (StoreProblemList::iterator i = bad_dbc_files.begin(); i != bad_dbc_files.end(); ++i)
            str += *i + "\n";

        sLog->outError(LOG_FILTER_GENERAL, "Some required *.dbc files (%u from %d) not found or not compatible:\n%s", (uint32)bad_dbc_files.size(), DBCFileCount, str.c_str());
        exit(1);
    }

    // Check loaded DBC files proper version
    if (!sAreaStore.LookupEntry(5491)          ||     // last area (areaflag) added in 5.4.1 (17538)
        !sCharTitlesStore.LookupEntry(389)     ||     // last char title added in 5.4.1 (17538)
        !sGemPropertiesStore.LookupEntry(2467) ||     // last gem property added in 5.4.1 (17538)
        !sMapStore.LookupEntry(1173)           ||     // last map added in 5.4.1 (17538)
        !sSpellStore.LookupEntry(152028)       )      // last spell added in 5.4.1 (17538)
    {
        sLog->outError(LOG_FILTER_GENERAL, "You have _outdated_ DBC files. Please extract correct versions from current using client.");
        exit(1);
    }

    sLog->outInfo(LOG_FILTER_SERVER_LOADING, ">> Initialized %d DBC data stores in %u ms", DBCFileCount, GetMSTimeDiffToNow(oldMSTime));
}

void InitDBCCustomStores()
{
    // must be after sAreaStore loading
    for (uint32 i = 0; i < sAreaStore.GetNumRows(); ++i)           // areaflag numbered from 0
    {
        if (AreaTableEntry const* area = sAreaStore.LookupEntry(i))
        {
            // fill AreaId->DBC records
            sAreaFlagByAreaID.insert(AreaFlagByAreaID::value_type(uint16(area->ID), area->AreaBit));

            // fill MapId->DBC records (skip sub zones and continents)
            if (area->ParentAreaID == 0)
                sAreaFlagByMapID.insert(AreaFlagByMapID::value_type(area->mapid, area->AreaBit));
        }
    }

    for (uint32 i = 0; i < sAchievementStore.GetNumRows(); ++i)
    {
        if(AchievementEntry const* as = sAchievementStore.LookupEntry(i))
            if(as->criteriaTree > 0)
            sAchievementEntryParentList[as->criteriaTree] = i;
    }

    for (uint32 i = 0; i < sCriteriaTreeStore.GetNumRows(); ++i)
    {
        if(CriteriaTreeEntry const* ct = sCriteriaTreeStore.LookupEntry(i))
            if(ct->parent > 0)
            sCriteriaTreeEntryList[ct->parent].push_back(i);
    }

    for (uint32 i = 0; i < sModifierTreeStore.GetNumRows(); ++i)
    {
        if(ModifierTreeEntry const* mt = sModifierTreeStore.LookupEntry(i))
            if(mt->parent > 0)
            sModifierTreeEntryList[mt->parent].push_back(i);
    }

    for (uint32 i=0; i<sFactionStore.GetNumRows(); ++i)
    {
        FactionEntry const* faction = sFactionStore.LookupEntry(i);
        if (faction && faction->team)
        {
            SimpleFactionsList &flist = sFactionTeamMap[faction->team];
            flist.push_back(i);
        }
    }

    for (uint32 i = 0; i < sGameObjectDisplayInfoStore.GetNumRows(); ++i)
    {
        if (GameObjectDisplayInfoEntry const* info = sGameObjectDisplayInfoStore.LookupEntry(i))
        {
            if (info->maxX < info->minX)
                std::swap(*(float*)(&info->maxX), *(float*)(&info->minX));
            if (info->maxY < info->minY)
                std::swap(*(float*)(&info->maxY), *(float*)(&info->minY));
            if (info->maxZ < info->minZ)
                std::swap(*(float*)(&info->maxZ), *(float*)(&info->minZ));
        }
    }

    for (uint32 i = 0; i < sItemSpecStore.GetNumRows(); ++i)
    {
        if (ItemSpecEntry const* isp = sItemSpecStore.LookupEntry(i))
            sItemSpecsList[isp->m_itemID].push_back(isp->m_specID);
    }

    for (uint32 i = 0; i < sMapDifficultyStore.GetNumRows(); ++i)
    {
        if (MapDifficultyEntry const* entry = sMapDifficultyStore.LookupEntry(i))
        {
            if (!sMapStore.LookupEntry(entry->MapID))
            {
                sLog->outInfo(LOG_FILTER_SERVER_LOADING, "DB table `mapdifficulty_dbc` or MapDifficulty.dbc has non-existant map %u.", entry->MapID);
                continue;
            }
            if (entry->DifficultyID && !sDifficultyStore.LookupEntry(entry->DifficultyID))
            {
                sLog->outInfo(LOG_FILTER_SERVER_LOADING, "DB table `mapdifficulty_dbc` or MapDifficulty.dbc has non-existant difficulty %u.", entry->DifficultyID);
                continue;
            }
            sMapDifficultyMap[entry->MapID][entry->DifficultyID] = MapDifficulty(entry->DifficultyID, entry->RaidDuration, entry->MaxPlayers, entry->Message_lang[0] > 0);
        }
    }
    sMapDifficultyStore.Clear();

    for (uint32 i = 0; i < sNameGenStore.GetNumRows(); ++i)
        if (NameGenEntry const* entry = sNameGenStore.LookupEntry(i))
            sGenNameVectoArraysMap[entry->race].stringVectorArray[entry->gender].push_back(std::string(entry->name));
    sNameGenStore.Clear();

    for (uint32 i = 0; i < sPvPDifficultyStore.GetNumRows(); ++i)
        if (PvPDifficultyEntry const* entry = sPvPDifficultyStore.LookupEntry(i))
            if (entry->bracketId > MAX_BATTLEGROUND_BRACKETS)
                ASSERT(false && "Need update MAX_BATTLEGROUND_BRACKETS by DBC data");

    for (uint32 i = 0; i < sResearchProjectStore.GetNumRows(); ++i)
    {
        ResearchProjectEntry const* rp = sResearchProjectStore.LookupEntry(i);
        if (!rp || !rp->IsVaid())
            continue;

        sResearchProjectSet.insert(rp);
    }

    for (uint32 i = 0; i < sResearchSiteStore.GetNumRows(); ++i)
    {
        ResearchSiteEntry const* rs = sResearchSiteStore.LookupEntry(i);
        if (!rs || !rs->IsValid())
            continue;

        ResearchSiteData& data = sResearchSiteDataMap[rs->ID];

        data.entry = rs;

        for (uint32 i = 0; i < sQuestPOIPointStore.GetNumRows(); ++i)
            if (QuestPOIPointEntry const* poi = sQuestPOIPointStore.LookupEntry(i))
                if (poi->POIId == rs->POIid)
                    data.points.push_back(ResearchPOIPoint(poi->x, poi->y));

        if (data.points.size() == 0)
            sLog->outDebug(LOG_FILTER_SERVER_LOADING,"Research site %u POI %u map %u has 0 POI points in DBC!", rs->ID, rs->POIid, rs->MapID);
    }


    for (uint32 i = 0; i < sScenarioStepStore.GetNumRows(); ++i)
    {
        ScenarioStepEntry const* entry = sScenarioStepStore.LookupEntry(i);
        if (!entry || !entry->m_criteriaTreeId)
            continue;

        if (!sCriteriaTreeStore.LookupEntry(entry->m_criteriaTreeId))
            continue;

        sScenarioCriteriaTreeStore.insert(entry->m_criteriaTreeId);
    }

    for (uint32 i = 1; i < sSpellStore.GetNumRows(); ++i)
    {
        SpellCategoriesEntry const* spell = sSpellCategoriesStore.LookupEntry(i);
        if (spell && spell->Category)
            sSpellCategoryStore[spell->Category].insert(i);
    }

    for (uint32 j = 0; j < sSkillLineAbilityStore.GetNumRows(); ++j)
    {
        SkillLineAbilityEntry const *skillLine = sSkillLineAbilityStore.LookupEntry(j);
        if (!skillLine)
            continue;

        SpellEntry const* spellInfo = sSpellStore.LookupEntry(skillLine->spellId);
        if (!spellInfo)
            continue;

        SpellMiscEntry const* spellMisc = sSpellMiscStore.LookupEntry(spellInfo->MiscID);
        if (!spellMisc)
            continue;

        SpellLevelsEntry const* levels = sSpellLevelsStore.LookupEntry(spellInfo->LevelsID);
        if (levels && levels->spellLevel)
            continue;

        if (spellMisc->Attributes & SPELL_ATTR0_PASSIVE)
        {
            for (uint32 i = 0; i < sCreatureFamilyStore.GetNumRows(); ++i)
            {
                CreatureFamilyEntry const* cFamily = sCreatureFamilyStore.LookupEntry(i);
                if (!cFamily)
                    continue;

                if (skillLine->skillId != cFamily->skillLine[0] && skillLine->skillId != cFamily->skillLine[1])
                    continue;

                if (skillLine->learnOnGetSkill != ABILITY_LEARNED_ON_GET_RACE_OR_CLASS_SKILL)
                    continue;

                sPetFamilySpellsStore[i].insert(spellInfo->ID);
            }
        }
    }

    for(uint32 i = 1; i < sSpellTargetRestrictionsStore.GetNumRows(); ++i)
        if(SpellTargetRestrictionsEntry const *restriction = sSpellTargetRestrictionsStore.LookupEntry(i))
            sSpellRestrictionDiffMap[restriction->SpellId].restrictions.insert(restriction);


    for (uint32 i = 0; i < sSpellProcsPerMinuteModStore.GetNumRows(); ++i)
    {
        if(SpellProcsPerMinuteModEntry const* sppm = sSpellProcsPerMinuteModStore.LookupEntry(i))
            sSpellProcsPerMinuteModEntryList[sppm->ProcsPerMinuteId].push_back(i);
    }

    for(uint32 i = 1; i < sSpellEffectStore.GetNumRows(); ++i)
    {
        if(SpellEffectEntry const *spellEffect = sSpellEffectStore.LookupEntry(i))
        {
            if(spellEffect->EffectIndex > MAX_SPELL_EFFECTS)
                continue;

            if (spellEffect->DifficultyID)
                sSpellEffectDiffMap[spellEffect->SpellID].effects[MAKE_PAIR16(spellEffect->EffectIndex, spellEffect->DifficultyID)] = spellEffect;
            else
                sSpellEffectMap[spellEffect->SpellID].effects[spellEffect->EffectIndex] = spellEffect;

            if(spellEffect->Effect == SPELL_EFFECT_LEARN_SPELL)
                sRevertLearnSpellList[spellEffect->EffectTriggerSpell] = spellEffect->SpellID;
        }
    }

    for (uint32 i = 1; i < sSpellStore.GetNumRows(); ++i)
    {
        if (SpellEntry const * spell = sSpellStore.LookupEntry(i))
            if (const SpellEffectEntry* spellEffect = spell->GetSpellEffect(EFFECT_1))
                if (spellEffect->Effect == SPELL_EFFECT_SKILL && IsProfessionSkill(spellEffect->EffectMiscValue))
                    sSpellSkillingList.push_back(spell);
    }

    for (uint32 i = 1; i < sTalentStore.GetNumRows(); ++i)
        if (TalentEntry const* entry = sTalentStore.LookupEntry(i))
            sTalentSpellList.insert(entry->spellId);

    for (uint32 i = 0; i < sTransportAnimationStore.GetNumRows(); ++i)
        if (TransportAnimationEntry const* entry = sTransportAnimationStore.LookupEntry(i))
            sTransportAnimationsByEntry[entry->TransportID][entry->TimeIndex] = entry;

    for (uint32 i = 0; i < sTransportRotationStore.GetNumRows(); ++i)
    {
        TransportRotationEntry const* rot = sTransportRotationStore.LookupEntry(i);
        if (!rot)
            continue;

        //WoD::ToDo
        //sTransportMgr->AddPathRotationToTransport(rot->TransportID, rot->TimeIndex, rot);
    }
    for (uint32 i = 0; i < sWMOAreaTableStore.GetNumRows(); ++i)
        if (WMOAreaTableEntry const* entry = sWMOAreaTableStore.LookupEntry(i))
            sWMOAreaInfoByTripple.insert(WMOAreaInfoByTripple::value_type(WMOAreaTableTripple(entry->WMOID, entry->NameSet, entry->WMOGroupID), entry));

    for (uint32 i = 0; i < sItemSetSpellStore.GetNumRows(); ++i)
        if (ItemSetSpellEntry const* entry = sItemSetSpellStore.LookupEntry(i))
            sItemSetSpellsStore[entry->ItemSetID].push_back(entry);
}

const std::string* GetRandomCharacterName(uint8 race, uint8 gender)
{
    uint32 size = sGenNameVectoArraysMap[race].stringVectorArray[gender].size();
    uint32 randPos = urand(0,size-1);

    return &sGenNameVectoArraysMap[race].stringVectorArray[gender][randPos];
}

SimpleFactionsList const* GetFactionTeamList(uint32 faction)
{
    FactionTeamMap::const_iterator itr = sFactionTeamMap.find(faction);
    if (itr != sFactionTeamMap.end())
        return &itr->second;

    return NULL;
}

std::list<uint32> GetItemSpecsList(uint32 ItemID)
{
    return sItemSpecsList[ItemID];
}

uint32 GetsAchievementEntryByTreeList(uint32 criteriaTree)
{
    UNORDERED_MAP<uint32, uint32 >::const_iterator itr = sAchievementEntryParentList.find(criteriaTree);
    if(itr != sAchievementEntryParentList.end())
        return itr->second;
    return 0;
}

uint32 GetLearnSpell(uint32 trigerSpell)
{
    UNORDERED_MAP<uint32, uint32 >::const_iterator itr = sRevertLearnSpellList.find(trigerSpell);
    if(itr != sRevertLearnSpellList.end())
        return itr->second;
    return 0;
}

std::list<uint32> const* GetCriteriaTreeList(uint32 parent)
{
    UNORDERED_MAP<uint32, std::list<uint32> >::const_iterator itr = sCriteriaTreeEntryList.find(parent);
    if(itr != sCriteriaTreeEntryList.end())
        return &itr->second;
    return NULL;
}

std::list<uint32> const* GetModifierTreeList(uint32 parent)
{
    UNORDERED_MAP<uint32, std::list<uint32> >::const_iterator itr = sModifierTreeEntryList.find(parent);
    if(itr != sModifierTreeEntryList.end())
        return &itr->second;
    return NULL;
}

std::list<uint32> const* GetSpellProcsPerMinuteModList(uint32 PerMinId)
{
    UNORDERED_MAP<uint32, std::list<uint32> >::const_iterator itr = sSpellProcsPerMinuteModEntryList.find(PerMinId);
    if(itr != sSpellProcsPerMinuteModEntryList.end())
        return &itr->second;
    return NULL;
}

char const* GetPetName(uint32 petfamily, uint32 /*dbclang*/)
{
    if (!petfamily)
        return NULL;
    CreatureFamilyEntry const* pet_family = sCreatureFamilyStore.LookupEntry(petfamily);
    if (!pet_family)
        return NULL;
    return pet_family->Name ? pet_family->Name : NULL;
}

SpellEffectEntry const* GetSpellEffectEntry(uint32 spellId, uint32 effect, uint8 difficulty)
{
    if(spellId == 9262) //hack fix Segmentation fault
       return NULL;

    if(difficulty)
    {
        uint16 index = MAKE_PAIR16(effect, difficulty);
        SpellEffectDiffMap::const_iterator itr = sSpellEffectDiffMap.find(spellId);
        if(itr != sSpellEffectDiffMap.end())
        {
            SpellEffectsMap const* effects = &itr->second.effects;
            SpellEffectsMap::const_iterator itrsecond = effects->find(index);
            if(itrsecond != effects->end())
                return itrsecond->second;
        }
    }
    else
    {
        SpellEffectMap::const_iterator itr = sSpellEffectMap.find(spellId);
        if(itr != sSpellEffectMap.end())
            if(itr->second.effects[effect])
                return itr->second.effects[effect];
    }

    return NULL;
}

SpellEffectScalingEntry const* GetSpellEffectScalingEntry(uint32 effectId)
{
    return sSpellEffectScalingStore.LookupEntry(effectId);
}

SpellTargetRestrictionsEntry const *GetSpellTargetRestrioctions(uint32 spellId, uint16 difficulty)
{
    SpellRestrictionDiffMap::const_iterator itr = sSpellRestrictionDiffMap.find(spellId);
    if(itr != sSpellRestrictionDiffMap.end())
    {
        SpellRestrictionMap const* restr = &itr->second.restrictions;
        for (SpellRestrictionMap::const_iterator sr = restr->begin(); sr != restr->end(); ++sr)
        {
            if ((*sr)->m_difficultyID == difficulty)
                return *sr;
        }
    }

    return NULL;
}

TalentSpellPos const* GetTalentSpellPos(uint32 spellId)
{
    return NULL;
    /*TalentSpellPosMap::const_iterator itr = sTalentSpellPosMap.find(spellId);
    if (itr == sTalentSpellPosMap.end())
        return NULL;

    return &itr->second;*/
}

uint32 GetTalentSpellCost(uint32 spellId)
{
    if (TalentSpellPos const* pos = GetTalentSpellPos(spellId))
        return pos->rank+1;

    return 0;
}

int32 GetAreaFlagByAreaID(uint32 area_id)
{
    AreaFlagByAreaID::iterator i = sAreaFlagByAreaID.find(area_id);
    if (i == sAreaFlagByAreaID.end())
        return -1;

    return i->second;
}

WMOAreaTableEntry const* GetWMOAreaTableEntryByTripple(int32 rootid, int32 adtid, int32 groupid)
{
    WMOAreaInfoByTripple::iterator i = sWMOAreaInfoByTripple.find(WMOAreaTableTripple(rootid, adtid, groupid));
        if (i == sWMOAreaInfoByTripple.end())
            return NULL;
        return i->second;
}

AreaTableEntry const* GetAreaEntryByAreaID(uint32 area_id)
{
    int32 areaflag = GetAreaFlagByAreaID(area_id);
    if (areaflag < 0)
        return NULL;

    return sAreaStore.LookupEntry(areaflag);
}

AreaTableEntry const* GetAreaEntryByAreaFlagAndMap(uint32 area_flag, uint32 map_id)
{
    if (area_flag)
        return sAreaStore.LookupEntry(area_flag);

    if (MapEntry const* mapEntry = sMapStore.LookupEntry(map_id))
        return GetAreaEntryByAreaID(mapEntry->linked_zone);

    return NULL;
}

uint32 GetAreaFlagByMapId(uint32 mapid)
{
    AreaFlagByMapID::iterator i = sAreaFlagByMapID.find(mapid);
    if (i == sAreaFlagByMapID.end())
        return 0;
    else
        return i->second;
}

uint32 GetVirtualMapForMapAndZone(uint32 mapid, uint32 zoneId)
{
    if (mapid != 530 && mapid != 571 && mapid != 732)   // speed for most cases
        return mapid;

    if (WorldMapAreaEntry const* wma = sWorldMapAreaStore.LookupEntry(zoneId))
        return wma->DisplayMapID >= 0 ? wma->DisplayMapID : wma->MapID;

    return mapid;
}

int32 GetMapFromZone(uint32 zoneId)
{
    if (WorldMapAreaEntry const* wma = sWorldMapAreaStore.LookupEntry(zoneId))
        return wma->DisplayMapID >= 0 ? wma->DisplayMapID : wma->MapID;

    return -1;
}

uint32 GetMaxLevelForExpansion(uint32 expansion)
{
    switch (expansion)
    {
        case CONTENT_1_60:
            return 60;
        case CONTENT_61_70:
            return 70;
        case CONTENT_71_80:
            return 80;
        case CONTENT_81_85:
            return 85;
        case CONTENT_86_90:
            return 90;
        default:
            break;

    }
    return 0;
}

/*
Used only for calculate xp gain by content lvl.
Calculation on Gilneas and group maps of LostIslands calculated as CONTENT_1_60.
*/
ContentLevels GetContentLevelsForMapAndZone(uint32 mapid, uint32 zoneId)
{
    mapid = GetVirtualMapForMapAndZone(mapid, zoneId);
    if (mapid < 2)
        return CONTENT_1_60;

    MapEntry const* mapEntry = sMapStore.LookupEntry(mapid);
    if (!mapEntry)
        return CONTENT_1_60;

    // no need enum all maps from phasing
    if (mapEntry->rootPhaseMap >= 0)
        mapid = mapEntry->rootPhaseMap;

    switch (mapid)
    {
        case 648:   //LostIslands
        case 654:   //Gilneas
            return CONTENT_1_60;
        default:
            return ContentLevels(mapEntry->Expansion());
    }
}

bool IsTotemCategoryCompatiableWith(uint32 itemTotemCategoryId, uint32 requiredTotemCategoryId)
{
    if (requiredTotemCategoryId == 0)
        return true;
    if (itemTotemCategoryId == 0)
        return false;

    TotemCategoryEntry const* itemEntry = sTotemCategoryStore.LookupEntry(itemTotemCategoryId);
    if (!itemEntry)
        return false;
    TotemCategoryEntry const* reqEntry = sTotemCategoryStore.LookupEntry(requiredTotemCategoryId);
    if (!reqEntry)
        return false;

    if (itemEntry->categoryType != reqEntry->categoryType)
        return false;

    return (itemEntry->categoryMask & reqEntry->categoryMask) == reqEntry->categoryMask;
}

void Zone2MapCoordinates(float& x, float& y, uint32 zone)
{
    WorldMapAreaEntry const* maEntry = sWorldMapAreaStore.LookupEntry(zone);

    // if not listed then map coordinates (instance)
    if (!maEntry)
        return;

    std::swap(x, y);                                         // at client map coords swapped
    x = x*((maEntry->LocBottom - maEntry->LocTop) / 100) + maEntry->LocTop;
    y = y*((maEntry->LocRight - maEntry->LocLeft) / 100) + maEntry->LocLeft;      // client y coord from top to down
}

void Map2ZoneCoordinates(float& x, float& y, uint32 zone)
{
    WorldMapAreaEntry const* maEntry = sWorldMapAreaStore.LookupEntry(zone);

    // if not listed then map coordinates (instance)
    if (!maEntry)
        return;

    x = (x - maEntry->LocTop) / ((maEntry->LocBottom - maEntry->LocTop) / 100);
    y = (y - maEntry->LocLeft) / ((maEntry->LocRight - maEntry->LocLeft) / 100);    // client y coord from top to down
    std::swap(x, y);                                         // client have map coords swapped
}

MapDifficulty const* GetDefaultMapDifficulty(uint32 mapID)
{
    auto itr = sMapDifficultyMap.find(mapID);
    if (itr == sMapDifficultyMap.end())
        return nullptr;

    if (itr->second.empty())
        return nullptr;

    for (auto& p : itr->second)
    {
        DifficultyEntry const* difficulty = sDifficultyStore.LookupEntry(p.first);
        if (!difficulty)
            continue;

        if (difficulty->Flags & DIFFICULTY_FLAG_DEFAULT)
            return &p.second;
    }

    return &itr->second.begin()->second;
}


MapDifficulty const* GetMapDifficultyData(uint32 mapId, Difficulty difficulty)
{
    auto itr = sMapDifficultyMap.find(mapId);
    if (itr == sMapDifficultyMap.end())
        return nullptr;

    auto diffItr = itr->second.find(difficulty);
    if (diffItr == itr->second.end())
        return nullptr;

    return &diffItr->second;
}

MapDifficulty const* GetDownscaledMapDifficultyData(uint32 mapId, Difficulty &difficulty)
{
    DifficultyEntry const* diffEntry = sDifficultyStore.LookupEntry(difficulty);
    if (!diffEntry)
        return GetDefaultMapDifficulty(mapId);

    uint32 tmpDiff = difficulty;
    MapDifficulty const* mapDiff = GetMapDifficultyData(mapId, Difficulty(tmpDiff));
    while (!mapDiff)
    {
        tmpDiff = diffEntry->FallbackDifficultyID;
        diffEntry = sDifficultyStore.LookupEntry(tmpDiff);
        if (!diffEntry)
            return GetDefaultMapDifficulty(mapId);

        // pull new data
        mapDiff = GetMapDifficultyData(mapId, Difficulty(tmpDiff)); // we are 10 normal or 25 normal
    }

    difficulty = Difficulty(tmpDiff);
    return mapDiff;
}

PvPDifficultyEntry const* GetBattlegroundBracketByLevel(uint32 mapid, uint32 level)
{
    PvPDifficultyEntry const* maxEntry = NULL;              // used for level > max listed level case
    for (uint32 i = 0; i < sPvPDifficultyStore.GetNumRows(); ++i)
    {
        if (PvPDifficultyEntry const* entry = sPvPDifficultyStore.LookupEntry(i))
        {
            // skip unrelated and too-high brackets
            if (entry->mapId != mapid || entry->minLevel > level)
                continue;

            // exactly fit
            if (entry->maxLevel >= level)
                return entry;

            // remember for possible out-of-range case (search higher from existed)
            if (!maxEntry || maxEntry->maxLevel < entry->maxLevel)
                maxEntry = entry;
        }
    }

    return maxEntry;
}

PvPDifficultyEntry const* GetBattlegroundBracketById(uint32 mapid, BattlegroundBracketId id)
{
    for (uint32 i = 0; i < sPvPDifficultyStore.GetNumRows(); ++i)
        if (PvPDifficultyEntry const* entry = sPvPDifficultyStore.LookupEntry(i))
            if (entry->mapId == mapid && entry->GetBracketId() == id)
                return entry;

    return NULL;
}

std::vector<uint32> const* GetTalentTreePrimarySpells(uint32 talentTree)
{
    return NULL;
    /*TalentTreePrimarySpellsMap::const_iterator itr = sTalentTreePrimarySpellsMap.find(talentTree);
    if (itr == sTalentTreePrimarySpellsMap.end())
        return NULL;

    return &itr->second;*/
}

uint32 GetLiquidFlags(uint32 liquidType)
{
    if (LiquidTypeEntry const* liq = sLiquidTypeStore.LookupEntry(liquidType))
        return 1 << liq->Type;

    return 0;
}

float GetCurrencyPrecision(uint32 currencyId)
{
    CurrencyTypesEntry const * entry = sCurrencyTypesStore.LookupEntry(currencyId);

    return entry ? entry->GetPrecision() : 1.0f;
}

ResearchSiteEntry const* GetResearchSiteEntryById(uint32 id)
{
    ResearchSiteDataMap::const_iterator itr = sResearchSiteDataMap.find(id);
    if (itr == sResearchSiteDataMap.end())
        return NULL;

    return itr->second.entry;
}

bool MapEntry::IsDifficultyModeSupported(uint32 difficulty) const
{
    return IsValidDifficulty(difficulty, IsRaid());
}

bool IsValidDifficulty(uint32 diff, bool isRaid)
{
    if (diff == DIFFICULTY_NONE)
        return true;

    switch (diff)
    {
        case DIFFICULTY_NORMAL:
        case DIFFICULTY_HEROIC:
        case DIFFICULTY_N_SCENARIO:
        case DIFFICULTY_HC_SCENARIO:
        case DIFFICULTY_CHALLENGE:
            return !isRaid;
        default:
            break;
    }

    return isRaid;
}

uint32 GetQuestUniqueBitFlag(uint32 questId)
{
    QuestV2Entry const* v2 = sQuestV2Store.LookupEntry(questId);
    if (!v2)
        return 0;

    return v2->UniqueBitFlag;
}

bool IsScenarioCriteriaTree(uint32 criteriaTreeId)
{
    return sScenarioCriteriaTreeStore.find(criteriaTreeId) != sScenarioCriteriaTreeStore.end();
}

