import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import stats  # noqa: E402


def _driver(slot, player, character, place, points, **extra):
    entry = {
        "slot": slot,
        "human": player is not None,
        "player": player,
        "character_id": slot,
        "character": character,
        "engine": "BALANCED",
    }
    entry.update(place=place, points=points, **extra)
    return entry


def _race(cup_run, track_index, level_id, track_name, order):
    results = []
    for place, (slot, player, character) in enumerate(order, 1):
        results.append(
            _driver(
                slot,
                player,
                character,
                place,
                [10, 8, 6, 4, 3, 2, 1, 0][place - 1],
                cup_points=0,
                finished=True,
                race_time_ms=120000 + place * 1000,
                best_lap_ms=38000 + place * 500,
                turbo_ms=9000 - place * 100,
                avg_speed=9000 - place * 10,
                top_speed=12000 - place * 10,
                turbos=10,
            )
        )
    return {
        "type": "race",
        "version": 1,
        "cup_run": cup_run,
        "recorded_at": "2026-11-02T20:00:00",
        "mode": "arcade",
        "cup_id": 0,
        "cup_name": "Wumpa Cup",
        "difficulty": 160,
        "humans": 2,
        "drivers_in_cup": 4,
        "track_index": track_index,
        "level_id": level_id,
        "track_name": track_name,
        "laps": 3,
        "results": results,
    }


ZAMIZA = (0, "Zamiza", "crash")
FROTTIS = (1, "Fröttis", "oxide")
AI_A = (2, None, "coco")
AI_B = (3, None, "tiny")


class SpecialNightsStatsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def _build(self, events, trailing=""):
        path = self.dir / "special_nights_stats.jsonl"
        path.write_text("".join(json.dumps(e, ensure_ascii=False) + "\n" for e in events) + trailing, encoding="utf-8")
        return stats.build_database([path], self.dir / "special_nights.db")

    def test_only_completed_cups_count(self):
        events = [
            _race("run-1", 0, 3, "Crash Cove", [ZAMIZA, FROTTIS, AI_A, AI_B]),
            _race("run-1", 1, 6, "Roo's Tubes", [FROTTIS, ZAMIZA, AI_B, AI_A]),
            _race("run-1", 2, 0, "Dingo Canyon", [ZAMIZA, AI_A, FROTTIS, AI_B]),
            _race("run-1", 3, 4, "Mystery Caves", [ZAMIZA, FROTTIS, AI_A, AI_B]),
            {
                "type": "cup",
                "cup_run": "run-1",
                "recorded_at": "2026-11-02T20:20:00",
                "mode": "arcade",
                "cup_id": 0,
                "cup_name": "Wumpa Cup",
                "results": [
                    _driver(0, "Zamiza", "crash", 1, 38, final_place=1),
                    _driver(1, "Fröttis", "oxide", 2, 32, final_place=2),
                    _driver(2, None, "coco", 3, 20, final_place=3),
                    _driver(3, None, "tiny", 4, 14, final_place=4),
                ],
            },
            # Quit after one race: stored, never counted.
            _race("run-2", 0, 3, "Crash Cove", [FROTTIS, ZAMIZA, AI_A, AI_B]),
        ]
        db, warnings = self._build(events, trailing='{"type": "race", "cup_run"')

        self.assertEqual(len(warnings), 1)  # the half-written last line
        self.assertEqual(db.execute("SELECT COUNT(*) FROM cup_runs WHERE completed = 1").fetchone()[0], 1)
        self.assertEqual(db.execute("SELECT COUNT(*) FROM races").fetchone()[0], 5)

        summary = dict(
            (row[0], row[1:]) for row in db.execute("SELECT player, cups, cup_wins, avg_final_place, total_points FROM player_cup_summary")
        )
        self.assertEqual(summary["Zamiza"], (1, 1, 1.0, 38))
        self.assertEqual(summary["Fröttis"], (1, 0, 2.0, 32))

        h2h = dict(((p, o), (n, a, b)) for p, o, n, a, b in db.execute("SELECT * FROM head_to_head_races"))
        self.assertEqual(h2h[("Zamiza", "Fröttis")], (4, 3, 1))
        self.assertEqual(h2h[("Fröttis", "Zamiza")], (4, 1, 3))

        cove = db.execute(
            "SELECT races, wins, best_lap_ms FROM player_track_summary WHERE player = 'Fröttis' AND level_id = 3"
        ).fetchone()
        self.assertEqual(cove, (1, 0, 39000))  # run-2's Crash Cove win does not count

        text = stats.report(db)
        self.assertIn("Cup leaderboard", text)
        self.assertIn("Zamiza", text)
        db.close()

    def test_repeated_race_replaces_earlier(self):
        events = [
            _race("run-1", 0, 3, "Crash Cove", [ZAMIZA, FROTTIS, AI_A, AI_B]),
            _race("run-1", 0, 3, "Crash Cove", [FROTTIS, ZAMIZA, AI_A, AI_B]),
        ]
        db, warnings = self._build(events)
        self.assertEqual(warnings, [])
        self.assertEqual(db.execute("SELECT COUNT(*) FROM races").fetchone()[0], 1)
        self.assertEqual(db.execute("SELECT COUNT(*) FROM race_results").fetchone()[0], 4)
        self.assertEqual(db.execute("SELECT player FROM race_results WHERE place = 1").fetchone()[0], "Fröttis")
        db.close()


if __name__ == "__main__":
    unittest.main()
