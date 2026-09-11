//----------------------------------------------------------------------------
//! @file   OctahedralUnwrapper.hpp
//! @brief  八面体射影による UV 展開
//! @detail 岩が星形立体であることを使って、頂点の方向ベクトルをそのまま
//!         UV へ写します。緯度経度と違って極の縮退が無く、星形保証があるので
//!         必ず単射になります。xatlas より何桁も速いので、Phase 3 以降も
//!         高速プレビュー用に残します。
//!
//!         八面体射影は正方形の内部では連続ですが、正方形の縁どうしが
//!         貼り合わされているため、縁をまたぐ三角形だけ UV が飛びます。
//!         そこは頂点を複製して隣のタイルへ展開します（実装の大半はこの処理）。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Unwrap/IUnwrapper.hpp>

#include <RockCore/Math/Vec.hpp>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! 単位方向ベクトルを八面体射影して [-1,1]^2 の座標を返します。
    //!
    //! @param  [in] direction 単位方向ベクトル
    //! @return 射影後の座標
    //------------------------------------------------------------------------
    Vec2 OctahedralEncode(const Vec3& direction);

    //------------------------------------------------------------------------
    //! @class OctahedralUnwrapper
    //! 八面体射影の UV 展開器
    //------------------------------------------------------------------------
    class OctahedralUnwrapper final : public IUnwrapper {
    public:
        //! メッシュへ UV を付けます。
        //! @param  [in,out] mesh     対象のメッシュ
        //! @param  [in]     settings 展開の設定
        //! @return 成功したら true
        bool Unwrap(MeshBuilder& mesh, const UnwrapSettings& settings) override;

        //! 表示用の名前を返します。
        //! @return 展開器の名前
        const char* GetName() const override { return "Octahedral"; }
    };

}    // namespace RockCore
