#!/usr/bin/env python3
"""Special Nights cup statistics report.

The game writes every scored race and every finished cup straight into
special_nights.db (SQLite, in the game's base folder, next to assets/).
This tool prints reports from it; any SQLite browser can open it too.

    python tools/special_nights/stats.py                 # ./special_nights.db
    python tools/special_nights/stats.py path/to/special_nights.db

Only completed cups count: a cup that was quit keeps its races in the
database but every view (and so every report) leaves it out. The game only
records cups in which every human picked a player name.
"""

from __future__ import annotations

import argparse
import sqlite3
import sys
from pathlib import Path

SCHEMA_PATH = Path(__file__).resolve().with_name("schema.sql")


def open_database(db_path: Path) -> sqlite3.Connection:
    """Opens a stats database and applies the schema, as the game does on every open.

    The schema only creates what is missing and rebuilds the views, so this never
    touches recorded data; it keeps reports current after a schema update.
    """
    db = sqlite3.connect(db_path)
    db.executescript(SCHEMA_PATH.read_text(encoding="utf-8"))
    return db


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
            "Cup leaderboard (overall places, AI racers as one; points from placement_points)",
            ["Player", "Cups", "Wins", "Podiums", "Avg place", "Points", "Avg points", "Cup points"],
            db.execute(
                """SELECT player, cups, cup_wins, cup_podiums, avg_final_place, total_points, avg_points, total_cup_points
                   FROM player_cup_summary ORDER BY total_points DESC, cup_wins DESC, avg_final_place"""
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
    parser.add_argument("db", nargs="?", type=Path, default=Path("special_nights.db"), help="stats database written by the game")
    args = parser.parse_args(argv)

    if not args.db.is_file():
        parser.error(f"{args.db} does not exist yet: finish a cup with every player named first")

    db = open_database(args.db)
    # Windows consoles may not be UTF-8; never fail a report on a name.
    sys.stdout.reconfigure(errors="replace")
    print(report(db))
    db.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
