# mkicon.py - 生成 src/app.ico（纯标准库，无第三方依赖）
# 设计：深色圆角底 + 三根升起的性能柱（最高一根青色），小尺寸下依然可读。
# 抗锯齿：4x 超采样后盒式降采样；每个尺寸独立渲染，不做缩放。
import zlib, struct, math, os

def clamp(v, a, b): return a if v < a else (b if v > b else v)

def sd_round_box(px, py, cx, cy, bx, by, r):
    qx = abs(px - cx) - (bx - r)
    qy = abs(py - cy) - (by - r)
    ox = qx if qx > 0.0 else 0.0
    oy = qy if qy > 0.0 else 0.0
    return math.hypot(ox, oy) + min(max(qx, qy), 0.0) - r

BG    = (18, 22, 28)
GREEN = (74, 222, 128)
CYAN  = (34, 211, 238)
BARS = [  # (中心x, 顶边y, 高度, 颜色)；底边统一 0.82
    (0.315, 0.56, 0.26, GREEN),
    (0.500, 0.37, 0.45, GREEN),
    (0.685, 0.18, 0.64, CYAN),
]
BAR_HW, BAR_R, BOTTOM = 0.0775, 0.035, 0.82

def render(size):
    S = 4
    N = size * S
    raw = bytearray(N * N * 4)
    for yi in range(N):
        py = (yi + 0.5) / N
        base = yi * N * 4
        for xi in range(N):
            px = (xi + 0.5) / N
            i = base + xi * 4
            if sd_round_box(px, py, 0.5, 0.5, 0.46, 0.46, 0.20) > 0:
                continue  # 圆角外：透明
            r, g, b = BG
            for cx, top, h, col in BARS:
                if sd_round_box(px, py, cx, (top + BOTTOM) / 2, BAR_HW, h / 2, BAR_R) <= 0:
                    r, g, b = col
                    break
            raw[i], raw[i+1], raw[i+2], raw[i+3] = r, g, b, 255
    out = bytearray(size * size * 4)
    for y in range(size):
        for x in range(size):
            sr = sg = sb = sa = 0
            for dy in range(S):
                base = ((y * S + dy) * N + x * S) * 4
                for dx in range(S):
                    i = base + dx * 4
                    sr += raw[i]; sg += raw[i+1]; sb += raw[i+2]; sa += raw[i+3]
            n = S * S
            o = (y * size + x) * 4
            out[o], out[o+1], out[o+2], out[o+3] = sr//n, sg//n, sb//n, sa//n
    return bytes(out)

def png(width, height, rgba):
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    raw = b''.join(b'\x00' + rgba[y*width*4:(y+1)*width*4] for y in range(height))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')

def build_ico(sizes):
    imgs = [png(s, s, render(s)) for s in sizes]
    hdr = struct.pack('<HHH', 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    entries, data = b'', b''
    for s, img in zip(sizes, imgs):
        w = 0 if s >= 256 else s
        entries += struct.pack('<BBBBHHII', w, w, 0, 0, 1, 32, len(img), offset)
        data += img
        offset += len(img)
    return hdr + entries + data

if __name__ == '__main__':
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sizes = [16, 24, 32, 48, 64, 128, 256]
    with open(os.path.join(root, 'src', 'app.ico'), 'wb') as f:
        f.write(build_ico(sizes))
    with open(os.path.join(root, 'build', 'icon_preview.png'), 'wb') as f:
        f.write(png(128, 128, render(128)))
    print('OK: src/app.ico (sizes:', ', '.join(map(str, sizes)), ')')
