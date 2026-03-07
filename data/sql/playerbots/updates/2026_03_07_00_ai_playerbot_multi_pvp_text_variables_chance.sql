INSERT INTO `ai_playerbot_texts_chance` (`name`, `probability`)
SELECT 'pvp_currency', 100
WHERE NOT EXISTS (
    SELECT 1 FROM `ai_playerbot_texts_chance` WHERE `name` = 'pvp_currency'
);

INSERT INTO `ai_playerbot_texts_chance` (`name`, `probability`)
SELECT 'pvp_arena_team', 100
WHERE NOT EXISTS (
    SELECT 1 FROM `ai_playerbot_texts_chance` WHERE `name` = 'pvp_arena_team'
);

INSERT INTO `ai_playerbot_texts_chance` (`name`, `probability`)
SELECT 'pvp_no_arena_team', 100
WHERE NOT EXISTS (
    SELECT 1 FROM `ai_playerbot_texts_chance` WHERE `name` = 'pvp_no_arena_team'
);
