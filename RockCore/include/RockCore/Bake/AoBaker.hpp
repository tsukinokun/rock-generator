//----------------------------------------------------------------------------
//! @file   AoBaker.hpp
//! @brief  AO（アンビエントオクルージョン）のベイク
//! @detail 星形立体の恩恵がもう一度効くところです。レイと三角形の交差判定も
//!         BVH も要らず、F(origin + dir*t) の符号変化を 1 次元で探すだけで
//!         遮蔽が分かります（RockField::Raycast1D）。
//!
//!         それでも**このパイプラインで一番重い段**です。テクセルごとに
//!         数十本のレイを撃ち、1 本あたり十数回 F を評価するため、
//!         1024x1024 だと Normal ベイクの 100 倍規模の評価回数になります。
//!         レイ本数と距離をパラメータに出してあるのはそのためです。
//!
//!         AO は独立した 1 枚として焼きます。MetallicRoughness の R へ
//!         詰める（ORM パッキング）手もありますが、エンジンは AO を専用の
//!         スロットで読み、glTF も occlusionTexture を別に持てるので、
//!         分けておく方が素直です。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Bake/ImageBuffer.hpp>
#include <RockCore/Bake/UvRasterizer.hpp>
#include <RockCore/Pipeline/CancelToken.hpp>
#include <RockCore/Shape/RockField.hpp>
#include <RockCore/Shape/RockParams.hpp>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @struct AoBakeStats
    //! ベイク結果を UI へ見せるための数値
    //------------------------------------------------------------------------
    struct AoBakeStats {
        u32 texelsWritten = 0;

        float minOcclusion = 0.0f;    //!< 焼けた AO の下限（0 が真っ黒）
        float meanOcclusion = 0.0f;   //!< 焼けた AO の平均

        //! 1 テクセルあたりに実際に撃ったレイの平均本数。
        //! 外接球へ入らないレイは撃たずに落とすので、設定本数より小さくなる
        float averageRaysPerTexel = 0.0f;
    };

    //------------------------------------------------------------------------
    //! AO を焼きます。
    //!
    //! 未被覆のテクセルには 1.0（遮蔽なし）を書きます。dilate が後から
    //! 周囲の値で埋め直しますが、埋め損ねても暗くならない側に倒しておきます。
    //!
    //! R チャンネルだけを使います（エンジンの GBuffer.ps.hlsl が .r を読む）。
    //! G と B にも同じ値を入れるので、画像として開いても灰色に見えます。
    //!
    //! @param  [in]  gbuffer   UV 空間へラスタライズした結果
    //! @param  [in]  fullField 全帯域の半径関数
    //! @param  [in]  params    岩のパラメータ
    //! @param  [out] outImage  書き込み先
    //! @param  [in]  progress  進捗の通知先。空でもよい
    //! @param  [in]  cancel    中断フラグ。nullptr でもよい
    //! @param  [out] outStats  ベイク結果の数値。nullptr でもよい
    //! @return 完走したら true。中断されたら false
    //------------------------------------------------------------------------
    bool BakeAoMap(const BakeGBuffer&      gbuffer,
                   const RockField&        fullField,
                   const RockParams&       params,
                   ImageBuffer&            outImage,
                   const ProgressCallback& progress,
                   const CancelToken*      cancel,
                   AoBakeStats*            outStats);

}    // namespace RockCore
