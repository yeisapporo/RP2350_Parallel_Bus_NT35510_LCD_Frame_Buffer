#include <Arduino.h>


#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "parallel_out.pio.h"

#define TFT_D0 0       // GPIO0〜7をデータバスに使用
#define TFT_CS 12
#define TFT_RS 8          // GPIO8をRSピンに使用
#define TFT_RD 9
#define TFT_RESET 10
#define TFT_WR 11         // ★★順番変えた方がよい
#define DATA_SIZE 400*480   // 画面サイズ

__attribute__((aligned(4))) uint16_t framebuffer[DATA_SIZE];// ピクセルデータを格納
volatile __attribute__((aligned(4))) uint16_t *startaddr = framebuffer;
volatile uint dma_channel[2];

void setup_gpio(void){
    for (int i = TFT_D0; i < TFT_D0 + 8; i++) {
        gpio_init(i);
        gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_4MA);
        gpio_set_dir(i, GPIO_OUT);
    }

    int tft_pins[] = {TFT_RS, TFT_RD, TFT_WR, TFT_RESET, TFT_CS};
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
   sleep_us(1000);
    digitalWrite(TFT_RD, HIGH);
   sleep_us(1000);
    //gpio_clr_mask(0xff);
    gpio_put_masked(0xff, 0xff & (addr >> 8));    // put upper byte
    digitalWrite(TFT_WR, LOW);
   sleep_us(1000);
    digitalWrite(TFT_WR, HIGH);
   sleep_us(1000);
    //gpio_clr_mask(0xff);
    gpio_put_masked(0xff, 0xff & addr);         // put lower byte
    digitalWrite(TFT_WR, LOW);
   sleep_us(1000);
    digitalWrite(TFT_WR, HIGH);
   sleep_us(1000);
}

void setByteData(uint8_t data) {
    digitalWrite(TFT_RS, HIGH); // data mode
   sleep_us(1000);
    //gpio_clr_mask(0xff);
    gpio_put_masked(0xff, data);
    digitalWrite(TFT_WR, LOW);
   sleep_us(1000);
    digitalWrite(TFT_WR, HIGH);
   sleep_us(1000);
}

// for some types of frame buffer.
void setWordData(uint16_t data) {
    digitalWrite(TFT_RS, HIGH); // data mode
   sleep_us(1000);
    //gpio_clr_mask(0xff);
    gpio_put_masked(0xff, 0xff & (data >> 8));
    digitalWrite(TFT_WR, LOW);
   sleep_us(1000);
    digitalWrite(TFT_WR, HIGH);
   sleep_us(1000);

    //gpio_clr_mask(0xff);
    gpio_put_masked(0xff, 0xff & data );
    digitalWrite(TFT_WR, LOW);
   sleep_us(1000);
    digitalWrite(TFT_WR, HIGH);
   sleep_us(1000);
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
    sendCommand(0x1100);    // Sleep out
    delay(200);
    sendCommand(0x2900);    // Display on
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
    // GP11 を PIO に設定
    pio_gpio_init(pio, TFT_WR);

    // ピン方向の設定
    pio_sm_set_consecutive_pindirs(pio, sm, TFT_D0, 8, true); // データピン出力
    pio_sm_set_consecutive_pindirs(pio, sm, TFT_WR, 1, true);      // GP11も出力に設定

    // PIOの初期設定
    pio_sm_config config = parallel_out_program_get_default_config(offset);
    sm_config_set_out_pins(&config, TFT_D0, 8); // データバスの出力ピン設定
    sm_config_set_set_pins(&config, TFT_WR, 1);      // GP11をSET命令で操作対象
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    sm_config_set_out_shift(&config, false, false, 16);

    sm_config_set_clkdiv(&config, 1.0f); // クロック分周値

    // ステートマシンを初期化
    pio_sm_init(pio, sm, offset, &config);
    // ステートマシンを有効化
    pio_sm_set_enabled(pio, sm, true);
}

void setup_dma(PIO pio, uint sm) {
    dma_channel_config dma_config[2];
        /* データ転送DMA */
        dma_config[0] = dma_channel_get_default_config(dma_channel[0]);
        channel_config_set_transfer_data_size(&dma_config[0], DMA_SIZE_16);
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
            (void *)framebuffer,       // 転送元
            DATA_SIZE,    // 転送回数
            false           // 自動開始しない
        );

        /* 転送元再設定DMA */
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
            &startaddr,       // 転送元
            1,    // 転送回数
            false           // 自動開始しない
        );

}

void setup_debug_serial_out(void) {
    #define DBG Serial1.printf
    Serial1.setRX(17u);
    Serial1.setTX(16u);
    Serial1.begin(9600);
}

void setup()
{
    volatile int aa;
    setup_debug_serial_out();
    
    aa = rp2040.getPSRAMSize();
    DBG("PSRAM size:%d\n", aa);

    // NT35510初期化
    setup_gpio();
    init_LCD();    
    
    // PIOとDMAの初期化
    PIO pio = pio0;
    uint sm = 0;
    dma_channel[0] = dma_claim_unused_channel(true);
    dma_channel[1] = dma_claim_unused_channel(true);
    uint offset = pio_add_program(pio, &parallel_out_program);

    setup_pio(pio, sm, offset);
    setup_dma(pio, sm);

    // 最初のDMAチャンネルを起動
    sleep_us(2000);
    while(dma_hw->ch[dma_channel[0]].al1_ctrl & DMA_CH0_CTRL_TRIG_BUSY_BITS) {
        __asm("nop");
    }
    while(dma_hw->ch[dma_channel[1]].al1_ctrl & DMA_CH0_CTRL_TRIG_BUSY_BITS) {
        __asm("nop");
    }
    dma_channel_start(dma_channel[1]);
    while(dma_hw->ch[dma_channel[0]].al1_ctrl & DMA_CH0_CTRL_TRIG_BUSY_BITS) {
        __asm("nop");
    }

    //dma_channel_wait_for_finish_blocking(dma_channel);
}

#if defined(YOKO_GAMEN)
    #include "churu_yoko-gamen.h"
#elif defined(TATE_GAMEN)
    #include "churu_tate-gamen.h"
#endif

void loop()
{
    static int a;
    //bool flg = dma_hw->ch[dma_channel[0]].al1_ctrl & DMA_CH0_CTRL_TRIG_BUSY_BITS;
    //if(flg == false) {
    //    dma_channel_set_read_addr(dma_channel[0], framebuffer, true);
    //}

    if(++a == 2) {
        a = 0;
    }

    if(a) {
        sio_hw->gpio_set = (1ul << 25) | 1;
    } else {
        sio_hw->gpio_clr = (1ul << 25) | 1;
    }

# if 0
    for(int i = 0; i < 800 * 480; i += 2) {
        framebuffer[i / 2] = (uint16_t)(churu[i + 1] *256) + churu[i];
    }
#else
    memcpy((void *)framebuffer, (const void *)churu, sizeof(churu));
#endif

    //sleep_ms(10); // 適当に遅延を入れてみる
}
