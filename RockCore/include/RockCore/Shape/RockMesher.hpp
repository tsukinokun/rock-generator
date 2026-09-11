//----------------------------------------------------------------------------
//! @file   RockMesher.hpp
//! @brief  半径関数から岩のメッシュを組み立てるもの
//! @detail 正二十面体を細分割し、各頂点を Mul(anisoScale, dir * r_low(dir)) へ
//!         射影します。星形立体なのでこれだけで多様体・穴なし・自己交差なしの
//!         メッシュになり、法線も解析勾配で求まります。
//!
//!         Phase 3 でここへ半空間クリップとハードエッジ化が入ります。
//!         planeCutCount = 0 のときが今の実装（丸い転石）に退化する形なので、
//!         角張った岩と丸い岩が1つのパラメータ体系に収まります。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Mesh/MeshBuilder.hpp>
#include <RockCore/Shape/RockField.hpp>
#include <RockCore/Shape/RockParams.hpp>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @struct RockMeshStats
    //! メッシュ生成の結果を UI へ見せるための数値
    //------------------------------------------------------------------------
    struct RockMeshStats {
        int   subdivisions     = 0;
        u32   vertexCount      = 0;
        u32   triangleCount    = 0;
        float actualEdgeLength = 0.0f;    //!< 実際の最長エッジ長
    };

    //------------------------------------------------------------------------
    //! メッシュが表現できる周波数の上限（ナイキスト）を求めます。
    //!
    //! Radius() はノイズを単位球上で評価するため、周波数 f の特徴の
    //! ワールドサイズはおおよそ radius / f になります。これがエッジ長の
    //! 2倍を下回ると頂点変位では表現できず、エイリアスになるだけなので
    //! メッシュ側には載せません（その差分が Normal マップへ焼かれます）。
    //!
    //! @param  [in] params 岩のパラメータ
    //! @return 周波数の上限
    //------------------------------------------------------------------------
    float ComputeMeshNyquistFrequency(const RockParams& params);

    //------------------------------------------------------------------------
    //! 岩のメッシュを組み立てます。
    //!
    //! normals は解析勾配から作られ、uvs は空のままです（UV 展開は後段）。
    //!
    //! @param [in]  params   岩のパラメータ
    //! @param [in]  lowField メッシュ用の半径関数（帯域制限済み）
    //! @param [out] outMesh  書き込み先。中身は上書きされる
    //! @param [out] outStats 生成結果の数値。nullptr を渡してもよい
    //------------------------------------------------------------------------
    void BuildRockMesh(const RockParams& params, const RockField& lowField, MeshBuilder& outMesh, RockMeshStats* outStats);

}    // namespace RockCore
