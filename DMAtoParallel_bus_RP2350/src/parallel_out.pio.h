#pragma once

#if !PICO_NO_HARDWARE
#include "hardware/pio.h"
#endif

// ------------ //
// parallel_out //
// ------------ //

#define parallel_out_wrap_target 0
#define parallel_out_wrap 13
#define parallel_out_pio_version 0

static const uint16_t parallel_out_program_instructions[] = {
            //     .wrap_target
#if 1
    0x80a0, //  0: pull   block

    0xe100, //  2: set    pins, 0                [1]
    0x6008, //  1: out    pins, 8
    0xe101, //  3: set    pins, 1                [1]
    0xe100, //  2: set    pins, 0                [1]
    0x6008, //  1: out    pins, 8
    0xe101, //  3: set    pins, 1                [1]
    0xe100, //  2: set    pins, 0                [1]
    0x6008, //  1: out    pins, 8
    0xe101, //  3: set    pins, 1                [1]
    0xe100, //  2: set    pins, 0                [1]
    0x6008, //  1: out    pins, 8
    0xe101, //  3: set    pins, 1                [1]
#else
            //     .wrap_target
    0x80a0, //  0: pull   block
    0xe044, //  1: set    y, 4
    0xe100, //  2: set    pins, 0                [1]
    0xa127, //  3: mov    x, osr                 [1]
    0xa101, //  4: mov    pins, x                [1]
    0xe101, //  5: set    pins, 1                [1]
    0xe100, //  6: set    pins, 0                [1]
    0x6008, //  7: out    pins, 8
    0xe101, //  8: set    pins, 1                [1]
    0x0082, //  9: jmp    y--, 2
            //     .wrap
#endif
};

#if !PICO_NO_HARDWARE
static const struct pio_program parallel_out_program = {
    .instructions = parallel_out_program_instructions,
    .length = 13,
    .origin = -1,
    .pio_version = parallel_out_pio_version,
#if PICO_PIO_VERSION > 0
    .used_gpio_ranges = 0x0
#endif
};

static inline pio_sm_config parallel_out_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + parallel_out_wrap_target, offset + parallel_out_wrap);
    return c;
}
#endif
