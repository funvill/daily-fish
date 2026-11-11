"""Renders the same fish with the original fishdraw.js and with the C++ port,
stacked in one PNG, to check the port by eye.

  python tools/preview/compare.py path/to/fishdraw.js build/preview.exe out.png "Name one" "Name two" ...

The original is drawn on top (scaled from its 500x300 page), the port below.
"""
import json
import os
import subprocess
import sys

from PIL import Image, ImageDraw

W, H = 800, 480


def render_js(fishdraw_js, name):
    out = subprocess.run(
        ["node", fishdraw_js, "--seed", name, "--format", "json"],
        capture_output=True, text=True, check=True,
    ).stdout
    img = Image.new("1", (W, H), 1)
    d = ImageDraw.Draw(img)
    for poly in json.loads(out):
        pts = [(x * W / 500, y * H / 300) for x, y in poly if x is not None and y is not None]
        if len(pts) >= 2:
            d.line(pts, fill=0)
    return img


def render_port(preview_exe, name, tmp):
    r = subprocess.run([os.path.abspath(preview_exe), "--name", name, tmp], capture_output=True, text=True)
    print(r.stdout.strip())
    return Image.open(tmp).convert("1")


def main():
    fishdraw_js, preview_exe, out = sys.argv[1:4]
    names = sys.argv[4:]
    sheet = Image.new("1", (W * len(names), H * 2 + 4), 0)
    for i, name in enumerate(names):
        sheet.paste(render_js(fishdraw_js, name), (W * i, 0))
        sheet.paste(render_port(preview_exe, name, out + ".pbm"), (W * i, H + 4))
    sheet.save(out)


if __name__ == "__main__":
    main()
