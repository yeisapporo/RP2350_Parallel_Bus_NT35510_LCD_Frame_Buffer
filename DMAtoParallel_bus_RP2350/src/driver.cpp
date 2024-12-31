///////////////////////////////////////////////////////////////////////////////////
// This programme is an 800x480 LCD driver (parallel bus) controlled by NT35510.
// Do not define the following #define on boards without PSRAM.
///////////////////////////////////////////////////////////////////////////////////
#define PIMORINI_PICO_PLUS_2

#include <Arduino.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "parallel_out.pio.h"

#include "pngle.h"

#define TFT_D0 0       // GPIO0〜7をデータバスに使用
#define TFT_CS 12
#define TFT_RS 8
#define TFT_RD 9
#define TFT_RESET 10
#define TFT_WR 11
#define TFT_DEBUG_SIG 15
#if defined(PIMORINI_PICO_PLUS_2)
#define DATA_SIZE 800*480   // 画面サイズ
#else
#define DATA_SIZE 400*480   // 画面サイズ(landscape) or 800*240 (portrait)
#endif
#if defined(PIMORINI_PICO_PLUS_2)
__attribute__((aligned(4))) uint16_t framebuffer[2][DATA_SIZE] PSRAM; // ピクセルデータを格納
__attribute__((aligned(4))) uint16_t chip_map[256 * 256] PSRAM;
#else
__attribute__((aligned(4))) uint16_t framebuffer[1][DATA_SIZE];
#endif
volatile __attribute__((aligned(4))) uint16_t *transferbuffer = framebuffer[0];
#if defined(PIMORINI_PICO_PLUS_2)
volatile __attribute__((aligned(4))) uint16_t *drawingbuffer = framebuffer[1];
#else
volatile __attribute__((aligned(4))) uint16_t *drawingbuffer = framebuffer[0];
#endif
volatile uint dma_channel[4];

void setup_gpio(void){
    for (int i = TFT_D0; i < TFT_D0 + 8; i++) {
        gpio_init(i);
        gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_4MA);
        gpio_set_dir(i, GPIO_OUT);
    }

    int tft_pins[] = {TFT_RS, TFT_RD, TFT_WR, TFT_RESET, TFT_CS, TFT_DEBUG_SIG};
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
    sleep_us(500);
    digitalWrite(TFT_RD, HIGH);
    sleep_us(500);
    gpio_put_masked(0xff, 0xff & (addr >> 8));    // put upper byte
    digitalWrite(TFT_WR, LOW);
    sleep_us(500);
    digitalWrite(TFT_WR, HIGH);
    sleep_us(500);
    gpio_put_masked(0xff, 0xff & addr);         // put lower byte
    digitalWrite(TFT_WR, LOW);
    sleep_us(500);
    digitalWrite(TFT_WR, HIGH);
    sleep_us(500);
}

void setByteData(uint8_t data) {
    digitalWrite(TFT_RS, HIGH); // data mode
    sleep_us(500);
    gpio_put_masked(0xff, data);
    digitalWrite(TFT_WR, LOW);
    sleep_us(500);
    digitalWrite(TFT_WR, HIGH);
    sleep_us(500);
}

// for some types of frame buffer.
void setWordData(uint16_t data) {
    digitalWrite(TFT_RS, HIGH); // data mode
    sleep_us(500);
    gpio_put_masked(0xff, 0xff & (data >> 8));
    digitalWrite(TFT_WR, LOW);
    sleep_us(500);
    digitalWrite(TFT_WR, HIGH);
    sleep_us(500);

    gpio_put_masked(0xff, 0xff & data );
    digitalWrite(TFT_WR, LOW);
    sleep_us(500);
    digitalWrite(TFT_WR, HIGH);
    sleep_us(500);
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
    delay(150);
    digitalWrite(TFT_RESET, LOW);
    delay(150);
    digitalWrite(TFT_RESET, HIGH);
    delay(150);

    // send initializing commands.
    sendCommand(0x0100);
    delay(150);
    sendCommand(0x1100);    // Sleep out
    delay(150);
    sendCommand(0x2900);    // Display on
    delay(150);
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

    // The following sections must not change the order of the commands.
    // FROM HERE:
#if 1
    #define YOKO_GAMEN
    sendCommand(0x3600, {0b00100001});    // MADCTL display direction
    sendCommand(0x3a00, {0b01010101});    // 16bit-16bit (two commands are required)
    sendCommand(0x2a00, {0, 0, 0x03, 0x1f});  // long side width: 800
    sendCommand(0x2b00, {0, 0, 0x02, 0x57});  // short side width: 600 ? for 480
    sendCommand(0x3000, {0, 0, 0x03, 0x1F});
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

    delay(150);
    // start image transfer (column and row are reset to start positions.)             
    sendCommand(0x2c00);
    
}

void setup_pio(PIO pio, uint sm, uint offset) {
    // データピン (8ピン) を PIO に設定
    for (int i = TFT_D0; i < TFT_D0 + 8; i++) {
        pio_gpio_init(pio, i);
    }
    // WR, DEGUG_SIG を PIO に設定
    pio_gpio_init(pio, TFT_WR);
    //pio_gpio_init(pio, TFT_DEBUG_SIG);

    // ピン方向の設定
    pio_sm_set_consecutive_pindirs(pio, sm, TFT_D0, 8, true); // データピン出力
    pio_sm_set_consecutive_pindirs(pio, sm, TFT_WR, 1, true);      // GP11も出力に設定

    // PIOの初期設定
    pio_sm_config config = parallel_out_program_get_default_config(offset);
    sm_config_set_out_pins(&config, TFT_D0, 8); // データバスの出力ピン設定
#if 0
    sm_config_set_set_pins(&config, TFT_WR, 1);      // GP11をSET命令で操作対象
#else
    sm_config_set_set_pins(&config, TFT_WR, 2);      // GP11ほかをSET命令で操作対象
#endif
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    #if defined(PIMORINI_PICO_PLUS_2)
    #else
    sm_config_set_out_shift(&config, false, false, 16);
    #endif
    sm_config_set_clkdiv(&config, 1.0f); // クロック分周値

    // ステートマシンを初期化
    pio_sm_init(pio, sm, offset, &config);
    // ステートマシンを有効化
    pio_sm_set_enabled(pio, sm, true);
}

void dma_irq_handler(void) {
    dma_hw->ints0 = 1u << dma_channel[1];   // clear irq.
    digitalWrite(TFT_DEBUG_SIG, HIGH);
    digitalWrite(TFT_DEBUG_SIG, LOW);
    digitalWrite(TFT_DEBUG_SIG, HIGH);
    digitalWrite(TFT_DEBUG_SIG, LOW);
}

dma_channel_config dma_config[4];
void setup_dma(PIO pio, uint sm) {
        /* データ転送DMA (to LCD) */
        dma_config[0] = dma_channel_get_default_config(dma_channel[0]);
#if defined(PIMORINI_PICO_PLUS_2)
        channel_config_set_transfer_data_size(&dma_config[0], DMA_SIZE_32);
        channel_config_set_bswap(&dma_config[0], true);
        channel_config_set_high_priority(&dma_config[3], true);        

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
#if defined(PIMORINI_PICO_PLUS_2)
            DATA_SIZE / 2,    // 転送回数
#else
            DATA_SIZE,
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

#if 1
        /* データ転送DMA (to frame buffer) 何に使っているか？不要になったかも */
        dma_config[2] = dma_channel_get_default_config(dma_channel[2]);
        channel_config_set_transfer_data_size(&dma_config[2], DMA_SIZE_32);
        channel_config_set_read_increment(&dma_config[2], true);
        channel_config_set_write_increment(&dma_config[2], true);
        dma_channel_set_config(dma_channel[2], &dma_config[2], false);
        dma_channel_configure(
            dma_channel[2],  // チャネル番号
            &dma_config[2],         // 設定
            (void *)drawingbuffer,  // 転送先
            (void *)transferbuffer,       // 転送元
            DATA_SIZE / 2,    // 転送回数
            false           // 自動開始しない
        );
#endif
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
            DATA_SIZE / 2,    // 転送回数
            false           // 自動開始しない
        );
#endif
}

void setup_debug_serial_out(void) {
    #define DBG Serial1.printf
    Serial1.setRX(17u);
    Serial1.setTX(16u);
    Serial1.begin(9600);
}

void initframebuffers(void) {
    //memset((void *)transferbuffer, 0, sizeof(DATA_SIZE * 2));
    //memset((void *)drawingbuffer, 15, sizeof(DATA_SIZE * 2));
    memset((void *)framebuffer, 0x00, sizeof(framebuffer));
}

#include "churu_yoko-gamen.h"
#include "churu_tate-gamen.h"
#if defined(PIMORINI_PICO_PLUS_2)
#include "churu_full.h"
#include "image004.h"
#include "data.h"
#endif

// API
uint16_t _color = 0;
#if defined(PIMORINI_PICO_PLUS_2)
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
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3);
}

inline int pset(int16_t x, int16_t y, uint16_t color, bool draw_both) {
    if(0 <= x && x <= _screen_width && 0 <= y && y <= _screen_height) {
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
    // 座標の正規化（x1, y1 が左上、x2, y2 が右下になるよう調整）
    if (x1 > x2) { int16_t temp = x1; x1 = x2; x2 = temp; }
    if (y1 > y2) { int16_t temp = y1; y1 = y2; y2 = temp; }

    // 上辺と下辺を描画
    for (int x = x1; x <= x2; x++) {
        pset(x, y1, color, both); // 上辺
        pset(x, y2, color, both); // 下辺
    }

    // 左辺と右辺を描画（上下の角は既に描画されているため除外）
    for (int y = y1 + 1; y < y2; y++) {
        pset(x1, y, color, both); // 左辺
        pset(x2, y, color, both); // 右辺
    }

    return 0;
}

int boxfill(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color, bool both) {
    uint16_t smaller_x = LARGER((SMALLER(x1, x2)), 0);
    uint16_t larger_x = SMALLER((LARGER(x1, x2)), _screen_width);
    uint16_t smaller_y = LARGER((SMALLER(y1, y2)), 0);
    uint16_t larger_y = SMALLER((LARGER(y1, y2)), _screen_height);

    for(int y = smaller_y; y <= larger_y; y++) {
        for(int x = smaller_x; x <= larger_x; x++) {
            pset(x, y, color, both);
        }
    }

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
void pngle_on_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t rgba[4]) {
    uint16_t color = (rgba[0] << 8 & 0xf800) | (rgba[1] << 3 & 0x07e0) | (rgba[2] >> 3 & 0x001f);
    //pset(x, y, color);
    chip_map[y * 256 + x] = color;

}

int loadpng(const uint8_t *p_pngdata) {
    int fed = pngle_feed(pngle, p_pngdata, sizeof(pngdata));
    pngle_ihdr_t *ihdr = pngle_get_ihdr(pngle);
    DBG("%d\n", ihdr->width);
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

void setup()
{
    volatile int psram_size;
    setup_debug_serial_out();
    
    psram_size = rp2040.getPSRAMSize();
    DBG("PSRAM size:%d\n", psram_size);

    // load png to draw on chip map (PSRAM)
    pngle = pngle_new();
    pngle_set_draw_callback(pngle, pngle_on_draw);
    loadpng(pngdata);

    setup_gpio();
    // reset DEBUG_SIG
    dma_irq_handler();

    // NT35510初期化
    init_LCD();    
    
    // PIOとDMAの初期化
    PIO pio = pio0;
    uint sm = 0;
    dma_channel[0] = dma_claim_unused_channel(true);
    dma_channel[1] = dma_claim_unused_channel(true);
    dma_channel[2] = dma_claim_unused_channel(true);
    dma_channel[3] = dma_claim_unused_channel(true);
    uint offset = pio_add_program(pio, &parallel_out_program);

    setup_pio(pio, sm, offset);
    setup_dma(pio, sm);

    // 最初のDMAチャンネルを起動
    initframebuffers();
    sleep_us(2000);
    dma_channel_wait_for_finish_blocking(dma_channel[1]);
    dma_channel_start(dma_channel[0]);
    
}

void swapbuffer(void) {
    volatile uint16_t *tmp;
    //uint32_t addr = dma_hw->ch[dma_channel[0]].al1_read_addr;
    tmp = drawingbuffer;
    drawingbuffer = transferbuffer;
    transferbuffer = tmp;

    return;
}

void loop()
{
    static int a;

    if(++a == 2) {
        a = 0;
    }

    if(a) {
        sio_hw->gpio_set = (1ul << 25) | 1;
    } else {
        sio_hw->gpio_clr = (1ul << 25) | 1;
    }

#if 1 // 画面テスト0
    static int w = 0;
    static int cnt = 0;
#if defined(YOKO_GAMEN)
    //clsfast(0);
    ////rollv(0);
    #if defined(PIMORINI_PICO_PLUS_2)
    //for(int i = 0; i < DATA_SIZE; i++) {
    //    drawingbuffer[i] = churu_full[i * 2] + churu_full[i * 2 + 1] * 256;
    //}
#if 0
    memcpy((void *)&drawingbuffer[0], (const void *)&churu_full[_screen_width * (_screen_height - w) * 2], _screen_width * w * 2);
    memcpy((void *)&drawingbuffer[800 * w], (const void *)&churu_full[0], _screen_width * (_screen_height - w) * 2);
#else
    ////memcpy((void *)&drawingbuffer[0], (const void *)&image004[_screen_width * (1352 - w) * 2], _screen_width * 4 * 2);
    //cls(rgb(64, 64, 64));
    #if 1
    for(int i = 0; i < 8; i++) {
        sprite(random(800)/24*24, random(480)/24*24, random(10)*24, random(10)*24, 24, 24, rgb(255, 255, 255), true);
        // スクロール単位分のライン数で転送するように変えること
        line(random(800), random(480), random(800), random(480), rgb(random(256), random(256), random(256)), true);
        box(random(800), random(480), random(800), random(480), rgb(random(256), random(256), random(256)), true);
        circle(random(800), random(480), random(480), rgb(random(256), random(256), random(256)), true);
    }
    #endif

#endif

    w += 1; if(w >= 1352) {w = 0;}
    cnt++;if(cnt >=4){cnt = 0;}

    #else
    memcpy((void *)(framebuffer[0] + w * 400), (const void *)(churu), 480 * 400 * 2 - w * 800);
    memcpy((void *)(framebuffer[0]), (const void *)(churu + (480 - w) * 800), 400 * w * 2);
    w += 4; if(w >= 480) {w = 0;}
    #endif

#elif defined(TATE_GAMEN)
    for(int i = 0; i < 240 * 800 * 2 - w * 480; i += 2) {
        framebuffer[0][i / 2 + w * 240] = (uint16_t)(churu2[i]) + (uint16_t)(churu2[i + 1]  << 8);
    }
    for(int i = 0; i < (240 * w * 2); i += 2) {
        framebuffer[0][i / 2] = (uint16_t)(churu2[i + (800 - w) * 480]) + (uint16_t)(churu2[i + (800 - w) * 480 + 1] << 8);
    }
    w += 2; if(w >= 800) {w = 0;}
#endif
#endif

    swapbuffer();
    
    sleep_us(10); // 適当に遅延
}

void setup1(void) {
    ;
}

void loop1(void) {
    ;
}
