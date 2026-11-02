#!/usr/bin/env python3
"""Special Nights cup statistics.

The game appends one JSON line per finished race and per finished cup to
special_nights_stats.jsonl (in the game's base folder, next to assets/).
This tool rebuilds a SQLite database from that file and prints reports.

    python tools/special_nights/stats.py special_nights_stats.jsonl
    python tools/special_nights/stats.py special_nights_stats.jsonl --db stats.db --no-report

Only completed cups count: a cup whose run has no "cup" line (quit early,
crash) keeps its races in the database but is left out of every view.
The game itself only writes cups in which every human picked a player name.
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path

SCHEMA = """
CREATE TABLE players (
    name TEXT PRIMARY KEY
);

CREATE TABLE cup_runs (
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

CREATE TABLE races (
    race_id INTEGER PRIMARY KEY,
    cup_run TEXT NOT NULL REFERENCES cup_runs(cup_run),
    track_index INTEGER NOT NULL,
    level_id INTEGER,
    track_name TEXT,
    laps INTEGER,
    recorded_at TEXT,
    UNIQUE (cup_run, track_index)
);

CREATE TABLE race_results (
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

CREATE TABLE cup_results (
    cup_run TEXT NOT NULL REFERENCES cup_runs(cup_run),
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

CREATE VIEW counted_race_results AS
SELECT rr.*, r.cup_run, r.track_index, r.level_id, r.track_name, r.recorded_at,
       c.mode, c.cup_name, c.difficulty
FROM race_results rr
JOIN races r ON r.race_id = rr.race_id
JOIN cup_runs c ON c.cup_run = r.cup_run
WHERE c.completed = 1;

CREATE VIEW counted_cup_results AS
SELECT cr.*, c.mode, c.cup_name, c.difficulty, c.started_at, c.humans
FROM cup_results cr
JOIN cup_runs c ON c.cup_run = cr.cup_run
WHERE c.completed = 1;

CREATE VIEW player_cup_summary AS
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

CREATE VIEW player_track_summary AS
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
CREATE VIEW head_to_head_races AS
SELECT a.player AS player, b.player AS opponent,
       COUNT(*) AS races,
       SUM(a.place < b.place) AS ahead,
       SUM(a.place > b.place) AS behind
FROM counted_race_results a
JOIN counted_race_results b ON b.race_id = a.race_id AND b.slot <> a.slot
WHERE a.human = 1 AND b.human = 1
GROUP BY a.player, b.player;

CREATE VIEW head_to_head_cups AS
SELECT a.player AS player, b.player AS opponent,
       COUNT(*) AS cups,
       SUM(a.final_place < b.final_place) AS ahead,
       SUM(a.final_place > b.final_place) AS behind
FROM counted_cup_results a
JOIN counted_cup_results b ON b.cup_run = a.cup_run AND b.slot <> a.slot
WHERE a.human = 1 AND b.human = 1
GROUP BY a.player, b.player;

CREATE VIEW track_records AS
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
"""


def _bool(value) -> int:
    return 1 if value else 0


def _ensure_cup_run(db: sqlite3.Connection, event: dict) -> None:
    db.execute(
        """INSERT INTO cup_runs (cup_run, started_at, mode, cup_id, cup_name, difficulty, humans, drivers_in_cup)
           VALUES (?, ?, ?, ?, ?, ?, ?, ?)
           ON CONFLICT(cup_run) DO NOTHING""",
        (
            event["cup_run"],
            event.get("recorded_at"),
            event.get("mode"),
            event.get("cup_id"),
            event.get("cup_name"),
            event.get("difficulty"),
            event.get("humans"),
            event.get("drivers_in_cup"),
        ),
    )


def _ensure_player(db: sqlite3.Connection, name) -> None:
    if name:
        db.execute("INSERT OR IGNORE INTO players (name) VALUES (?)", (name,))


def _import_race(db: sqlite3.Connection, event: dict) -> None:
    _ensure_cup_run(db, event)
    # A repeated race (same cup run and track) replaces the earlier record.
    db.execute(
        "DELETE FROM races WHERE cup_run = ? AND track_index = ?",
        (event["cup_run"], event["track_index"]),
    )
    cursor = db.execute(
        """INSERT INTO races (cup_run, track_index, level_id, track_name, laps, recorded_at)
           VALUES (?, ?, ?, ?, ?, ?)""",
        (
            event["cup_run"],
            event["track_index"],
            event.get("level_id"),
            event.get("track_name"),
            event.get("laps"),
            event.get("recorded_at"),
        ),
    )
    race_id = cursor.lastrowid
    for result in event.get("results", []):
        _ensure_player(db, result.get("player"))
        db.execute(
            """INSERT INTO race_results (race_id, slot, human, player, character, character_id, engine, place,
                                         points, cup_points, finished, race_time_ms, best_lap_ms, turbo_ms,
                                         avg_speed, top_speed, turbos)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
            (
                race_id,
                result["slot"],
                _bool(result.get("human")),
                result.get("player"),
                result.get("character"),
                result.get("character_id"),
                result.get("engine"),
                result.get("place"),
                result.get("points"),
                result.get("cup_points"),
                _bool(result.get("finished")),
                result.get("race_time_ms"),
                result.get("best_lap_ms"),
                result.get("turbo_ms"),
                result.get("avg_speed"),
                result.get("top_speed"),
                result.get("turbos"),
            ),
        )


def _import_cup(db: sqlite3.Connection, event: dict) -> None:
    _ensure_cup_run(db, event)
    db.execute(
        "UPDATE cup_runs SET completed = 1, ended_at = ? WHERE cup_run = ?",
        (event.get("recorded_at"), event["cup_run"]),
    )
    db.execute("DELETE FROM cup_results WHERE cup_run = ?", (event["cup_run"],))
    for result in event.get("results", []):
        _ensure_player(db, result.get("player"))
        db.execute(
            """INSERT INTO cup_results (cup_run, slot, human, player, character, engine, final_place, points)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?)""",
            (
                event["cup_run"],
                result["slot"],
                _bool(result.get("human")),
                result.get("player"),
                result.get("character"),
                result.get("engine"),
                result.get("final_place"),
                result.get("points"),
            ),
        )


def build_database(jsonl_paths: list[Path], db_path: Path) -> tuple[sqlite3.Connection, list[str]]:
    """Rebuild db_path from scratch. Returns the connection and any warnings."""
    if db_path.exists():
        db_path.unlink()
    db = sqlite3.connect(db_path)
    db.execute("PRAGMA foreign_keys = ON")
    db.executescript(SCHEMA)

    warnings: list[str] = []
    for path in jsonl_paths:
        with path.open("r", encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    event = json.loads(line)
                    if event.get("type") == "race":
                        _import_race(db, event)
                    elif event.get("type") == "cup":
                        _import_cup(db, event)
                    else:
                        warnings.append(f"{path}:{line_number}: unknown event type {event.get('type')!r}")
                except (json.JSONDecodeError, KeyError, TypeError, sqlite3.Error) as error:
                    # A half-written last line (game closed mid-write) must not lose the rest.
                    warnings.append(f"{path}:{line_number}: skipped ({error})")
    db.commit()
    return db, warnings


# ---------------------------------------------------------------------------
# Reports


def _ms(value) -> str:
    if value is None:
        return "-"
    minutes, rest = divmod(int(value), 60000)
    return f"{minutes}:{rest / 1000:06.3f}"


def _table(title: str, headers: list[str], rows: list[tuple]) -> str:
    lines = [title, "=" * len(title)]
    if not rows:
        lines.append("(nothing yet)")
        return "\n".join(lines)
    cells = [[("-" if v is None else str(v)) for v in row] for row in rows]
    widths = [max(len(h), *(len(r[i]) for r in cells)) for i, h in enumerate(headers)]
    lines.append("  ".join(h.ljust(widths[i]) for i, h in enumerate(headers)))
    lines.append("  ".join("-" * w for w in widths))
    for row in cells:
        lines.append("  ".join(v.ljust(widths[i]) for i, v in enumerate(row)))
    return "\n".join(lines)


def report(db: sqlite3.Connection) -> str:
    sections = []

    counted = db.execute("SELECT COUNT(*) FROM cup_runs WHERE completed = 1").fetchone()[0]
    unfinished = db.execute("SELECT COUNT(*) FROM cup_runs WHERE completed = 0").fetchone()[0]
    sections.append(f"Special Nights stats: {counted} counted cups ({unfinished} unfinished, not counted)")

    sections.append(
        _table(
            "Cup leaderboard",
            ["Player", "Cups", "Wins", "Podiums", "Avg place", "Points", "Avg points"],
            db.execute(
                """SELECT player, cups, cup_wins, cup_podiums, avg_final_place, total_points, avg_points
                   FROM player_cup_summary ORDER BY cup_wins DESC, avg_final_place, total_points DESC"""
            ).fetchall(),
        )
    )

    sections.append(
        _table(
            "Cup history",
            ["Cup run", "Cup", "Mode", "Winner", "Standings"],
            [
                (
                    run,
                    cup_name,
                    mode,
                    winner,
                    standings,
                )
                for run, cup_name, mode, winner, standings in db.execute(
                    """SELECT c.cup_run, c.cup_name, c.mode,
                              (SELECT COALESCE(player, character) FROM cup_results w
                                WHERE w.cup_run = c.cup_run ORDER BY final_place LIMIT 1),
                              (SELECT GROUP_CONCAT(final_place || '. ' || player || ' (' || points || ')', ', ')
                                 FROM (SELECT * FROM cup_results h WHERE h.cup_run = c.cup_run AND h.human = 1
                                       ORDER BY final_place))
                       FROM cup_runs c WHERE c.completed = 1 ORDER BY c.cup_run"""
                ).fetchall()
            ],
        )
    )

    sections.append(
        _table(
            "Per track",
            ["Track", "Player", "Races", "Wins", "Avg place", "Best lap", "Top speed", "Avg speed", "Avg turbo"],
            [
                (track, player, races, wins, avg_place, _ms(best_lap), top_speed, avg_speed, _ms(avg_turbo))
                for track, player, races, wins, avg_place, best_lap, top_speed, avg_speed, avg_turbo in db.execute(
                    """SELECT track_name, player, races, wins, avg_place, best_lap_ms, top_speed,
                              CAST(avg_speed AS INTEGER), avg_turbo_ms
                       FROM player_track_summary ORDER BY level_id, avg_place, player"""
                ).fetchall()
            ],
        )
    )

    sections.append(
        _table(
            "Head to head (races)",
            ["Player", "Opponent", "Races", "Ahead", "Behind"],
            db.execute(
                """SELECT player, opponent, races, ahead, behind FROM head_to_head_races
                   ORDER BY player, opponent"""
            ).fetchall(),
        )
    )

    sections.append(
        _table(
            "Head to head (cups)",
            ["Player", "Opponent", "Cups", "Ahead", "Behind"],
            db.execute(
                """SELECT player, opponent, cups, ahead, behind FROM head_to_head_cups
                   ORDER BY player, opponent"""
            ).fetchall(),
        )
    )

    sections.append(
        _table(
            "Track records",
            ["Track", "Best lap", "By", "Top speed", "By"],
            [
                (track, _ms(lap), lap_player, top_speed, speed_player)
                for track, lap_player, lap, speed_player, top_speed in db.execute(
                    """SELECT track_name, best_lap_player, best_lap_ms, top_speed_player, top_speed
                       FROM track_records ORDER BY level_id"""
                ).fetchall()
            ],
        )
    )

    return "\n\n".join(sections)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("jsonl", nargs="+", type=Path, help="special_nights_stats.jsonl file(s) written by the game")
    parser.add_argument("--db", type=Path, help="SQLite file to (re)build (default: special_nights.db next to the first input)")
    parser.add_argument("--no-report", action="store_true", help="only build the database")
    args = parser.parse_args(argv)

    for path in args.jsonl:
        if not path.is_file():
            parser.error(f"{path} does not exist")

    db_path = args.db or args.jsonl[0].with_name("special_nights.db")
    db, warnings = build_database(args.jsonl, db_path)
    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)

    if not args.no_report:
        # Windows consoles may not be UTF-8; never fail a report on a name.
        sys.stdout.reconfigure(errors="replace")
        print(report(db))
        print(f"\nDatabase: {db_path}")
    db.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
