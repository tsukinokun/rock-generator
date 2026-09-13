//----------------------------------------------------------------------------
//! @file   GlbWriter.hpp
//! @brief  仕様どおりの glTF 2.0 バイナリ（.glb）を自前で組み立てるもの
//! @detail 【Phase 0 で実測した事実に基づく設計。詳細は ExportProfile.hpp】
//!
//!         Assimp 5.0.1 の glTF2 エクスポータ（"glb2"）は、aiMaterial から
//!         書き出すときに DIFFUSE / NORMALS / EMISSIVE の3スロットしか
//!         glTF のテクスチャへ変換しません。metallicRoughnessTexture と
//!         occlusionTexture は最初から glTF の語彙にしか存在しない概念で、
//!         Assimp 側の aiTextureType には対応する型が無いため、Assimp の
//!         マテリアルを経由する限りどうやっても書けません
//!         （Phase 0 で実測済み。DIFFUSE_ROUGHNESS / AMBIENT_OCCLUSION の
//!         テクスチャを付けても "glb2" の出力からは消える）。
//!
//!         だから GLB は Assimp を一切使わず、glTF 2.0 の仕様書どおりの
//!         JSON + バイナリを直接書きます。エンジンが実際に読む経路
//!         （ModelImporter → Assimp のインポータ）は自前で組んだ GLB でも
//!         正しく解釈できることを Phase 0 で確認済みです。
//!
//!         構造は「12バイトのヘッダ + JSONチャンク + BINチャンク」という
//!         素直なもので、標準ライブラリだけで書けます
//!         （原型: Tools/make_test_glb.py）。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Bake/ImageBuffer.hpp>
#include <RockCore/Mesh/MeshBuilder.hpp>

#include <string>

// 名前空間 RockExport
namespace RockExport {

    //------------------------------------------------------------------------
    //! GLB を書き出します。
    //!
    //! mesh は positions / normals / uvs が同じ頂点数を持っていること
    //! （UV 展開とハードエッジ化を終えたメッシュ、= Pipeline::GetMesh()）。
    //!
    //! テクスチャは4枚とも埋め込みます（bufferView 経由。外部ファイル
    //! 参照にはしません — ExportProfile.hpp の embedTextures を参照）。
    //! V 座標の反転は行いません。glTF 経由では aiProcess_FlipUVs が
    //! 往復で相殺されるため、書く側で反転する必要が無いことを
    //! Phase 0 で確認済みです（ExportProfile::flipV も false）。
    //!
    //! @param  [in]  mesh              書き出すメッシュ
    //! @param  [in]  albedo            Albedo（sRGB 符号化済みの PNG バイト列を想定）
    //! @param  [in]  normal            Normal（リニア）
    //! @param  [in]  metallicRoughness MetallicRoughness（リニア。G=ラフネス, B=メタリック）
    //! @param  [in]  ao                AO（リニア。R チャンネルだけをエンジンが読む）
    //! @param  [in]  outputPath        書き出し先のファイルパス
    //! @param  [out] outError          失敗理由。nullptr でもよい
    //! @return 成功したら true
    //------------------------------------------------------------------------
    bool WriteGlb(const RockCore::MeshBuilder& mesh,
                 const RockCore::ImageBuffer& albedo,
                 const RockCore::ImageBuffer& normal,
                 const RockCore::ImageBuffer& metallicRoughness,
                 const RockCore::ImageBuffer& ao,
                 const std::string&           outputPath,
                 std::string*                 outError);

}    // namespace RockExport
