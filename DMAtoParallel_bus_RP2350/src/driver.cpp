#include <Arduino.h>


#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "parallel_out.pio.h"

#define TFT_DATA_BASE 0       // GPIO0〜7をデータバスに使用
#define TFT_RS_PIN 8          // GPIO8をRSピンに使用
#define TFT_CS_PIN 9
#define TFT_RESET_PIN 10
#define TFT_WR_PIN 11
#define DATA_SIZE 800*480   // 画面サイズ

__attribute__((aligned(4))) uint8_t framebuffer[DATA_SIZE];// ピクセルデータを格納
volatile __attribute__((aligned(4))) uint8_t *startaddr = framebuffer;
volatile uint dma_channel[2];

void setup_datagpio(void){
    for (int i = TFT_DATA_BASE; i < TFT_DATA_BASE + 8; i++) {
        gpio_init(i);
        gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_4MA);
        gpio_set_dir(i, GPIO_OUT);
    }

    gpio_init(TFT_WR_PIN);
    gpio_set_drive_strength(TFT_WR_PIN, GPIO_DRIVE_STRENGTH_4MA);
    gpio_set_dir(TFT_WR_PIN, GPIO_OUT);


    pinMode(PIN_LED, OUTPUT);
}

void setup_pio(PIO pio, uint sm, uint offset) {
    // データピン (8ピン) を PIO に設定
    for (int i = TFT_DATA_BASE; i < TFT_DATA_BASE + 8; i++) {
        pio_gpio_init(pio, i);
    }
    // GP11 を PIO に設定
    pio_gpio_init(pio, TFT_WR_PIN);

    // ピン方向の設定
    pio_sm_set_consecutive_pindirs(pio, sm, TFT_DATA_BASE, 8, true); // データピン出力
    pio_sm_set_consecutive_pindirs(pio, sm, TFT_WR_PIN, 1, true);      // GP11も出力に設定

    // PIOの初期設定
    pio_sm_config config = parallel_out_program_get_default_config(offset);
    sm_config_set_out_pins(&config, TFT_DATA_BASE, 8); // データバスの出力ピン設定
    sm_config_set_set_pins(&config, TFT_WR_PIN, 1);      // GP11を `SET` 命令で操作対象に追加
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);

    //sm_config_set_clkdiv(&config, 200.0f); // クロック分周値

    // ステートマシンを初期化
    pio_sm_init(pio, sm, offset, &config);

    // ステートマシンを有効化
    pio_sm_set_enabled(pio, sm, true);
}

void setup_dma(PIO pio, uint sm) {
    dma_channel_config dma_config[2];
        /* データ転送DMA */
        dma_config[0] = dma_channel_get_default_config(dma_channel[0]);
        channel_config_set_transfer_data_size(&dma_config[0], DMA_SIZE_8);
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
            &pio->txf[sm],  // 転送先
            framebuffer,       // 転送元
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
    DBG(">PSRAM size:%d\n", aa);
    //stdio_init_all();
    setup_datagpio();
    // PIOとDMAの初期化
    PIO pio = pio0;
    uint sm = 0;
    dma_channel[0] = dma_claim_unused_channel(true);
    dma_channel[1] = dma_claim_unused_channel(true);
    uint offset = pio_add_program(pio, &parallel_out_program);

    setup_pio(pio, sm, offset);
    setup_dma(pio, sm);


    // フレームバッファ内容変更
    for (int i = 0; i < DATA_SIZE; i++) {
        framebuffer[i] = i;
    }

    // 最初のDMAチャンネルを起動
    dma_channel_start(dma_channel[0]);
 
    //dma_start_channel_mask((1u << dma_channel[0]));
    //delay(200);
    volatile bool flg = dma_hw->ch[dma_channel[0]].al1_ctrl & DMA_CH0_CTRL_TRIG_BUSY_BITS;
    //dma_channel_wait_for_finish_blocking(dma_channel);
    printf("%d",(const char*)flg);
}

void loop()
{
    static int a;

        if(++a == 2) {
            a = 0;
        }
        //digitalWrite(PIN_LED, a);
        if(a) {
            sio_hw->gpio_set = (1ul << 25) | 1;
        } else {
            sio_hw->gpio_clr = (1ul << 25) | 1;
        }

        sleep_ms(100); // 適当に遅延を入れてみる
}
