#!/usr/bin/env python3
"""仕様どおりの GLB を自前で組み立てる（Phase 0 の検証用 / 将来の GlbWriter の原型）。

Assimp 5.0.1 の glTF2 エクスポータは metallicRoughnessTexture と
occlusionTexture を書き出さないことが実測で分かった。よって自前で書くしか
ないのだが、その前に「自前で正しく書いたら Assimp のインポータはどの
aiTextureType へ入れるのか」を確かめる必要がある。

このスクリプトは5枚のテクスチャすべてを埋め込んだ GLB を出力する。
出力を phase0.exe --dump に食わせると、エンジンが読む5スロットに
何が届くかが分かる。

glTF 2.0 の GLB は「12バイトのヘッダ + JSONチャンク + BINチャンク」という
素直な構造で、外部ライブラリなしで書ける。標準ライブラリのみ。
"""

import json
import os
import struct

# glTF のコンポーネント型
FLOAT = 5126
UNSIGNED_SHORT = 5123

# bufferView.target
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963

# テクスチャの並び。glTF のどのスロットへ入れるかは MATERIAL で決める
TEXTURE_FILES = ["albedo.png", "normal.png", "mr.png", "emissive.png", "ao.png"]


def _pad4(data: bytearray) -> None:
    """4バイト境界まで 0 で埋める（glTF は bufferView の境界を要求する）。"""
    while len(data) % 4 != 0:
        data.append(0)


def build_glb(texture_dir: str) -> bytes:
    """板1枚 + フルPBRマテリアルの GLB を組み立てて返す。"""
    # 四隅の UV を変えて、往復で V が反転するかを見られるようにする
    positions = [(-1.0, 0.0, -1.0), (1.0, 0.0, -1.0), (1.0, 0.0, 1.0), (-1.0, 0.0, 1.0)]
    normals = [(0.0, 1.0, 0.0)] * 4
    uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    indices = [0, 1, 2, 0, 2, 3]

    blob = bytearray()
    buffer_views = []

    def add_view(payload: bytes, target=None) -> int:
        """バイト列を buffer へ追加し、その bufferView の番号を返す。"""
        _pad4(blob)
        offset = len(blob)
        blob.extend(payload)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
        if target is not None:
            view["target"] = target
        buffer_views.append(view)
        return len(buffer_views) - 1

    pos_view = add_view(b"".join(struct.pack("<3f", *p) for p in positions), ARRAY_BUFFER)
    nrm_view = add_view(b"".join(struct.pack("<3f", *n) for n in normals), ARRAY_BUFFER)
    uv_view = add_view(b"".join(struct.pack("<2f", *t) for t in uvs), ARRAY_BUFFER)
    idx_view = add_view(struct.pack("<%dH" % len(indices), *indices), ELEMENT_ARRAY_BUFFER)

    # POSITION の accessor は min/max が必須
    xs, ys, zs = zip(*positions)
    accessors = [
        {"bufferView": pos_view, "componentType": FLOAT, "count": 4, "type": "VEC3",
         "min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]},
        {"bufferView": nrm_view, "componentType": FLOAT, "count": 4, "type": "VEC3"},
        {"bufferView": uv_view, "componentType": FLOAT, "count": 4, "type": "VEC2"},
        {"bufferView": idx_view, "componentType": UNSIGNED_SHORT, "count": len(indices), "type": "SCALAR"},
    ]

    # 画像は bufferView 参照で埋め込む（外部ファイルにしない）。
    # エンジンはモデル埋め込みのテクスチャだけがミップ生成とBC3圧縮を通る
    images = []
    for name in TEXTURE_FILES:
        with open(os.path.join(texture_dir, name), "rb") as f:
            view = add_view(f.read())
        images.append({"bufferView": view, "mimeType": "image/png", "name": name})

    gltf = {
        "asset": {"version": "2.0", "generator": "RockForge Phase0 GlbWriter prototype"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "Phase0Quad"}],
        "meshes": [{
            "name": "Phase0Mesh",
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                "indices": 3,
                "material": 0,
            }],
        }],
        "materials": [{
            "name": "Phase0PbrMaterial",
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0},
                "metallicRoughnessTexture": {"index": 2},    # G=roughness, B=metallic
                "metallicFactor": 1.0,
                "roughnessFactor": 1.0,
            },
            "normalTexture": {"index": 1},
            "occlusionTexture": {"index": 4},
            "emissiveTexture": {"index": 3},
            "emissiveFactor": [1.0, 1.0, 1.0],
        }],
        "textures": [{"source": i, "sampler": 0} for i in range(len(TEXTURE_FILES))],
        "images": images,
        "samplers": [{"wrapS": 10497, "wrapT": 10497}],
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{"byteLength": len(blob)}],
    }

    #--------------------------------------------------------------
    # GLB のコンテナへ詰める。
    # JSONチャンクは空白(0x20)で、BINチャンクは 0 で 4バイト境界まで埋める
    #--------------------------------------------------------------
    json_bytes = bytearray(json.dumps(gltf, separators=(",", ":")).encode("utf-8"))
    while len(json_bytes) % 4 != 0:
        json_bytes.extend(b" ")
    _pad4(blob)

    total = 12 + 8 + len(json_bytes) + 8 + len(blob)
    out = bytearray()
    out.extend(struct.pack("<4sII", b"glTF", 2, total))
    out.extend(struct.pack("<I4s", len(json_bytes), b"JSON"))
    out.extend(json_bytes)
    out.extend(struct.pack("<I4s", len(blob), b"BIN\x00"))
    out.extend(blob)
    return bytes(out)


def main() -> None:
    here = os.path.dirname(os.path.abspath(__file__))
    texture_dir = os.path.normpath(os.path.join(here, "..", "TestData"))
    out_dir = os.path.join(texture_dir, "out")
    os.makedirs(out_dir, exist_ok=True)

    out_path = os.path.join(out_dir, "handwritten_full_pbr.glb")
    with open(out_path, "wb") as f:
        f.write(build_glb(texture_dir))
    print("wrote", out_path, os.path.getsize(out_path), "bytes")


if __name__ == "__main__":
    main()
