#!/usr/bin/env python3
"""
Local web demo for FPGA MNIST CNN over UDP.

Browser canvas -> Flask backend -> UDP MNST packet -> FPGA/W5500 -> UDP RSLT reply.

Install:
    python3 -m pip install flask pillow

Run:
    python3 app.py --fpga-ip 192.168.8.50 --fpga-port 5000 --host 127.0.0.1 --port 8000

Open:
    http://127.0.0.1:8000
"""

import argparse
import base64
import io
import socket
import struct
import threading
from typing import Tuple

from flask import Flask, jsonify, request, render_template_string
from PIL import Image, ImageOps

IMG_SIZE = 14
IMG_PIXELS = IMG_SIZE * IMG_SIZE
PKT_MAGIC = b"MNST"
RSP_MAGIC = b"RSLT"
RSP_LEN = 12

app = Flask(__name__)

FPGA_IP = "192.168.8.50"
FPGA_PORT = 5000
UDP_TIMEOUT_S = 2.0

_seq_lock = threading.Lock()
_seq_counter = 0


HTML = r"""
<!doctype html>
<html lang="it">
<head>
  <meta charset="utf-8">
  <title>FPGA MNIST CNN Demo</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    :root {
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #111827;
      color: #f9fafb;
    }

    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
    }

    .card {
      width: min(920px, 94vw);
      background: #1f2937;
      border: 1px solid #374151;
      border-radius: 22px;
      padding: 28px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.35);
    }

    h1 {
      margin: 0 0 8px 0;
      font-size: 28px;
    }

    .subtitle {
      margin: 0 0 24px 0;
      color: #cbd5e1;
      line-height: 1.45;
    }

    .layout {
      display: grid;
      grid-template-columns: 320px 1fr;
      gap: 28px;
      align-items: start;
    }

    canvas#drawCanvas {
      width: 280px;
      height: 280px;
      background: #000;
      border: 3px solid #64748b;
      border-radius: 18px;
      touch-action: none;
      display: block;
      cursor: crosshair;
    }

    .buttons {
      display: flex;
      gap: 12px;
      margin-top: 16px;
      flex-wrap: wrap;
    }

    button {
      border: 0;
      border-radius: 14px;
      padding: 12px 18px;
      font-weight: 700;
      font-size: 15px;
      cursor: pointer;
    }

    #predictBtn {
      background: #22c55e;
      color: #052e16;
    }

    #clearBtn {
      background: #e5e7eb;
      color: #111827;
    }

    .resultBox {
      background: #111827;
      border: 1px solid #374151;
      border-radius: 18px;
      padding: 22px;
      min-height: 240px;
    }

    .pred {
      font-size: 92px;
      font-weight: 900;
      line-height: 1;
      margin: 10px 0 16px 0;
      color: #a7f3d0;
    }

    .status {
      color: #cbd5e1;
      font-size: 16px;
      line-height: 1.5;
      white-space: pre-wrap;
    }

    .previewWrap {
      margin-top: 18px;
      display: flex;
      align-items: center;
      gap: 18px;
    }

    canvas#previewCanvas {
      width: 140px;
      height: 140px;
      background: #000;
      border: 2px solid #64748b;
      border-radius: 12px;
      image-rendering: pixelated;
    }

    .hint {
      color: #94a3b8;
      font-size: 14px;
      margin-top: 12px;
    }

    @media (max-width: 760px) {
      .layout {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <div class="card">
    <h1>FPGA MNIST CNN Demo</h1>
    <p class="subtitle">
      Disegna un numero a mano. Il backend locale lo converte in un frame 14×14,
      lo invia via UDP alla FPGA e legge la risposta UDP dell'acceleratore CNN.
    </p>

    <div class="layout">
      <div>
        <canvas id="drawCanvas" width="280" height="280"></canvas>
        <div class="buttons">
          <button id="predictBtn">Predict</button>
          <button id="clearBtn">Clear</button>
        </div>
        <p class="hint">Suggerimento: disegna grande, centrato, bianco su sfondo nero.</p>
      </div>

      <div class="resultBox">
        <div>Predicted digit</div>
        <div id="pred" class="pred">—</div>
        <div id="status" class="status">Pronto.</div>

        <div class="previewWrap">
          <canvas id="previewCanvas" width="14" height="14"></canvas>
          <div class="hint">
            Preview del frame 14×14 effettivamente inviato alla FPGA.
          </div>
        </div>
      </div>
    </div>
  </div>

<script>
const canvas = document.getElementById("drawCanvas");
const ctx = canvas.getContext("2d");

const preview = document.getElementById("previewCanvas");
const pctx = preview.getContext("2d");

const predEl = document.getElementById("pred");
const statusEl = document.getElementById("status");

function clearCanvas() {
  ctx.fillStyle = "black";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  predEl.textContent = "—";
  statusEl.textContent = "Pronto.";
  pctx.fillStyle = "black";
  pctx.fillRect(0, 0, preview.width, preview.height);
}

clearCanvas();

let drawing = false;
let last = null;

function posFromEvent(e) {
  const rect = canvas.getBoundingClientRect();
  const clientX = e.touches ? e.touches[0].clientX : e.clientX;
  const clientY = e.touches ? e.touches[0].clientY : e.clientY;
  return {
    x: (clientX - rect.left) * (canvas.width / rect.width),
    y: (clientY - rect.top) * (canvas.height / rect.height)
  };
}

function startDraw(e) {
  e.preventDefault();
  drawing = true;
  last = posFromEvent(e);
}

function draw(e) {
  if (!drawing) return;
  e.preventDefault();

  const p = posFromEvent(e);

  ctx.strokeStyle = "white";
  ctx.lineWidth = 22;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";

  ctx.beginPath();
  ctx.moveTo(last.x, last.y);
  ctx.lineTo(p.x, p.y);
  ctx.stroke();

  last = p;
}

function stopDraw(e) {
  e.preventDefault();
  drawing = false;
  last = null;
}

canvas.addEventListener("mousedown", startDraw);
canvas.addEventListener("mousemove", draw);
window.addEventListener("mouseup", stopDraw);

canvas.addEventListener("touchstart", startDraw, {passive: false});
canvas.addEventListener("touchmove", draw, {passive: false});
canvas.addEventListener("touchend", stopDraw, {passive: false});

document.getElementById("clearBtn").addEventListener("click", clearCanvas);

document.getElementById("predictBtn").addEventListener("click", async () => {
  predEl.textContent = "…";
  statusEl.textContent = "Invio alla FPGA...";

  try {
    const dataUrl = canvas.toDataURL("image/png");

    const res = await fetch("/predict", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({image: dataUrl})
    });

    const data = await res.json();

    if (!res.ok || !data.ok) {
      predEl.textContent = "!";
      statusEl.textContent = data.error || "Errore sconosciuto.";
      return;
    }

    predEl.textContent = data.prediction.toString();
    statusEl.textContent =
      `SEQ=${data.seq}\n` +
      `Predizione=${data.prediction}\n` +
      `Round-trip UDP=${data.roundtrip_ms.toFixed(2)} ms`;

    drawPreview(data.frame14);
  } catch (err) {
    predEl.textContent = "!";
    statusEl.textContent = "Errore: " + err;
  }
});

function drawPreview(frame) {
  const img = pctx.createImageData(14, 14);
  for (let i = 0; i < 196; i++) {
    const v = frame[i];
    img.data[4*i + 0] = v;
    img.data[4*i + 1] = v;
    img.data[4*i + 2] = v;
    img.data[4*i + 3] = 255;
  }
  pctx.putImageData(img, 0, 0);
}
</script>
</body>
</html>
"""


def next_seq() -> int:
    global _seq_counter
    with _seq_lock:
        seq = _seq_counter & 0xFFFFFFFF
        _seq_counter = (_seq_counter + 1) & 0xFFFFFFFF
    return seq


def decode_data_url(data_url: str) -> Image.Image:
    if "," in data_url:
        _, b64 = data_url.split(",", 1)
    else:
        b64 = data_url

    raw = base64.b64decode(b64)
    img = Image.open(io.BytesIO(raw)).convert("L")
    return img


def preprocess_to_14x14(img: Image.Image) -> bytes:
    """
    Convert browser drawing to MNIST-like 14x14 uint8 frame.

    Assumptions:
    - browser canvas is black background + white stroke
    - output is 196 bytes, row-major order, values 0..255
    """

    img = img.convert("L")

    # Remove very small background noise.
    binary = img.point(lambda p: 255 if p > 10 else 0)

    bbox = binary.getbbox()

    if bbox is None:
        return bytes([0] * IMG_PIXELS)

    cropped = img.crop(bbox)

    w, h = cropped.size
    side = max(w, h)

    # Padding around the digit. This helps mimic centered MNIST samples.
    pad = max(8, int(0.25 * side))
    canvas_side = side + 2 * pad

    square = Image.new("L", (canvas_side, canvas_side), 0)
    x = (canvas_side - w) // 2
    y = (canvas_side - h) // 2
    square.paste(cropped, (x, y))

    # Resize to the exact input size expected by the FPGA CNN.
    small = square.resize((IMG_SIZE, IMG_SIZE), Image.Resampling.BILINEAR)

    # Optional contrast stretch to use full 0..255 range.
    small = ImageOps.autocontrast(small)

    return bytes(small.getdata())


def build_mnst_packet(seq: int, frame14: bytes, label: int = 0xFF) -> bytes:
    if len(frame14) != IMG_PIXELS:
        raise ValueError(f"Expected {IMG_PIXELS} image bytes, got {len(frame14)}")

    return PKT_MAGIC + struct.pack(">I", seq) + bytes([label & 0xFF]) + frame14


def send_to_fpga_and_wait(seq: int, frame14: bytes) -> Tuple[int, float]:
    pkt = build_mnst_packet(seq, frame14, label=0xFF)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(UDP_TIMEOUT_S)

    try:
        import time
        t0 = time.perf_counter()

        # Bind to an ephemeral local UDP port.
        # The FPGA will reply to this source port.
        sock.bind(("0.0.0.0", 0))
        sock.sendto(pkt, (FPGA_IP, FPGA_PORT))

        while True:
            data, addr = sock.recvfrom(64)
            dt_ms = (time.perf_counter() - t0) * 1000.0

            if len(data) < RSP_LEN:
                continue

            if data[0:4] != RSP_MAGIC:
                continue

            rsp_seq = struct.unpack(">I", data[4:8])[0]
            pred = data[8]
            ok_flag = data[9]

            if rsp_seq != seq:
                continue

            if ok_flag != 1:
                raise RuntimeError("FPGA returned ok_flag=0")

            return pred, dt_ms
    finally:
        sock.close()


@app.get("/")
def index():
    return render_template_string(HTML)


@app.post("/predict")
def predict():
    try:
        body = request.get_json(force=True)
        data_url = body.get("image", "")

        if not data_url:
            return jsonify(ok=False, error="Missing image field."), 400

        img = decode_data_url(data_url)
        frame14 = preprocess_to_14x14(img)

        seq = next_seq()
        pred, dt_ms = send_to_fpga_and_wait(seq, frame14)

        return jsonify(
            ok=True,
            seq=seq,
            prediction=int(pred),
            roundtrip_ms=float(dt_ms),
            frame14=list(frame14),
        )

    except socket.timeout:
        return jsonify(ok=False, error="Timeout: nessuna risposta UDP dalla FPGA."), 504
    except Exception as exc:
        return jsonify(ok=False, error=str(exc)), 500


def main():
    global FPGA_IP, FPGA_PORT, UDP_TIMEOUT_S

    parser = argparse.ArgumentParser()
    parser.add_argument("--fpga-ip", default=FPGA_IP)
    parser.add_argument("--fpga-port", type=int, default=FPGA_PORT)
    parser.add_argument("--timeout", type=float, default=UDP_TIMEOUT_S)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)

    args = parser.parse_args()

    FPGA_IP = args.fpga_ip
    FPGA_PORT = args.fpga_port
    UDP_TIMEOUT_S = args.timeout

    print(f"FPGA UDP target: {FPGA_IP}:{FPGA_PORT}")
    print(f"Web app: http://{args.host}:{args.port}")

    app.run(host=args.host, port=args.port, debug=False, threaded=True)


if __name__ == "__main__":
    main()
