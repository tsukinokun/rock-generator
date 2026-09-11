//----------------------------------------------------------------------------
//! @file   XAtlasUnwrapper.hpp
//! @brief  xatlas による UV 展開
//! @detail 八面体射影は必ず単射になる代わりに、テクセル密度が方向で大きく
//!         変わります。破断面が入った岩では、正方形の対角へ向いた面だけ
//!         解像度が半分以下になり、Normal マップのディテールが面ごとに
//!         違って見えます。xatlas はメッシュを歪みの少ないチャートへ切り、
//!         それを詰め込むのでこの差が消えます。
//!
//!         代わりに遅い（数百ms〜数秒）ので、スライダ操作中は使いません。
//!         どちらを使うかは Pipeline が決めます。
//!
//!         **このヘッダは xatlas を include しません。** xatlas.h は
//!         RockCore の公開ヘッダから見えない位置に置いてあり、
//!         利用側（RockEditor / RockCli）が 3rd party を意識せずに
//!         済むようにしてあります。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Unwrap/IUnwrapper.hpp>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @class XAtlasUnwrapper
    //! xatlas の UV 展開器
    //------------------------------------------------------------------------
    class XAtlasUnwrapper final : public IUnwrapper {
    public:
        //------------------------------------------------------------------
        //! メッシュへ UV を付けます。
        //!
        //! 展開に失敗したら mesh を変更せずに false を返します
        //! （呼び出し側が八面体射影へ落とせるようにするため）。
        //! 失敗するのは次の場合です。
        //!
        //!   - 中断が要求された
        //!   - チャートがテクスチャ 1 枚に収まらなかった
        //!   - アトラスに入らなかった頂点が残った
        //!
        //! @param  [in,out] mesh     対象のメッシュ
        //! @param  [in]     settings 展開の設定
        //! @param  [in]     cancel   中断フラグ。nullptr でもよい
        //! @param  [out]    outStats 結果の数値。nullptr を渡してもよい
        //! @return 成功したら true
        //------------------------------------------------------------------
        bool Unwrap(MeshBuilder&          mesh,
                    const UnwrapSettings& settings,
                    const CancelToken*    cancel,
                    UnwrapStats*          outStats) override;

        //! 表示用の名前を返します。
        //! @return 展開器の名前
        const char* GetName() const override { return "xatlas"; }
    };

}    // namespace RockCore
