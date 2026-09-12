//----------------------------------------------------------------------------
//! @file   SurfaceBaker.hpp
//! @brief  Albedo と MetallicRoughness のベイク
//! @detail Normal ベイクと同じく、ハイポリもレイキャストも要りません。
//!         テクセルのローポリ位置から方向を出し、半径関数へ問い合わせるだけです。
//!
//!         2 枚を 1 回のループで焼きます。どちらも同じ per-texel の信号
//!         （斑のノイズと窪み具合）から作るので、分けると場の評価が倍になります。
//!
//!         **Albedo だけは sRGB で符号化して書きます。** 他のマップと違って
//!         Albedo は sRGB として解釈される前提だからです（プレビューは
//!         DXGI_FORMAT_R8G8B8A8_UNORM_SRGB の SRV、glTF も baseColorTexture を
//!         sRGB と定めている）。リニアのまま 8bit へ詰めると暗部が破綻します。
//!         ImageBuffer 自体は「生のバイト列」なので、どう解釈するかは
//!         SRV のフォーマットと書き出し側が決めます（ImageBuffer.hpp の方針）。
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
    //! @struct SurfaceBakeStats
    //! ベイク結果を UI へ見せるための数値
    //------------------------------------------------------------------------
    struct SurfaceBakeStats {
        u32 texelsWritten = 0;

        //! 窪みと判定されたテクセルの割合 [0,1]。
        //! 0 のままなら窪みの信号がどこにも効いていないので、
        //! 「暗くしたつもりが真っ平ら」に気付ける
        float cavityRatio = 0.0f;

        float minLuminance = 0.0f;    //!< 焼けた明度の下限（リニア）
        float maxLuminance = 0.0f;    //!< 焼けた明度の上限（リニア）

        //! 測れた窪みの生の値の最大。RockField::Relief の戻り値そのもの。
        //!
        //! 窪みを [0,1] へ写す倍率をこの数字から決めてある。ノイズ層の構成を
        //! 変えると窪みの深さも変わるので、倍率が合っているかはこれで見る
        float maxRelief = 0.0f;
    };

    //------------------------------------------------------------------------
    //! Albedo と MetallicRoughness を焼きます。
    //!
    //! 未被覆のテクセルには基本色と基準ラフネスを書きます（dilate が後から
    //! 周囲の色で埋め直しますが、埋め損ねても破綻しない値にしておく）。
    //!
    //! MetallicRoughness のチャンネル割り当ては glTF 慣例に従います。
    //!   R: 未使用（Phase 5 で AO を入れる余地。今は 255）
    //!   G: ラフネス
    //!   B: メタリック（岩なので常に 0）
    //!
    //! @param  [in]  gbuffer      UV 空間へラスタライズした結果
    //! @param  [in]  fullField    全帯域の半径関数
    //! @param  [in]  params       岩のパラメータ
    //! @param  [out] outAlbedo    Albedo の書き込み先（sRGB 符号化）
    //! @param  [out] outMetallicRoughness MR の書き込み先（リニア）
    //! @param  [in]  progress     進捗の通知先。空でもよい
    //! @param  [in]  cancel       中断フラグ。nullptr でもよい
    //! @param  [out] outStats     ベイク結果の数値。nullptr でもよい
    //! @return 完走したら true。中断されたら false
    //------------------------------------------------------------------------
    bool BakeSurfaceMaps(const BakeGBuffer&      gbuffer,
                         const RockField&        fullField,
                         const RockParams&       params,
                         ImageBuffer&            outAlbedo,
                         ImageBuffer&            outMetallicRoughness,
                         const ProgressCallback& progress,
                         const CancelToken*      cancel,
                         SurfaceBakeStats*       outStats);

}    // namespace RockCore
