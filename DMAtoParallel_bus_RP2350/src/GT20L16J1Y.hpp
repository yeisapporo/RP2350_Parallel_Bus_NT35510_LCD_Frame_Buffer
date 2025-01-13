#pragma once
#include <SPI.h>
#include "utf16_kuten.h"

// mode 0: positive pulse, front latch, back shift
SPISettings spisettings(300000, MSBFIRST, SPI_MODE0);

typedef struct _Mapping {
        uint16_t utf16;  // UTF-16コードポイント
        uint16_t shift_jis; // Shift JISコードポイント
} MAPPING;

// ★要作成マッピングテーブル★
constexpr MAPPING utf16_to_shift_jis_map[] = {
    {0x0041, 0x8260}, // 'A'
    {0x0042, 0x8261}, // 'B'
    {0x3042, 0x82A0}, // 'あ'
    {0x3044, 0x82A2}, // 'い'
    {0x3046, 0x82A4}, // 'う'
    {0x4E00, 0x88EA} // '一'
    // 必要に応じて追加
};

size_t map_size = sizeof(utf16_to_shift_jis_map) / sizeof(MAPPING);

class GT20L16J1Y {
public:
    // use as 4 byte variable.
    #pragma pack (1)
    union {
        struct {
            uint8_t cmd;
            uint32_t addr;
        };
        uint8_t uint8[4];
    } send_buf;
    #pragma unpack
    uint8_t recv_buf[32];

    SPIClassRP2040 *p_spi = &SPI1;
    pin_size_t _rx;
    pin_size_t _tx;
    pin_size_t _sck;
    pin_size_t _cs;

    GT20L16J1Y(pin_size_t rx, pin_size_t tx, pin_size_t sck, pin_size_t cs) : _rx(rx), _tx(tx), _sck(sck), _cs(cs) {}

    void init(void) {
        p_spi->setRX(_rx); // MISO
        p_spi->setTX(_tx); // MOSI
        p_spi->setSCK(_sck);
        p_spi->setCS(_cs);
        p_spi->begin(false);
        pinMode(_cs, OUTPUT); digitalWrite(_cs, HIGH);
    }

    inline uint32_t swap_endian_32(uint32_t x) {
        return (((x & 0x000000FF) << 24) | 
                ((x & 0x0000FF00) << 8)  | 
                ((x & 0x00FF0000) >> 8)  | 
                ((x & 0xFF000000) >> 24));
    }

#if 0
    int utf8_to_utf16(const uint8_t *utf8_data, size_t utf8_len, uint16_t *utf16_data, size_t utf16_max_len) {
        size_t utf16_index = 0;

        for (size_t i = 0; i < utf8_len; ) {
            uint32_t code_point = 0;

            // 1バイト目を解析
            uint8_t byte1 = utf8_data[i];
            if (byte1 <= 0x7F) {
                // 1バイト文字 (ASCII)
                code_point = byte1;
                i += 1;
            } else if ((byte1 & 0xE0) == 0xC0) {
                // 2バイト文字
                if (i + 1 >= utf8_len) return -1; // 不完全なシーケンス
                uint8_t byte2 = utf8_data[i + 1];
                if ((byte2 & 0xC0) != 0x80) return -1; // 無効なバイト

                code_point = ((byte1 & 0x1F) << 6) | (byte2 & 0x3F);
                i += 2;
            } else if ((byte1 & 0xF0) == 0xE0) {
                // 3バイト文字
                if (i + 2 >= utf8_len) return -1; // 不完全なシーケンス
                uint8_t byte2 = utf8_data[i + 1];
                uint8_t byte3 = utf8_data[i + 2];
                if ((byte2 & 0xC0) != 0x80 || (byte3 & 0xC0) != 0x80) return -1; // 無効なバイト

                code_point = ((byte1 & 0x0F) << 12) | ((byte2 & 0x3F) << 6) | (byte3 & 0x3F);
                i += 3;
            } else if ((byte1 & 0xF8) == 0xF0) {
                // 4バイト文字
                if (i + 3 >= utf8_len) return -1; // 不完全なシーケンス
                uint8_t byte2 = utf8_data[i + 1];
                uint8_t byte3 = utf8_data[i + 2];
                uint8_t byte4 = utf8_data[i + 3];
                if ((byte2 & 0xC0) != 0x80 || (byte3 & 0xC0) != 0x80 || (byte4 & 0xC0) != 0x80) return -1; // 無効なバイト

                code_point = ((byte1 & 0x07) << 18) | ((byte2 & 0x3F) << 12) | ((byte3 & 0x3F) << 6) | (byte4 & 0x3F);
                i += 4;
            } else {
                // 無効なUTF-8バイト
                return -1;
            }

            // コードポイントをUTF-16に変換
            if (code_point <= 0xFFFF) {
                // BMP (Basic Multilingual Plane)
                if (utf16_index >= utf16_max_len) return -2; // バッファ不足
                utf16_data[utf16_index++] = (uint16_t)code_point;
            } else if (code_point <= 0x10FFFF) {
                // サロゲートペア
                if (utf16_index + 2 > utf16_max_len) return -2; // バッファ不足
                code_point -= 0x10000;
                utf16_data[utf16_index++] = (uint16_t)((code_point >> 10) + 0xD800); // 上位サロゲート
                utf16_data[utf16_index++] = (uint16_t)((code_point & 0x3FF) + 0xDC00); // 下位サロゲート
            } else {
                // 無効なコードポイント
                return -1;
            }
        }

        return utf16_index; // 変換後のUTF-16データのサイズを返す
    }
#endif

#if 0
    int utf16_to_shift_jis(uint16_t *utf16_data, size_t utf16_len, uint8_t *shift_jis_data, size_t max_len) {
        size_t out_index = 0;

        for (size_t i = 0; i < utf16_len; i++) {
            uint16_t utf16_char = utf16_data[i];
            int found = 0;

            for (size_t j = 0; j < map_size; j++) {
                if (utf16_to_shift_jis_map[j].utf16 == utf16_char) {
                    uint16_t sjis_char = utf16_to_shift_jis_map[j].shift_jis;

                    if (sjis_char < 0x100) {
                        if (out_index + 1 > max_len) return -1;
                        shift_jis_data[out_index++] = (uint8_t)sjis_char;
                    } else {
                        if (out_index + 2 > max_len) return -1;
                        shift_jis_data[out_index++] = (uint8_t)(sjis_char >> 8); // h
                        shift_jis_data[out_index++] = (uint8_t)(sjis_char & 0xFF); // l
                    }
                    found = 1;
                    break;
                }
            }

            if (!found) {
                return -2; // マッピングなし
            }
        }

        return out_index; // 変換後のバイト数
    }
#endif

    uint16_t jis2kuten(uint16_t jis) {
        return jis - 0x2020;
    }

    uint16_t kuten2jis(uint16_t kuten) {
        return kuten + 0x2020;
    }

#if 0
    uint16_t jis2sjis(uint16_t jis) {
        uint16_t h = jis >> 8;
        uint16_t l = jis & 0xff;
    
        if (h & 1) {
            if (l < 0x60) {
                l += 0x1f;
            } else {
                l += 0x20;
            }
        } else {
            l += 0x7e;
        }

        if (h < 0x5f) {
            h = (h + 0xe1) >> 1;
        }else{
            h = (h + 0x161) >> 1;
        }

        return h << 8 | l;
    }
#endif

#if 0
    uint16_t sjis2jis(uint16_t sjis) {
        uint16_t h = sjis >> 8;
        uint16_t l = sjis & 0xff;

        if (h <= 0x9f) {
            if (l < 0x9f) {
                h = (h << 1) - 0xe1;
            } else {
                h = (h << 1) - 0xe0;
            }
        } else {
            if (l < 0x9f) {
                h = (h << 1) - 0x161;
            } else {
                h = (h << 1) - 0x160;
            }
        }
    
        if (l < 0x7f) {
            l -= 0x1f;
        } else if (l < 0x9f) {
            l -= 0x20;
        } else {
            l -= 0x7e;
        }

        return h << 8 | l;
    }
#endif

    /* ★ referred to https://qiita.com/benikabocha/items/e943deb299d0f816f161 */
    /* ★ */
    int count_utf8_bytes(uint8_t c) {
        if(0x00 <= c && c < 0x80) {
            return 1;
        } else if(0xc2 <= c && c < 0xe0) {
            return 2;
        } else if(0xe0 <= c && c < 0xf0) {
            return 3;
        } else if(0xf0 <= c && c < 0xf8) {
            return 4;
        }
        return 0;
    }

    /* ★ */
    bool is_trail_byte(uint8_t c) {
        return (0x80 <= c && c < 0xc0);
    }

    /* ★ */
    bool utf8_to_utf32(uint8_t *u8c, uint32_t *u32c /* out */) {
        int cnt = count_utf8_bytes(u8c[0]);

        switch(cnt) {
            case 0:
                return false;
                break;
            case 1:
                *u32c = (uint32_t)u8c[0];
                break;
            case 2:
                if(!is_trail_byte(u8c[1])) {
                    return false;
                } else if(u8c[0] & 0x1e == 0) {
                    return false;
                }
                *u32c = (uint32_t)(u8c[0] & 0x1f) << 6
                      | (uint32_t)(u8c[1] & 0x3f);
                break;
            case 3:
                if(!is_trail_byte(u8c[1]) || !is_trail_byte(u8c[2])) {
                    return false;
                } else if((u8c[0] & 0x0f) == 0 && (u8c[1] & 0x20) == 0) {
                    return false;
                }
                *u32c = (uint32_t)(u8c[0] & 0x0f) << 12
                      | (uint32_t)(u8c[1] & 0x3f) << 6
                      | (uint32_t)(u8c[2] & 0x3f);
                break;
            case 4:
                if(!is_trail_byte(u8c[1]) || !is_trail_byte(u8c[2]) || !is_trail_byte(u8c[3])) {
                    return false;
                } else if((u8c[0] & 0x07) == 0 && (u8c[1] & 0x30) == 0) {
                    return false;
                }
                *u32c = (uint32_t)(u8c[0] & 0x07) << 18
                      | (uint32_t)(u8c[1] & 0x3F) << 12
                      | (uint32_t)(u8c[2] & 0x3F) << 6
                      | (uint32_t)(u8c[3] & 0x3F);
                break;
            default:
                return false;
        }
        return true; /* success */
    }    

    /* ★ */
    bool utf32_to_uft16(uint32_t u32c, uint16_t *u16c /* out */) {
        if(u32c < 0 || u32c > 0x10ffff) {
            return false;
        } else if(u32c < 0x10000) {
            u16c[0] = (uint16_t)(u32c);
            u16c[1] = 0;
        } else {
            u16c[0] = (uint16_t)((u32c - 0x10000) / 0x400 + 0xd800);
            u16c[1] = (uint16_t)((u32c - 0x10000) % 0x400 + 0xdc00);
        }
        return true;
    }

    const size_t utf16_kuten_table_size = sizeof(utf16_kuten_table) / sizeof(utf16_kuten_table[0]);
    bool get_kuten(uint16_t utf16, uint8_t *ku, uint8_t *ten) {
        size_t left = 0;
        size_t right = utf16_kuten_table_size - 1;

        // 二分探索
        while (left <= right) {
            size_t mid = (left + right) / 2;
            uint16_t mid_value = utf16_kuten_table[mid][0];

            if (utf16 == mid_value) {
                // 一致する値を見つけた場合
                *ku = (uint8_t)utf16_kuten_table[mid][1];
                *ten = (uint8_t)utf16_kuten_table[mid][2];
                return true;
            } else if (utf16 < mid_value) {
                // 左側を探索
                right = mid - 1;
            } else {
                // 右側を探索
                left = mid + 1;
            }
        }

        // 見つからなかった場合
        return false;
    }

    uint16_t utf8_to_kuten(uint8_t *u8c, uint8_t *ku, uint8_t *ten) {
        uint32_t utf32_char;
        uint32_t utf16_char;

        utf8_to_utf32(u8c, &utf32_char);
        utf32_to_uft16(utf32_char, (uint16_t *)&utf16_char);        
        // テーブルを引いて句点を求める(2分探索など)。
        return get_kuten(utf16_char, ku, ten);
    }

    uint32_t kuten_rom_addr(uint8_t ku, uint8_t ten) {
        uint32_t ret = 0;
        if(ku >=1 && ku <= 15 && ten >=1 && ten <= 94) {
            ret =((ku - 1) * 94 + (ten - 1)) * 32;
        } else if(ku >=16 && ku <= 47 && ten >=1 && ten <= 94) {
            ret =((ku - 16) * 94 + (ten - 1)) * 32 + 43584;
        } else if(ku >=48 && ku <=84 && ten >=1 && ten <= 94) {
            ret = ((ku - 48) * 94 + (ten - 1)) * 32+ 138464;
        } else if(ku == 85 && ten >= 1 && ten <= 94) {
            ret = ((ku - 85) * 94 + (ten - 1)) * 32 + 246944;
        } else if(ku >=88 && ku <=89 && ten >=1 && ten <= 94) {
            ret = ((ku - 88) * 94 + (ten - 1)) * 32 + 249952;
        }
        return ret;
    }

    void load_by_kuten(uint8_t ku, uint8_t ten) {
        this->send_buf.cmd = 0x03;
        this->send_buf.addr = this->swap_endian_32(this->kuten_rom_addr(ku, ten)) >> 8;

        this->p_spi->beginTransaction(spisettings);
        digitalWrite(this->_cs, LOW);
        this->p_spi->transfer(this->send_buf.uint8, nullptr, sizeof(uint32_t));
        this->p_spi->transfer(nullptr, this->recv_buf, sizeof(this->recv_buf));
        digitalWrite(this->_cs, HIGH);
        this->p_spi->endTransaction();
    }



};
