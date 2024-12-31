///////////////////////////////////////////////////////////////////////////////////
// This programme is an 800x480 LCD driver (parallel bus) controlled by NT35510.
// Do not define the following #define on boards without PSRAM.
///////////////////////////////////////////////////////////////////////////////////
#include <Arduino.h>
#include "nt35510.hpp"
class NT35510LCD lcd;

void setup_debug_serial_out(void) {
    #define DBG Serial1.printf
    Serial1.setRX(17u);
    Serial1.setTX(16u);
    Serial1.begin(9600);
}

inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3);
}

void setup()
{
    volatile int psram_size;
    setup_debug_serial_out();
    
    psram_size = rp2040.getPSRAMSize();
    DBG("PSRAM size:%d\n", psram_size);

    // load png to draw on chip map (PSRAM)
    lcd.pngle = pngle_new();
    pngle_set_draw_callback(lcd.pngle, lcd.pngle_on_draw);
    lcd.loadpng(pngdata);

    lcd.setup_gpio();
    // reset DEBUG_SIG
    lcd.dma_irq_handler();

    // NT35510初期化
    lcd.init_LCD();    
    
    // PIOとDMAの初期化
    PIO pio = pio0;
    uint sm = 0;
    dma_channel[0] = dma_claim_unused_channel(true);
    dma_channel[1] = dma_claim_unused_channel(true);
    dma_channel[2] = dma_claim_unused_channel(true);
    dma_channel[3] = dma_claim_unused_channel(true);
    uint offset = pio_add_program(pio, &parallel_out_program);

    lcd.setup_pio(pio, sm, offset);
    lcd.setup_dma(pio, sm);

    // 最初のDMAチャンネルを起動
    lcd.initframebuffers();
    sleep_us(2000);
    dma_channel_wait_for_finish_blocking(dma_channel[1]);
    dma_channel_start(dma_channel[0]);
    
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
    #if defined(PIMORONI_PICO_PLUS_2)
    //for(int i = 0; i < SCREEN_SIZE; i++) {
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
        lcd.sprite(random(800)/24*24, random(480)/24*24, random(10)*24, random(10)*24, 24, 24, rgb(255, 255, 255), true);
        // スクロール単位分のライン数で転送するように変えること
        lcd.line(random(800), random(480), random(800), random(480), rgb(random(128), random(128), random(128)), true);
        lcd.box(random(800), random(480), random(800), random(480), rgb(random(128), random(128), random(128)), true);
        lcd.circle(random(800), random(480), random(480), rgb(random(128), random(128), random(128)), true);
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

    lcd.swapbuffer();
    
    sleep_us(10); // 適当に遅延
}

void setup1(void) {
    ;
}

void loop1(void) {
    ;
}
