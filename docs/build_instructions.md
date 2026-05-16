# Build Instructions

Toolchain version:

```text
Vivado 2024.1
Vitis 2024.1
Vitis HLS 2024.1
```

## 1. Open the Vivado Project

Open:

```text
vivado/project/CNN_on_FPGA_pj/CNN_on_FPGA_pj.xpr
```

Then open the Block Design:

```text
CNN_on_FPGA_BD
```

Run:

```text
Validate Design
Generate Output Products
Generate Bitstream
```

## 2. Export Hardware

After bitstream generation:

```text
File → Export → Export Hardware
```

Enable:

```text
Include bitstream
```

This produces the `.xsa` file.

## 3. Update Vitis Platform

In Vitis 2024.1:

1. Import or update the platform from the generated `.xsa`.
2. Build the platform.
3. Build the bare-metal application in `vitis/src/app_main.c`.
4. Program the FPGA.
5. Launch the application on hardware.

## 4. Run the Web Demo

```bash
cd web_app
python3 -m pip install -r requirements.txt

python3 app.py \
  --fpga-ip 192.168.8.50 \
  --fpga-port 5000 \
  --host 127.0.0.1 \
  --port 8000
```

Open:

```text
http://127.0.0.1:8000
```
