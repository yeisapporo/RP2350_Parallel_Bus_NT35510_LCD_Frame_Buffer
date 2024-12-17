#pragma once

#if !PICO_NO_HARDWARE
#include "hardware/pio.h"
#endif

// ------------ //
// parallel_out //
// ------------ //

#define parallel_out_wrap_target 0
#define parallel_out_wrap 1
#define parallel_out_pio_version 0

static const uint16_t parallel_out_program_instructions[] = {
            //     .wrap_target
    0x80a0, //  0: pull   block
    0x6008, //  1: out    pins, 8
    0xe02b, //  1: set    pins, 0 [1] ; GP11 を LOW に (立ち下げ)
    0xe12b, //  3: set    pins, 1 [1] ; GP11 を HIGH に (立ち上げ)
            //     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program parallel_out_program = {
    .instructions = parallel_out_program_instructions,
    .length = 4,
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
