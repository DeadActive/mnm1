import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("bench", Path(__file__).parents[1] / "tools/fm1_bench.py")
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


class FakePort:
    def __init__(self, _):
        self.reads = 0

    def read(self, _):
        self.reads += 1
        return {2: "fm1-drums test\n", 3: "bench start 1 period_us 5804\n",
                4: "bench 0 idle halves 344 sum_us 1 max_us 1 late 0\n"}.get(self.reads, "")

    def send(self, _):
        pass


class PartialLogTest(unittest.TestCase):
    def test_timeout_keeps_received_measurements(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            with patch.object(bench, "Port", FakePort), patch.object(bench, "OUT", out), \
                    patch.object(bench.time, "time", side_effect=range(0, 10000, 10)), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    bench.device_run("test")
            log = out / "device.log"
            self.assertIn("bench 0 idle", log.read_text())
            self.assertIn(str(log), str(error.exception))
