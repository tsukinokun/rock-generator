//----------------------------------------------------------------------------
//! @file   Ridged.cpp
//! @brief  Ridged multifractal の実装
//----------------------------------------------------------------------------
#include <RockCore/Noise/Ridged.hpp>

#include <RockCore/Random/Pcg32.hpp>

#include <algorithm>
#include <cmath>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! オクターブを用意します。
    //------------------------------------------------------------------------
    Ridged::Ridged(const FbmParams& params, u64 seed, float maxFrequency, float sharpness)
        : m_sharpness(std::clamp(sharpness, 0.0f, 1.0f)) {

        const int octaveCount = std::clamp(params.octaves, 0, 12);

        float frequency    = std::max(params.frequency, 0.0f);
        float amplitude    = 1.0f;
        float amplitudeSum = 0.0f;

        m_octaves.reserve(static_cast<size_t>(octaveCount));

        for(int i = 0; i < octaveCount; ++i) {
            // 帯域制限の考え方は Fbm と同じ。メッシュで表現できない周波数は
            // 頂点に載せず、その差分が Normal マップへ焼かれる
            if(maxFrequency > 0.0f && frequency > maxFrequency) {
                break;
            }

            // オクターブごとに独立したシードを振る。Fbm と同じ seed を
            // 渡されても層として別の形になるよう、番号をずらしてある
            m_octaves.push_back(Octave{Perlin(DeriveSeed(seed, static_cast<u32>(i) + 64u)), frequency, amplitude});
            amplitudeSum += amplitude;

            frequency *= params.lacunarity;
            amplitude *= params.gain;
        }

        m_normalizer = (amplitudeSum > 0.0f) ? (1.0f / amplitudeSum) : 0.0f;
    }

    //------------------------------------------------------------------------
    //! 指定位置のノイズ値を求めます。
    //------------------------------------------------------------------------
    float Ridged::Sample(const Vec3& p) const {
        float sum = 0.0f;

        // 前のオクターブの値で次を絞る（multifractal）。
        // 低い場所には高周波が乗らなくなり、稜線だけが細かくなる
        float weight = 1.0f;

        for(const Octave& octave : m_octaves) {
            // 0 交差で折り返すと、谷が尖った稜線に変わる
            float ridge = 1.0f - std::abs(octave.perlin.Sample(p * octave.frequency));

            // 尖り具合。2乗すると稜線が細くなり、1乗のままだと丸い
            ridge = ridge * ridge * m_sharpness + ridge * (1.0f - m_sharpness);

            sum += ridge * octave.amplitude * weight;

            // 次のオクターブの重み。1 を超えないようクランプする
            weight = std::clamp(ridge, 0.0f, 1.0f);
        }

        // ridge は [0,1] なので、そのままだと片側にしか振れない。
        // 他の層と同じ [-1,1] の尺度へ移す
        return sum * m_normalizer * 2.0f - 1.0f;
    }

}    // namespace RockCore
