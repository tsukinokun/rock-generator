//----------------------------------------------------------------------------
//! @file   Ridged.hpp
//! @brief  Ridged multifractal による風化した稜線
//! @detail 1 - |perlin| でノイズの 0 交差を折り返すと、谷だった場所が尖った
//!         稜線になります。fBm の丸い起伏に対して、こちらは「線」が出るので
//!         風化で削れ残った筋の表現になります。
//!
//!         オクターブを足し算ではなく掛け算で重ねるのが multifractal の肝で、
//!         低い場所ほど高周波が乗らなくなり、稜線だけが細かくなります。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Noise/Fbm.hpp>
#include <RockCore/Noise/Perlin.hpp>

#include <RockCore/Math/Vec.hpp>
#include <RockCore/Types.hpp>

#include <vector>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @class Ridged
    //! 帯域制限付きの ridged multifractal
    //------------------------------------------------------------------------
    class Ridged {
    public:
        //------------------------------------------------------------------
        //! オクターブを用意します。
        //!
        //! @param [in] params       形を決めるパラメータ（Fbm と共用）
        //! @param [in] seed         シード
        //! @param [in] maxFrequency これを超える周波数のオクターブは捨てる。
        //!                          0 以下を渡すと全オクターブを使う
        //! @param [in] sharpness    稜線の鋭さ。1 に近いほど尖る
        //------------------------------------------------------------------
        Ridged(const FbmParams& params, u64 seed, float maxFrequency, float sharpness);

        //! 指定位置のノイズ値を求めます。
        //! @param  [in] p 評価位置
        //! @return おおむね [-1, 1] の値。稜線が正側へ出る
        float Sample(const Vec3& p) const;

        //! 実際に使われたオクターブ数を返します。
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
        float               m_normalizer = 1.0f;
        float               m_sharpness  = 0.9f;
    };

}    // namespace RockCore
