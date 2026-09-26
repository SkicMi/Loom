#!/usr/bin/env python3
"""kimodo_service.py bez modela (LOOM_KIMODO_FAKE=1): protokol, ponovna upotreba radnika, pad.

Sto se brani:
  - klijent sam pokrene radnika, prosljedi ispis i izlazni kod
  - drugi poziv ide ISTOM radniku (isti pid) - inace bi se modeli ucitavali svaki put, a bas
    to je servis trebao ukloniti (44 s -> 20 s po generiranju, izmjereno)
  - socket je samo vlasnikov (0600)
  - radnik koji padne pri ucitavanju ne drzi klijenta do isteka roka: prvi pokusaj je cekao 600 s
  - stop ugasi radnika i ukloni socket
"""
import os
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

SERVICE = Path(__file__).resolve().parents[1] / "tools" / "weavermotion" / "kimodo_service.py"


class KimodoServiceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.env = dict(os.environ, LOOM_KIMODO_FAKE="1", LOOM_KIMODO_SOCKET=str(Path(self.tmp.name) / "k.sock"),
                        LOOM_KIMODO_LOG_DIR=self.tmp.name, LOOM_KIMODO_IDLE_S="30")

    def tearDown(self):
        self.call("stop")
        self.tmp.cleanup()

    def call(self, *args, env=None):
        return subprocess.run([sys.executable, str(SERVICE), *args], env=env or self.env,
                              capture_output=True, text=True, timeout=60)

    def test_worker_is_started_once_and_reused(self):
        first = self.call("run", "walk", "--seed", "3")
        second = self.call("run", "jump")
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        self.assertIn("argv walk --seed 3", first.stdout)
        pid = lambda out: out.split("fake worker pid ")[1].split()[0]
        self.assertEqual(pid(first.stdout), pid(second.stdout))
        mode = stat.S_IMODE(os.stat(self.env["LOOM_KIMODO_SOCKET"]).st_mode)
        self.assertEqual(mode, 0o600)

    def test_exit_code_is_forwarded(self):
        env = dict(self.env, LOOM_KIMODO_FAKE_EXIT="7")
        self.assertEqual(self.call("run", "x", env=env).returncode, 7)

    def test_crashing_worker_falls_back_quickly(self):
        env = dict(self.env, LOOM_KIMODO_FAKE_CRASH="1", LOOM_KIMODO_START_S="120")
        started = time.monotonic()
        result = self.call("run", "x", env=env)
        self.assertEqual(result.returncode, 3, result.stdout)
        self.assertIn("running Kimodo directly", result.stdout)
        self.assertLess(time.monotonic() - started, 20.0)

    def test_stop_removes_socket(self):
        self.call("run", "x")
        self.assertTrue(Path(self.env["LOOM_KIMODO_SOCKET"]).exists())
        self.call("stop")
        time.sleep(1.5)
        self.assertFalse(Path(self.env["LOOM_KIMODO_SOCKET"]).exists())


if __name__ == "__main__":
    unittest.main()
