# SPDX-License-Identifier: GPL-3.0-only
"""Run: python3 -m unittest discover -s tests -p test_mul64_check.py"""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "tools/check_mul64.py"
spec = importlib.util.spec_from_file_location("check_mul64", SCRIPT)
check = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = check
spec.loader.exec_module(check)


def listing(*instructions, name="render", start=0x2000200):
    return name + ":\n" + "".join(f" {start + 4 * i:08x}:  01 02 03 04 \t{x}\n"
                                   for i, x in enumerate(instructions))


class MultiplySchedulingTests(unittest.TestCase):
    def assert_hazard(self, text, address=0x2000200):
        count, hazards = check.scan(text)
        self.assertEqual(count, 1)
        self.assertEqual(len(hazards), 1)
        self.assertEqual(hazards[0].multiply.address, address)
        return hazards[0]

    def test_shift_consumes_product_before_load(self):
        self.assertEqual(check.scan(listing("r1_r0 = r2 * r3 (s)", "r1_r0 >>>= r4",
                                           "r5 = h[r6]", "rts")), (1, []))

    def test_preceding_load_windows(self):
        for load in ("r5 = [r6]", "r5 = h[r6+2]", "r5 = b[r6]", "r5 = d[r6]"):
            with self.subTest(load=load):
                h = self.assert_hazard(listing(load, "nop", "r1_r0 = r2 * r3", "r0 >>= 4"), 0x2000208)
                self.assertEqual(h.cause.address, 0x2000200)
        self.assertEqual(check.scan(listing("r5 = h[r6]", "nop", "nop", "r1_r0 = r2 * r3",
                                           "r0 >>= 4")), (1, []))

    def test_following_load_before_use(self):
        self.assert_hazard(listing("r1_r0 = r2 * r3", "r4 += 1", "r8 = h[r9]", "r0 >>= 4"))

    def test_control_transfer_before_use(self):
        for branch in ("call 16 <loader: 2000500>", "goto 16", "rts", "rti", "tbb r8", "tbh r8"):
            with self.subTest(branch=branch):
                self.assert_hazard(listing("r1_r0 = r2 * r3", branch, "r0 >>= 4"))

    def test_write_to_result_is_not_consumption(self):
        self.assert_hazard(listing("r1_r0 = r2 * r3", "r0 = h[r4]", "rts"))
        self.assert_hazard(listing("r1_r0 = r2 * r3", "r0 = 0", "rts"))

    def test_multiload_destinations_are_not_product_consumption(self):
        for load in ("{r4, r0} = [r5+]", "{pc, r7-r0} = [sp++]", "{r1, r0} = d[r5]"):
            with self.subTest(load=load):
                self.assert_hazard(listing("r1_r0 = r2 * r3", load, "rts"))
        self.assertEqual(check.scan(listing("r1_r0 = r2 * r3", "{r5, r6} = [r0+]", "rts")), (1, []))

    def test_read_product_as_load_address_is_consumption(self):
        self.assertEqual(check.scan(listing("r1_r0 = r2 * r3", "r5 = h[r0]", "rts")), (1, []))

    def test_store_consumes_product_without_counting_as_load(self):
        self.assertEqual(check.scan(listing("h[r8] = r9", "r1_r0 = r2 * r3", "h[r8] = r0", "rts")), (1, []))

    def test_function_boundaries_and_local_labels(self):
        text = listing("r1_r0 = r2 * r3") + ".LBB0_1:\n" + listing("r8 = h[r9]", name=".GJTIS0", start=0x2000204)
        self.assert_hazard(text)
        text = listing("r1_r0 = r2 * r3") + listing("r0 >>= 4", name="next", start=0x2000204)
        self.assert_hazard(text)

    def test_compiler_runtime_not_exempt(self):
        self.assert_hazard(listing("r1_r0 = r2 * r3", "r5 = h[r6]", name="__helper"))

    def test_gnu_header_unlabeled_code_and_compound_multiply(self):
        text = "02000200 <wide>:\n 02000200: 01 02 03 04\tr3_r2 += r6 * r7\n 02000204: 01 02 03 04\tr5 = h[r8]\n"
        h = self.assert_hazard(text)
        self.assertEqual(h.multiply.function, "wide")
        self.assert_hazard(text.split("\n", 1)[1])

    def test_symbol_annotation_is_not_register_use(self):
        self.assert_hazard(listing("r1_r0 = r2 * r3", "call 16 <r0: 2000500>", "r0 >>= 4"))

    def test_32bit_multiply_ignored(self):
        self.assertEqual(check.scan(listing("r0 = r2 * r3", "r5 = h[r6]")), (0, []))

    def test_scan_matches_donor_four_instruction_window(self):
        self.assertEqual(check.scan(listing("r1_r0 = r2 * r3", "nop", "nop", "nop", "nop", "r5 = h[r6]")), (1, []))
        self.assert_hazard(listing("r1_r0 = r2 * r3", "nop", "nop", "nop"))

    def test_no_instructions_rejected(self):
        for text in ("", "garbage", "render:\n"):
            with self.assertRaises(ValueError):
                check.scan(text)

    def test_cli_status_and_actionable_locations(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.dis"
            for text, expected in ((listing("r1_r0 = r2 * r3", "r0 >>= 4"), 0),
                                   (listing("r1_r0 = r2 * r3", "r5 = h[r6]"), 1), ("garbage", 2)):
                path.write_text(text)
                result = subprocess.run([sys.executable, str(SCRIPT), str(path)], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, result.stderr)
                if expected == 1:
                    self.assertIn("render @ 0x02000200", result.stderr)
                    self.assertIn("0x02000204: r5 = h[r6]", result.stderr)


class BaselineTests(unittest.TestCase):
    def hazard(self, text):
        count, hazards = check.scan(text)
        self.assertEqual(count, 1)
        self.assertEqual(len(hazards), 1)
        return hazards[0]

    def signature(self, text):
        return check.fingerprint(text, self.hazard(text))

    def test_relocations_and_branch_layout_normalized(self):
        a = listing("r6 = 29438688 <part_buf : 1c132e0 >", "r5 = [r6]",
                    "r1_r0 = r2 * r3", "r0 >>= 16", "if (r0 > 2) goto 12 <render+0xAB : 20002ab >")
        b = listing("r6 = 29442784 <part_buf : 1c142e0 >", "r5 = [r6]",
                    "r1_r0 = r2 * r3", "r0 >>= 16", "if (r0 > 2) goto 32 <render+0xCD : 20003cd >", start=0x2000300)
        self.assertEqual(self.signature(a), self.signature(b))
        self.assertEqual(len(check.canonical_window(a, self.hazard(a))), 7)
        self.assertIn("r6 = @part_buf", check.canonical_window(a, self.hazard(a)))
        self.assertNotEqual(self.signature(a), self.signature(a.replace("part_buf :", "other_buf :")))
        self.assertNotEqual(self.signature(a), self.signature(a.replace("part_buf :", "part_buf+0x4 :")))

    def test_register_opcode_and_constant_changes_never_masked(self):
        base = listing("r5 = [r6]", "r4 = 29438688", "r1_r0 = r2 * r3", "r0 >>= 16", "r7 = 127")
        for old, new in (("r2 * r3", "r8 * r3"), ("r0 >>= 16", "r0 >>>= 16"),
                         ("r7 = 127", "r7 = 126"), ("29438688", "29442784"),
                         ("r4 = 29438688", "r4 = 42 <_data_load+0xFF : 2a >")):
            with self.subTest(old=old, new=new):
                self.assertNotEqual(self.signature(base), self.signature(base.replace(old,new)))
        self.assertNotEqual(self.signature(base), self.signature(base.replace("render:","other:")))

    def test_only_resolved_branch_targets_are_masked(self):
        base = listing("r1_r0 = r2 * r3", "call 16 <worker: 2000500>", "r0 >>= 16")
        self.assertEqual(self.signature(base), self.signature(base.replace("16 <worker: 2000500>","32 <worker: 2000800>")))
        self.assertNotEqual(self.signature(base), self.signature(base.replace("worker:","other:")))
        plain=listing("r1_r0 = r2 * r3", "call 16", "r0 >>= 16")
        self.assertNotEqual(self.signature(plain), self.signature(plain.replace("call 16","call 32")))

    def test_function_boundaries_and_hazard_reasons_distinct(self):
        a=listing("r1_r0 = r2 * r3", "r5 = [r6]")
        b=listing("nop", "r1_r0 = r2 * r3", "r5 = [r6]")
        self.assertNotEqual(self.signature(a),self.signature(b))
        from dataclasses import replace
        h=self.hazard(a)
        self.assertNotEqual(check.fingerprint(a,h),check.fingerprint(a,replace(h,reason="different cause")))

    def test_duplicate_windows_consume_baseline_quota(self):
        block=("nop", "r5 = [r6]", "r1_r0 = r2 * r3", "r0 >>= 16", "nop", "nop", "nop")
        one=listing(*block);two=listing(*block,*block)
        key=self.signature(one);_,hazards=check.scan(two)
        self.assertEqual(len(hazards),2)
        self.assertEqual([check.fingerprint(two,h) for h in hazards],[key,key])
        inherited,new=check.classify(two,hazards,[key])
        self.assertEqual((len(inherited),len(new)),(1,1))
        self.assertEqual(check.classify(two,hazards,[key,key]),(hazards,[]))
        # A baseline occurrence from another function cannot authorize this one.
        other=self.signature(one.replace("render:","other:"))
        self.assertEqual(check.classify(two,hazards,[other,other]),([],hazards))

    def test_invalid_manifest_rejected_even_with_no_hazards(self):
        for bad in (None, {}, "x", [None], ["0"], ["A"*64]):
            with self.assertRaises(ValueError):check.classify("",[],bad)

    def test_cli_baseline_is_explicit_and_reports_unresolved_debt(self):
        text=listing("r1_r0 = r2 * r3", "r5 = [r6]")
        with tempfile.TemporaryDirectory() as directory:
            dis=Path(directory)/"fw.dis";base=Path(directory)/"baseline.json"
            dis.write_text(text)
            base.write_text(json.dumps({"schema":1,"fingerprints":[self.signature(text)]}))
            command=[sys.executable,str(SCRIPT),str(dis),"--baseline",str(base)]
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn("1 inherited hazards, 0 new hazards",result.stdout)
            self.assertIn("remain unresolved",result.stdout)
            dis.write_text(text.replace("r2 * r3","r7 * r3"))
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,1,result.stderr)
            self.assertIn("0 inherited hazards, 1 new hazards",result.stdout)
            base.write_text('{"schema":true,"fingerprints":[]}')
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,2)


if __name__ == "__main__":
    unittest.main()
