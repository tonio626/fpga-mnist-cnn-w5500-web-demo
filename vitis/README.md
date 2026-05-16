# Vitis Bare-Metal Firmware

The firmware runs on MicroBlaze and performs the following tasks:

1. Initializes UART, GPIO and SPI.
2. Resets and configures the external W5500 Ethernet module.
3. Opens a UDP socket on port 5000.
4. Receives `MNST` packets containing 14×14 image frames.
5. Writes the image into shared BRAM.
6. Starts the HLS CNN accelerator.
7. Reads the predicted digit.
8. Sends a `RSLT` UDP response back to the sender.

Source:

```text
vitis/src/app_main.c
```

UART debug commands:

```text
t = write pattern 0..195 to BRAM
r = dump first 8 BRAM words
R = dump all 49 BRAM words
g = run CNN on current BRAM
d = dump HLS debug regs
x = dump raw HLS AXI-Lite regs
```
