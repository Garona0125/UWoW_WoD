INSERT INTO `game_tele` (`id`, `position_x`, `position_y`, `position_z`, `orientation`, `map`, `name`) VALUES 
(1760, 8852.54, 1369.35, 97, 4.6, 1116, 'IronDocks');

delete from areatrigger_teleport where id in (10097, 10098);
INSERT INTO `areatrigger_teleport` (`id`, `name`, `target_map`, `target_position_x`, `target_position_y`, `target_position_z`, `target_orientation`) VALUES 
(10097, 'IronDocks (exit)', 1116, 8856.75, 1358.35, 97.1, 1.5),
(10098, 'IronDocks (enter)', 1195, 6749.86, -543.8, 5, 4.7);

delete from instance_template where map = 1195;
INSERT INTO `instance_template` (`map`, `parent`, `script`, `allowMount`, `bonusChance`) VALUES
(1195, 0, 'instance_iron_docks', 0, 0);

/* -----------------------------------------------
   Fleshrender Nok'gar / ����������� ����� ���'���
   ----------------------------------------------- */
update creature set spawntimesecs = 86400 where id = 81305;
update creature_template set rank = 3, equipment_id = 81305, mechanic_immune_mask = 617299967, flags_extra = 1, ScriptName = 'boss_fleshrender_nokgar' where entry = 81305;
update creature_template set mechanic_immune_mask = 617299967, ScriptName = 'npc_dreadfang' where entry = 81297;
update creature_template set flags_extra = 130 where entry = 81832;
update creature_template set inhabittype = 7 where entry = 81279;

delete from conditions where SourceTypeOrReferenceId = 13 and SourceEntry in (164732,166923);
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `NegativeCondition`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES 
(13, 1, 164732, 0, 0, 31, 0, 3, 81832, 0, 0, 0, '', NULL),
(13, 1, 166923, 0, 0, 31, 0, 3, 81279, 0, 0, 0, '', NULL);

delete from spell_linked_spell where spell_trigger in (164731,164733)
INSERT INTO `spell_linked_spell` (`spell_trigger`, `spell_effect`, `type`, `caster`, `target`, `hastalent`, `hastalent2`, `chance`, `cooldown`, `type2`, `hitmask`, `learnspell`, `removeMask`, `comment`) VALUES 
(164731, 164732, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Shredding Swipes Jump'),
(164733, 164731, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Shredding Swipes AT');

delete from spell_trigger_dummy where spell_id in (166186,166805);
INSERT INTO `spell_trigger_dummy` (`spell_id`, `spell_trigger`, `option`, `target`, `caster`, `targetaura`, `bp0`, `bp1`, `bp2`, `effectmask`, `aura`, `chance`, `group`, `check_spell_id`, `comment`) VALUES 
(166186, 164234, 14, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 'Nokgar - Burning Arrows'),
(166805, 166186, 14, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 'Nokgar - Burning Arrows');

delete from areatrigger_data where entry in (2614,2569,2596);
INSERT INTO `areatrigger_data` (`entry`, `sphereScale`, `sphereScaleMax`, `isMoving`, `moveType`, `speed`, `activationDelay`, `updateDelay`, `maxCount`, `customVisualId`, `customEntry`, `hitType`, `Height`, `RadiusTarget`, `Float5`, `Float4`, `Radius`, `HeightTarget`, `MoveCurveID`, `ElapsedTime`, `comment`) VALUES 
(2614, 1, 0, 0, 0, 0, 0, 0, 0, 40588, 7238, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Shredding Swipes'),
(2569, 1, 0, 0, 0, 0, 0, 0, 0, 40425, 7198, 0, 0, 7.5, 0, 0, 7.5, 0, 0, 0, 'Nokgar - Barbed Arrow Barrage'),
(2596, 1, 0, 0, 0, 0, 0, 0, 0, 40549, 7224, 0, 0, 3, 0, 0, 3, 0, 0, 0, 'Nokgar - Burning Arrows');

delete from areatrigger_actions where entry in (2614,2569,2596);
INSERT INTO `areatrigger_actions` (`entry`, `id`, `moment`, `actionType`, `targetFlags`, `spellId`, `maxCharges`, `aura`, `hasspell`, `chargeRecoveryTime`, `scale`, `hitMaxCount`, `comment`) VALUES 
(2614, 0, 1, 0, 2, 164734, 0, 0, 0, 0, 0, 0, 'Shredding Swipes - Apply'),
(2614, 1, 42, 1, 2, 164734, 0, 0, 0, 0, 0, 0, 'Shredding Swipes - Remove'),
(2569, 0, 1, 0, 2, 164648, 0, 0, 0, 0, 0, 0, 'Nokgar - Barbed Arrow Barrage Apply'),
(2569, 1, 42, 1, 2, 164648, 0, 0, 0, 0, 0, 0, 'Nokgar - Barbed Arrow Barrage Remove'),
(2596, 0, 1, 0, 2, 164632, 0, 0, 0, 0, 0, 0, 'Nokgar - Burning Arrows Apply'),
(2596, 1, 42, 1, 2, 164632, 0, 0, 0, 0, 0, 0, 'Nokgar - Burning Arrows Remove');

delete from creature_text where entry = 81305;
INSERT INTO `creature_text` (`entry`, `groupid`, `id`, `text`, `type`, `language`, `probability`, `emote`, `duration`, `sound`, `comment`) VALUES
-- Aggro
(81305, 0, 0, 'Warsong arrows sing from the sky! They\'ll be the last thing you hear as I crush your skull.', 14, 0, 100, 396, 0, 46059, '����������� ����� ���'),
-- Burning Arrows
(81305, 1, 0, 'Start the funeral pyres!', 14, 0, 100, 396, 0, 46057, '����������� ����� ���'),
-- Barbed Arrow Barrage
(81305, 2, 0, 'Let them have it!', 14, 0, 100, 396, 0, 46062, '����������� ����� ���'),
-- Reckless Provocation
(81305, 3, 0, 'That\'s it? You barely drew blood!', 14, 0, 100, 0, 0, 46063, '����������� ����� ���'),
(81305, 3, 1, 'Terror overwhelms you, death is near.', 14, 0, 100, 396, 0, 46064, '����������� ����� ���'),
(81305, 3, 2, 'A death worthy of a warrior!', 14, 0, 100, 396, 0, 46065, '����������� ����� ���'),
(81305, 4, 0, '|TInterface\\Icons\\ability_warrior_rampage.blp:20|t %s begins to apply |cFFFF0000|Hspell:164426|h[Reckless Provocation]|h|r!', 41, 0, 100, 396, 0, 46057, '����������� ����� ���'),
-- Killed a player
(81305, 5, 0, 'Revel in the slaughter!', 14, 0, 100, 0, 0, 46060, '����������� ����� ���'),
(81305, 5, 1, 'My blade thirsts for more!', 14, 0, 100, 0, 0, 46061, '����������� ����� ���'),
-- Death
(81305, 6, 0, 'You will burn with me.', 14, 0, 100, 0, 0, 46058, '����������� ����� ���');

delete from locales_creature_text where entry = 81305;
INSERT INTO `locales_creature_text` (`entry`, `textGroup`, `id`, `text_loc8`) VALUES 
(81305, 0, 0, '���� ������ ����� ���� � ����! ��� ���������, ��� �� �������� ����� �������.'),
(81305, 1, 0, '������� ������������ ������!'),
(81305, 2, 0, '����� ������� ����!'),
(81305, 3, 0, '� ��� ���? ���� �������!'),
(81305, 3, 1, '���� ����������� ���, ��� ������ ������.'),
(81305, 3, 2, '������, ��������� �����!'),
(81305, 4, 0, '|TInterface\\Icons\\ability_warrior_rampage.blp:20|t %s �������� ��������� |cFFFF0000|Hspell:164426|h[������� �����]|h|r!'),
(81305, 5, 0, '����������� ������!'),
(81305, 5, 1, '��� ������ ������ ������ �����!'),
(81305, 6, 0, '�� ������ ������.');

-------------- END --------------

/* -----------------------------
   Grimrail Enforcers / ���������� ������� �����
   ----------------------------- */
delete from creature where id in (80805,80808,80816);
INSERT INTO `creature` (`id`, `map`, `zoneId`, `areaId`, `spawnMask`, `phaseMask`, `PhaseId`, `modelid`, `equipment_id`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`, `spawndist`, `currentwaypoint`, `curhealth`, `curmana`, `MovementType`, `npcflag`, `npcflag2`, `unit_flags`, `dynamicflags`, `AiID`, `MovementID`, `MeleeID`, `isActive`) VALUES 
(80805, 1195, 6951, 7309, 262, 1, '', 0, 0, 6507.6, -1129.63, 4.95913, 1.55792, 86400, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(80808, 1195, 6951, 7309, 262, 1, '', 0, 0, 6501.99, -1134.61, 4.95913, 1.51315, 86400, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(80816, 1195, 6951, 7309, 262, 1, '', 0, 0, 6513.61, -1134.4, 4.95913, 1.60111, 86400, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

update creature_template set rank = 3, equipment_id = entry, mechanic_immune_mask = 617299967, flags_extra = 1 where entry in (80805,80808,80816);
update creature_template set ScriptName = 'boss_makogg_emberblade' where entry = 80805;
update creature_template set ScriptName = 'boss_neesa_nox' where entry = 80808;
update creature_template set ScriptName = 'boss_ahriok_dugru' where entry = 80816;
update creature_template set exp = 5, minlevel = 100, maxlevel = 100, faction = 16, unit_flags = 33554432, ScriptName = 'npc_ogre_trap' where entry = 88758;
update creature_template set exp = 5, faction = 16, unit_flags = 559104, unit_flags2 = 4196352 where entry = 80875;

delete from spell_script_names where spell_id in (163390,163689);
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES 
(163390, 'spell_neesa_ogre_traps'),
(163689, 'spell_sanguine_sphere');

delete from conditions where SourceTypeOrReferenceId = 13 and SourceEntry = 163689;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `NegativeCondition`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES 
(13, 1, 163689, 0, 0, 31, 0, 3, 80816, 0, 0, 0, '', NULL),
(13, 1, 163689, 0, 1, 31, 0, 3, 80808, 0, 0, 0, '', NULL),
(13, 1, 163689, 0, 2, 31, 0, 3, 80805, 0, 0, 0, '', NULL);

delete from areatrigger_data where entry in (2479,2634,2636);
INSERT INTO `areatrigger_data` (`entry`, `sphereScale`, `sphereScaleMax`, `isMoving`, `moveType`, `speed`, `activationDelay`, `updateDelay`, `maxCount`, `customVisualId`, `customEntry`, `hitType`, `Height`, `RadiusTarget`, `Float5`, `Float4`, `Radius`, `HeightTarget`, `MoveCurveID`, `ElapsedTime`, `comment`) VALUES 
(2479, 1, 0, 0, 0, 0, 0, 0, 0, 40144, 7089, 0, 0, 8, 0, 0, 8, 0, 0, 0, 'Makogg - Flaming Slash'),
(2634, 1, 0, 0, 0, 0, 0, 0, 0, 40654, 7276, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Makogg - Lava Sweep. 164901'),
(2636, 1, 0, 0, 0, 0, 0, 0, 0, 40668, 7276, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Makogg - Lava Sweep. 164956');

delete from areatrigger_actions where entry in (2479,2634,2636);
INSERT INTO `areatrigger_actions` (`entry`, `id`, `moment`, `actionType`, `targetFlags`, `spellId`, `maxCharges`, `aura`, `hasspell`, `chargeRecoveryTime`, `scale`, `hitMaxCount`, `comment`) VALUES 
(2479, 0, 1, 0, 2, 163668, 0, 0, 0, 0, 0, 0, 'Makogg - Flaming Slash Apply'),
(2479, 1, 42, 1, 2, 163668, 0, 0, 0, 0, 0, 0, 'Makogg - Flaming Slash Remove'),
(2634, 0, 1, 0, 2, 165152, 0, 0, 0, 0, 0, 0, 'Makogg - Lava Sweep Apply'),
(2634, 1, 42, 1, 2, 165152, 0, 0, 0, 0, 0, 0, 'Makogg - Lava Sweep Remove'),
(2636, 0, 1, 0, 2, 165152, 0, 0, 0, 0, 0, 0, 'Makogg - Lava Sweep Apply'),
(2636, 1, 42, 1, 2, 165152, 0, 0, 0, 0, 0, 0, 'Makogg - Lava Sweep Remove'),
(2443, 0, 1, 0, 40, 163276, 0, 0, 0, 0, 0, 0, 'Neesa - Ogre Trap');

delete from creature_text where entry in (80805,80808,80816);
INSERT INTO `creature_text` (`entry`, `groupid`, `id`, `text`, `type`, `language`, `probability`, `emote`, `duration`, `sound`, `comment`) VALUES
-- Makogg
(80805, 0, 0, 'Hah. Ditto.', 14, 0, 100, 0, 0, 45800, '������ �������� ������'),
(80805, 1, 0, 'You will burn!', 14, 0, 100, 0, 0, 45801, '������ �������� ������'),
(80805, 2, 0, 'This is far from over.', 14, 0, 100, 0, 0, 45799, '������ �������� ������'),
-- Neesa
(80808, 0, 0, 'Ooooh! Is there killin\' ta be done? Lemme at\'em!', 14, 0, 100, 0, 0, 46128, 0, '����� ����'),
(80808, 1, 0, 'You\'d better step back. I\'m calling in the Bombsquad!', 14, 0, 100, 0, 0, 46131, '����� ����'),
(80808, 2, 0, 'Ahhhhhhhh! I\'m hit!', 14, 0, 100, 0, 0, 46127, '����� ����'),
-- Dugru
(80816, 0, 0, 'Right away, captain! It will be my pleasure.', 14, 0, 100, 0, 0, 44691, '����''�� �����'),
(80816, 1, 0, 'You dare contest my power??? Our blood surges with energy!', 14, 0, 100, 0, 0, 44693, '����''�� �����'),
(80816, 2, 0, '|TInterface\\Icons\\ability_deathwing_bloodcorruption_earth.blp:20|t%s begins to create |cFFFF0000|Hspell:163689|h[Blood Sphere]|h|r!', 41, 0, 100, 0, 0, 0, '����''�� �����'),
(80816, 3, 0, 'No! None shall possess my potency!', 14, 0, 100, 0, 0, 44690, '����''�� �����');

delete from locales_creature_text where entry = (80805,80808,80816);
INSERT INTO `locales_creature_text` (`entry`, `textGroup`, `id`, `text_loc8`) VALUES 
-- Makogg
(80805, 0, 0, '� ����.'),
(80805, 1, 0, '�� �������!'),
(80805, 2, 0, '��� ��� �� �����.'),
-- Neesa
(80808, 0, 0, '���, ���� ����-�� �����? � ����, �!'),
(80808, 1, 0, '����� ������� �����, � ������� ���������!'),
(80808, 2, 0, '��! � ���� ������!'),
-- Dugru
(80816, 0, 0, '������ ��, �������! � �������������.'),
(80816, 1, 0, '�� ������� ����� ���?! ���� ����� ������ �� �������!'),
(80816, 2, 0, '|TInterface\\Icons\\ability_deathwing_bloodcorruption_earth.blp:20|t%s �������� ��������� |cFFFF0000|Hspell:163689|h[����� �����]|h|r!'),
(80816, 3, 0, '���! ������ �� �������� ��� ����! ���� �� ����� �����������');

-------------- END --------------

/* -----------------------------
          Oshir / ����
   ----------------------------- */
update creature set spawntimesecs = 86400 where id = 79852;
update creature_template set rank = 3, mechanic_immune_mask = 617299967, ScriptName = 'boss_oshir', flags_extra = 1 where entry = 79852;
update creature_template set faction = 2000, unit_flags = 33554688, flags_extra = 130 where entry in (89021, 89022);
update creature_template set flags_extra = 130 where entry = 79889;
update creature_template set AIName = 'SmartAI' where entry in (89011,89012);

delete from smart_scripts where entryorguid in (89011,89012);
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `action_type`, `action_param1`, `action_param2`, `action_param3`, `action_param4`, `action_param5`, `action_param6`, `target_type`, `target_param1`, `target_param2`, `target_param3`, `target_x`, `target_y`, `target_z`, `target_o`, `comment`) VALUES 
(89011, 0, 0, 0, 0, 0, 100, 0, 2000, 2000, 3000, 3000, 11, 178154, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 'Time 2s - Cast Acid Spit'),
(89012, 0, 0, 0, 4, 0, 100, 0, 0, 0, 0, 0, 11, 178150, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 'On Aggro - Cast Strength of the Pack');

delete from conditions where SourceTypeOrReferenceId = 13 and SourceEntry in (178126,178128);
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `NegativeCondition`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES 
(13, 1, 178126, 0, 0, 31, 0, 3, 89021, 0, 0, 0, '', NULL),
(13, 1, 178128, 0, 0, 31, 0, 3, 89022, 0, 0, 0, '', NULL),
(13, 1, 161530, 0, 0, 31, 0, 4, 0, 0, 0, 0, '', NULL);

delete from spell_trigger_dummy where spell_id in (178126,178128,161530);
INSERT INTO `spell_trigger_dummy` (`spell_id`, `spell_trigger`, `option`, `target`, `caster`, `targetaura`, `bp0`, `bp1`, `bp2`, `effectmask`, `aura`, `chance`, `group`, `check_spell_id`, `comment`) VALUES 
(178126, 178124, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 'Oshir - Breakout Rylak'),
(178128, 178124, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 'Oshir - Breakout Wolf'),
(161530, 163189, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 'Oshir - Time to Feed'),
(178154, 178155, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 'Oshir - Acid Spit');

delete from areatrigger_data where entry in (2279);
INSERT INTO `areatrigger_data` (`entry`, `sphereScale`, `sphereScaleMax`, `isMoving`, `moveType`, `speed`, `activationDelay`, `updateDelay`, `maxCount`, `customVisualId`, `customEntry`, `hitType`, `Height`, `RadiusTarget`, `Float5`, `Float4`, `Radius`, `HeightTarget`, `MoveCurveID`, `ElapsedTime`, `comment`) VALUES 
(2279, 1, 0, 0, 0, 0, 0, 0, 0, 38870, 6833, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Oshir - Rending Slashes'),
(3596, 1, 0, 0, 0, 0, 0, 0, 0, 45127, 8200, 0, 0, 6, 0, 0, 6, 0, 0, 0, 'Oshir - Acid Spit');
-- (3595, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Oshir - Strength of the Pack'); need fix

delete from areatrigger_actions where entry in (2279);
INSERT INTO `areatrigger_actions` (`entry`, `id`, `moment`, `actionType`, `targetFlags`, `spellId`, `maxCharges`, `aura`, `hasspell`, `chargeRecoveryTime`, `scale`, `hitMaxCount`, `comment`) VALUES 
(2279, 0, 1, 0, 2, 161256, 0, 0, 0, 0, 0, 0, 'Oshir - Rending Slashes'),
(2279, 1, 42, 0, 2, 161256, 0, 0, 0, 0, 0, 0, 'Oshir - Rending Slashes'),
(3596, 0, 1, 0, 2, 178156, 0, 0, 0, 0, 0, 0, 'Oshir - Acid Spit Apply'),
(3596, 1, 42, 1, 2, 178156, 0, 0, 0, 0, 0, 0, 'Oshir - Acid Spit Remove');
-- (3595, 0, 1, 0, 1, 178149, 0, 0, 0, 0, 0, 0, 'Oshir - Strength of the Pack Apply'),
-- (3595, 1, 40, 1, 1, 178149, 0, 0, 0, 0, 0, 0, 'Oshir - Strength of the Pack Remove');

-------------- END --------------

/* -----------------------------
          Skulloc / �������
   ----------------------------- */
delete from creature where id in (84030,84109,84464);
update creature set spawntimesecs = 86400 where id in (83612,83613,83616);
update creature_template set rank = 3, mechanic_immune_mask = 617299967, ScriptName = 'boss_skulloc', flags_extra = 1 where entry = 83612;
update creature_template set rank = 3, speed_walk = '2', speed_run = '2', mechanic_immune_mask = 617299967, ScriptName = 'boss_koramar', flags_extra = 1 where entry = 83613;
update creature_template set rank = 3, mechanic_immune_mask = 617299967, ScriptName = 'boss_zoggosh', flags_extra = 1 where entry = 83616;
update creature_template set ScriptName = 'npc_blackhand_might_turret' where entry = 84215;
update creature_template set exp = 5, MinLevel =100, MaxLevel =100, faction = 114, unit_flags = 33554432, inhabittype = 7, flags_extra = 130 where entry = 84464;

delete from spell_linked_spell where spell_trigger in (-168929,168929);
INSERT INTO `spell_linked_spell` (`spell_trigger`, `spell_effect`, `type`, `comment`) VALUES 
(-168929, -168820, 0, 'Skulloc - Remove Cannon Barrage'),
(168929, 168820, 0, 'Skulloc - Apply Cannon Barrage');

delete from areatrigger_scripts where ScriptName = 'at_cannon_barrage_loss';
INSERT INTO `areatrigger_scripts` (`entry`, `ScriptName`) VALUES 
(10159, 'at_cannon_barrage_loss'),
(10160, 'at_cannon_barrage_loss'),
(10161, 'at_cannon_barrage_loss'),
(10162, 'at_cannon_barrage_loss');

delete from conditions where SourceTypeOrReferenceId = 13 and SourceEntry in (168820);
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `NegativeCondition`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES 
(13, 1, 168820, 0, 0, 31, 0, 3, 84030, 0, 0, 0, '', NULL),
(13, 2, 168820, 0, 0, 31, 0, 3, 84109, 0, 0, 0, '', NULL);

delete from spell_target_position where id = 168167;
INSERT INTO `spell_target_position` (`id`, `target_map`, `target_position_x`, `target_position_y`, `target_position_z`, `target_orientation`) VALUES 
(168167, 1195, 6857.7, -990.1, 24, 3);

delete from spell_trigger_dummy where spell_id in (168227,169073);
INSERT INTO `spell_trigger_dummy` (`spell_id`, `spell_trigger`, `option`, `target`, `caster`, `targetaura`, `bp0`, `bp1`, `bp2`, `effectmask`, `aura`, `chance`, `group`, `check_spell_id`, `comment`) VALUES 
(168227, 168167, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 'Skulloc - Gronn Smash'),
(169073, 168939, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 'Koramar - Shattering Blade'),
(168970, 168400, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 'Koramar - Berserker Leap');

delete from npc_spellclick_spells where npc_entry = 83612;
INSERT INTO `npc_spellclick_spells` (`npc_entry`, `spell_id`, `cast_flags`, `user_type`) VALUES 
(83612, 48754, 1, 0);

delete from vehicle_template_accessory where EntryOrAura = 83612;
INSERT INTO `vehicle_template_accessory` (`EntryOrAura`, `accessory_entry`, `seat_id`, `minion`, `description`, `summontype`, `summontimer`) VALUES 
(83612, 84030, 0, 1, 'Skulloc Cannon 1', 6, 30000),
(83612, 84109, 1, 1, 'Skulloc Cannon 2', 6, 30000);

delete from waypoint_data where id = 8361300;
INSERT INTO `waypoint_data` (`id`, `point`, `position_x`, `position_y`, `position_z`, `orientation`, `delay`, `move_flag`, `action`, `action_chance`, `entry`, `wpguid`) VALUES 
(8361300, 1, 6754.64, -979.937, 22.8173, 0, 0, 1, 0, 100, 0, 0),
(8361300, 2, 6765.4, -999.779, 23.0471, 0, 0, 1, 0, 100, 0, 0),
(8361300, 3, 6771.34, -961.237, 23.0473, 0, 0, 1, 0, 100, 0, 0),
(8361300, 4, 6774.23, -978.967, 23.0455, 0, 0, 1, 0, 100, 0, 0),
(8361300, 5, 6783.6, -991.868, 23.0465, 0, 0, 1, 0, 100, 0, 0),
(8361300, 6, 6793.95, -1000.73, 23.0465, 0, 0, 1, 0, 100, 0, 0),
(8361300, 7, 6804.59, -988.942, 23.1284, 0, 0, 1, 0, 100, 0, 0),
(8361300, 8, 6813.55, -973.701, 23.0469, 0, 0, 1, 0, 100, 0, 0),
(8361300, 9, 6826.54, -988.055, 23.1396, 0, 0, 1, 0, 100, 0, 0),
(8361300, 10, 6834.98, -971.328, 23.0466, 0, 0, 1, 0, 100, 0, 0),
(8361300, 11, 6754.67, -980.256, 22.8168, 0, 0, 1, 0, 100, 0, 0);


