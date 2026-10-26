#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate full sounds from the MONO2K export by Inigo Hernandez.

Default: write firmware/src/mm_sound_bank.h. --check verifies it without writing.
--self-test checks validation and deterministic source-identity UIDs. Standard library only.
UIDs 211..330 are permanently assigned to source IDs, never to JSON ordering.
"""
import argparse
import copy
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PARAMS = tuple("""SYN_A SYN_B SYN_C SYN_D SYN_E SYN_F SYN_G SYN_H
AMP_ATK AMP_HOLD AMP_DEC AMP_REL AMP_DIST AMP_VOL AMP_PAN AMP_PORT
FILT_BASE FILT_WDTH FILT_HPQ FILT_LPQ FILT_ATK FILT_DEC FILT_BOFS FILT_WOFS
FX_EQF FX_EQG FX_SRR FX_DTIM FX_DSND FX_DFB FX_DBAS FX_DWID
DSTR_BITS DSTR_FOLD DSTR_XOR DSTR_SPU TRACK_PTCH TRACK_LEVL
ARP_MODE ARP_SPEED ARP_RANGE TRACK_GATE
LFO1_TRACK LFO1_DEST LFO1_WAVE LFO1_TRIG LFO1_MULT LFO1_SPEED LFO1_PHASE LFO1_DEPTH
LFO2_TRACK LFO2_DEST LFO2_WAVE LFO2_TRIG LFO2_MULT LFO2_SPEED LFO2_PHASE LFO2_DEPTH
LFO3_TRACK LFO3_DEST LFO3_WAVE LFO3_TRIG LFO3_MULT LFO3_SPEED LFO3_PHASE LFO3_DEPTH
MOD_TYPE MOD_RATE MOD_DEPTH MOD_MIX WAVE_BANK""".split())
# Frozen donor firmware/src/m2k.h enum; this order is part of the sound format.
MACHINES = tuple("M_GSIN M_GPRC M_SID M_FMS M_FMD M_SAW M_ENS M_WAV M_VOX M_FMP M_PUL M_FXC M_FXR M_FXD M_GNOI M_DKIK M_DSNR M_DHAT M_DCYM".split())
CATEGORIES = ("KICK", "SNARE", "HATS", "PERC", "BASS", "LEAD", "PAD", "FX")
BANKS = {"current": (211, 4), "historical_rev9": (235, 16)}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def integer(value, low, high):
    return type(value) is int and low <= value <= high


def validate(data):
    require(type(data) is dict, "root must be an object")
    require(type(data.get("schema")) is int and data["schema"] == 1, "schema must be 1")
    require(data.get("source") == "MONO2K" and data.get("license") == "GPL-3.0-only", "source/license mismatch")
    require(data.get("author") == "Inigo Hernandez", "author mismatch")
    require(data.get("repository") == "https://github.com/InigoHernandez/mono2k", "repository mismatch")
    require(type(data.get("commit")) is str and re.fullmatch(r"[0-9a-f]{40}", data["commit"]), "invalid source commit")
    require(type(data.get("parameter_count")) is int and data["parameter_count"] == len(PARAMS) == 71, "expected 71 parameters")
    require(type(data.get("banks")) is list and len(data["banks"]) == 2, "expected two banks")
    rows, banks, uids, names = [], set(), set(), set()
    for bank in data["banks"]:
        require(type(bank) is dict, "bank must be an object")
        name = bank.get("bank")
        require(type(name) is str and name in BANKS and name not in banks, "unknown/duplicate bank")
        banks.add(name)
        first, patterns = BANKS[name]
        sounds = bank.get("sounds")
        require(type(sounds) is list and len(sounds) == patterns * 6, f"{name}: wrong sound count")
        require(type(bank.get("count")) is int and bank["count"] == len(sounds), f"{name}: count mismatch")
        for sound in sounds:
            require(type(sound) is dict, "sound must be an object")
            pattern, track = sound.get("pattern"), sound.get("track")
            require(integer(pattern, 1, patterns) and integer(track, 1, 6), "invalid source pattern/track")
            uid = first + (pattern - 1) * 6 + track - 1
            source_id = f"{name}:A{pattern:02d}:T{track}"
            require(sound.get("source_id") == source_id, "source ID disagrees with bank/pattern/track")
            require(uid not in uids, f"duplicate UID/source: {source_id}")
            uids.add(uid)
            require(integer(sound.get("slot"), 1, patterns*6) and sound["slot"] == uid-first+1, f"{source_id}: slot mismatch")
            label = sound.get("name")
            require(type(label) is str and re.fullmatch(r"[A-Z0-9 _-]{1,8}", label), f"{source_id}: invalid 8-character name")
            require(label not in names, f"duplicate name: {label}")
            names.add(label)
            machine = sound.get("machine_id")
            require(integer(machine, 0, 18), f"{source_id}: invalid machine index")
            require(sound.get("machine_enum") == MACHINES[machine], f"{source_id}: machine enum mismatch")
            category = sound.get("category")
            require(type(category) is str and category in CATEGORIES, f"{source_id}: unknown category")
            params = sound.get("params")
            require(type(params) is dict and set(params) == set(PARAMS), f"{source_id}: wrong parameter keys")
            require(all(integer(params[k], 0, 127) for k in PARAMS), f"{source_id}: raw parameters must be integers 0..127")
            rows.append((uid, source_id, label, machine, category, track-1, [params[k] for k in PARAMS]))
    require(uids == set(range(211,331)), "expected all 120 unique UIDs 211..330")
    return sorted(rows)


def header(data):
    rows = validate(data)
    out = ["/* SPDX-License-Identifier: GPL-3.0-only",
           " * Generated by tools/gen_mono2k_presets.py; do not edit.",
           " * Full raw sounds from MONO2K by Inigo Hernandez.",
           " * https://github.com/InigoHernandez/mono2k",
           f" * Source commit: {data['commit']}",
           " * UIDs are permanent source identities, appended after the original 210 presets.",
           " * source_track is ZERO-BASED (0..5), for remapping source-relative LFO targets.",
           " * raw uses the donor's 71-parameter order, including bipolar centre 64.",
           " */", "#ifndef MNM_SOUND_BANK_H", "#define MNM_SOUND_BANK_H", "#include <stdint.h>",
           "#define MM_SOUND_COUNT 120u", "#define MM_SOUND_PARAM_COUNT 71u",
           "enum { " + ", ".join("MM_SOUND_CAT_" + c for c in CATEGORIES) + " };",
           "enum {"]
    for i, param in enumerate(PARAMS):
        out.append(f"    MM_SOUND_{param} = {i},")
    out += ["};", "typedef struct {", "    uint16_t uid;", "    char name[9]; /* at most eight visible characters, NUL terminated */",
            "    uint8_t machine, category, source_track;", "    uint8_t raw[MM_SOUND_PARAM_COUNT];", "} mm_sound_t;",
            "static const mm_sound_t MM_SOUNDS[MM_SOUND_COUNT] = {"]
    for uid, source, name, machine, category, track, raw in rows:
        out.append(f'    /* {source}, {MACHINES[machine]} */')
        out.append(f'    {{{uid}, "{name}", {machine}, MM_SOUND_CAT_{category}, {track}, {{')
        for start in range(0,71,16):
            out.append("        " + ", ".join(map(str, raw[start:start+16])) + ",")
        out.append("    }},")
    return "\n".join(out + ["};", "#endif", ""])


def self_test(data):
    expected = header(data)
    shuffled = copy.deepcopy(data)
    shuffled["banks"].reverse()
    for bank in shuffled["banks"]:
        bank["sounds"].reverse()
        for sound in bank["sounds"]:
            sound["params"] = dict(reversed(list(sound["params"].items())))
    assert header(shuffled) == expected
    mutations = [lambda d: d.update(parameter_count=70),
                 lambda d: d["banks"][0].update(count=25),
                 lambda d: d["banks"][0]["sounds"].__setitem__(1, d["banks"][0]["sounds"][0])]
    for field, value in [("name", "TOO-LONG!X"), ("name", 3), ("machine_id", 19),
                         ("machine_id", True), ("machine_enum", "M_BAD"), ("track", 0),
                         ("category", []), ("source_id", "wrong"), ("slot", True)]:
        mutations.append(lambda d, f=field, v=value: d["banks"][0]["sounds"][0].update({f:v}))
    for value in [-1, 128, True, 1.5, "64"]:
        mutations.append(lambda d, v=value: d["banks"][0]["sounds"][0]["params"].update(SYN_A=v))
    mutations.append(lambda d: d["banks"][0]["sounds"][0]["params"].pop("SYN_A"))
    for mutation in mutations:
        bad = copy.deepcopy(data)
        mutation(bad)
        try:
            header(bad)
        except ValueError:
            continue
        raise AssertionError("invalid data accepted")
    print(f"fullsound self-test: deterministic reorder and {len(mutations)} rejected invalid inputs")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=ROOT / "tests/mono2k_sounds.json")
    parser.add_argument("--output", type=Path, default=ROOT / "firmware/src/mm_sound_bank.h")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        data = json.loads(args.input.read_text())
        result = header(data)
        if args.self_test:
            self_test(data)
        if args.check:
            if not args.output.exists() or args.output.read_text() != result:
                parser.exit(1, f"{args.output}: generated header differs\n")
        else:
            args.output.write_text(result)
    except (OSError, ValueError) as error:
        parser.exit(2, f"gen_mono2k_presets: {error}\n")


if __name__ == "__main__":
    main()
