# Vitis HLS CNN Accelerator

Design files:

```text
cnn_accel.cpp
cnn_accel.h
weights_fpga.h
```

Testbench:

```text
tb_cnn_accel.cpp
```

Top function:

```text
cnn_accel
```

Target part for Cmod A7-35T:

```text
xc7a35tcpg236-1
```

The accelerator receives 49 packed 32-bit words corresponding to a 14×14 grayscale image.

Important detail: the flatten stage must follow Keras/TensorFlow `channels_last` order:

```text
row → column → channel
```

Using `channel → row → column` breaks the Dense layer input order and leads to wrong predictions.
