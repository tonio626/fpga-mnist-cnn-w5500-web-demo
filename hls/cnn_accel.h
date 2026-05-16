#ifndef CNN_ACCEL_H
#define CNN_ACCEL_H

#include <ap_int.h>

#define IMG_H 14
#define IMG_W 14
#define IMG_SIZE 196
#define IMG_WORDS 49

#define CONV1_OUT_CH 4
#define CONV1_H 14
#define CONV1_W 14

#define POOL1_H 7
#define POOL1_W 7

#define CONV2_OUT_CH 8
#define CONV2_H 7
#define CONV2_W 7

#define POOL2_H 3
#define POOL2_W 3

#define FLAT_SIZE 72
#define FC1_SIZE 16
#define FC2_SIZE 10

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
);

#endif