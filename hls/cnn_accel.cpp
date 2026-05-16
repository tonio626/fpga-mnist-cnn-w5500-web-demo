#include "cnn_accel.h"
#include "weights_fpga.h"
#include <stdint.h>

typedef ap_int<16> act_t;
typedef ap_int<32> acc_t;

static act_t relu(act_t x) {
    return (x > 0) ? x : (act_t)0;
}

static ap_uint<8> get_pixel_from_word(ap_uint<32> w, ap_uint<2> byte_idx) {
    switch (byte_idx) {
        case 0: return w.range(7, 0);
        case 1: return w.range(15, 8);
        case 2: return w.range(23, 16);
        default:return w.range(31, 24);
    }
}

void cnn_accel(
    volatile ap_uint<32> img_words[IMG_WORDS],
    ap_uint<32> *predicted_digit,
    ap_uint<32> *dbg_p0,
    ap_uint<32> *dbg_p1,
    ap_uint<32> *dbg_p2,
    ap_uint<32> *dbg_p3,
    ap_uint<32> *dbg_p4,
    ap_uint<32> *dbg_p5,
    ap_uint<32> *dbg_p6,
    ap_uint<32> *dbg_p7,
    ap_uint<32> *dbg_sum8
) {
#pragma HLS INTERFACE ap_memory port=img_words
#pragma HLS INTERFACE s_axilite port=predicted_digit bundle=CTRL
#pragma HLS INTERFACE s_axilite port=dbg_p0 bundle=CTRL
#pragma HLS INTERFACE s_axilite port=dbg_p1 bundle=CTRL
#pragma HLS INTERFACE s_axilite port=dbg_p2 bundle=CTRL
#pragma HLS INTERFACE s_axilite port=dbg_p3 bundle=CTRL
#pragma HLS INTERFACE s_axilite port=dbg_p4 bundle=CTRL
#pragma HLS INTERFACE s_axilite port=dbg_p5 bundle=CTRL
#pragma HLS INTERFACE s_axilite port=dbg_p6 bundle=CTRL
#pragma HLS INTERFACE s_axilite port=dbg_p7 bundle=CTRL
#pragma HLS INTERFACE s_axilite port=dbg_sum8 bundle=CTRL
#pragma HLS INTERFACE s_axilite port=return bundle=CTRL

    act_t input[IMG_H][IMG_W];
    act_t conv1_out[CONV1_OUT_CH][CONV1_H][CONV1_W];
    act_t pool1_out[CONV1_OUT_CH][POOL1_H][POOL1_W];
    act_t conv2_out[CONV2_OUT_CH][CONV2_H][CONV2_W];
    act_t pool2_out[CONV2_OUT_CH][POOL2_H][POOL2_W];
    act_t flat[FLAT_SIZE];
    act_t fc1_out[FC1_SIZE];
    acc_t fc2_out[FC2_SIZE];

    ap_uint<32> dbg_words[8];

#pragma HLS ARRAY_PARTITION variable=flat complete dim=1
#pragma HLS ARRAY_PARTITION variable=fc1_out complete dim=1
#pragma HLS ARRAY_PARTITION variable=fc2_out complete dim=1
#pragma HLS ARRAY_PARTITION variable=dbg_words complete dim=1

    for (int i = 0; i < 8; i++) {
#pragma HLS UNROLL
        dbg_words[i] = 0;
    }

    // =====================================================
    // LOAD INPUT
    // Load input image from 32-bit words.
    // Ogni word contiene 4 pixel:
    // byte 0 -> pixel più basso
    // byte 1 -> secondo pixel
    // byte 2 -> terzo pixel
    // byte 3 -> quarto pixel
    // =====================================================
    LOAD_INPUT:
    for (int r = 0; r < IMG_H; r++) {
        for (int c = 0; c < IMG_W; c++) {
#pragma HLS PIPELINE II=1
            int pix_idx = r * IMG_W + c;
            int word_idx = pix_idx >> 2;
            ap_uint<2> byte_idx = pix_idx & 0x3;

            ap_uint<32> w = img_words[word_idx];
            ap_uint<8> pix = get_pixel_from_word(w, byte_idx);

            input[r][c] = (act_t)pix;

            // Salva le prime 8 word raw per debug.
            if ((word_idx < 8) && (byte_idx == 0)) {
                dbg_words[word_idx] = w;
            }
        }
    }

    // =====================================================
    // CONV1
    // Input:  14x14x1
    // Output: 14x14x4
    // Padding: same
    // Layout pesi TensorFlow: conv1_w[KH][KW][Cin][Cout]
    // =====================================================
    CONV1_OC:
    for (int oc = 0; oc < CONV1_OUT_CH; oc++) {
        CONV1_R:
        for (int r = 0; r < CONV1_H; r++) {
            CONV1_C:
            for (int c = 0; c < CONV1_W; c++) {
#pragma HLS PIPELINE II=1
                acc_t acc = (acc_t)conv1_b[oc];

                for (int kr = 0; kr < 3; kr++) {
                    for (int kc = 0; kc < 3; kc++) {
                        int in_r = r + kr - 1;
                        int in_c = c + kc - 1;

                        if (in_r >= 0 && in_r < IMG_H && in_c >= 0 && in_c < IMG_W) {
                            acc += (acc_t)input[in_r][in_c] * (acc_t)conv1_w[kr][kc][0][oc];
                        }
                    }
                }

                act_t y = (act_t)(acc >> WEIGHT_SHIFT);
                conv1_out[oc][r][c] = relu(y);
            }
        }
    }

    // =====================================================
    // POOL1
    // Input:  14x14x4
    // Output: 7x7x4
    // MaxPool 2x2, stride 2
    // =====================================================
    POOL1_OC:
    for (int oc = 0; oc < CONV1_OUT_CH; oc++) {
        POOL1_R:
        for (int r = 0; r < POOL1_H; r++) {
            POOL1_C:
            for (int c = 0; c < POOL1_W; c++) {
#pragma HLS PIPELINE II=1
                act_t m = conv1_out[oc][2*r][2*c];

                if (conv1_out[oc][2*r][2*c + 1] > m)
                    m = conv1_out[oc][2*r][2*c + 1];

                if (conv1_out[oc][2*r + 1][2*c] > m)
                    m = conv1_out[oc][2*r + 1][2*c];

                if (conv1_out[oc][2*r + 1][2*c + 1] > m)
                    m = conv1_out[oc][2*r + 1][2*c + 1];

                pool1_out[oc][r][c] = m;
            }
        }
    }

    // =====================================================
    // CONV2
    // Input:  7x7x4
    // Output: 7x7x8
    // Padding: same
    // Layout pesi TensorFlow: conv2_w[KH][KW][Cin][Cout]
    // =====================================================
    CONV2_OC:
    for (int oc = 0; oc < CONV2_OUT_CH; oc++) {
        CONV2_R:
        for (int r = 0; r < CONV2_H; r++) {
            CONV2_C:
            for (int c = 0; c < CONV2_W; c++) {
#pragma HLS PIPELINE II=1
                acc_t acc = (acc_t)conv2_b[oc];

                for (int ic = 0; ic < CONV1_OUT_CH; ic++) {
                    for (int kr = 0; kr < 3; kr++) {
                        for (int kc = 0; kc < 3; kc++) {
                            int in_r = r + kr - 1;
                            int in_c = c + kc - 1;

                            if (in_r >= 0 && in_r < POOL1_H && in_c >= 0 && in_c < POOL1_W) {
                                acc += (acc_t)pool1_out[ic][in_r][in_c] * (acc_t)conv2_w[kr][kc][ic][oc];
                            }
                        }
                    }
                }

                act_t y = (act_t)(acc >> WEIGHT_SHIFT);
                conv2_out[oc][r][c] = relu(y);
            }
        }
    }

    // =====================================================
    // POOL2
    // Input:  7x7x8
    // Output: 3x3x8
    // MaxPool 2x2, stride 2, valid
    // Coerente con Keras MaxPooling2D(pool_size=(2,2))
    // su input 7x7: floor(7/2) = 3
    // =====================================================
    POOL2_OC:
    for (int oc = 0; oc < CONV2_OUT_CH; oc++) {
        POOL2_R:
        for (int r = 0; r < POOL2_H; r++) {
            POOL2_C:
            for (int c = 0; c < POOL2_W; c++) {
#pragma HLS PIPELINE II=1
                act_t m = conv2_out[oc][2*r][2*c];

                if (conv2_out[oc][2*r][2*c + 1] > m)
                    m = conv2_out[oc][2*r][2*c + 1];

                if (conv2_out[oc][2*r + 1][2*c] > m)
                    m = conv2_out[oc][2*r + 1][2*c];

                if (conv2_out[oc][2*r + 1][2*c + 1] > m)
                    m = conv2_out[oc][2*r + 1][2*c + 1];

                pool2_out[oc][r][c] = m;
            }
        }
    }

    // =====================================================
    // FLATTEN
    // IMPORTANTISSIMO:
    // Keras/TensorFlow usa channels_last.
    //
    // Dopo POOL2 la feature map equivalente è:
    //     [POOL2_H][POOL2_W][CONV2_OUT_CH] = [3][3][8]
    //
    // Flatten di Keras appiattisce in ordine:
    //     r -> c -> channel
    //
    // Quindi qui deve essere:
    //     for r
    //       for c
    //         for oc
    //
    // NON:
    //     for oc
    //       for r
    //         for c
    // =====================================================
    int idx = 0;

    FLATTEN_R:
    for (int r = 0; r < POOL2_H; r++) {
        FLATTEN_C:
        for (int c = 0; c < POOL2_W; c++) {
            FLATTEN_OC:
            for (int oc = 0; oc < CONV2_OUT_CH; oc++) {
#pragma HLS PIPELINE II=1
                flat[idx++] = pool2_out[oc][r][c];
            }
        }
    }

    // =====================================================
    // FC1
    // TensorFlow Dense layout: fc1_w[IN][OUT]
    // =====================================================
    FC1_O:
    for (int o = 0; o < FC1_SIZE; o++) {
#pragma HLS PIPELINE II=1
        acc_t acc = (acc_t)fc1_b[o];

        for (int i = 0; i < FLAT_SIZE; i++) {
            acc += (acc_t)flat[i] * (acc_t)fc1_w[i][o];
        }

        fc1_out[o] = relu((act_t)(acc >> WEIGHT_SHIFT));
    }

    // =====================================================
    // FC2
    // TensorFlow Dense layout: fc2_w[IN][OUT]
    // =====================================================
    ap_uint<32> best_idx = 0;
    acc_t best_val = 0;

    FC2_O:
    for (int o = 0; o < FC2_SIZE; o++) {
#pragma HLS PIPELINE II=1
        acc_t acc = (acc_t)fc2_b[o];

        for (int i = 0; i < FC1_SIZE; i++) {
            acc += (acc_t)fc1_out[i] * (acc_t)fc2_w[i][o];
        }

        fc2_out[o] = (acc >> WEIGHT_SHIFT);

        if (o == 0 || fc2_out[o] > best_val) {
            best_val = fc2_out[o];
            best_idx = o;
        }
    }

    // =====================================================
    // DEBUG OUTPUTS
    // Le uscite dbg_p0..dbg_p7 devono contenere le prime
    // 8 word raw lette dalla BRAM.
    //
    // Dopo aver aggiornato davvero IP + bitstream, se in
    // MicroBlaze scrivi:
    //     W00=0x03020100
    //     W01=0x07060504
    // allora devi leggere:
    //     P0=0x03020100
    //     P1=0x07060504
    // =====================================================
    *dbg_p0   = dbg_words[0];
    *dbg_p1   = dbg_words[1];
    *dbg_p2   = dbg_words[2];
    *dbg_p3   = dbg_words[3];
    *dbg_p4   = dbg_words[4];
    *dbg_p5   = dbg_words[5];
    *dbg_p6   = dbg_words[6];
    *dbg_p7   = dbg_words[7];
    *dbg_sum8 = 0;

    *predicted_digit = best_idx;
}