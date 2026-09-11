//----------------------------------------------------------------------------
//! @file   IUnwrapper.hpp
//! @brief  UV 展開の差し替え口
//! @detail Phase 1 は八面体射影（速い・必ず単射）、Phase 3 は xatlas（歪みが
//!         少ない）を使います。ベイク側はどちらで展開されたかを知らなくても
//!         動くよう、インターフェースで切ってあります。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Mesh/MeshBuilder.hpp>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @struct UnwrapSettings
    //! UV 展開の設定
    //------------------------------------------------------------------------
    struct UnwrapSettings {
        int   textureSize   = 1024;      //!< テクスチャの一辺。余白をテクセルで計算するのに使う
        int   padding       = 4;         //!< チャート間の余白（テクセル）
        float texelsPerUnit = 2048.0f;   //!< テクセル密度（xatlas 用。Phase 3）
    };

    //------------------------------------------------------------------------
    //! @class IUnwrapper
    //! UV 展開器の共通インターフェース
    //------------------------------------------------------------------------
    class IUnwrapper {
    public:
        virtual ~IUnwrapper() = default;

        //------------------------------------------------------------------
        //! メッシュへ UV を付けます。
        //!
        //! シームをまたぐ三角形があれば頂点を複製します。つまり
        //! positions / normals / indices も書き換わります。
        //!
        //! @param  [in,out] mesh     対象のメッシュ
        //! @param  [in]     settings 展開の設定
        //! @return 成功したら true
        //------------------------------------------------------------------
        virtual bool Unwrap(MeshBuilder& mesh, const UnwrapSettings& settings) = 0;

        //! 表示用の名前を返します。
        //! @return 展開器の名前
        virtual const char* GetName() const = 0;
    };

}    // namespace RockCore
