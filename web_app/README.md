# Local Web Demo

This Flask application provides a local browser interface for drawing digits and sending them to the FPGA over UDP.

## Install

```bash
python3 -m pip install -r requirements.txt
```

## Run

```bash
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

## Notes

The browser cannot directly send UDP packets. The Flask backend receives the canvas image through HTTP, preprocesses it into a 14×14 frame, sends it to the FPGA through UDP, waits for the `RSLT` response packet and returns the prediction to the browser.
