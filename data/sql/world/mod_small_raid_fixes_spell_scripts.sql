-- mod-small-raid-fixes: bind the Wrath of Ragnaros fix to its spell
-- Applied automatically by the worldserver updater from data/sql/world/.

DELETE FROM `spell_script_names` WHERE `spell_id` = 20566 AND `ScriptName` = 'SmallRaidFixes_WrathOfRagnaros';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(20566, 'SmallRaidFixes_WrathOfRagnaros');
