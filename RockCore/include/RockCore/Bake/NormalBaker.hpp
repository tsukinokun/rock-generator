//----------------------------------------------------------------------------
//! @file   NormalBaker.hpp
//! @brief  Normal マップのベイク
//! @detail 星形立体の恩恵が最も大きいところです。テクセルのローポリ位置を
//!         全帯域の半径関数へ射影するだけで「真の表面」が出るため、
//!         ハイポリメッシュも BVH も hi→lo のレイキャストも要りません。
//!
//!         差分幅 h をテクセルのワールドサイズに合わせているので、
//!         ローポリのテッシレーション誤差と高周波ディテールが 1 回で焼けます。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Bake/ImageBuffer.hpp>
#include <RockCore/Bake/UvRasterizer.hpp>
#include <RockCore/Pipeline/CancelToken.hpp>
#include <RockCore/Shape/RockField.hpp>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @struct NormalBakeStats
    //! ベイク結果を UI へ見せるための数値
    //------------------------------------------------------------------------
    struct NormalBakeStats {
        u32 texelsWritten    = 0;
        u32 degenerateTexels = 0;    //!< UV が潰れていて平坦な法線を書いたテクセル数
    };

    //------------------------------------------------------------------------
    //! Normal マップを焼きます。
    //!
    //! 未被覆のテクセルには平坦な法線を書きます（dilate が後から
    //! 周囲の色で埋め直します）。
    //!
    //! @param  [in]  gbuffer   UV 空間へラスタライズした結果
    //! @param  [in]  fullField 全帯域の半径関数
    //! @param  [out] outImage  書き込み先
    //! @param  [in]  progress  進捗の通知先。空でもよい
    //! @param  [in]  cancel    中断フラグ。nullptr でもよい
    //! @param  [out] outStats  ベイク結果の数値。nullptr でもよい
    //! @return 完走したら true。中断されたら false
    //------------------------------------------------------------------------
    bool BakeNormalMap(const BakeGBuffer&     gbuffer,
                       const RockField&       fullField,
                       ImageBuffer&           outImage,
                       const ProgressCallback& progress,
                       const CancelToken*     cancel,
                       NormalBakeStats*       outStats);

}    // namespace RockCore
