from __future__ import annotations
from PIL import Image
from io import BytesIO


def center_crop_resize_to_panel(data: bytes, width: int, height: int) -> Image.Image:
    img = Image.open(BytesIO(data)).convert("L")
    # Center-crop to aspect ratio, then resize with high-quality filter
    src_w, src_h = img.size
    target_ratio = width / height
    src_ratio = src_w / src_h
    if src_ratio > target_ratio:
        # Wider than target: crop left/right
        new_w = int(src_h * target_ratio)
        offset = (src_w - new_w) // 2
        img = img.crop((offset, 0, offset + new_w, src_h))
    else:
        # Taller than target: crop top/bottom
        new_h = int(src_w / target_ratio)
        offset = (src_h - new_h) // 2
        img = img.crop((0, offset, src_w, offset + new_h))
    img = img.resize((width, height), Image.LANCZOS)
    return img


def pack_grayscale_4bpp(img: Image.Image) -> bytes:
    # Input is L-mode 8-bit grayscale; output packs two 4-bit pixels per byte, MS nibble first
    if img.mode != "L":
        img = img.convert("L")
    pixels = img.tobytes()
    buf = bytearray()
    for i in range(0, len(pixels), 2):
        p0 = pixels[i] >> 4
        p1 = (pixels[i + 1] >> 4) if i + 1 < len(pixels) else 0
        buf.append((p0 << 4) | p1)
    return bytes(buf)
