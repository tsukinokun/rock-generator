//----------------------------------------------------------------------------
//! @file   NoiseStack.cpp
//! @brief  多層ノイズの実装
//----------------------------------------------------------------------------
#include <RockCore/Noise/NoiseStack.hpp>

#include <RockCore/Random/Pcg32.hpp>

#include <algorithm>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! 各層のノイズを構築します。
    //------------------------------------------------------------------------
    NoiseStack::NoiseStack(const std::vector<NoiseLayerParams>& layers, u64 seed, float maxFrequency) {
        m_layers.reserve(layers.size());

        for(size_t i = 0; i < layers.size(); ++i) {
            const NoiseLayerParams& params = layers[i];
            if(!params.enabled || params.amplitude == 0.0f) {
                continue;
            }

            // 層番号で固定のサブシードを割る。スライダを1本動かしたときに
            // 他の層の形が変わらないことが狙い。i に定数を足しているのは
            // 各ノイズの内部がオクターブ用に使うシードと衝突させないため
            const u64 layerSeed = DeriveSeed(seed, static_cast<u32>(i) * 1024u + 7u);

            Layer layer{};
            layer.kind      = params.kind;
            layer.amplitude = params.amplitude;

            switch(params.kind) {
            case NoiseLayerKind::Fbm:
                layer.fbm = std::make_unique<Fbm>(params.fbm, layerSeed, maxFrequency);

                // 帯域制限で全オクターブが落ちた層は評価しても 0 なので持たない
                if(layer.fbm->GetActiveOctaveCount() == 0) {
                    continue;
                }
                break;

            case NoiseLayerKind::Worley:
                // Worley はオクターブを重ねない。セル境界の溝は
                // 1 スケールで十分岩に見えるし、重ねると溝が潰れて
                // ただのざらつきになる。
                // セル周波数がメッシュのナイキストを超えるなら載せない
                if(maxFrequency > 0.0f && params.fbm.frequency > maxFrequency) {
                    continue;
                }
                layer.worley = std::make_unique<Worley>(layerSeed, params.fbm.frequency, params.sharpness);
                break;

            case NoiseLayerKind::Ridged:
                layer.ridged = std::make_unique<Ridged>(params.fbm, layerSeed, maxFrequency, params.sharpness);
                if(layer.ridged->GetActiveOctaveCount() == 0) {
                    continue;
                }
                break;

            default:
                continue;
            }

            m_layers.push_back(std::move(layer));
        }
    }

    //------------------------------------------------------------------------
    //! 合計の変位を求めます。
    //------------------------------------------------------------------------
    float NoiseStack::Sample(const Vec3& p) const {
        float sum = 0.0f;

        for(const Layer& layer : m_layers) {
            switch(layer.kind) {
            case NoiseLayerKind::Fbm:
                sum += layer.fbm->Sample(p) * layer.amplitude;
                break;

            case NoiseLayerKind::Worley:
                // 割れ目は必ず内側へ彫る。他の層と同じように正負へ振ると
                // 溝の隣が盛り上がってしまい、削れた跡に見えなくなる
                sum -= layer.worley->SampleCrack(p) * layer.amplitude;
                break;

            case NoiseLayerKind::Ridged:
                sum += layer.ridged->Sample(p) * layer.amplitude;
                break;

            default:
                break;
            }
        }

        return sum;
    }

}    // namespace RockCore
