//----------------------------------------------------------------------------
//! @file   NoiseStack.cpp
//! @brief  多層ノイズの実装
//----------------------------------------------------------------------------
#include <RockCore/Noise/NoiseStack.hpp>

#include <RockCore/Random/Pcg32.hpp>

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

            // Worley / Ridged は Phase 3 で実装する。未実装の種類を
            // こっそり Fbm として扱うと「設定したのに形が変わらない」より
            // 分かりにくい挙動になるため、無効な層として落とす
            if(params.kind != NoiseLayerKind::Fbm) {
                continue;
            }

            // 層番号で固定のサブシードを割る。スライダを1本動かしたときに
            // 他の層の形が変わらないことが狙い。i に定数を足しているのは
            // Fbm 内部のオクターブ用シードと衝突させないため
            const u64 layerSeed = DeriveSeed(seed, static_cast<u32>(i) * 1024u + 7u);

            Layer layer{};
            layer.fbm       = std::make_unique<Fbm>(params.fbm, layerSeed, maxFrequency);
            layer.amplitude = params.amplitude;

            // 帯域制限で全オクターブが落ちた層は評価しても 0 なので持たない
            if(layer.fbm->GetActiveOctaveCount() == 0) {
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
            sum += layer.fbm->Sample(p) * layer.amplitude;
        }
        return sum;
    }

}    // namespace RockCore
