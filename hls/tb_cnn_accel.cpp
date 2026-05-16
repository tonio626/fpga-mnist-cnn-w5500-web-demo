#include "cnn_accel.h"
#include <iostream>

int main() {
    ap_uint<32> img_words[IMG_WORDS];

    ap_uint<32> pred = 0;

    ap_uint<32> dbg_p0 = 0;
    ap_uint<32> dbg_p1 = 0;
    ap_uint<32> dbg_p2 = 0;
    ap_uint<32> dbg_p3 = 0;
    ap_uint<32> dbg_p4 = 0;
    ap_uint<32> dbg_p5 = 0;
    ap_uint<32> dbg_p6 = 0;
    ap_uint<32> dbg_p7 = 0;
    ap_uint<32> dbg_sum8 = 0;

    for (int i = 0; i < IMG_WORDS; i++) {
        img_words[i] = 0;
    }

    // Pattern semplice nei primi 8 word.
    // Serve per verificare che l'HLS legga davvero la BRAM
    // e che i registri debug riportino le prime 8 word raw.
    img_words[0] = 0x03020100;
    img_words[1] = 0x07060504;
    img_words[2] = 0x0B0A0908;
    img_words[3] = 0x0F0E0D0C;
    img_words[4] = 0x13121110;
    img_words[5] = 0x17161514;
    img_words[6] = 0x1B1A1918;
    img_words[7] = 0x1F1E1D1C;

    cnn_accel(
        img_words,
        &pred,
        &dbg_p0,
        &dbg_p1,
        &dbg_p2,
        &dbg_p3,
        &dbg_p4,
        &dbg_p5,
        &dbg_p6,
        &dbg_p7,
        &dbg_sum8
    );

    std::cout << "Predicted digit = " << (unsigned)pred << std::endl;

    std::cout << std::hex;
    std::cout << "DBG_P0 = 0x" << (unsigned)dbg_p0 << std::endl;
    std::cout << "DBG_P1 = 0x" << (unsigned)dbg_p1 << std::endl;
    std::cout << "DBG_P2 = 0x" << (unsigned)dbg_p2 << std::endl;
    std::cout << "DBG_P3 = 0x" << (unsigned)dbg_p3 << std::endl;
    std::cout << "DBG_P4 = 0x" << (unsigned)dbg_p4 << std::endl;
    std::cout << "DBG_P5 = 0x" << (unsigned)dbg_p5 << std::endl;
    std::cout << "DBG_P6 = 0x" << (unsigned)dbg_p6 << std::endl;
    std::cout << "DBG_P7 = 0x" << (unsigned)dbg_p7 << std::endl;
    std::cout << "DBG_SUM8 = 0x" << (unsigned)dbg_sum8 << std::endl;
    std::cout << std::dec;

    int errors = 0;

    if (dbg_p0 != img_words[0]) errors++;
    if (dbg_p1 != img_words[1]) errors++;
    if (dbg_p2 != img_words[2]) errors++;
    if (dbg_p3 != img_words[3]) errors++;
    if (dbg_p4 != img_words[4]) errors++;
    if (dbg_p5 != img_words[5]) errors++;
    if (dbg_p6 != img_words[6]) errors++;
    if (dbg_p7 != img_words[7]) errors++;

    if (errors == 0) {
        std::cout << "TB_DEBUG_WORDS_OK" << std::endl;
        return 0;
    } else {
        std::cout << "TB_DEBUG_WORDS_FAIL errors=" << errors << std::endl;
        return 1;
    }
}