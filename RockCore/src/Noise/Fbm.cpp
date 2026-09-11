//----------------------------------------------------------------------------
//! @file   Fbm.cpp
//! @brief  帯域制限付き fBm の実装
//----------------------------------------------------------------------------
#include <RockCore/Noise/Fbm.hpp>

#include <RockCore/Random/Pcg32.hpp>

#include <algorithm>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! オクターブを用意します。
    //------------------------------------------------------------------------
    Fbm::Fbm(const FbmParams& params, u64 seed, float maxFrequency) {
        const int octaveCount = std::clamp(params.octaves, 0, 12);

        float frequency    = std::max(params.frequency, 0.0f);
        float amplitude    = 1.0f;
        float amplitudeSum = 0.0f;

        m_octaves.reserve(static_cast<size_t>(octaveCount));

        for(int i = 0; i < octaveCount; ++i) {
            // 帯域制限。ここで打ち切るのは「捨てる」のではなく
            // 「このメッシュ解像度では表現できない周波数を載せない」ため。
            // 同じ NoiseStack から maxFrequency 違いで r_low と r_full を作り、
            // 差分（= 載せられなかった高周波）がそのまま Normal マップへ焼かれる
            if(maxFrequency > 0.0f && frequency > maxFrequency) {
                break;
            }

            // オクターブごとに独立したシードを振る。共有すると octaves を
            // 1つ増やしただけで既存オクターブの形まで変わってしまう
            m_octaves.push_back(Octave{Perlin(DeriveSeed(seed, static_cast<u32>(i))), frequency, amplitude});
            amplitudeSum += amplitude;

            frequency *= params.lacunarity;
            amplitude *= params.gain;
        }

        m_normalizer = (amplitudeSum > 0.0f) ? (1.0f / amplitudeSum) : 0.0f;
    }

    //------------------------------------------------------------------------
    //! 指定位置のノイズ値を求めます。
    //------------------------------------------------------------------------
    float Fbm::Sample(const Vec3& p) const {
        float sum = 0.0f;
        for(const Octave& octave : m_octaves) {
            sum += octave.perlin.Sample(p * octave.frequency) * octave.amplitude;
        }
        return sum * m_normalizer;
    }

}    // namespace RockCore
