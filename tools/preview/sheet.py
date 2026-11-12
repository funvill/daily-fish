"""Renders several pages with the desktop preview into one contact sheet, or
into separate PNGs for the README.

  python tools/preview/sheet.py build/preview.exe out.png fish 6 [first-seed-number]
  python tools/preview/sheet.py build/preview.exe docs/images/flower flower 5 --separate
"""
import os
import subprocess
import sys

from PIL import Image

W, H = 800, 480


def main():
    exe, out, kind, count = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
    rest = sys.argv[5:]
    separate = "--separate" in rest
    nums = [a for a in rest if a.isdigit()]
    first = int(nums[0]) if nums else 1
    cols = 2
    sheet = Image.new("1", (W * cols, H * ((count + cols - 1) // cols)), 1)
    tmp = out + ".tmp.pbm"
    for i in range(count):
        args = [os.path.abspath(exe)]
        if kind in ("flower", "insect"):
            args.append("--" + kind)
        args += ["--date", f"sample-{first + i}", tmp]
        r = subprocess.run(args, capture_output=True, text=True)
        print(r.stdout.strip())
        img = Image.open(tmp).convert("1")
        if separate:
            img.save(f"{out}-{i + 1}.png", optimize=True)
        else:
            sheet.paste(img, (W * (i % cols), H * (i // cols)))
    os.remove(tmp)
    if not separate:
        sheet.save(out)


if __name__ == "__main__":
    main()
