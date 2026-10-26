# SPDX-License-Identifier: GPL-3.0-only
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_audio_worker import check


def symbol(name, address, size=0, kind="g"):
    return f"{address:08x} {kind} .ram_text {size:08x} {name}\n"


def symbols(**changes):
    values = {"_rt_start": 0x1C00000, "_rt_end": 0x1C01000,
              "fm1_core1_start": 0x2000200, "audio_worker_loop": 0x1C00100,
              "fm1_core1_main": 0x1C00200, "_cpu1_stacks_lo": 0x1C10000,
              "_cpu1_stacks_hi": 0x1C12000, "_cpu1_ustack_top": 0x1C11000,
              "_cpu1_sstack_top": 0x1C12000, "flash_erase": 0x1C00300,
              "_data_start": 0x1C08000, "_data_end": 0x1C09000,
              "_pool_start": 0x1C20000, "_pool_end": 0x1C70000}
    values.update(changes)
    return "".join(symbol(name, address) for name, address in values.items() if address is not None)


def listing(worker=None, main=None, extra=None):
    worker = worker if worker is not None else ["r0 = 0x1c08100", "csync", "call r3", "goto -12 <audio_worker_loop : 1c00100>"]
    main = main if main is not None else ["r0 = 0x1eef2d0", "r1 = [r0]", "goto -264 <audio_worker_loop : 1c00100>"]
    extra = extra if extra is not None else ["r0 = 0", "rts"]
    text = "Disassembly of section .ram_text:\n\n"
    for name, address, ops in (("audio_worker_loop", 0x1C00100, worker),
                               ("fm1_core1_main", 0x1C00200, main), ("flash_erase", 0x1C00300, extra)):
        text += name + ":\n"
        text += "".join(f" {address + 4*i:08x}:  01 02 03 04  {op}\n" for i, op in enumerate(ops))
        text += "\n"
    return text


class AudioWorkerCheckTests(unittest.TestCase):
    def rejects(self, dis=None, syms=None):
        errors, _ = check(dis or listing(), symbols() if syms is None else syms)
        self.assertTrue(errors)
        return errors

    def test_valid_worker_and_local_labels(self):
        text = listing().replace(" 01c00108:", ".Ldispatch:\n 01c00108:")
        errors, notes = check(text, symbols() + symbol("cpu1_stacks", 0x1C10000, 8192, "l O"))
        self.assertEqual(errors, [])
        self.assertTrue(notes)
        self.assertEqual(check(listing(main=["call -256 <audio_worker_loop : 1c00100>"]), symbols())[0], [])

    def test_predicated_block_and_noreturn_fallback(self):
        text = listing(worker=["if (r0 > r1) {", "[r4+24] = r0", "call r3"],
                       main=["call 0x1c00100", "goto -2 <fm1_core1_main+0x4 : 1c00204 >"])
        text = text.replace(" 01c00108:", "  }\n 01c00108:")
        self.assertEqual(check(text, symbols())[0], [])
        self.rejects(text.replace("  }\n", ""))
        self.rejects(text.replace("  }\n", "  }\n  }\n"))
        self.rejects(text.replace("if (r0 > r1)", "if (unknown)"))
        self.rejects(listing(main=["goto -2 <fm1_core1_main : 1c00200 >", "call 0x1c00100"]))
        self.rejects(listing(main=["call 0x1c00100", "goto 0x1c00204", "nop"]))

    def test_original_no_calls_policy_without_worker(self):
        text = "flash_erase:\n 01c00300: 01 02 03 04 rts\n"
        self.assertEqual(check(text, symbol("flash_erase", 0x1C00300))[0], [])
        self.rejects(text.replace("rts", "call r1"), symbol("flash_erase", 0x1C00300))
        self.rejects(syms=symbols(fm1_core1_start=None))

    def test_missing_duplicate_and_misplaced_symbols(self):
        for name in ("_rt_start", "_rt_end", "audio_worker_loop", "fm1_core1_main", "_cpu1_stacks_lo", "_cpu1_stacks_hi"):
            with self.subTest(name=name):
                self.rejects(syms=symbols(**{name: None}))
        for name, address in (("fm1_core1_start", 0x1C00000), ("audio_worker_loop", 0x2001000),
                              ("fm1_core1_main", 0x1C02000), ("_rt_end", 0x1C07000)):
            with self.subTest(name=name):
                self.rejects(syms=symbols(**{name: address}))
        self.rejects(syms=symbols() + symbol("audio_worker_loop", 0x1C00100))

    def test_dispatch_exactly_once_and_only_register_call(self):
        for ops in (["r0 = 1"], ["call r1", "call r2"], ["call 0x2000400"],
                    ["call r1", "call 0x1c00300"], ["call r1", "goto r2"], ["call r1", "rts"]):
            with self.subTest(ops=ops):
                self.rejects(listing(worker=ops))
        self.rejects(listing(extra=["call r2"]))
        self.rejects(listing(extra=["call 0x1c00100"]))

    def test_main_only_direct_worker_transfer(self):
        for ops in (["goto r2"], ["call 0x2000400"], ["call 0x1c00300"],
                    ["call r2 <audio_worker_loop : 1c00100>"], ["nop"],
                    ["call 0x1c00100", "goto 0x1c00100"]):
            with self.subTest(ops=ops):
                self.rejects(listing(main=ops))

    def test_xip_constants_and_annotated_branch_targets(self):
        for value in ("0x02001234", "33559092", "12 <table : 02001234>"):
            self.rejects(listing(worker=[f"r0 = {value}", "call r3"]))
            self.rejects(listing(main=[f"r0 = {value}", "goto 0x1c00100"]))
        self.rejects(listing(worker=["call r3", "goto -4 <outside : 02001234>"]))
        self.rejects(listing(worker=["call r3", "goto 0x1c00300"]))
        self.rejects(listing(worker=["call r3", "goto -4"]))

    def test_unknown_unparsed_missing_and_incomplete_bodies(self):
        self.rejects(listing(worker=["mystery r1", "call r3"]))
        self.rejects(listing().replace("audio_worker_loop:\n", "audio_worker_loop:\nTHIS IS NOT DISASSEMBLY\n"))
        self.rejects(listing(worker=[]))
        self.rejects(listing().replace("01c00104:", "01c00106:"))
        self.rejects(listing().replace("fm1_core1_main:\n", "unknown:\n"))
        self.rejects(listing().replace(" 01c00108:", "unlisted_label:\n 01c00108:"))
        self.rejects(listing().replace("01c00100:", "01c000fc:"))

    def test_stack_size_alignment_ranges_and_overlap(self):
        for changes in ({"_cpu1_stacks_hi": 0x1C11000}, {"_cpu1_stacks_lo": 0x1C10004},
                        {"_cpu1_stacks_lo": 0x1C07000, "_cpu1_stacks_hi": 0x1C09000},
                        {"_cpu1_stacks_lo": 0x1C1F000, "_cpu1_stacks_hi": 0x1C21000},
                        {"_data_end": 0x1C11000}, {"_cpu1_ustack_top": 0x1C12000},
                        {"_ustack_lo": 0x1C11000, "_ustack_top": 0x1C14000}):
            with self.subTest(changes=changes):
                self.rejects(syms=symbols(**changes))
        self.rejects(syms=symbols() + symbol("other_buffer", 0x1C11000, 64, "l O"))
        self.rejects(syms=symbols() + symbol("cpu1_stacks", 0x1C10000, 4096, "l O"))


if __name__ == "__main__":
    unittest.main()
