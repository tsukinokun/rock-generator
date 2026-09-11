//----------------------------------------------------------------------------
//! @file   NoiseStack.hpp
//! @brief  岩の表面を作る多層ノイズ
//! @detail 層ごとに種類・振幅・周波数を持ち、合計を半径への変位として返します。
//!         層の種類を後から増やしても Shape 側を触らずに済むよう、
//!         インターフェースは Phase 1 の時点で確定させてあります。
//!
//!         層の役割:
//!           L0 Fbm    塊感。Phase 1 から使う
//!           L1 Worley 割れ・欠け。「岩に見える」最大の要因（Phase 3）
//!           L2 Ridged 風化した稜線（Phase 3）
//!           L3 高周波 粒・小穴。メッシュには載せず Normal だけへ（Phase 4）
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Noise/Fbm.hpp>

#include <RockCore/Math/Vec.hpp>
#include <RockCore/Types.hpp>

#include <memory>
#include <vector>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @enum  NoiseLayerKind
    //! ノイズ層の種類
    //------------------------------------------------------------------------
    enum class NoiseLayerKind : u8 {
        Fbm = 0,    //!< 塊感を作る fBm
        Worley,     //!< Worley の F2-F1。割れ・欠け（Phase 3 で実装）
        Ridged,     //!< Ridged multifractal。風化した稜線（Phase 3 で実装）
    };

    //------------------------------------------------------------------------
    //! @struct NoiseLayerParams
    //! ノイズ1層ぶんのパラメータ
    //------------------------------------------------------------------------
    struct NoiseLayerParams {
        bool           enabled   = true;
        NoiseLayerKind kind      = NoiseLayerKind::Fbm;
        float          amplitude = 0.18f;    // 半径に対する変位の比率
        FbmParams      fbm{};
    };

    //------------------------------------------------------------------------
    //! @class NoiseStack
    //! 複数のノイズ層をまとめて評価するもの
    //------------------------------------------------------------------------
    class NoiseStack {
    public:
        //------------------------------------------------------------------
        //! 各層のノイズを構築します。
        //!
        //! @param [in] layers       層のパラメータ
        //! @param [in] seed         元のシード。層ごとに DeriveSeed で分ける
        //! @param [in] maxFrequency これを超える周波数を載せない。0 以下で無制限
        //------------------------------------------------------------------
        NoiseStack(const std::vector<NoiseLayerParams>& layers, u64 seed, float maxFrequency);

        //! 合計の変位を求めます。
        //! @param  [in] p 評価位置
        //! @return 半径へ足す変位（半径 1 を基準とした比率）
        float Sample(const Vec3& p) const;

        //! 有効な層が1つも無いかを返します。
        //! @return 1つも無ければ true
        bool IsEmpty() const { return m_layers.empty(); }

    private:
        //------------------------------------------------------------------
        //! @struct Layer
        //! 構築済みの1層
        //------------------------------------------------------------------
        struct Layer {
            std::unique_ptr<Fbm> fbm;
            float                amplitude = 0.0f;
        };

        std::vector<Layer> m_layers;
    };

}    // namespace RockCore
