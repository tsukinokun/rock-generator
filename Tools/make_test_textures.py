#!/usr/bin/env python3
"""Phase 0 のラウンドトリップ検証で使うテスト用 PNG を生成する。

5つの PBR スロットを「見ただけでどれか分かる」色に塗り分けた画像と、
UV の向き（aiProcess_FlipUVs の影響）を判定するための非対称な
チェッカー画像を出力する。

Pillow や numpy は使わない。zlib と struct だけで PNG を書く
（CombatAndroid の generate_dirt_ground.py と同じ方針）。
"""

import os
import struct
import zlib

SIZE = 64


def _chunk(tag: bytes, data: bytes) -> bytes:
    """PNG のチャンク1つ分（長さ・タグ・データ・CRC）を組み立てる。"""
    body = tag + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def write_png(path: str, pixels: list, width: int, height: int) -> None:
    """8bit RGB の PNG を書き出す。

    pixels は (r, g, b) のリストで、左上から右下へ行優先で並べる。
    PNG の走査線は各行の先頭にフィルタ種別のバイトが必要で、ここでは
    常に 0（フィルタなし）を使う。
    """
    raw = bytearray()
    for y in range(height):
        raw.append(0)    # フィルタ種別: なし
        for x in range(width):
            r, g, b = pixels[y * width + x]
            raw += bytes((r, g, b))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)    # 8bit / colortype2(RGB)
    png = b"\x89PNG\r\n\x1a\n"
    png += _chunk(b"IHDR", ihdr)
    png += _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += _chunk(b"IEND", b"")

    with open(path, "wb") as f:
        f.write(png)


def solid(color: tuple) -> list:
    """単色で塗りつぶした画像データを作る。"""
    return [color] * (SIZE * SIZE)


def orientation_checker() -> list:
    """UV の向きを判定するための、上下左右が非対称な画像を作る。

    上半分と下半分で色が違い、さらに左上の隅にだけ白い正方形を置く。
    こうしておくと、上下反転・左右反転・その両方を1枚で見分けられる。
    """
    top = (220, 60, 60)       # 上半分: 赤
    bottom = (60, 60, 220)    # 下半分: 青
    mark = (255, 255, 255)    # 左上の目印: 白

    pixels = []
    for y in range(SIZE):
        for x in range(SIZE):
            if y < SIZE // 8 and x < SIZE // 8:
                pixels.append(mark)
            elif (x // 8 + y // 8) % 2 == 0:
                # 市松模様にして、タイリングやUVスケールの異常も見えるようにする
                base = top if y < SIZE // 2 else bottom
                pixels.append(tuple(min(255, c + 35) for c in base))
            else:
                pixels.append(top if y < SIZE // 2 else bottom)
    return pixels


# スロットごとに色を変えておく。再インポート後にどのファイルが
# どの aiTextureType へ入ったかを、パス名だけでなく色でも追える
SLOT_COLORS = {
    "albedo.png": (200, 120, 60),      # 橙
    "normal.png": (128, 128, 255),     # 標準的な平坦法線
    "mr.png": (0, 180, 40),            # G=roughness, B=metallic が見えるよう緑寄り
    "emissive.png": (255, 0, 255),     # マゼンタ
    "ao.png": (90, 90, 90),            # 灰
}


def main() -> None:
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "TestData")
    out_dir = os.path.normpath(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    for name, color in SLOT_COLORS.items():
        write_png(os.path.join(out_dir, name), solid(color), SIZE, SIZE)
        print("wrote", name)

    write_png(os.path.join(out_dir, "orient.png"), orientation_checker(), SIZE, SIZE)
    print("wrote orient.png")


if __name__ == "__main__":
    main()
