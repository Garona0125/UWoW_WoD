REPLACE INTO `quest_template_addon` (`ID`, `PrevQuestID`, `NextQuestID`, `ExclusiveGroup`) VALUES 
('34584', '34583', '34585', '-34584'),
('34616', '34583', '34585', '-34584'),
('34586', '34585', '0', '0');

DELETE FROM `spell_area` WHERE area in (6719);
REPLACE INTO `spell_area` (`spell`, `area`, `quest_start`, `quest_end`, `aura_spell`, `racemask`, `gender`, `autocast`, `quest_start_status`, `quest_end_status`) VALUES 
('158228', '6719', '34584', '34584', '0', '0', '2', '1', '10', '64');

-- 4352 ���
-- 3329 - ���� ����� ����� �������.
-- 3639 - 34582 - ����� ������, ������� �� ����� �����.
-- 3353 - while not complete 34582. �� ������ ������ ���������������.
-- 3695 - at SMSG_QUEST_UPDATE_ADD_CREDIT 34583 id 84778 remove 79242 ��� ������ ����� ������ ������� 79242. � ��� ����� ����� ������.

REPLACE INTO `phase_definitions` (`zoneId`, `entry`, `phasemask`, `phaseId`, `PreloadMapID`, `VisibleMapID`, `flags`, `comment`) VALUES
-- 2877 2884 2988 3054 3055 3122 3184 3238 3244 3253 3329 3353 3420 3434 3695 3926 3934 4086 4318 4352
(6719, 1, 0, '4352', 0, 0, 16, 'Draenor. ShadowMoon.Valley. ���� ����'),
-- 2877 2884 2988 3054 3055 3122 3184 3238 3244 3253 3353 3420 3434 3695 3926 3934 4086 4318 4352               while not take 34582
(6719, 2, 0, '3329', 0, 0, 16, 'Draenor. ShadowMoon.Valley. ���� �� ���� �����  34582'), 
-- 2877 2884 2988 3054 3055 3122 3184 3238 3244 3253 3353 3420 3434 3639 3695 3926 3934 4086 4318 4352          while take 34582
(6719, 3, 0, '3639', 0, 0, 16, 'Draenor. ShadowMoon.Valley.  ���������� ����� ��������� 34582 �����.'),
-- 2877 2884 2988 3054 3055 3122 3184 3238 3244 3253 3420 3434 3639 3695 3926 3934 4086 4318 4352               while not Complete 34582
(6719, 4, 0, '3353', 0, 0, 16, 'Draenor. ShadowMoon.Valley. while not complete or reward.'),
-- 2877 2884 2988 3054 3055 3122 3184 3238 3244 3253 3420 3434 3639 3926 3934 4086 4318 4352                    at SMSG_QUEST_UPDATE_ADD_CREDIT 34583 id 84778
(6719, 5, 0, '3695', 0, 0, 16, 'Draenor. ShadowMoon.Valley. ������'),


(6719, 100, 0, '2877 2884 2988 3054 3055 3122 3184 3238 3244 3253 3420 3434 3926 3934 4086 4318', 0, 0, 16, 'Draenor. ShadowMoon.Valley. general.');


DELETE FROM `conditions` WHERE SourceTypeOrReferenceId = 23 AND SourceGroup = 6719;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `NegativeCondition`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(23, 6719, 2, 0, 0, 14, 0, 34582, 0, 0, 0, 0, '', 'Draenor. ShadowMoon.Valley. 34582'),
(23, 6719, 3, 0, 0, 14, 0, 34582, 0, 0, 1, 0, '', 'Draenor. ShadowMoon.Valley. 34582'),
(23, 6719, 4, 0, 0, 28, 0, 34582, 0, 0, 1, 0, '', 'Draenor. ShadowMoon.Valley. 34582'),
(23, 6719, 4, 0, 0, 8, 0, 34582, 0, 0, 1, 0, '', 'Draenor. ShadowMoon.Valley. 34582');


-- Q34583 spell 160405 npc 82125
DELETE FROM `creature_text` WHERE entry = 82125;
REPLACE INTO `creature_text` (`entry`, `groupid`, `id`, `text`, `type`, `language`, `probability`, `emote`, `duration`, `sound`, `comment`) VALUES
(82125, 0, 0, '� ���� ������� ������ � ���������, �� ���� ���������.', 12, 0, 100, 0, 0, 44990, '��������� ��� ������ to Player');

-- Q:34584
REPLACE INTO `gameobject_quest_visual` (`goID`, `questID`, `incomplete_state_spell_visual`, `incomplete_state_world_effect`, `complete_state_spell_visual`, `complete_state_world_effect`, `Comment`) VALUES
('230335', '34584', '37794', '2100', '0', '0', '');

-- Q: 34586
DELETE FROM `smart_scripts` WHERE `entryorguid` = 79243;
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `action_type`, `action_param1`, `action_param2`, `action_param3`, `action_param4`, `action_param5`, `action_param6`, `target_type`, `target_param1`, `target_param2`, `target_param3`, `target_x`, `target_y`, `target_z`, `target_o`, `comment`) VALUES
(79243, 0, 0, 1, 62, 0, 100, 0, 16871, 0, 0, 0, 85, 156020, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 'on gossip select - cast spell');
