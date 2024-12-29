#pragma once

// 画像サイズ、ビット深度、インターレースなど
typedef struct _ihdr {
    uint32_t length;                // big endian
    uint8_t type[4];
    struct _data {
        uint32_t width;
        uint32_t height;
        uint8_t bit_depth;          // 1, 2, 4, [8], [16]
        uint8_t color_type;         // 0, [2:RGB], 3, 4, [6:RGB+a]
        uint8_t compression_type;
        uint8_t filter_type;
        uint8_t interlace_type;
    } data;
    uint32_t crc;                   // CRC-32 of data
} IHDR;

// フィルター種別
enum {
    None = 0,   // フィルタなし
    Sub,        // 左隣のピクセル値との差分
    Up,         // 上隣のピクセル値との差分
    Average,    // 左隣と上隣のピクセル値の平均との差分
    Peath,      // Peath アルゴリズムによって左・上・左上の中から選択されたピクセルの値との差分
} IDAT_FILTER;

typedef struct _idat {
    uint32_t length;
    uint8_t type[4];    // IDAT
    uint8_t *p_data;
    uint32_t crc;
} IDAT;

typedef struct _iend {
    uint32_t length;    // 0
    uint8_t type[4];    // IEND
    uint32_t crc;
} IEND;

typedef struct _png {
    uint8_t signature[8];
    IHDR chunk_ihdr;
    void *p_chunks_aux;
    void *p_chunks_IDAT;
    IEND chunk_iend;
} PNG;

