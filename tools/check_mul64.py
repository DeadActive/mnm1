#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Ported from MONO2K by Inigo Hernandez (GPL-3.0-only),
# https://github.com/InigoHernandez/mono2k (tools/fm1/build.py, mul64_check).
"""Reject wide multiply/load scheduling hazards in final pi32v2 objdump output.

    python3 tools/check_mul64.py build/felucca.dis
    python3 tools/check_mul64.py build/felucca.dis --baseline reviewed-baseline.json

A load within two preceding instructions, or a load/control transfer before the
product is consumed in the next four instructions, is hazardous on the target.
Use the linked application's complete `objdump -d` output, not source/assembly or
an object before optimization/linking. This is a scheduling heuristic, not a CPU
emulator: passing it does not replace hardware tests. No functions are exempt.
An explicit schema-1 baseline contains a fingerprints list plus caller-maintained
provenance. Matching occurrences remain reported as unresolved inherited hazards;
only their exact multiset quota is accepted. The default remains strict.
"""
import argparse
from collections import Counter
from functools import lru_cache
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
import re
import sys

INSTRUCTION = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s+)+(.+?)\s*$", re.I)
BARE_LABEL = re.compile(r"^\s*([A-Za-z_.$][\w.$]*):\s*$")
GNU_LABEL = re.compile(r"^\s*[0-9a-f]+\s+<([^>]+)>:\s*$", re.I)
WIDE_MULTIPLY = re.compile(r"^(r\d+)_(r\d+)\s*[+-]?=\s*r\d+\s*\*\s*r\d+\b", re.I)
LOAD = re.compile(r"=\s*[hbd]?\[", re.I)
LEAVE = re.compile(r"\b(?:goto|call|rts|rti|tbb|tbh)\b", re.I)
ASSIGN = re.compile(r"^\s*(?:r\d+(?:_r\d+)?|\{[^{}]*\})\s*=\s*(.*)$", re.I)
REGISTER = re.compile(r"(?<![A-Za-z0-9])r\d+(?![A-Za-z0-9])", re.I)


@dataclass(frozen=True)
class Instruction:
    address: int
    text: str
    line: int
    function: str


@dataclass(frozen=True)
class Hazard:
    multiply: Instruction
    cause: Instruction
    reason: str

    def describe(self):
        a, b = self.multiply, self.cause
        return (f"{a.function} @ 0x{a.address:08x} (line {a.line}): {self.reason}; "
                f"0x{b.address:08x}: {b.text}; multiply: {a.text}")


def parse(disassembly):
    """Keep local labels in the surrounding function; retain unlabeled code too."""
    functions, current, name = [], [], "<unlabelled>"
    for number, raw in enumerate(disassembly.splitlines(), 1):
        label = GNU_LABEL.match(raw) or BARE_LABEL.match(raw)
        if label:
            found = label.group(1)
            if found.startswith(".") or re.search(r"\+0x[0-9a-f]+$", found, re.I):
                continue
            if current:
                functions.append(current)
                current = []
            name = found
            continue
        match = INSTRUCTION.match(raw)
        if match:
            # Symbol annotations can mention register-like names; they are not operands.
            asm = re.sub(r"<[^>]*>", "", match.group(2)).split("#", 1)[0].strip()
            current.append(Instruction(int(match.group(1), 16), asm, number, name))
    if current:
        functions.append(current)
    if not functions:
        raise ValueError("no pi32v2 instructions parsed; supply complete objdump -d output")
    return functions


def reads(instruction):
    # A load INTO a product register does not consume its old value. Compound
    # assignments do read their destination; stores read both address and source.
    match = ASSIGN.match(instruction)
    return {r.lower() for r in REGISTER.findall(match.group(1) if match else instruction)}


def scan(disassembly):
    """Return (wide multiply count, hazards), scanning every parsed function."""
    count, hazards = 0, []
    for body in parse(disassembly):
        for i, ins in enumerate(body):
            match = WIDE_MULTIPLY.match(ins.text)
            if not match:
                continue
            count += 1
            product = {match.group(1).lower(), match.group(2).lower()}
            preceding = next((b for b in body[max(0, i - 2):i]
                              if LOAD.search(b.text) and not re.search(r"\bcall\b", b.text)), None)
            if preceding:
                hazards.append(Hazard(ins, preceding, "load within two instructions before wide multiply"))
                continue
            consumed = False
            for b in body[i + 1:i + 5]:
                if reads(b.text) & product:
                    consumed = True
                    break
                if LOAD.search(b.text) or LEAVE.search(b.text):
                    hazards.append(Hazard(ins, b, "load/control transfer before product consumption"))
                    consumed = True
                    break
            if not consumed and i + 5 > len(body):
                hazards.append(Hazard(ins, body[-1], "function ends before product consumption"))
    return count, hazards



def _canonical_asm(asm):
    """Normalize layout relocations only, never registers or numeric DSP values.

    Only symbol-resolved RAM/XIP address assignments and symbol-resolved direct
    control transfers qualify. Unresolved numbers (even address-shaped numbers)
    remain exact. Data-symbol offsets remain exact; local branch offsets do not.
    """
    asm = asm.split("#", 1)[0].strip()
    def address(match):
        literal, symbol, target = match.groups()
        value = int(literal, 16 if literal.lower().startswith("0x") else 10)
        target = int(target, 16)
        if value == target and (0x01C00000 <= target < 0x01C80000 or
                                0x02000000 <= target < 0x02100000):
            return "= @" + symbol.strip()
        return "= " + literal
    asm = re.sub(r"=\s*(-?(?:0x[0-9a-f]+|\d+))\s*<([^:>]+)\s*:\s*([0-9a-f]+)\s*>",
                 address, asm, flags=re.I)
    def branch(match):
        symbol = re.sub(r"\+0x[0-9a-f]+$", "", match.group(2).strip(), flags=re.I)
        return match.group(1).lower() + " @" + symbol
    asm = re.sub(r"\b(goto|call)\s+-?\d+\s*<([^:>]+)\s*:\s*[0-9a-f]+\s*>",
                 branch, asm, flags=re.I)
    asm = re.sub(r"<[^>]*>", "", asm)
    return re.sub(r"\s+", " ", asm).strip()


@lru_cache(maxsize=2)
def _window_index(disassembly):
    # Cache the two builds normally compared. Fingerprinting hundreds of hazards
    # must not reparse a multi-megabyte listing for each individual occurrence.
    raw = {}
    for line in disassembly.splitlines():
        match = INSTRUCTION.match(line)
        if match:
            raw[int(match.group(1), 16)] = match.group(2)
    windows = {}
    for body in parse(disassembly):
        for i, ins in enumerate(body):
            if WIDE_MULTIPLY.match(ins.text):
                window = ["<function-start>"] * max(0, 2-i)
                window += [_canonical_asm(raw[x.address]) for x in body[max(0,i-2):i+5]]
                window += ["<function-end>"] * (7-len(window))
                windows[(ins.address, ins.function, ins.line)] = tuple(window)
    return windows


def canonical_window(disassembly, hazard):
    """Seven instructions: two before, multiply, four after; padded at boundaries."""
    key = (hazard.multiply.address, hazard.multiply.function, hazard.multiply.line)
    try:
        return list(_window_index(disassembly)[key])
    except KeyError as error:
        raise ValueError("hazard does not belong to this disassembly") from error


def fingerprint(disassembly, hazard):
    """Stable SHA256 of function, hazard reason and conservative asm window."""
    record = [hazard.multiply.function, hazard.reason, canonical_window(disassembly, hazard)]
    encoded = json.dumps(record, ensure_ascii=True, separators=(",", ":")).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def classify(disassembly, hazards, fingerprints):
    """Return (inherited, new), consuming baseline multiplicities, not a set.

    A removed occurrence cannot authorize two copies of the same hazardous
    window. Changed registers/constants/operations always require new review.
    """
    if not isinstance(fingerprints, list) or any(not isinstance(x, str) or
            not re.fullmatch(r"[0-9a-f]{64}", x) for x in fingerprints):
        raise ValueError("baseline fingerprints must be a list of SHA256 hex strings")
    remaining = Counter(fingerprints)
    inherited, new = [], []
    for hazard in hazards:
        key = fingerprint(disassembly, hazard)
        if remaining[key]:
            remaining[key] -= 1
            inherited.append(hazard)
        else:
            new.append(hazard)
    return inherited, new


def load_baseline(path):
    manifest = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or type(manifest.get("schema")) is not int or manifest["schema"] != 1:
        raise ValueError("baseline must be a schema 1 manifest")
    values = manifest.get("fingerprints")
    classify("", [], values)  # validate the list even when the current build is clean
    return values

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("disassembly", type=Path)
    parser.add_argument("--baseline", type=Path, help="explicit frozen hazard manifest; fail only unmatched hazards")
    args = parser.parse_args(argv)
    try:
        disassembly = args.disassembly.read_text(encoding="utf-8")
        count, hazards = scan(disassembly)
        inherited, new = ([], hazards)
        if args.baseline:
            inherited, new = classify(disassembly, hazards, load_baseline(args.baseline))
    except (OSError, UnicodeError, ValueError) as error:
        print(f"mul64 check: {error}", file=sys.stderr)
        return 2
    for hazard in new:
        print(f"{args.disassembly}:{hazard.multiply.line}: {hazard.describe()}", file=sys.stderr)
    if args.baseline:
        print(f"mul64 check: {count} wide multiplies, {len(inherited)} inherited hazards, "
              f"{len(new)} new hazards (baseline: {args.baseline}); inherited hazards remain unresolved")
    else:
        print(f"mul64 check: {count} wide multiplies, {len(hazards)} hazards (strict)")
    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main())
