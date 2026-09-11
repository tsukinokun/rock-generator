//----------------------------------------------------------------------------
//! @file   Fbm.hpp
//! @brief  帯域制限付きの fBm（fractional Brownian motion）
//! @detail オクターブを足し合わせる素直な fBm ですが、maxFrequency を超える
//!         オクターブを捨てられる点が要です。同じパラメータから
//!         「メッシュ用（低帯域）」と「ベイク用（全帯域）」の2つを作り、
//!         メッシュのナイキストより高い周波数を頂点変位に載せないために使います。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Noise/Perlin.hpp>

#include <RockCore/Math/Vec.hpp>
#include <RockCore/Types.hpp>

#include <vector>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @struct FbmParams
    //! fBm の形を決めるパラメータ
    //------------------------------------------------------------------------
    struct FbmParams {
        float frequency  = 1.5f;    // 最初のオクターブの周波数
        int   octaves    = 4;       // 足し合わせるオクターブ数
        float lacunarity = 2.0f;    // オクターブごとの周波数の倍率
        float gain       = 0.5f;    // オクターブごとの振幅の倍率
    };

    //------------------------------------------------------------------------
    //! @class Fbm
    //! 帯域制限付きの fBm
    //------------------------------------------------------------------------
    class Fbm {
    public:
        //------------------------------------------------------------------
        //! オクターブを用意します。
        //!
        //! @param [in] params       形を決めるパラメータ
        //! @param [in] seed         シード
        //! @param [in] maxFrequency これを超える周波数のオクターブは捨てる。
        //!                          0 以下を渡すと全オクターブを使う
        //------------------------------------------------------------------
        Fbm(const FbmParams& params, u64 seed, float maxFrequency);

        //! 指定位置のノイズ値を求めます。
        //! @param  [in] p 評価位置
        //! @return おおむね [-1, 1] の値。振幅の総和で正規化済み
        float Sample(const Vec3& p) const;

        //! 実際に使われたオクターブ数を返します。（帯域制限の効きを UI で見せる用）
        //! @return 採用されたオクターブ数
        int GetActiveOctaveCount() const { return static_cast<int>(m_octaves.size()); }

    private:
        //------------------------------------------------------------------
        //! @struct Octave
        //! 1オクターブぶんのノイズと係数
        //------------------------------------------------------------------
        struct Octave {
            Perlin perlin;
            float  frequency = 0.0f;
            float  amplitude = 0.0f;
        };

        std::vector<Octave> m_octaves;
        float               m_normalizer = 1.0f;    // 振幅の総和の逆数
    };

}    // namespace RockCore
