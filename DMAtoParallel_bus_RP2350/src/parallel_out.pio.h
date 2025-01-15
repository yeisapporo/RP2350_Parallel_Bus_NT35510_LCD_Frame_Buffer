#pragma once

#if !PICO_NO_HARDWARE
#include "hardware/pio.h"
#endif

// ------------ //
// parallel_out //
// ------------ //

#define parallel_out_wrap_target 0
#if defined(PIMORONI_PICO_PLUS_2)
#define parallel_out_wrap 4 //19
#else
#define parallel_out_wrap 14
#endif
#define parallel_out_pio_version 0

static const uint16_t parallel_out_program_instructions[] = {
#if defined(PIMORONI_PICO_PLUS_2)
            //     .wrap_target
#if 0
    0x80a0, //  0: pull   block
    0x6008, //  3: out    pins, 8
    0xe000, //  4: set    pins, 0
    0xe001, //  5: set    pins, 1
    0x6008, //  3: out    pins, 8
    0xe000, //  4: set    pins, 0
    0xe001, //  5: set    pins, 1
#else
    // optimized using side-set. sm_config_set_sideset() is necessary.
    0x80a0, //  0: pull   block           side 0     
    0x6008, //  1: out    pins, 8         side 0     
    0xb142, //  2: nop                    side 1 [1] 
    0x6008, //  3: out    pins, 8         side 0     
    0xb142, //  4: nop                    side 1 [1] 
#endif
            //     .wrap
#else
            //     .wrap_target
    0x80e0, //  0: pull   ifempty block
    0xa027, //  1: mov    x, osr
    0x6008, //  2: out    pins, 8
    0xe000, //  3: set    pins, 0                [1]
    0xe001, //  4: set    pins, 1                [1]
    0x6008, //  5: out    pins, 8
    0xe000, //  6: set    pins, 0                [1]
    0xe001, //  7: set    pins, 1                [1]

    0xa0e1, //  8: mov    osr, x
    0x6008, //  9: out    pins, 8
    0xe000, // 10: set    pins, 0                [1]
    0xe001, // 11: set    pins, 1                [1]
    0x6008, // 12: out    pins, 8
    0xe000, // 13: set    pins, 0                [1]
    0xe001, // 14: set    pins, 1                [1]
            //     .wrap
#endif

};

#if !PICO_NO_HARDWARE
static const struct pio_program parallel_out_program = {
    .instructions = parallel_out_program_instructions,
#if defined(PIMORONI_PICO_PLUS_2)
    .length = 5, //20,
#else
    .length = 15,
#endif
    .origin = -1,
    .pio_version = parallel_out_pio_version,
#if PICO_PIO_VERSION > 0
    .used_gpio_ranges = 0x0
#endif
};

static inline pio_sm_config parallel_out_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + parallel_out_wrap_target, offset + parallel_out_wrap);
    // optimized using side-set.
    sm_config_set_sideset(&c, 1, false, false);
    return c;
}
#endif
