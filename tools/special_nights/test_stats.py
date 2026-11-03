import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import stats  # noqa: E402

ZAMIZA = (0, "Zamiza", "crash")
FROTTIS = (1, "Fröttis", "oxide")
AI_A = (2, None, "coco")
AI_B = (3, None, "tiny")
POINTS = [10, 8, 6, 4]


class FakeGame:
    """Writes rows the way game/SpecialNightsStats.c does."""

    def __init__(self, path: Path):
        self.db = sqlite3.connect(path)
        self.db.executescript(stats.SCHEMA_PATH.read_text(encoding="utf-8"))

    def _cup_run(self, run):
        for name in ("Zamiza", "Fröttis"):
            self.db.execute("INSERT OR IGNORE INTO players (name) VALUES (?)", (name,))
        self.db.execute(
            "INSERT OR IGNORE INTO cup_runs (cup_run, started_at, mode, cup_id, cup_name, difficulty, humans, drivers_in_cup) "
            "VALUES (?, '2026-11-02T20:00:00', 'arcade', 0, 'Wumpa Cup', 160, 2, 4)",
            (run,),
        )

    def race(self, run, track_index, level_id, track_name, order):
        self._cup_run(run)
        self.db.execute("DELETE FROM races WHERE cup_run = ? AND track_index = ?", (run, track_index))
        race_id = self.db.execute(
            "INSERT INTO races (cup_run, track_index, level_id, track_name, laps, recorded_at) VALUES (?, ?, ?, ?, 3, 'x')",
            (run, track_index, level_id, track_name),
        ).lastrowid
        for place, (slot, player, character) in enumerate(order, 1):
            self.db.execute(
                "INSERT INTO race_results (race_id, slot, human, player, character, character_id, engine, place, points, cup_points, "
                "finished, race_time_ms, best_lap_ms, turbo_ms, avg_speed, top_speed, turbos) "
                "VALUES (?, ?, ?, ?, ?, ?, 'BALANCED', ?, ?, 0, 1, ?, ?, ?, ?, ?, 10)",
                (
                    race_id,
                    slot,
                    player is not None,
                    player,
                    character,
                    slot,
                    place,
                    POINTS[place - 1],
                    120000 + place * 1000,
                    38000 + place * 500,
                    9000 - place * 100,
                    9000 - place * 10,
                    12000 - place * 10,
                ),
            )
        self.db.commit()

    def cup_end(self, run, standings):
        self._cup_run(run)
        self.db.execute("DELETE FROM cup_results WHERE cup_run = ?", (run,))
        for place, ((slot, player, character), points) in enumerate(standings, 1):
            self.db.execute(
                "INSERT INTO cup_results (cup_run, slot, human, player, character, engine, final_place, points) "
                "VALUES (?, ?, ?, ?, ?, 'BALANCED', ?, ?)",
                (run, slot, player is not None, player, character, place, points),
            )
        self.db.execute("UPDATE cup_runs SET completed = 1, ended_at = 'y' WHERE cup_run = ?", (run,))
        self.db.commit()


class SpecialNightsStatsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / "special_nights.db"
        self.game = FakeGame(self.path)

    def tearDown(self):
        self.game.db.close()
        self.tmp.cleanup()

    def test_schema_can_be_rerun(self):
        # The game runs the schema on every open.
        self.game.db.executescript(stats.SCHEMA_PATH.read_text(encoding="utf-8"))
        self.assertEqual(self.game.db.execute("PRAGMA user_version").fetchone()[0], 3)

    def test_only_completed_cups_count(self):
        g = self.game
        g.race("run-1", 0, 3, "Crash Cove", [ZAMIZA, FROTTIS, AI_A, AI_B])
        g.race("run-1", 1, 6, "Roo's Tubes", [FROTTIS, ZAMIZA, AI_B, AI_A])
        g.race("run-1", 2, 0, "Dingo Canyon", [ZAMIZA, AI_A, FROTTIS, AI_B])
        g.race("run-1", 3, 4, "Mystery Caves", [ZAMIZA, FROTTIS, AI_A, AI_B])
        g.cup_end("run-1", [(ZAMIZA, 38), (FROTTIS, 32), (AI_A, 20), (AI_B, 14)])
        # Quit after one race: stored, never counted.
        g.race("run-2", 0, 3, "Crash Cove", [FROTTIS, ZAMIZA, AI_A, AI_B])

        db = stats.open_database(self.path)
        self.assertEqual(db.execute("SELECT COUNT(*) FROM races").fetchone()[0], 5)

        summary = {
            row[0]: row[1:]
            for row in db.execute("SELECT player, cups, cup_wins, avg_final_place, total_points, total_cup_points FROM player_cup_summary")
        }
        self.assertEqual(summary["Zamiza"], (1, 1, 1.0, 10, 38))  # 1st: 10 placement points
        self.assertEqual(summary["Fröttis"], (1, 0, 2.0, 6, 32))
        self.assertEqual(summary["AI"], (1, 0, 3.0, 3, 20))  # best AI racer was 3rd

        h2h = {(p, o): (n, a, b) for p, o, n, a, b in db.execute("SELECT * FROM head_to_head_races")}
        self.assertEqual(h2h[("Zamiza", "Fröttis")], (4, 3, 1))
        self.assertEqual(h2h[("Fröttis", "Zamiza")], (4, 1, 3))
        # AI's best racer per race: 3rd, 3rd, 2nd, 3rd.
        self.assertEqual(h2h[("Zamiza", "AI")], (4, 4, 0))
        self.assertEqual(h2h[("Fröttis", "AI")], (4, 3, 1))
        self.assertEqual(h2h[("AI", "Fröttis")], (4, 1, 3))

        cove = db.execute("SELECT races, wins, best_lap_ms FROM player_track_summary WHERE player = 'Fröttis' AND level_id = 3").fetchone()
        self.assertEqual(cove, (1, 0, 39000))  # run-2's Crash Cove win does not count

        text = stats.report(db)
        self.assertIn("1 counted cups (1 unfinished, not counted)", text)
        self.assertIn("Zamiza", text)
        self.assertEqual(db.execute("SELECT COUNT(*) FROM races").fetchone()[0], 5)  # opening never touches data
        db.close()

    def test_places_are_overall_with_ai_as_one_competitor(self):
        # Arcade cup: AI racers finish 1st and 3rd, the players 2nd and 4th.
        self.game.cup_end("run-1", [(AI_A, 40), (FROTTIS, 30), (AI_B, 20), (ZAMIZA, 10)])
        db = stats.open_database(self.path)
        places = dict(db.execute("SELECT player, player_place FROM counted_player_cup_places").fetchall())
        self.assertEqual(places, {"Fröttis": 2, "Zamiza": 4, "AI": 1})
        summary = dict(db.execute("SELECT player, total_points FROM player_cup_summary").fetchall())
        self.assertEqual(summary, {"Fröttis": 6, "Zamiza": 1, "AI": 10})
        cups = {(p, o): (a, b) for p, o, _, a, b in db.execute("SELECT * FROM head_to_head_cups")}
        self.assertEqual(cups[("AI", "Fröttis")], (1, 0))
        db.close()

    def test_placement_points_are_editable(self):
        self.game.cup_end("run-1", [(ZAMIZA, 38), (FROTTIS, 32)])
        self.game.db.execute("UPDATE placement_points SET points = 25 WHERE place = 1")
        self.game.db.commit()
        db = stats.open_database(self.path)  # re-running the schema keeps the edit
        summary = dict(db.execute("SELECT player, total_points FROM player_cup_summary").fetchall())
        self.assertEqual(summary, {"Zamiza": 25, "Fröttis": 6})
        db.close()

    def test_old_database_gets_new_views(self):
        self.game.cup_end("run-1", [(ZAMIZA, 38), (FROTTIS, 32)])
        # A version 1 database: old summary view, no placement scoring.
        self.game.db.executescript(
            "DROP VIEW player_cup_summary; DROP VIEW counted_player_cup_places; DROP TABLE placement_points;"
            "CREATE VIEW player_cup_summary AS SELECT player, COUNT(*) AS cups FROM counted_cup_results GROUP BY player;"
            "PRAGMA user_version = 1;"
        )
        db = stats.open_database(self.path)
        self.assertEqual(db.execute("SELECT total_points FROM player_cup_summary WHERE player = 'Zamiza'").fetchone()[0], 10)
        self.assertEqual(db.execute("PRAGMA user_version").fetchone()[0], 3)
        db.close()

    def test_repeated_race_replaces_earlier(self):
        self.game.race("run-1", 0, 3, "Crash Cove", [ZAMIZA, FROTTIS, AI_A, AI_B])
        self.game.race("run-1", 0, 3, "Crash Cove", [FROTTIS, ZAMIZA, AI_A, AI_B])
        db = self.game.db
        self.assertEqual(db.execute("SELECT COUNT(*) FROM races").fetchone()[0], 1)
        self.assertEqual(db.execute("SELECT COUNT(*) FROM race_results").fetchone()[0], 4)  # old results cascaded away
        self.assertEqual(db.execute("SELECT player FROM race_results WHERE place = 1").fetchone()[0], "Fröttis")


if __name__ == "__main__":
    unittest.main()
