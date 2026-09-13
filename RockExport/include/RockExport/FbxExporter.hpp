//----------------------------------------------------------------------------
//! @file   FbxExporter.hpp
//! @brief  Assimp を使った FBX 書き出し（DCC 相互運用用）
//! @detail 【Phase 0 で実測した事実に基づく設計。詳細は ExportProfile.hpp】
//!
//!         GLB と違い、FBX は Assimp のエクスポータをそのまま使います。
//!         理由は逆で、FBX には元々 metallicRoughnessTexture や
//!         occlusionTexture に相当する概念が無い（レガシーな Phong 系の
//!         マテリアルモデルしか持たない）ので、Assimp を経由しても
//!         困る場面が無いからです。
//!
//!         実測（Phase0RoundTrip.cpp を Phase 6 で再実行して確認）:
//!           - DIFFUSE と NORMALS は書いたとおりに読み戻せる
//!           - DIFFUSE_ROUGHNESS と AMBIENT_OCCLUSION は
//!             Assimp の FBX エクスポータが最初から書かない
//!             （マテリアルに設定しても出力から消える）
//!           - テクスチャの埋め込み（aiScene::mTextures 経由の "*N" 参照）は
//!             FBX でも機能する
//!
//!         したがって FBX には Albedo と Normal の2枚だけを埋め込みます。
//!         MetallicRoughness と AO は書いても失われるだけなので書きません
//!         （DCC 側で見た目を作り込みたいなら Albedo/Normal を土台に
//!         アーティストが自分でシェーダを組む、という前提です）。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Bake/ImageBuffer.hpp>
#include <RockCore/Mesh/MeshBuilder.hpp>

#include <string>

// 名前空間 RockExport
namespace RockExport {

    //------------------------------------------------------------------------
    //! FBX を書き出します。
    //!
    //! UV の V 座標は書く前に反転します（ExportProfile::flipV。FBX 経路では
    //! aiProcess_FlipUVs の反転が往復で相殺されないことを Phase 0 で確認済み）。
    //! mesh 自体は書き換えません。
    //!
    //! @param  [in]  mesh       書き出すメッシュ（Pipeline::GetMesh()）
    //! @param  [in]  albedo     Albedo（sRGB 符号化済みの PNG バイト列を想定）
    //! @param  [in]  normal     Normal（リニア）
    //! @param  [in]  outputPath 書き出し先のファイルパス
    //! @param  [out] outError   失敗理由。nullptr でもよい
    //! @return 成功したら true
    //------------------------------------------------------------------------
    bool WriteFbx(const RockCore::MeshBuilder& mesh,
                 const RockCore::ImageBuffer& albedo,
                 const RockCore::ImageBuffer& normal,
                 const std::string&           outputPath,
                 std::string*                 outError);

}    // namespace RockExport
