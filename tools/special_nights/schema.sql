-- Special Nights stats database (special_nights.db, in the game's base folder).
--
-- The game runs this whole script every time it opens the database, so every
-- statement must be safe to repeat. CMake embeds it into the game as a C string.
-- Bump user_version (and migrate in game/SpecialNightsStats.c) when tables change.

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

-- Everything below only looks at completed cups.

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

CREATE VIEW IF NOT EXISTS player_cup_summary AS
SELECT player,
       COUNT(*) AS cups,
       SUM(final_place = 1) AS cup_wins,
       SUM(final_place <= 3) AS cup_podiums,
       ROUND(AVG(final_place), 2) AS avg_final_place,
       SUM(points) AS total_points,
       ROUND(AVG(points), 1) AS avg_points
FROM counted_cup_results
WHERE human = 1
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
FROM counted_race_results
WHERE human = 1
GROUP BY player, level_id;

-- One row per (player, opponent) pair of humans who raced each other.
CREATE VIEW IF NOT EXISTS head_to_head_races AS
SELECT a.player AS player, b.player AS opponent,
       COUNT(*) AS races,
       SUM(a.place < b.place) AS ahead,
       SUM(a.place > b.place) AS behind
FROM counted_race_results a
JOIN counted_race_results b ON b.race_id = a.race_id AND b.slot <> a.slot
WHERE a.human = 1 AND b.human = 1
GROUP BY a.player, b.player;

CREATE VIEW IF NOT EXISTS head_to_head_cups AS
SELECT a.player AS player, b.player AS opponent,
       COUNT(*) AS cups,
       SUM(a.final_place < b.final_place) AS ahead,
       SUM(a.final_place > b.final_place) AS behind
FROM counted_cup_results a
JOIN counted_cup_results b ON b.cup_run = a.cup_run AND b.slot <> a.slot
WHERE a.human = 1 AND b.human = 1
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

PRAGMA user_version = 1;
