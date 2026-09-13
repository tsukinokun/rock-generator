//----------------------------------------------------------------------------
//! @file   PngWriter.hpp
//! @brief  RGBA8 の ImageBuffer を PNG のバイト列へ符号化するもの
//! @detail GlbWriter と FbxExporter の両方が、テクスチャをファイルへ
//!         埋め込むためにこれを使います（Phase 6 で実測したとおり、
//!         埋め込みでない外部参照は DCC 側での紛失事故が起きやすいので、
//!         両方の形式で埋め込みに統一しています）。
//!
//!         **圧縮は掛けません。** zlib の「格納（無圧縮）」ブロックだけを
//!         使って、正しい PNG の形にしているだけです。実データの数%増しの
//!         サイズにしかならず（zlib/deflate のヘッダとブロック境界ぶんだけ）、
//!         見た目にも読み込み互換性にも影響しません。
//!
//!         これは手抜きではなく選択です。本物の DEFLATE（LZ77 + ハフマン）を
//!         自前で書くのは実装量と検証コストに対して見合わず、かといって
//!         zlib や stb_image_write のような 3rd party をこのためだけに
//!         vendor するのも大げさです。ここは「self-authored」の対象外と
//!         割り切り、正しさが検証しやすい最小実装を選びました。
//!         本気でファイルサイズを詰めたくなったら、後から
//!         EncodePng の中身だけを差し替えれば済みます（呼び出し側には影響しません）。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Bake/ImageBuffer.hpp>
#include <RockCore/Types.hpp>

#include <vector>

// 名前空間 RockExport
namespace RockExport {

    //------------------------------------------------------------------------
    //! RGBA8 の画像を PNG のバイト列へ符号化します。
    //!
    //! 色空間の解釈（sRGB かリニアか）は PNG 側には持たせません。
    //! ImageBuffer が「生のバイト列」である方針（RockCore/Bake/ImageBuffer.hpp）
    //! をそのまま引き継ぎ、glTF の materials 側（baseColorTexture は sRGB、
    //! normalTexture / metallicRoughnessTexture / occlusionTexture はリニア、
    //! というのが glTF 2.0 仕様の既定）が解釈を決めます。
    //!
    //! @param  [in] image 符号化する画像。空（IsValid() が false）なら空配列を返す
    //! @return PNG のバイト列
    //------------------------------------------------------------------------
    std::vector<RockCore::u8> EncodePng(const RockCore::ImageBuffer& image);

}    // namespace RockExport
