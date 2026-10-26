# SPDX-License-Identifier: GPL-3.0-only
"""Verify the narrowly permitted CPU1 RAM dispatch in linked pi32v2 output.

Protocol rationale follows MONO2K by Inigo Hernandez (GPL-3.0-only): an idle
worker polls in internal RAM; its sole indirect call runs a published job while
XIP is available. Completion is published after return. This checks placement
and call shape, not synchronization correctness or the callback's safety.
"""
import re

from check_mul64 import INSTRUCTION, BARE_LABEL, GNU_LABEL

WORKER = "audio_worker_loop"
MAIN = "fm1_core1_main"
ENTRY = "fm1_core1_start"
XIP_LO, XIP_HI = 0x02000000, 0x02100000
IF_BLOCK = re.compile(r"if\s*\(r\d+\s*(?:==|!=|<=|>=|<|>)\s*r\d+\)\s*\{", re.I)


def symbols(text):
    result, duplicates = {}, set()
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 2 or not re.fullmatch(r"[0-9a-fA-F]+", fields[0]):
            continue
        name = fields[-1]
        if not re.fullmatch(r"[A-Za-z_.$][\w.$]*", name):
            continue
        size = int(fields[-2], 16) if len(fields) >= 4 and re.fullmatch(r"[0-9a-fA-F]+", fields[-2]) else 0
        if name in result:
            duplicates.add(name)
        result[name] = (int(fields[0], 16), size, "O" in fields[1:-2])
    return result, duplicates


def xip_literal(asm):
    annotations = re.findall(r"<[^>]*:\s*([0-9a-fA-F]+)\s*>", asm)
    values = [int(value, 16) for value in annotations]
    plain = re.sub(r"<[^>]*>", "", asm)
    for token in re.findall(r"(?<![\w.])(?:0x[0-9a-fA-F]+|[0-9]+)(?![\w.])", plain):
        values.append(int(token, 16 if token.lower().startswith("0x") else 10))
    return any(XIP_LO <= value < XIP_HI for value in values)


def direct_target(asm, table):
    annotation = re.findall(r"<([^:>]+):\s*([0-9a-fA-F]+)\s*>", asm)
    if annotation:
        name, address = annotation[-1]
        return int(address, 16)
    match = re.search(r"\b(?:call|goto)\s+([^\s;]+)", asm, re.I)
    if not match:
        return None
    target = match[1]
    if target in table:
        return table[target][0]
    # Bare decimal branch operands are relative offsets in pi32v2 objdump.
    if re.fullmatch(r"0x[0-9a-fA-F]+", target):
        return int(target, 16)
    return None


def known_instruction(asm):
    plain = re.sub(r"<[^>]*>", "", asm).split("#", 1)[0].strip()
    if re.fullmatch(r"(?:csync|ssync|nop|idle|rts|rti|rte)", plain, re.I):
        return True
    if re.match(r"^(?:call|goto)\s+", plain, re.I):
        return True
    if re.match(r"^if\b", plain, re.I):
        return bool(IF_BLOCK.fullmatch(plain) or re.search(r"\bgoto\b", plain, re.I))
    return bool(re.match(r"^(?:r\d+(?:_r\d+)?|sp|usp|ssp|psr|icfg|reti|rets|\{[^}]+\}|[bhd]?\[[^]]+\])\s*(?:[+*/&|^%<>-]*=)", plain, re.I))


def check(rt_text, syms):
    """Return (errors, notes). Accept objdump -d .ram_text and objdump -t text."""
    errors, notes = [], []
    table, duplicates = symbols(syms)
    instruction_count = sum(bool(INSTRUCTION.match(line)) for line in rt_text.splitlines())
    if ENTRY not in table:
        calls = [line.strip() for line in rt_text.splitlines() if re.search(r"\bcall\b", line)]
        if calls:
            errors.append(f".ram_text contains calls without CPU1 entry: {calls[:3]}")
        else:
            notes.append(f".ram_text: {instruction_count} insns, no calls")
        if WORKER in table or MAIN in table:
            errors.append("worker symbols exist without fm1_core1_start")
        return errors, notes

    required = (ENTRY, WORKER, MAIN, "_rt_start", "_rt_end", "_cpu1_stacks_lo", "_cpu1_stacks_hi")
    for name in required:
        if name not in table or name in duplicates:
            errors.append(f"missing or duplicate worker symbol: {name}")
    if errors:
        return errors, notes
    address = lambda name: table[name][0]
    lo, hi = address("_rt_start"), address("_rt_end")
    if not 0x01C00000 <= lo < hi <= 0x01C06000:
        errors.append("invalid .ram_text bounds")
    if not XIP_LO <= address(ENTRY) < XIP_HI:
        errors.append("fm1_core1_start is not in XIP")
    for name in (WORKER, MAIN):
        if not lo <= address(name) < hi or address(name) + table[name][1] > hi:
            errors.append(f"{name} is not within .ram_text")
    if address(WORKER) == address(MAIN):
        errors.append("worker/main symbols overlap")
    stack_lo, stack_hi = address("_cpu1_stacks_lo"), address("_cpu1_stacks_hi")
    if not (0x01C08000 <= stack_lo < stack_hi <= 0x01C20000 and
            stack_hi - stack_lo == 8192 and stack_lo % 32 == stack_hi % 32 == 0):
        errors.append("CPU1 stacks must be aligned 8192 bytes within data RAM")
    for name, expected in (("_cpu1_ustack_top", stack_lo + 4096), ("_cpu1_sstack_top", stack_hi)):
        if name in table and address(name) != expected:
            errors.append(f"incorrect {name}")
    for begin, end in (("_rt_start", "_rt_end"), ("_data_start", "_data_end"),
                       ("_pool_start", "_pool_end"), ("_ustack_lo", "_ustack_top"), ("_sstack_lo", "_sstack_top")):
        if begin in table and end in table and stack_lo < address(end) and address(begin) < stack_hi:
            errors.append(f"CPU1 stacks overlap {begin}..{end}")
    for name, (start, size, is_object) in table.items():
        if is_object and size and stack_lo < start + size and start < stack_hi:
            if name != "cpu1_stacks" or start != stack_lo or size != 8192:
                errors.append(f"CPU1 stacks overlap object {name}")

    bodies = {WORKER: [], MAIN: []}
    current = None
    previous_end = {}
    seen_addresses = set()
    dispatch = main_jumps = 0
    brace_depth = 0
    main_traps = []
    for number, line in enumerate(rt_text.splitlines(), 1):
        label = GNU_LABEL.match(line) or BARE_LABEL.match(line)
        if label:
            name = label[1]
            if name.startswith(".") or re.search(r"\+0x[0-9a-f]+$", name, re.I):
                continue
            if current in bodies and brace_depth:
                errors.append(f"unclosed conditional block in {current}")
                brace_depth = 0
            if current in bodies and name not in table:
                errors.append(f"unknown label inside {current}: {name}")
            current = name
            continue
        ins = INSTRUCTION.match(line)
        if not ins:
            if current in bodies and line.strip() == "}" and brace_depth:
                brace_depth -= 1
                continue
            if current in bodies and line.strip():
                errors.append(f"unparsed {current} body at line {number}: {line.strip()}")
            elif re.search(r"\bcall\b", line):
                errors.append(f"unparsed RAM call at line {number}")
            continue
        pc, asm = int(ins[1], 16), ins[2]
        byte_count = len(line[ins.end(1) + 1:ins.start(2)].split())
        if pc in seen_addresses:
            errors.append(f"duplicate RAM instruction address 0x{pc:x}")
        seen_addresses.add(pc)
        if not lo <= pc < hi or pc + byte_count > hi:
            errors.append(f"RAM instruction outside bounds at 0x{pc:x}")
        if current not in bodies:
            if re.search(r"\bcall\b", asm):
                errors.append(f"call in non-worker RAM function {current}: {asm}")
            continue
        if current in previous_end and pc != previous_end[current]:
            errors.append(f"gap/overlap in parsed {current} instructions at 0x{pc:x}")
        previous_end[current] = pc + byte_count
        bodies[current].append((pc, asm))
        if not known_instruction(asm):
            errors.append(f"unknown {current} instruction at 0x{pc:x}: {asm}")
        if xip_literal(asm):
            errors.append(f"embedded XIP address in {current} at 0x{pc:x}")
        plain = re.sub(r"<[^>]*>", "", asm).strip()
        if IF_BLOCK.fullmatch(plain):
            brace_depth += 1
        if re.search(r"\b(?:rts|rti|rte)\b", plain):
            errors.append(f"unexpected return from {current}")
        transfer = re.search(r"\b(call|goto)\b", plain)
        if not transfer:
            continue
        if current == WORKER and re.fullmatch(r"call\s+r\d+", plain, re.I):
            dispatch += 1
        elif current == MAIN and not re.search(r"\b(?:call|goto)\s+r\d+\b", plain, re.I) and direct_target(asm, table) == address(WORKER):
            main_jumps += 1
        elif current == MAIN and re.match(r"^goto\s", plain) and direct_target(asm, table) == pc and main_jumps == 1:
            # Compiler's noreturn fallback after the direct worker call.
            main_traps.append(pc)
        elif current == WORKER and transfer[1] == "goto":
            target = direct_target(asm, table)
            if target is None or not lo <= target < hi:
                errors.append(f"unresolved/outside worker branch at 0x{pc:x}: {asm}")
        else:
            errors.append(f"unexpected {current} call/branch at 0x{pc:x}: {asm}")
    if brace_depth:
        errors.append("unclosed worker conditional block")
    if len(main_traps) > 1 or (main_traps and bodies[MAIN][-1][0] != main_traps[0]):
        errors.append("core1 noreturn self-loop must be the sole final fallback")
    for name, body in bodies.items():
        if not body or body[0][0] != address(name):
            errors.append(f"missing or misplaced parsed body: {name}")
        if any(b[0] <= a[0] for a, b in zip(body, body[1:])):
            errors.append(f"nonascending/duplicate instructions in {name}")
    # A worker loop branch must remain in its own body, not another RAM routine.
    worker_addresses = {pc for pc, _ in bodies[WORKER]}
    for pc, asm in bodies[WORKER]:
        if re.search(r"\bgoto\b", asm) and direct_target(asm, table) not in worker_addresses:
            errors.append(f"worker branch leaves parsed worker body at 0x{pc:x}")
    if dispatch != 1:
        errors.append(f"audio_worker_loop needs exactly one indirect dispatch call, found {dispatch}")
    if main_jumps != 1:
        errors.append(f"fm1_core1_main needs exactly one direct worker transfer, found {main_jumps}")
    if not errors:
        notes.append(f".ram_text: {instruction_count} insns; CPU1 RAM loop has one indirect job dispatch, entry/8192-byte stacks verified")
    return errors, notes
