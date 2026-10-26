/* SPDX-License-Identifier: GPL-3.0-only
 * Machine reference: MONO2K by Inigo Hernandez (GPL-3.0-only),
 * https://github.com/InigoHernandez/mono2k
 * cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
 * -Wno-unused-function -I../mono2k/firmware/src -I../mono2k/firmware/gen
 * tests/machines_port_test.c -o /tmp/machines_port_test
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "m2k_core.c"
#include "../firmware/src/mm_machines.h"

_Static_assert(M_COUNT == MM2_COUNT, "source machine count changed");
static uint32_t test_rng = 937u;
static uint32_t test_random(void)
{
    test_rng = test_rng * 1664525u + 1013904223u;
    return test_rng;
}

static unsigned long sample_checks;

static void compare_machine(int machine, int mode)
{
    mm2_machine_t port;
    int16_t line[MM2_DELAY_CELLS], out[16], side[16], input[16], carrier[16];
    uint8_t params[8]; int32_t deltas[8]={0};
    const unsigned track = 2;
    int audible = 0;
    m2k_init(1);
    memset(&trk[track], 0, sizeof trk[track]);
    memset(ens_mem[track], 0, sizeof ens_mem[track]);
    trk[track].lfsr = 0x1234567u + track * 7919u;
    trk[track].lfsr2 = 0x9E3779B9u + track * 104729u;
    mm2_machine_init(&port, track, line);
    memcpy(params, MACH_DEF[machine], sizeof params);
    if (mode == 1) memset(params, 0, sizeof params);
    if (mode == 2) memset(params, 127, sizeof params);
    if (mode >= 3)
        for (int k = 0; k < 8; k++) params[k] = (uint8_t)(test_random() >> 25);
    memcpy(trk[track].eff, params, sizeof params);
    if(mode>=3)for(unsigned k=0;k<8;k++)deltas[k]=(int32_t)(test_random()%2049)-1024;
    memcpy(trk[track].mod,deltas,sizeof deltas);port.mod_q8=deltas;
    mach_trig(&trk[track], machine);
    mm2_machine_trigger(&port, machine, params);
    for (int block = 0; block < 1024; block++) {
        int32_t pitch = (mode == 1 ? 24 : mode == 2 ? 96 : 60) * 64 + (block / 128) * 2;
        int32_t previous = (36 + block / 64) * 64;
        uint8_t wavebank = block < 512 ? 0 : 127;
        trk[track - 1].pitch = previous;
        trk[track].eff[P_WBNK] = wavebank;
        if (block && !(block % 257)) {
            mach_trig(&trk[track], machine);
            mm2_machine_trigger(&port, machine, params);
        }
        for (int i = 0; i < 16; i++) {
            uint32_t phase = (uint32_t)(block * 16 + i) * 31000000u;
            input[i] = (int16_t)(sinp(phase) / 2);
            carrier[i] = (int16_t)(sinp(phase * 3u) / 3);
            tout[track - 1][i] = tvol[track - 1][i] = input[i];
            tout[track - 2][i] = carrier[i];
            eside[track][i] = bside[track][i] = 0;
        }
        mach_block(track, machine, pitch);
        assert(mm2_machine_render(&port, machine, params, pitch, previous, wavebank,
                                  out, side, input, 0, carrier));
        for (int i = 0; i < 16; i++) {
            int16_t expected_side = machine == M_FXC ? bside[track][i] : eside[track][i];
            if (out[i] != blk[track][i] || side[i] != expected_side) {
                fprintf(stderr, "machine %d mode %d block %d sample %d: %d/%d != %d/%d\n",
                        machine, mode, block, i, out[i], side[i], blk[track][i], expected_side);
                assert(0 && "machine output differs from source");
            }
            assert(out[i] >= -32767 && side[i] >= -32767);
            audible |= out[i] != 0 || side[i] != 0;
            sample_checks++;
        }
    }
    if (!mode) assert(audible);
}

static void check_restart(void)
{
    mm2_machine_t s;
    int16_t line[1024], out[16], side[16];
    uint8_t params[8]; int32_t deltas[8]={0};
    const int machines[] = {MM2_SID, MM2_PUL};
    for (unsigned m = 0; m < sizeof machines / sizeof *machines; m++) {
        int machine = machines[m], rate = machine == MM2_SID ? 1 : 5;
        int reset = machine == MM2_SID ? 2 : 6;
        memcpy(params, MACH_DEF[machine], sizeof params);
        params[rate] = 73;
        params[reset] = 0;
        mm2_machine_init(&s, 0, line);
        mm2_machine_trigger(&s, machine, params);
        assert(mm2_machine_render(&s, machine, params, 60 * 64, 48 * 64, 0, out, side, 0, 0, 0));
        int32_t phase = s.ms[4];
        assert(phase != 0);
        mm2_machine_trigger(&s, machine, params);
        assert(s.ms[4] == phase);
        params[reset] = 127;
        mm2_machine_trigger(&s, machine, params);
        assert(s.ms[4] == 0);
    }
}

static void check_boundaries(void)
{
    mm2_machine_t s;
    int16_t out[16], side[16];
    uint8_t params[8] = {255,255,255,255,255,255,255,255};
    mm2_machine_init(&s, 0, 0);
    mm2_machine_trigger(&s, MM2_GSIN, params);
    for (int i = 0; i < 8; i++) assert(s.eff[i] == 127);
    assert(!mm2_machine_render(&s, MM2_COUNT, params, 60 * 64, 60 * 64, 0, out, side, 0, 0, 0));
    assert(!mm2_machine_render(&s, MM2_ENS, params, 60 * 64, 60 * 64, 0, out, side, 0, 0, 0));
    for (int i = 0; i < 16; i++) assert(!out[i] && !side[i]);
    assert(mm2_machine_side_after_pan(MM2_FXC));
    assert(!mm2_machine_side_after_pan(MM2_ENS));
    assert(mm2_machine_input_shift(MM2_GNOI) == 2);
    assert(mm2_machine_input_shift(MM2_FXR) == 1);
}

int main(void)
{
    for (int machine = 0; machine < M_COUNT; machine++)
        for (int mode = 0; mode < 19; mode++) compare_machine(machine, mode);
    check_restart();
    check_boundaries();
    printf("machines_port_test: %lu sample pairs identical across 19 machines; restarts/bounds passed; state %zu B, delay %zu B\n",
           sample_checks, sizeof(mm2_machine_t), (size_t)MM2_DELAY_CELLS * sizeof(int16_t));
    return 0;
}
