#pragma once
///////////////////////////////////////////////////////////////////////////////////////////
// This programme is an 800x480 LCD (MRB3973) driver (parallel bus) controlled by NT35510.
// Do not define the following #define on boards without PSRAM.
///////////////////////////////////////////////////////////////////////////////////////////
#define PIMORONI_PICO_PLUS_2

#define TFT_D0 0       // GPIO0〜7をデータバスに使用
#define TFT_CS 18
#define TFT_RS 8
#define TFT_RD 9    // readx
#define TFT_RESET 10
#define TFT_WR 11   // writex
#define DEBUG_SIG   28  // 仮

#include <Arduino.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "parallel_out.pio.h"

#include "pngle.h"

#if defined(PIMORONI_PICO_PLUS_2)
#include "data.h"
#endif

#if defined(PIMORONI_PICO_PLUS_2)
#define SCREEN_SIZE 800*480   // 画面サイズ
#else
#define SCREEN_SIZE 400*480   // 画面サイズ(landscape) or 800*240 (portrait)
#endif
#if defined(PIMORONI_PICO_PLUS_2)
__attribute__((aligned(4))) uint16_t framebuffer[2][SCREEN_SIZE] PSRAM; // ピクセルデータを格納
__attribute__((aligned(4))) uint16_t chip_map[256 * 256] PSRAM;
#else
__attribute__((aligned(4))) uint16_t framebuffer[1][SCREEN_SIZE];
#endif
volatile __attribute__((aligned(4))) uint16_t *transferbuffer = framebuffer[0];
#if defined(PIMORONI_PICO_PLUS_2)
// to select single buffer, change [1] to [0].
volatile __attribute__((aligned(4))) uint16_t *drawingbuffer = framebuffer[0];
#else
volatile __attribute__((aligned(4))) uint16_t *drawingbuffer = framebuffer[0];
#endif
// pseudo-hardware scrolling test
uint16_t fixed_y = 300;

volatile uint dma_channel[7];
typedef volatile struct _bitblt_dma {
    bool busy = false;
    uint16_t *src;
    uint16_t src_to_next;
    uint16_t *dest;
    uint16_t dest_to_next;
    int16_t hcnt;   // horizontal
    int16_t vcnt;   // vertical
} BITBLT_DMA;
volatile BITBLT_DMA bitblt_dma, bitblt_dma2, bitblt_dma3, bitblt_dma4;
semaphore_t bitblt_sem;

class NT35510LCD {
    public:
    PIO pio = pio0;
    uint sm = 0;

    void setup_gpio(void){
        for (int i = TFT_D0; i < TFT_D0 + 8; i++) {
            gpio_init(i);
            gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_4MA);
            gpio_set_dir(i, GPIO_OUT);
        }

        int tft_pins[] = {TFT_RS, TFT_RD, TFT_WR, TFT_RESET, TFT_CS, DEBUG_SIG};
        for (int i = 0; i < sizeof(tft_pins) / sizeof(int); i++) {
            gpio_init(tft_pins[i]);
            gpio_set_drive_strength(tft_pins[i], GPIO_DRIVE_STRENGTH_4MA);
            gpio_set_dir(tft_pins[i], GPIO_OUT);
        }

        pinMode(PIN_LED, OUTPUT); 
    }

    // NT35510 command requires a word address
    void setWordAddress(uint16_t addr) {
        digitalWrite(TFT_RS, LOW);  // command mode
        sleep_us(50);
        digitalWrite(TFT_RD, HIGH);
        sleep_us(50);
        gpio_put_masked(0xff, 0xff & (addr >> 8));    // put upper byte
        digitalWrite(TFT_WR, LOW);
        sleep_us(50);
        digitalWrite(TFT_WR, HIGH);
        sleep_us(50);
        gpio_put_masked(0xff, 0xff & addr);         // put lower byte
        digitalWrite(TFT_WR, LOW);
        sleep_us(50);
        digitalWrite(TFT_WR, HIGH);
        sleep_us(50);
    }

    void setByteData(uint8_t data) {
        digitalWrite(TFT_RS, HIGH); // data mode
        sleep_us(50);
        gpio_put_masked(0xff, data);
        digitalWrite(TFT_WR, LOW);
        sleep_us(50);
        digitalWrite(TFT_WR, HIGH);
        sleep_us(50);
    }

    // for some types of frame buffer.
    void setWordData(uint16_t data) {
        digitalWrite(TFT_RS, HIGH); // data mode
        sleep_us(50);
        gpio_put_masked(0xff, 0xff & (data >> 8));
        digitalWrite(TFT_WR, LOW);
        sleep_us(50);
        digitalWrite(TFT_WR, HIGH);
        sleep_us(50);

        gpio_put_masked(0xff, 0xff & data );
        digitalWrite(TFT_WR, LOW);
        sleep_us(50);
        digitalWrite(TFT_WR, HIGH);
        sleep_us(50);
    }

    void sendCommand(uint16_t command_addr, std::initializer_list<uint8_t> data = {}) {
        if(data.size() > 0) {
            for (uint8_t d:data) {
                // NT35510 requires to increment the command address for each byte data.
                setWordAddress(command_addr++);
                setByteData(d);
            }
        } else {
            setWordAddress(command_addr);
            // enable data mode for pixel data DMA, also in the absence of data.
            digitalWrite(TFT_RS, HIGH); // data mode (expects consecutive data)
        }
    }

    void init_LCD(void) {
        digitalWrite(TFT_CS, LOW);
        digitalWrite(TFT_RS, HIGH);
        digitalWrite(TFT_WR, HIGH);
        digitalWrite(TFT_RD, HIGH);

        // hardware reset
        delay(200);
        digitalWrite(TFT_RESET, LOW);
        delay(200);
        digitalWrite(TFT_RESET, HIGH);
        delay(200);

        // send initializing commands.
        sendCommand(0x0100);
        delay(200);
        sendCommand(0x1000);    // Sleep in
        delay(200);
        sendCommand(0xf000, {0x55, 0xaa, 0x52, 0x08, 0x01});    // enable Page1
        sendCommand(0xb600, {0x34, 0x34, 0x34});
        sendCommand(0xb000, {0x0d, 0x0d, 0x0d});    // AVDD Set AVDD 5.2V
        sendCommand(0xb700, {0x34, 0x34, 0x34});    // AVEE ratio
        sendCommand(0xb100, {0x0d, 0x0d, 0x0d});    // AVEE  -5.2V
        sendCommand(0xb800, {0x24, 0x24, 0x24});    // VCL ratio
        sendCommand(0xb900, {0x34, 0x34, 0x34});    // VGH  ratio
        sendCommand(0xb300, {0x0f, 0x0f, 0x0f});
        sendCommand(0xba00, {0x24, 0x24, 0x24});    // VGLX  ratio
        sendCommand(0xb500, {0x08, 0x08});
        sendCommand(0xbc00, {0x00, 0x78, 0x00});    // VGMP/VGSP 4.5V/0V
        sendCommand(0xbd00, {0x00, 0x78, 0x00});    // VGMN/VGSN -4.5V/0V
        sendCommand(0xbe00, {0x00, 0x89});  // VCOM  -1.325V

        sendCommand(0xd100, {   // r+
            0x00, 0x2d, 0x00, 0x2e, 0x00, 0x32, 0x00, 0x44, 0x00, 0x53, 0x00, 0x88, 0x00, 0xb6, 0x00, 0xf3, 0x01, 0x22, 0x01, 0x64,
            0x01, 0x92, 0x01, 0xd4, 0x02, 0x07, 0x02, 0x08, 0x02, 0x34, 0x02, 0x5f, 0x02, 0x78, 0x02, 0x94, 0x02, 0xa6, 0x02, 0xbb,
            0x02, 0xca, 0x02, 0xdb, 0x02, 0xe8, 0x02, 0xf9, 0x03, 0x1f, 0x03, 0x7f
        });
        sendCommand(0xd400, {   // r-
            0x00, 0x2d, 0x00, 0x2e, 0x00, 0x32, 0x00, 0x44, 0x00, 0x53, 0x00, 0x88, 0x00, 0xb6, 0x00, 0xf3, 0x01, 0x22, 0x01, 0x64,
            0x01, 0x92, 0x01, 0xd4, 0x02, 0x07, 0x02, 0x08, 0x02, 0x34, 0x02, 0x5f, 0x02, 0x78, 0x02, 0x94, 0x02, 0xa6, 0x02, 0xbb,
            0x02, 0xca, 0x02, 0xdb, 0x02, 0xe8, 0x02, 0xf9, 0x03, 0x1f, 0x03, 0x7f
        });
        sendCommand(0xd200, {   // g+
            0x00, 0x2d, 0x00, 0x2e, 0x00, 0x32, 0x00, 0x44, 0x00, 0x53, 0x00, 0x88, 0x00, 0xb6, 0x00, 0xf3, 0x01, 0x22, 0x01, 0x64,
            0x01, 0x92, 0x01, 0xd4, 0x02, 0x07, 0x02, 0x08, 0x02, 0x34, 0x02, 0x5f, 0x02, 0x78, 0x02, 0x94, 0x02, 0xa6, 0x02, 0xbb,
            0x02, 0xca, 0x02, 0xdb, 0x02, 0xe8, 0x02, 0xf9, 0x03, 0x1f, 0x03, 0x7f
        });
        sendCommand(0xd500, {   // g-
            0x00, 0x2d, 0x00, 0x2e, 0x00, 0x32, 0x00, 0x44, 0x00, 0x53, 0x00, 0x88, 0x00, 0xb6, 0x00, 0xf3, 0x01, 0x22, 0x01, 0x64,
            0x01, 0x92, 0x01, 0xd4, 0x02, 0x07, 0x02, 0x08, 0x02, 0x34, 0x02, 0x5f, 0x02, 0x78, 0x02, 0x94, 0x02, 0xa6, 0x02, 0xbb,
            0x02, 0xca, 0x02, 0xdb, 0x02, 0xe8, 0x02, 0xf9, 0x03, 0x1f, 0x03, 0x7f
        });
        sendCommand(0xd300, {   // b+
            0x00, 0x2d, 0x00, 0x2e, 0x00, 0x32, 0x00, 0x44, 0x00, 0x53, 0x00, 0x88, 0x00, 0xb6, 0x00, 0xf3, 0x01, 0x22, 0x01, 0x64,
            0x01, 0x92, 0x01, 0xd4, 0x02, 0x07, 0x02, 0x08, 0x02, 0x34, 0x02, 0x5f, 0x02, 0x78, 0x02, 0x94, 0x02, 0xa6, 0x02, 0xbb,
            0x02, 0xca, 0x02, 0xdb, 0x02, 0xe8, 0x02, 0xf9, 0x03, 0x1f, 0x03, 0x7f
        });
        sendCommand(0xd600, {   // b-
            0x00, 0x2d, 0x00, 0x2e, 0x00, 0x32, 0x00, 0x44, 0x00, 0x53, 0x00, 0x88, 0x00, 0xb6, 0x00, 0xf3, 0x01, 0x22, 0x01, 0x64,
            0x01, 0x92, 0x01, 0xd4, 0x02, 0x07, 0x02, 0x08, 0x02, 0x34, 0x02, 0x5f, 0x02, 0x78, 0x02, 0x94, 0x02, 0xa6, 0x02, 0xbb,
            0x02, 0xca, 0x02, 0xdb, 0x02, 0xe8, 0x02, 0xf9, 0x03, 0x1f, 0x03, 0x7f
        });
        
        sendCommand(0xf000, {0x55,0xAA,0x52,0x08,0x00});    // Enable Page0
        sendCommand(0xb000, {0x08,0x05,0x02,0x05,0x02});    // RGB I/F Setting
        sendCommand(0xb600, {0x08});    // SDT  source hold time
        sendCommand(0xb500, {0x50});    // 480*800 CGM?
        sendCommand(0xb700, {0x00,0x00});   // Gate EQ:
        sendCommand(0xb800, {0x01,0x05,0x05,0x05}); // Source EQ:
        sendCommand(0xbc00, {0x00,0x00,0x00});  // Inversion: Column inversion (NVT)
        sendCommand(0xcc00, {0x03,0x00,0x00});  // BOE's Setting(default)
        sendCommand(0xbd00, {0x01,0x84,0x07,0x31,0x00,0x01});   // Display Timing:
        
        sendCommand(0xff00, {0xaa,0x55,0x25,0x01}); // enable Page??

// The following section must not change the order of the commands.
// FROM HERE:
#if 1
#define YOKO_GAMEN
        sendCommand(0x3600, {0b00100001});    // MADCTL display direction
        sendCommand(0x3a00, {0b01010100});    // 16bit-16bit (two commands are required)
        //sendCommand(0x1200);                   // partial mode on    
        sendCommand(0x2a00, {0, 0, 0x03, 0x1f});  // long side width: 800
        sendCommand(0x2b00, {0, 0, 0x02, 0x57});  // short side width: 600(0x257) ? for 480(0x1df)
        sendCommand(0x3500);    // tearing effect line ON
        sendCommand(0x3a00, {0b01010101});    // 16bit-16bit (two commands are required)
#else
        #define TATE_GAMEN
        sendCommand(0x3600, {0b00000011});    // MADCTL display direction
        sendCommand(0x3a00, {0b01010101});    // 16bit-16bit (two commands are required)
        sendCommand(0x2a00, {0, 0, 0x01, 0xdf});  // long side width: 800
        sendCommand(0x2b00, {0, 0, 0x03, 0x1f});  // short side width: 480
        sendCommand(0x3000, {0, 0, 0x03, 0x1F});
        sendCommand(0x3500);    // tearing effect line ON
        sendCommand(0x3a00, {0b01010101});    // 16bit-16bit (two commands are required)
#endif
// :UP TO HERE.
        delay(200);
        sendCommand(0x1100);    // Sleep out
        delay(200);
        sendCommand(0x2900);    // Display on
        delay(200);

        // start image transfer (column and row are reset to start positions.)             
        sendCommand(0x2c00);        
    }

    void setup_pio(PIO pio, uint sm, uint offset) {
        // データピン (8ピン) を PIO に設定
        for (int i = TFT_D0; i < TFT_D0 + 8; i++) {
            pio_gpio_init(pio, i);
        }
        // WRを PIO に設定
        pio_gpio_init(pio, TFT_WR);

        // ピン方向の設定
        pio_sm_set_consecutive_pindirs(pio, sm, TFT_D0, 8, true); // データピン出力
        pio_sm_set_consecutive_pindirs(pio, sm, TFT_WR, 1, true);      // GP11も出力に設定

        // PIOの初期設定
        pio_sm_config config = parallel_out_program_get_default_config(offset);
        sm_config_set_out_pins(&config, TFT_D0, 8); // データバスの出力ピン設定
        sm_config_set_sideset_pins(&config, TFT_WR);
        sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
#if defined(PIMORONI_PICO_PLUS_2)
#else
        sm_config_set_out_shift(&config, false, false, 16);
#endif
        sm_config_set_clkdiv(&config, 1.0f); // クロック分周値

        // ステートマシンを初期化
        pio_sm_init(pio, sm, offset, &config);
        // ステートマシンを有効化
        pio_sm_set_enabled(pio, sm, true);
    }

    enum {
        DMA_IRQ_INDEX_0 = 0,
        DMA_IRQ_INDEX_1,
        DMA_IRQ_INDEX_2,
        DMA_IRQ_INDEX_3,
    };

    inline static void clear_dma_irq(uint dma_ch) {
        if(dma_ch <= 7) {
            dma_hw->ints0 = 1u << dma_ch;
        } else {
            dma_hw->ints1 = 1u << (dma_ch - 8);
        }
    }

    static void dma_irq_handler(void) {
        clear_dma_irq(dma_channel[1]);   // clear irq.
        fixed_y -= 8;
        if(fixed_y <= 0) {
            fixed_y = 480;
        }
    }

    static void bitblt_irq_handler1(void) {
        noInterrupts();
        if(bitblt_dma.vcnt > 0) {
            bitblt_dma.vcnt--;
            bitblt_dma.src += bitblt_dma.src_to_next;
            bitblt_dma.dest += bitblt_dma.dest_to_next;
            dma_channel_set_read_addr(dma_channel[2], bitblt_dma.src, false);
            dma_channel_set_write_addr(dma_channel[2], bitblt_dma.dest, false);
            dma_channel_set_trans_count(dma_channel[2], bitblt_dma.hcnt, false);
            dma_channel_start(dma_channel[2]);
        } else {
            bitblt_dma.busy = false;
        }
        clear_dma_irq(dma_channel[2]);
        interrupts();
    }

    static void bitblt_irq_handler2(void) {
        noInterrupts();
        if(bitblt_dma3.vcnt > 0) {
            bitblt_dma3.vcnt--;
            bitblt_dma3.src += bitblt_dma3.src_to_next;
            bitblt_dma3.dest += bitblt_dma3.dest_to_next;
            dma_channel_set_read_addr(dma_channel[5], bitblt_dma3.src, false);
            dma_channel_set_write_addr(dma_channel[5], bitblt_dma3.dest, false);
            dma_channel_set_trans_count(dma_channel[5], bitblt_dma3.hcnt, false);
            dma_channel_start(dma_channel[5]);
        } else {
            bitblt_dma3.busy = false;
        }
        clear_dma_irq(dma_channel[5]);
        interrupts();
    }

    dma_channel_config dma_config[7];
    void setup_dma(PIO pio, uint sm) {
        /* データ転送DMA (to LCD) */
        dma_config[0] = dma_channel_get_default_config(dma_channel[0]);
#if defined(PIMORONI_PICO_PLUS_2)
        channel_config_set_transfer_data_size(&dma_config[0], DMA_SIZE_16);
        channel_config_set_bswap(&dma_config[0], true);
#else
        channel_config_set_transfer_data_size(&dma_config[0], DMA_SIZE_16);
#endif
        channel_config_set_read_increment(&dma_config[0], true);
        channel_config_set_write_increment(&dma_config[0], false);
        // PIOのDREQ設定
        uint dreq = pio_get_dreq(pio, sm, true);
        channel_config_set_dreq(&dma_config[0], dreq);
        dma_channel_set_config(dma_channel[0], &dma_config[0], false);
        channel_config_set_chain_to(&dma_config[0], dma_channel[1]);
        dma_channel_configure(
            dma_channel[0],  // チャネル番号
            &dma_config[0],         // 設定
            (void *)&pio->txf[sm],  // 転送先
            (void *)transferbuffer,       // 転送元
#if defined(PIMORONI_PICO_PLUS_2)
            SCREEN_SIZE,    // 転送回数
#else
            SCREEN_SIZE,
#endif
            false           // 自動開始しない
        );

        /* 転送元再設定DMA 転送完了時*/
        dma_config[1] = dma_channel_get_default_config(dma_channel[1]);
        channel_config_set_transfer_data_size(&dma_config[1], DMA_SIZE_32);
        channel_config_set_read_increment(&dma_config[1], false);
        channel_config_set_write_increment(&dma_config[1], false);
        dma_channel_set_config(dma_channel[1], &dma_config[1], false);
        channel_config_set_chain_to(&dma_config[1], dma_channel[0]);
        dma_channel_configure(
            dma_channel[1],  // チャネル番号
            &dma_config[1],         // 設定
            &dma_hw->ch[dma_channel[0]].al1_read_addr,  // 転送先
            &transferbuffer,       // 転送元
            1,    // 転送回数
            false           // 自動開始しない
        );
        // ブロック終了でIRQライン0を上げるようにDMAに指示
        dma_channel_set_irq0_enabled(dma_channel[1], true);
        // 割り込みハンドラ登録
        irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
        irq_set_enabled(DMA_IRQ_0, true);

        /* 矩形転送DMA 裏画面(1回1ライン) [1] */
        dma_config[2] = dma_channel_get_default_config(dma_channel[2]);
        channel_config_set_transfer_data_size(&dma_config[2], DMA_SIZE_16);
        channel_config_set_read_increment(&dma_config[2], true);
        channel_config_set_write_increment(&dma_config[2], true);
        channel_config_set_dreq(&dma_config[2], DREQ_FORCE);
        dma_channel_set_config(dma_channel[2], &dma_config[2], false);
#if 0
        channel_config_set_chain_to(&dma_config[2], dma_channel[4]);
#endif
        dma_channel_configure(
            dma_channel[2],  // チャネル番号
            &dma_config[2],         // 設定
            (void *)nullptr,  // 転送先
            (void *)nullptr,       // 転送元
            1,    // 転送回数
            false           // 自動開始しない
        );

        /* 矩形転送DMA 表画面(1回1ライン) [1] */
        dma_config[4] = dma_channel_get_default_config(dma_channel[4]);
        channel_config_set_transfer_data_size(&dma_config[4], DMA_SIZE_16);
        channel_config_set_read_increment(&dma_config[4], true);
        channel_config_set_write_increment(&dma_config[4], true);
        channel_config_set_dreq(&dma_config[4], DREQ_FORCE);
        dma_channel_set_config(dma_channel[4], &dma_config[4], false);
        dma_channel_configure(
            dma_channel[4],  // チャネル番号
            &dma_config[4],         // 設定
            (void *)nullptr,  // 転送先
            (void *)nullptr,       // 転送元
            1,    // 転送回数
            false           // 自動開始しない
        );
#if 0
        dma_irqn_set_channel_enabled(DMA_IRQ_INDEX_1, dma_channel[4], true);
#else
        dma_irqn_set_channel_enabled(DMA_IRQ_INDEX_1, dma_channel[2], true);
#endif
        irq_set_exclusive_handler(DMA_IRQ_1, bitblt_irq_handler1);
        irq_set_enabled(DMA_IRQ_1, true);

        /* 矩形転送DMA 裏画面(1回1ライン) [2] */
        dma_config[5] = dma_channel_get_default_config(dma_channel[5]);
        channel_config_set_transfer_data_size(&dma_config[5], DMA_SIZE_16);
        channel_config_set_read_increment(&dma_config[5], true);
        channel_config_set_write_increment(&dma_config[5], true);
        channel_config_set_dreq(&dma_config[5], DREQ_FORCE);
        dma_channel_set_config(dma_channel[5], &dma_config[5], false);
        //channel_config_set_chain_to(&dma_config[5], dma_channel[6]);
        dma_channel_configure(
            dma_channel[5],
            &dma_config[5],
            (void *)nullptr,  // 転送先
            (void *)nullptr,       // 転送元
            1,    // 転送回数
            false
        );

        /* 矩形転送DMA 表画面(1回1ライン) [2] */
        dma_config[6] = dma_channel_get_default_config(dma_channel[6]);
        channel_config_set_transfer_data_size(&dma_config[6], DMA_SIZE_16);
        channel_config_set_read_increment(&dma_config[6], true);
        channel_config_set_write_increment(&dma_config[6], true);
        channel_config_set_dreq(&dma_config[6], DREQ_FORCE);
        dma_channel_set_config(dma_channel[6], &dma_config[6], false);
        dma_channel_configure(
            dma_channel[6],
            &dma_config[6],
            (void *)nullptr,  // 転送先
            (void *)nullptr,       // 転送元
            1,    // 転送回数
            false
        );
#if 0
        dma_irqn_set_channel_enabled(DMA_IRQ_INDEX_2, dma_channel[6], true);
#else
        dma_irqn_set_channel_enabled(DMA_IRQ_INDEX_2, dma_channel[5], true);
#endif
        irq_set_exclusive_handler(DMA_IRQ_2, bitblt_irq_handler2);
        irq_set_enabled(DMA_IRQ_2, true);


#if 1
        /* データ転送DMA (to frame buffer) */
        dma_config[3] = dma_channel_get_default_config(dma_channel[3]);
        channel_config_set_transfer_data_size(&dma_config[3], DMA_SIZE_32);
        channel_config_set_read_increment(&dma_config[3], true);
        channel_config_set_write_increment(&dma_config[3], true);
        dma_channel_set_config(dma_channel[3], &dma_config[3], false);
        dma_channel_configure(
            dma_channel[3],  // チャネル番号
            &dma_config[3],         // 設定
            (void *)drawingbuffer,  // 転送先
            (void *)transferbuffer,       // 転送元
            SCREEN_SIZE / 2,    // 転送回数
            false           // 自動開始しない
        );
#endif
    }

    void setup_debug_serial_out(void) {
        #define DBG Serial1.printf
        Serial1.setRX(17u);
        Serial1.setTX(16u);
        Serial1.begin(115200);
    }

    void initframebuffers(void) {
        memset((void *)framebuffer, 0x00, sizeof(framebuffer));
    }

    // API
    uint16_t _color = 0;
    #if defined(PIMORONI_PICO_PLUS_2)
    uint16_t _screen_width = 800;
    uint16_t _screen_height = 480;
    #elif defined(YOKO_GAMEN)
    uint16_t _screen_width = 400;
    uint16_t _screen_height = 480;
    #else
    uint16_t _screen_width = 800;
    uint16_t _screen_height = 240;
    #endif

    #define SMALLER(x,y) ((x)<=(y)?(x):(y))
    #define LARGER(x,y) ((x)>=(y)?(x):(y)) 

    inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
#if 1
        return ((r & 250 >> 3) << 11) | ((g & 250 >> 2) << 5) | (b & 250 >> 3);
#else
        return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3;
#endif
    }

    inline int pset(int16_t x, int16_t y, uint16_t color, bool draw_both) {
        if(0 <= x && x < _screen_width && 0 <= y && y < _screen_height) {
            drawingbuffer[y * _screen_width + x] = color;
            if(draw_both) {
                transferbuffer[y * _screen_width + x] = color;
            }
            return 0;
        } else {
            return -1;
        }
    }

    int clsfast(uint16_t value) {
        //memset((void *)drawingbuffer, value, _screen_width * _screen_height * sizeof(uint16_t));
        dma_hw->ch[dma_channel[2]].al1_read_addr = (io_rw_32)transferbuffer;
        dma_hw->ch[dma_channel[2]].al1_write_addr = (io_rw_32)drawingbuffer;
        dma_channel_start(dma_channel[2]);
        dma_channel_wait_for_finish_blocking(dma_channel[2]);
        return 0;
    }

    int cls(uint16_t color, bool both) {
        for(int y = 0; y < _screen_height; y++) {
            for(int x = 0; x < _screen_width; x++) {
                pset(x, y, color, both);
            }
        }
        return 0;
    }

    int box(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color, bool both) {
        if (x1 > x2) { int16_t temp = x1; x1 = x2; x2 = temp; }
        if (y1 > y2) { int16_t temp = y1; y1 = y2; y2 = temp; }

        for (int x = x1; x <= x2; x++) {
            pset(x, y1, color, both); // 上辺
            pset(x, y2, color, both); // 下辺
        }

        for (int y = y1 + 1; y < y2; y++) {
            pset(x1, y, color, both); // 左辺
            pset(x2, y, color, both); // 右辺
        }

        return 0;
    }

    // this needs to be modified to use DMA.
    int boxfill(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color, bool both) {
        uint16_t smaller_x = max((min(x1, x2)), 0);
        uint16_t larger_x = min((max(x1, x2)), _screen_width - 1);
        uint16_t smaller_y = max((min(y1, y2)), 0);
        uint16_t larger_y = min((max(y1, y2)), _screen_height - 1);

#if 0
        for(int y = smaller_y; y <= larger_y; y++) {
            for(int x = smaller_x; x <= larger_x; x++) {
                pset(x, y, color, both);
            }
        }
#else
        rect_dma_paint(smaller_x, smaller_y, larger_x - smaller_x, larger_y - smaller_y, color, both);
#endif
        return 0;
    }

    int line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color, bool both) {
        int16_t dx = abs(x2 - x1);
        int16_t dy = abs(y2 - y1);
        int16_t direction_x = (x1 < x2) ? 1 : -1;
        int16_t direction_y = (y1 < y2) ? 1 : -1;
        int16_t err = dx - dy; // 誤差
        int16_t bak;

        for(;;) {
            pset(x1, y1, color, both);
            if (x1 == x2 && y1 == y2) {
                break;
            }
            bak = err;
            // x方向の誤差が大きければxを更新
            if (bak > -dy) {
                err -= dy;
                x1 += direction_x;
            }
            // y方向の誤差が大きければyを更新
            if (bak < dx) {
                err += dx;
                y1 += direction_y;
            }
        }
        return 0;
    }

    int circle(int16_t x0, int16_t y0, int16_t r, uint16_t color, bool both) {
        int16_t x = r;
        int16_t y = 0;
        int16_t err = 1 - r;

        while (x >= y) {
            pset(x0 + x, y0 + y, color, both); // 第一象限
            pset(x0 + y, y0 + x, color, both); // 第二象限
            pset(x0 - y, y0 + x, color, both); // 第三象限
            pset(x0 - x, y0 + y, color, both); // 第四象限
            pset(x0 - x, y0 - y, color, both); // 第五象限
            pset(x0 - y, y0 - x, color, both); // 第六象限
            pset(x0 + y, y0 - x, color, both); // 第七象限
            pset(x0 + x, y0 - y, color, both); // 第八象限

            y++;
            if (err < 0) {
                err += y << 1 + 1;
            } else {
                x--;
                err += (y - x) << 1 + 1;
            }
        }

        return 0;
    }

    // Function to transfer the contents of a transferbuffer to a drawingbuffer
    // by the specified scrolling amount.
    int rollv(uint16_t value) {
        dma_hw->ch[dma_channel[3]].al1_read_addr = (io_rw_32)transferbuffer;
        dma_hw->ch[dma_channel[3]].al1_write_addr = (io_rw_32)&drawingbuffer[_screen_width * value];
        dma_hw->ch[dma_channel[3]].transfer_count = _screen_width * (_screen_height - value) / 2;
        dma_channel_start(dma_channel[3]);
        dma_channel_wait_for_finish_blocking(dma_channel[3]);
        return 0;
    }

    pngle_t *pngle;
    static void pngle_on_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t rgba[4]) {
        uint16_t color = (rgba[0] << 8 & 0xf800) | (rgba[1] << 3 & 0x07e0) | (rgba[2] >> 3 & 0x001f);
        //pset(x, y, color);
        chip_map[y * 256 + x] = color;
    }

    int loadpng(const uint8_t *p_pngdata) {
        int fed = pngle_feed(pngle, p_pngdata, sizeof(pngdata));
        pngle_ihdr_t *ihdr = pngle_get_ihdr(pngle);
        //DBG("%d\n", ihdr->width);
        return fed;
    }

    int sprite(uint16_t sx, uint16_t sy, uint16_t cx, uint16_t cy, uint16_t w, uint16_t h, uint16_t trans, bool both) {
        for(int y = 0; y < h; y++) {
            for(int x = 0; x < w; x++) {
                uint16_t pixel = chip_map[(y + cy) * 256 + (x + cx)];
                if(pixel != trans) {
                    pset(sx + x, sy + y, pixel, both);
                }
            }
        }
        return 0; 
    }

    int bitblt(uint16_t *src, uint16_t dest_x, uint16_t dest_y, uint16_t src_x, uint16_t src_y, uint16_t w, uint16_t h, bool both) {
        volatile uint32_t dummy = 0;
        BITBLT_DMA *dma_first;
        BITBLT_DMA *dma_second;
        int channel_pair[2];

#if 1
        while(bitblt_dma.busy || bitblt_dma3.busy) {dummy++;}
        if(get_core_num() == 1) {
            bitblt_dma.busy = true;
            channel_pair[0] = 2; channel_pair[1] = 4;
            dma_first = &bitblt_dma; dma_second = &bitblt_dma2;
        } else if(get_core_num() == 0) {
            bitblt_dma3.busy = true;
            channel_pair[0] = 5; channel_pair[1] = 6;
            dma_first = &bitblt_dma3; dma_second = &bitblt_dma4;
        } else {
            DBG("bitblt() busy.\n");
            return -1;
        }
#else
        while(bitblt_dma.busy) {dummy++;}
        if(!bitblt_dma.busy) {
            bitblt_dma.busy = true;
            channel_pair[0] = 2; channel_pair[1] = 4;
            dma_first = &bitblt_dma; dma_second = &bitblt_dma2;
//        } else if(!bitblt_dma3.busy) {
//            bitblt_dma3.busy = true;
//            channel_pair[0] = 5; channel_pair[1] = 6;
//            dma_first = &bitblt_dma3; dma_second = &bitblt_dma4;
        } else {
            DBG("bitblt() busy.\n");
            return -1;
        }
#endif
        // specified screen:
        if(src == (void *)chip_map) {
            dma_first->src_to_next = 256;
            dma_first->dest_to_next =_screen_width;
        } else {
            dma_first->src_to_next =_screen_width;
            dma_first->dest_to_next =_screen_width;
        }
        dma_first->src = &src[src_y * dma_first->src_to_next + src_x];
        dma_first->hcnt = w;
        dma_first->vcnt = h;    // h: height
        dma_first->dest = (uint16_t *)&drawingbuffer[dest_y * dma_first->dest_to_next + dest_x];
        if(both) {
            if(src == (void *)chip_map) {
                dma_second->src_to_next = 256;
                dma_second->dest_to_next =_screen_width;
            } else {
                dma_second->src_to_next =_screen_width;
                dma_second->dest_to_next =_screen_width;
            }
            dma_second->src = &src[src_y * dma_second->src_to_next + src_x];
            dma_second->hcnt = w;
            dma_second->vcnt = h;    // h: height
            dma_second->dest = (uint16_t *)&transferbuffer[dest_y * dma_second->dest_to_next + dest_x];
        } else {
            dma_second->src = &src[0];
            dma_second->hcnt = 0;
            dma_second->vcnt = h;    // h: height
            dma_second->dest = (uint16_t *)&transferbuffer[0];
            
        }

        dma_hw->ch[channel_pair[0]].al1_ctrl |= DMA_CH0_CTRL_TRIG_INCR_READ_BITS;
        dma_channel_set_read_addr(dma_channel[channel_pair[0]], dma_first->src, false);
        dma_channel_set_write_addr(dma_channel[channel_pair[0]], dma_first->dest, false);
        dma_channel_set_trans_count(dma_channel[channel_pair[0]], dma_first->hcnt, false);
        dma_channel_start(dma_channel[channel_pair[0]]);
        return 0;
    }

    #define COLOR_SRC_NUM (16)
    // The size of this buffer should be extra, because the old colour_src_buf[] is referenced 
    // by the execution of rect_paint() for a small area.
    volatile __attribute__((aligned(4))) uint16_t color_src_buf[COLOR_SRC_NUM];
    int rect_dma_paint(uint16_t dest_x, uint16_t dest_y, uint16_t w, uint16_t h, uint16_t color, bool both) {
        volatile uint32_t dummy = 0;
        BITBLT_DMA *dma_first;
        BITBLT_DMA *dma_second;
        int channel_pair[2];
        static int color_src_idx = 0;

#if 1
        while(bitblt_dma.busy || bitblt_dma3.busy) {dummy++;}
        if(get_core_num() == 1) {
            bitblt_dma.busy = true;
            channel_pair[0] = 2; channel_pair[1] = 4;
            dma_first = &bitblt_dma; dma_second = &bitblt_dma2;
        } else if(get_core_num() == 0) {
            bitblt_dma3.busy = true;
            channel_pair[0] = 5; channel_pair[1] = 6;
            dma_first = &bitblt_dma3; dma_second = &bitblt_dma4;
        } else {
            DBG("rect_dma_paint() busy.\n");
            return -1;
        }
#else
        while(bitblt_dma3.busy) {dummy++;}
        if(!bitblt_dma3.busy) {
            bitblt_dma3.busy = true;
            channel_pair[0] = 5; channel_pair[1] = 6;
            dma_first = &bitblt_dma3; dma_second = &bitblt_dma4;
//        } else if(!bitblt_dma.busy) {
//            bitblt_dma.busy = true;
//            channel_pair[0] = 2; channel_pair[1] = 4;
//            dma_first = &bitblt_dma; dma_second = &bitblt_dma2;
        } else {
            DBG("rect_dma_paint() busy.\n");
            return -1;
        }
#endif
        color_src_buf[color_src_idx] = color;
        // specified screen:
        dma_first->src_to_next = 0;
        dma_first->dest_to_next =_screen_width;

        dma_first->src = (uint16_t *)&color_src_buf[color_src_idx];
        dma_first->hcnt = w;
        dma_first->vcnt = h;    // h: height
        dma_first->dest = (uint16_t *)&drawingbuffer[dest_y * dma_first->dest_to_next + dest_x];

        if(both) {
            dma_second->src_to_next = 0;
            dma_second->dest_to_next =_screen_width;

            dma_second->src = (uint16_t *)&color_src_buf[color_src_idx];
            dma_second->hcnt = w;
            dma_second->vcnt = h;    // h: height
            dma_second->dest = (uint16_t *)&transferbuffer[dest_y * dma_second->dest_to_next + dest_x];
        } else {
            dma_second->src = (uint16_t *)&color_src_buf[color_src_idx];
            dma_second->hcnt = 0;
            dma_second->vcnt = h;    // h: height
            dma_second->dest = (uint16_t *)&transferbuffer[0];
            
        }

        dma_hw->ch[channel_pair[0]].al1_ctrl &= ~DMA_CH0_CTRL_TRIG_INCR_READ_BITS;
        dma_channel_set_read_addr(dma_channel[channel_pair[0]], dma_first->src, false);
        dma_channel_set_write_addr(dma_channel[channel_pair[0]], dma_first->dest, false);
        dma_channel_set_trans_count(dma_channel[channel_pair[0]], dma_first->hcnt, false);
        dma_channel_start(dma_channel[channel_pair[0]]);
        if(++color_src_idx >= COLOR_SRC_NUM) {
            color_src_idx = 0;
        }

        return 0;
    }

    void put_char(uint16_t x, uint16_t y, uint8_t *src_mono, uint16_t color, int width, bool both) {
        uint8_t *p = src_mono;
        uint8_t b[8];

        for(int i = 0; i < 2; i++) {
            for(int j = 0; j < width; j++) {
                b[0] = *p & 0b00000001;
                b[1] = *p & 0b00000010;
                b[2] = *p & 0b00000100;
                b[3] = *p & 0b00001000;
                b[4] = *p & 0b00010000;
                b[5] = *p & 0b00100000;
                b[6] = *p & 0b01000000;
                b[7] = *p & 0b10000000;

                for(int k = 0; k < 8; k++) {
                    if(b[k]) {
                        this->pset(x + j, y + k + 8 * i, color, both);
                    }
                }
                p++;
            }
        }
    }

    void put_char_scaled(uint16_t x, uint16_t y, uint8_t *src_mono, uint16_t color, uint8_t scale, int width, bool both) {
        uint8_t *p = src_mono;
        uint8_t b[8];

        for(int i = 0; i < 2; i++) {
            for(int j = 0; j < width; j++) {
                b[0] = *p & 0b00000001;
                b[1] = *p & 0b00000010;
                b[2] = *p & 0b00000100;
                b[3] = *p & 0b00001000;
                b[4] = *p & 0b00010000;
                b[5] = *p & 0b00100000;
                b[6] = *p & 0b01000000;
                b[7] = *p & 0b10000000;

                for(int k = 0; k < 8; k++) {
                    if(b[k]) {
                        for (uint8_t sx = 0; sx < scale; sx++) {
                            for (uint8_t sy = 0; sy < scale; sy++) {
                                this->pset(x + j * scale + sx, y + (k + 8 * i) * scale + sy, color, both);
                            }
                        }
                    }
                }
                p++;
            }
        }
    }

    void put_char_scaled_line(uint16_t x, uint16_t y, uint8_t *src_mono, uint16_t color, uint8_t scale, int width, bool both) {
        uint8_t *p = src_mono;
        uint8_t intermediate[16][16];

        // 中間データ作成
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < width; j++) {
                for (int k = 0; k < 8; k++) {
                    intermediate[j][k + 8 * i] = (*p & (1 << k)) ? 1 : 0;
                }
                p++;
            }
        }

        for (int oy = 0; oy < 16; oy++) {
            for (int ox = 0; ox < width; ox++) {
                if (intermediate[ox][oy]) {
                    int start_x = x + ox * scale;
                    int start_y = y + oy * scale;

                    // 右方向
                    if (ox < (width - 1) && intermediate[ox + 1][oy]) {
                        for (int i = 0; i < scale; i++) {
                            this->pset(start_x + i, start_y, color, both);
                        }
                    }
                    // 下方向
                    if (oy < 15 && intermediate[ox][oy + 1]) {
                        for (int i = 0; i < scale; i++) {
                            this->pset(start_x, start_y + i, color, both);
                        }
                    }
                    // 斜め方向(右下)
                    if (ox < (width - 1) && oy < 15 && intermediate[ox + 1][oy + 1] && !intermediate[ox + 1][oy] && !intermediate[ox][oy + 1]) {
                        for (int i = 0; i < scale; i++) {
                            this->pset(start_x + i, start_y + i, color, both);
                        }
                    }
                    // 斜め方向(右上)
                    if (0 < ox && ox < (width - 1) && oy < 15 && intermediate[ox - 1][oy + 1] && !intermediate[ox - 1][oy] && !intermediate[ox][oy + 1]) {
                        for (int i = 0; i < scale; i++) {
                            this->pset(start_x - i, start_y + i, color, both);
                        }
                    }

                    //this->pset(start_x, start_y, color, both);
                }
            }
        }
    }


    int16_t blend_rgb565_half(uint16_t color) {
        uint16_t r = (color >> 11) & 0x1F; // 赤5
        uint16_t g = (color >> 5) & 0x3F;  // 緑6
        uint16_t b = color & 0x1F;         // 青5

        r = r >> 1;
        g = g >> 1;
        b = b >> 1;

        return (r << 11) | (g << 5) | b;
    }

    void swapbuffer(void) {
        volatile uint16_t *tmp;
        //uint32_t addr = dma_hw->ch[dma_channel[0]].al1_read_addr;
        tmp = drawingbuffer;
        drawingbuffer = transferbuffer;
        transferbuffer = tmp;

        return;
    }

};
