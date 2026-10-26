/* Differential regression for the original CRT memmove, including the short
 * backward-copy jump table reached by the legacy Settings > About screen.
 * The wrapper test supplies the verified original table bytes privately. */
#include "engine_cpu.h"
#include "engine_flags.h"
#include <assert.h>
#include <stdio.h>

uint8_t *engine_flat_base;
static void engine_dispatch(EngineCPU *cpu, uint32_t address) {
    fprintf(stderr, "FAIL: memmove dispatched to missing block %08X (pc %08X)\n",
            address, cpu->pc);
    exit(3);
}
#include "engine_registers.h"
#include "sub_006236F0.c"

static unsigned checked;
static void compare_move(unsigned source, unsigned destination, unsigned length) {
    enum { DATA = 0x100000, SIZE = 1024, STACK = 0x200000 };
    uint8_t expected[SIZE];
    for (unsigned i = 0; i < SIZE; ++i) expected[i] = (uint8_t)(i * 37 + i / 7);
    memcpy(engine_flat_base + DATA, expected, SIZE);
    memmove(expected + destination, expected + source, length);
    EngineCPU state = {0};
    state.pc = 0x6236f0;
    state.gpr[4] = STACK;
    state.gpr[3] = 0x11112222;
    state.gpr[5] = 0x33334444;
    state.gpr[6] = 0x55556666;
    state.gpr[7] = 0x77778888;
    engine_write_u32(&state, STACK, 0x12345678);
    engine_write_u32(&state, STACK + 4, DATA + destination);
    engine_write_u32(&state, STACK + 8, DATA + source);
    engine_write_u32(&state, STACK + 12, length);
    sub_006236F0(&state);
    assert(!memcmp(engine_flat_base + DATA, expected, SIZE));
    assert(state.gpr[0] == DATA + destination);
    assert(state.gpr[4] == STACK + 4 && state.pc == 0x12345678);
    assert(state.gpr[3] == 0x11112222 && state.gpr[5] == 0x33334444);
    assert(state.gpr[6] == 0x55556666 && state.gpr[7] == 0x77778888);
    assert(!(state.flags & 0x400)); /* The CRT restores the direction flag. */
    checked++;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    engine_flat_base = calloc(1, 0x624000);
    assert(engine_flat_base);
    FILE *file = fopen(argv[1], "rb");
    assert(file && fread(engine_flat_base + 0x6236f0, 1, 0x340, file) == 0x340);
    assert(fgetc(file) == EOF);
    fclose(file);
    /* Exercise the exact missing 0x623994 target (ECX = -6) first. */
    compare_move(258, 266, 26);
    compare_move(258, 266, 38);
    for (unsigned alignment = 0; alignment < 4; ++alignment)
        for (unsigned length = 0; length <= 256; ++length)
            for (int overlap = -32; overlap <= 32; ++overlap)
                compare_move(256 + alignment, 256 + alignment + overlap, length);
    for (unsigned alignment = 0; alignment < 4; ++alignment) {
        compare_move(32 + alignment, 600 + alignment, 256);
        compare_move(600 + alignment, 32 + alignment, 256);
    }
    printf("PASS: %u original memmove cases match libc, preserve the ABI, and return; includes About overlap and every short backward-copy index.\n", checked);
    free(engine_flat_base);
}
