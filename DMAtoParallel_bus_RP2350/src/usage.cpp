///////////////////////////////////////////////////////////////////////////////////
// This programme is an 800x480 LCD driver (parallel bus) controlled by NT35510.
// Do not define the following #define on boards without PSRAM.
///////////////////////////////////////////////////////////////////////////////////
#include <Arduino.h>
#include "nt35510.hpp"
//#include <SPI.h>
#include "GT20L16J1Y.hpp"

#define GT20L16J1Y_RX (12)     // MISO
#define GT20L16J1Y_CS (13)
#define GT20L16J1Y_SCK (14)
#define GT20L16J1Y_TX (15)     // MOSI

GT20L16J1Y jis_rom(GT20L16J1Y_RX, GT20L16J1Y_TX, GT20L16J1Y_SCK, GT20L16J1Y_CS);
NT35510LCD lcd;


void setup_debug_serial_out(void) {
    #define DBG Serial1.printf
    Serial1.setRX(17u);
    Serial1.setTX(16u);
    Serial1.begin(9600);
}

///////// timer interrupt test
//#define ALARM_NUM 0
uint32_t timeout_us;
void repeatied_timer_us(uint32_t delay_us);
void timer_handler(void) {
    static int a;

    __freertos_task_enter_critical();
    hw_clear_bits(&timer_hw->intr, 1u << TIMER0_IRQ_0);
    // Alarm is only 32 bits so if trying to delay more
    // than that need to be careful and keep track of the upper
    // bits
    uint64_t target = timer_hw->timerawl + timeout_us;

    // Write the lower 32 bits of the target time to the alarm which
    // will arm it
    timer_hw->alarm[TIMER0_IRQ_0] = (uint32_t) target;

    if(++a == 2) {
        a = 0;
    }
    if(a) {
        sio_hw->gpio_set = (1ul << DEBUG_SIG) | 1;
    } else {
        sio_hw->gpio_clr = (1ul << DEBUG_SIG) | 1;
    }

    __freertos_task_exit_critical();

}

void repeatied_timer_us(uint32_t delay_us) {
    timeout_us = delay_us;
    // Enable the interrupt for our alarm (the timer outputs 4 alarm irqs)
    hw_set_bits(&timer_hw->inte, 1u << TIMER0_IRQ_0);
    // Set irq handler for alarm irq
    irq_set_exclusive_handler(TIMER0_IRQ_0, timer_handler);
    // Enable the alarm irq
    //irq_set_priority (TIMER0_IRQ_0, 0x50);
    irq_set_enabled(TIMER0_IRQ_0, true);
    // Enable interrupt in block and at processor

    // Alarm is only 32 bits so if trying to delay more
    // than that need to be careful and keep track of the upper
    // bits
    uint64_t target = timer_hw->timerawl + delay_us;

    // Write the lower 32 bits of the target time to the alarm which
    // will arm it
    timer_hw->alarm[TIMER0_IRQ_0] = (uint32_t) target;
}

//////////////////////////////


void setup() {
    volatile int psram_size;
    setup_debug_serial_out();

#if 1
    jis_rom.init();
#endif

    psram_size = rp2040.getPSRAMSize();
    DBG("PSRAM size:%d\n", psram_size);

    // load png to draw on chip map (PSRAM)
    lcd.pngle = pngle_new();
    pngle_set_draw_callback(lcd.pngle, lcd.pngle_on_draw);
    lcd.loadpng(pngdata);

    lcd.setup_gpio();

    // NT35510初期化
    lcd.init_LCD();    
    
    // PIOとDMAの初期化
    ////PIO pio = pio0;
    ////uint sm = 0;
    for(int i = 0; i < 7; i++) {
        dma_channel[i] = dma_claim_unused_channel(true);
    }
    uint offset = pio_add_program(lcd.pio, &parallel_out_program);

    lcd.setup_pio(lcd.pio, lcd.sm, offset);
    lcd.setup_dma(lcd.pio, lcd.sm);

    // 最初のDMAチャンネルを起動
    lcd.initframebuffers();
    sleep_us(1000);
    dma_channel_wait_for_finish_blocking(dma_channel[1]);
    dma_channel_start(dma_channel[0]);

////// timer test 22.6757us(44100Hz) when ESP32-YM2203C(VGM)
    DBG("%d\n", irq_get_priority(DMA_IRQ_0));
    repeatied_timer_us(22); //interval setting


    //sleep_ms(2500);
}

const uint16_t map_table[][2] = {
    //  Y         X
    { 7 * 24,  0 * 24},
    { 7 * 24,  1 * 24},
    { 7 * 24,  2 * 24},
    { 7 * 24,  3 * 24},
    { 7 * 24,  4 * 24},
    { 7 * 24,  5 * 24},
    { 7 * 24,  6 * 24},
    { 7 * 24,  7 * 24},
    { 7 * 24,  8 * 24},
    { 7 * 24,  9 * 24},
};

const uint8_t field_map[][32] = {
    {0x01, 0x02, 0x03, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x01, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x00, 0x01, 0x00, 0x01, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x02, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x02, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x09, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x09, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x01, 0x01, 0x01, 0x05,}, 
    {0x05, 0x04, 0x04, 0x01, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x09, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x09, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x09, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x09, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x09, 0x05, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x04, 0x04, 0x01, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x09, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x01, 0x01, 0x01, 0x05, 0x05, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x09, 0x05, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x09, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x02, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x02, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,}, 
    {0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x00, 0x01, 0x00, 0x01, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x01,}, 
};

void put_utf8(const char *str, uint16_t x, uint16_t y, uint16_t color, uint8_t scale, bool both) {
    uint8_t width;
    uint8_t ku;
    uint8_t ten;
    uint8_t ascii;
    uint8_t bytes;
    while(*str != '\0') {
        width = jis_rom.load_utf8_char((uint8_t *)str, &ku, &ten, &ascii, &bytes);
        switch(width) {
            case 8:
                lcd.put_char_scaled_line(x, y, &jis_rom.recv_buf[0], color, scale, width, both);
                x += width * scale;
                str++;
                break;
            case 16:
                lcd.put_char_scaled_line(x, y, &jis_rom.recv_buf[0], color, scale, width, both);
                x += width * scale;
                str += bytes;
                break;
            default:
                break;
        }
    }
}

bool loop_started = false;
void loop() {
    loop_started = true;
    static int a;

    // tick LED ctrl
    if(++a == 2) {
        a = 0;
    }
    if(a) {
        sio_hw->gpio_set = (1ul << 25) | 1;
    } else {
        sio_hw->gpio_clr = (1ul << 25) | 1;
    }

// drawing test
    static int cnt = 15;
    static int scroll_cnt = 0;
#if 0
    for(int i = 0; i < 8; i++) {
        lcd.line(384 + random(416), random(480), 384 + random(416), random(480), rgb(random(128), random(128), random(128)), true);
        lcd.box(384 + random(416), random(480), 384 + random(416), random(480), rgb(random(128), random(128), random(128)), true);
        lcd.circle(384 + random(416), random(480), random(480), rgb(random(128), random(128), random(128)), true);
    }
#endif

    static bool first_draw_done = false;

#if 1
    // draw flame
    lcd.boxfill(0, 0, 799,15, lcd.rgb(255, 0, 0), true);
    lcd.boxfill(0, 15, 15, 400, lcd.rgb(255, 0, 255), true);
    lcd.boxfill(784, 16, 799, 400, lcd.rgb(0, 0, 255), true);
    lcd.boxfill(0, 400, 799, 415, lcd.rgb(255, 255, 0), true);
#endif

#if 1
    for(int y = 0; y < 16; y++) {
        for(int x = 0; x < 32; x++) {
            uint16_t old_chip_map_y = abs(y + cnt + 1) % 16;
            uint16_t old_table_x = map_table[field_map[old_chip_map_y][x]][1];
            uint16_t old_table_y = map_table[field_map[old_chip_map_y][x]][0];
            uint16_t chip_map_y = abs(y + cnt) % 16;
            uint16_t table_x = map_table[field_map[chip_map_y][x]][1];
            uint16_t table_y = map_table[field_map[chip_map_y][x]][0];
            // Only actually draw if the chip about to be drawn is different than the chip
            // already in the drawing destination.
            if(!first_draw_done || (old_table_y != table_y || old_table_x != table_x)) {
                lcd.bitblt(chip_map, x * 24 + 16, y * 24 + 16, table_x, table_y, 24, 24, true);

    // パラレルバス共有お試し
                {

                }

            }
        }
    }
#endif
    // 漢字表示テスト
#if 1
    put_utf8("High-performance", 64, 128, lcd.rgb(255, 255, 255), 3, true);
    put_utf8("gaming machine with RP2350.", 64, 196, lcd.rgb(255, 255, 255), 3, true);
    put_utf8("魑魅魍魎が跳梁跋扈する。", 0, 416, lcd.rgb(0, 255, 0), 4, true);
#endif

    // 74HC595 test (no display due to RD control taken here.)
    // ★正常動作確認済み
    //shiftOut(9, 10, MSBFIRST, 0xaa);    // need to set CLK to L at the first time.
    //shiftOut(9, 10, MSBFIRST, 0xaa);    // need to set CLK to L at the first time.

    first_draw_done = true;
    cnt--; if(cnt < 0) {cnt = 15;}
    scroll_cnt += 4; if(scroll_cnt > 800) {scroll_cnt = 0;}

    lcd.swapbuffer();    
    //sleep_us(10); // 適当に遅延
}

void setup1(void) {
}

void loop1(void) {
    if(!loop_started) {
        return;
    }
// drawing test
    static int cnt = 0;
#if 0
    for(int i = 0; i < 8; i++) {
        lcd.line(384 + random(416), random(480), 384 + random(416), random(480), rgb(random(128), random(128), random(128)), true);
        lcd.box(384 + random(416), random(480), 384 + random(416), random(480), rgb(random(128), random(128), random(128)), true);
        lcd.circle(384 + random(416), random(480), random(480), rgb(random(128), random(128), random(128)), true);
    }
#endif

    static bool first_draw_done = false;
#if 0
    for(int y = 0; y < 16; y++) {
        for(int x = 0; x < 16; x++) {
            uint16_t old_chip_map_y = abs(y + cnt - 1) % 16;
            uint16_t old_table_x = map_table[field_map[old_chip_map_y][x]][1];
            uint16_t old_table_y = map_table[field_map[old_chip_map_y][x]][0];
            uint16_t chip_map_y = abs(y + cnt) % 16;
            uint16_t table_x = map_table[field_map[chip_map_y][x]][1];
            uint16_t table_y = map_table[field_map[chip_map_y][x]][0];
            // Only actually draw if the chip about to be drawn is different than the chip
            // already in the drawing destination.
            if(!first_draw_done || (old_table_y != table_y || old_table_x != table_x)) {
                lcd.bitblt(chip_map, 400 + y * 24 , x * 24, table_x, table_y, 24, 24, true);
                //lcd.sprite(400 + x * 24, y * 24, table_x, table_y, 24, 24, 0, true);
            }
        }
    }
    //put_utf8("Core 2", 450, 100, lcd.rgb(255, 255, 0), 6, true);
#endif

    first_draw_done = true;
    cnt++;if(cnt > 16){cnt = 0;}

}
