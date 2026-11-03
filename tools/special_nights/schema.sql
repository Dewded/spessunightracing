-- Special Nights stats database (special_nights.db, in the game's base folder).
--
-- The game runs this whole script every time it opens the database, so every
-- statement must be safe to repeat. CMake embeds it into the game as a C string.
-- Views are dropped and recreated on every open, so changing one here updates
-- existing databases too. Bump user_version (and migrate in
-- game/SpecialNightsStats.c) when tables change.

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS players (
    name TEXT PRIMARY KEY
);

-- One row per cup played with every human named. completed = 1 once the last
-- race is scored; unfinished cups keep their races but every view skips them.
CREATE TABLE IF NOT EXISTS cup_runs (
    cup_run TEXT PRIMARY KEY,
    started_at TEXT,
    ended_at TEXT,
    mode TEXT,
    cup_id INTEGER,
    cup_name TEXT,
    difficulty INTEGER,
    humans INTEGER,
    drivers_in_cup INTEGER,
    completed INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS races (
    race_id INTEGER PRIMARY KEY,
    cup_run TEXT NOT NULL REFERENCES cup_runs(cup_run) ON DELETE CASCADE,
    track_index INTEGER NOT NULL,
    level_id INTEGER,
    track_name TEXT,
    laps INTEGER,
    recorded_at TEXT,
    UNIQUE (cup_run, track_index)
);

-- One row per racer (AI included, player NULL) per race. Times in milliseconds,
-- speeds in the game's internal speed units.
CREATE TABLE IF NOT EXISTS race_results (
    race_id INTEGER NOT NULL REFERENCES races(race_id) ON DELETE CASCADE,
    slot INTEGER NOT NULL,
    human INTEGER NOT NULL,
    player TEXT REFERENCES players(name),
    character TEXT,
    character_id INTEGER,
    engine TEXT,
    place INTEGER,
    points INTEGER,
    cup_points INTEGER,
    finished INTEGER,
    race_time_ms INTEGER,
    best_lap_ms INTEGER,
    turbo_ms INTEGER,
    avg_speed INTEGER,
    top_speed INTEGER,
    turbos INTEGER,
    PRIMARY KEY (race_id, slot)
);

CREATE TABLE IF NOT EXISTS cup_results (
    cup_run TEXT NOT NULL REFERENCES cup_runs(cup_run) ON DELETE CASCADE,
    slot INTEGER NOT NULL,
    human INTEGER NOT NULL,
    player TEXT REFERENCES players(name),
    character TEXT,
    engine TEXT,
    final_place INTEGER,
    points INTEGER,
    PRIMARY KEY (cup_run, slot)
);

-- Points for a cup by overall finishing place (AI racers included).
-- Edit the rows to change the scoring; places without a row score 0.
CREATE TABLE IF NOT EXISTS placement_points (
    place INTEGER PRIMARY KEY,
    points INTEGER NOT NULL
);
INSERT OR IGNORE INTO placement_points (place, points) VALUES (1, 10), (2, 6), (3, 3), (4, 1);

-- Everything below only looks at completed cups.

DROP VIEW IF EXISTS counted_race_results;
DROP VIEW IF EXISTS counted_cup_results;
DROP VIEW IF EXISTS counted_race_places;
DROP VIEW IF EXISTS counted_player_cup_places;
DROP VIEW IF EXISTS player_cup_summary;
DROP VIEW IF EXISTS player_track_summary;
DROP VIEW IF EXISTS head_to_head_races;
DROP VIEW IF EXISTS head_to_head_cups;
DROP VIEW IF EXISTS track_records;

CREATE VIEW IF NOT EXISTS counted_race_results AS
SELECT rr.*, r.cup_run, r.track_index, r.level_id, r.track_name, r.recorded_at,
       c.mode, c.cup_name, c.difficulty
FROM race_results rr
JOIN races r ON r.race_id = rr.race_id
JOIN cup_runs c ON c.cup_run = r.cup_run
WHERE c.completed = 1;

CREATE VIEW IF NOT EXISTS counted_cup_results AS
SELECT cr.*, c.mode, c.cup_name, c.difficulty, c.started_at, c.humans
FROM cup_results cr
JOIN cup_runs c ON c.cup_run = cr.cup_run
WHERE c.completed = 1;

-- The AI racers count as one competitor, "AI": in each race and cup it takes
-- the result of its best-placed racer. Places are overall (1-8).

-- One row per competitor (each human, plus AI) per race.
CREATE VIEW IF NOT EXISTS counted_race_places AS
SELECT race_id, cup_run, track_index, level_id, track_name, recorded_at, mode, cup_name, difficulty,
       human, player, character, place, points, finished, race_time_ms, best_lap_ms, turbo_ms, avg_speed, top_speed, turbos
FROM counted_race_results
WHERE human = 1
UNION ALL
SELECT race_id, cup_run, track_index, level_id, track_name, recorded_at, mode, cup_name, difficulty,
       0, 'AI', character, place, points, finished, race_time_ms, best_lap_ms, turbo_ms, avg_speed, top_speed, turbos
FROM (SELECT *, ROW_NUMBER() OVER (PARTITION BY race_id ORDER BY place) AS ai_rank
      FROM counted_race_results WHERE human = 0)
WHERE ai_rank = 1;

-- One row per competitor (each human, plus AI) per cup, with the points its place scores.
CREATE VIEW IF NOT EXISTS counted_player_cup_places AS
SELECT ranked.*, COALESCE(pp.points, 0) AS place_points
FROM (SELECT cup_run, started_at, human, player, character, final_place AS player_place, points AS cup_points
      FROM counted_cup_results
      WHERE human = 1
      UNION ALL
      SELECT cup_run, started_at, 0, 'AI', character, final_place, points
      FROM (SELECT *, ROW_NUMBER() OVER (PARTITION BY cup_run ORDER BY final_place) AS ai_rank
            FROM counted_cup_results WHERE human = 0)
      WHERE ai_rank = 1) ranked
LEFT JOIN placement_points pp ON pp.place = ranked.player_place;

-- Wins, podiums and average place are overall places; total_points are
-- placement points, total_cup_points the game's own cup score.
CREATE VIEW IF NOT EXISTS player_cup_summary AS
SELECT player,
       COUNT(*) AS cups,
       SUM(player_place = 1) AS cup_wins,
       SUM(player_place <= 3) AS cup_podiums,
       ROUND(AVG(player_place), 2) AS avg_final_place,
       SUM(place_points) AS total_points,
       ROUND(AVG(place_points), 1) AS avg_points,
       SUM(cup_points) AS total_cup_points
FROM counted_player_cup_places
GROUP BY player;

CREATE VIEW IF NOT EXISTS player_track_summary AS
SELECT player, level_id, track_name,
       COUNT(*) AS races,
       SUM(place = 1) AS wins,
       ROUND(AVG(place), 2) AS avg_place,
       MIN(place) AS best_place,
       SUM(points) AS points,
       MIN(best_lap_ms) AS best_lap_ms,
       MIN(race_time_ms) AS best_race_ms,
       MAX(top_speed) AS top_speed,
       ROUND(AVG(avg_speed)) AS avg_speed,
       ROUND(AVG(turbo_ms)) AS avg_turbo_ms
FROM counted_race_places
GROUP BY player, level_id;

-- One row per (player, opponent) pair of competitors (AI included) who raced each other.
CREATE VIEW IF NOT EXISTS head_to_head_races AS
SELECT a.player AS player, b.player AS opponent,
       COUNT(*) AS races,
       SUM(a.place < b.place) AS ahead,
       SUM(a.place > b.place) AS behind
FROM counted_race_places a
JOIN counted_race_places b ON b.race_id = a.race_id AND b.player <> a.player
GROUP BY a.player, b.player;

CREATE VIEW IF NOT EXISTS head_to_head_cups AS
SELECT a.player AS player, b.player AS opponent,
       COUNT(*) AS cups,
       SUM(a.player_place < b.player_place) AS ahead,
       SUM(a.player_place > b.player_place) AS behind
FROM counted_player_cup_places a
JOIN counted_player_cup_places b ON b.cup_run = a.cup_run AND b.player <> a.player
GROUP BY a.player, b.player;

CREATE VIEW IF NOT EXISTS track_records AS
SELECT level_id, track_name,
       (SELECT player FROM counted_race_results x
         WHERE x.level_id = t.level_id AND x.human = 1 AND x.best_lap_ms IS NOT NULL
         ORDER BY x.best_lap_ms LIMIT 1) AS best_lap_player,
       MIN(CASE WHEN human = 1 THEN best_lap_ms END) AS best_lap_ms,
       (SELECT player FROM counted_race_results x
         WHERE x.level_id = t.level_id AND x.human = 1
         ORDER BY x.top_speed DESC LIMIT 1) AS top_speed_player,
       MAX(CASE WHEN human = 1 THEN top_speed END) AS top_speed
FROM counted_race_results t
GROUP BY level_id;

PRAGMA user_version = 3;
