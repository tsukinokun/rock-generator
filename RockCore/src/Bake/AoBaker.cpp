//----------------------------------------------------------------------------
//! @file   AoBaker.cpp
//! @brief  AO のベイクの実装
//----------------------------------------------------------------------------
#include <RockCore/Bake/AoBaker.hpp>

#include <algorithm>
#include <cmath>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //--------------------------------------------------------------------
        //! レイ 1 本あたりの探索分割数。
        //!
        //! Raycast1D は「外接球の内側にいる区間」を等分して符号の変化を探す。
        //! 分割を細かくするほど細い割れ目を飛び越えにくくなるが、そのまま
        //! ベイク時間に効く。既定の岩は区間が 0.1m 前後なので、20 分割で
        //! 刻みが 0.005m。割れ目の深さ（0.007m 前後）より細かい。
        //!
        //! **60 まで上げて実測したが、平均 AO は 0.995 → 0.994 しか動かず
        //! 時間だけ 2.8 倍になった。** 既定の岩の AO が薄いのは刻みの
        //! 取りこぼしではなく、形がほとんど凸だから。ここを上げても解決しない
        //--------------------------------------------------------------------
        constexpr int kMarchSteps = 20;

        //--------------------------------------------------------------------
        //! 自己交差を避けるために、開始点を法線方向へどれだけ浮かせるか
        //! （外接半径に対する比率）。
        //!
        //! **開始点そのものを浮かせること。** 表面から撃って探索開始距離だけ
        //! 遅らせる書き方にすると、ほぼ接線方向のレイが自分自身に当たる。
        //! 法線は 4 面体差分の近似なので真の法線から微小にずれており、
        //! 「接線のつもり」のレイがわずかに内側を向く。球で試すと
        //! 一部のテクセルだけ 1 本だけ当たる、という形で出た
        //--------------------------------------------------------------------
        constexpr float kRayBias = 2e-3f;

        //! レイ本数の上限。これ以上は時間が伸びるだけで絵が変わらない
        constexpr int kMaxRayCount = 256;

        //--------------------------------------------------------------------
        //! 2 進の基数逆順（van der Corput 列）。
        //!
        //! Hammersley 列の片側。乱数で方向を振ると同じ本数でもムラが大きく
        //! 出るので、低食い違い量列を使う。決定論も自動的に保たれる
        //!
        //! @param  [in] index 番号
        //! @return [0,1) の値
        //--------------------------------------------------------------------
        float RadicalInverse2(u32 index) {
            index = (index << 16u) | (index >> 16u);
            index = ((index & 0x55555555u) << 1u) | ((index & 0xAAAAAAAAu) >> 1u);
            index = ((index & 0x33333333u) << 2u) | ((index & 0xCCCCCCCCu) >> 2u);
            index = ((index & 0x0F0F0F0Fu) << 4u) | ((index & 0xF0F0F0F0u) >> 4u);
            index = ((index & 0x00FF00FFu) << 8u) | ((index & 0xFF00FF00u) >> 8u);

            return static_cast<float>(index) * 2.3283064365386963e-10f;    // 1 / 2^32
        }

        //--------------------------------------------------------------------
        //! テクセルごとの回転量を作ります。
        //!
        //! 全テクセルで同じ方向へ撃つと、低食い違い量列の規則性がそのまま
        //! 縞になって出る。テクセルごとに列全体を回して散らす
        //! （Cranley-Patterson 回転）。ハッシュなので決定論は保たれる。
        //!
        //! @param  [in] x    横位置
        //! @param  [in] y    縦位置
        //! @param  [in] seed シード
        //! @return [0,1) の値
        //--------------------------------------------------------------------
        float TexelJitter(int x, int y, u64 seed) {
            u64 hash = seed;
            hash     = (hash ^ static_cast<u64>(static_cast<u32>(x))) * 0x9E3779B97F4A7C15ULL;
            hash     = (hash ^ static_cast<u64>(static_cast<u32>(y))) * 0xC2B2AE3D27D4EB4FULL;

            hash = (hash ^ (hash >> 30)) * 0xBF58476D1CE4E5B9ULL;
            hash = (hash ^ (hash >> 27)) * 0x94D049BB133111EBULL;
            hash = hash ^ (hash >> 31);

            return static_cast<float>(hash >> 40) * (1.0f / 16777216.0f);    // 24bit
        }

        //--------------------------------------------------------------------
        //! 法線まわりの正規直交基底を作ります。
        //! @param  [in]  normal   法線
        //! @param  [out] outTangent  接線
        //! @param  [out] outBinormal 従法線
        //--------------------------------------------------------------------
        void BuildBasis(const Vec3& normal, Vec3& outTangent, Vec3& outBinormal) {
            // 成分の絶対値が最小の軸を選ぶ。法線に近い軸を選ぶと外積が潰れる
            const float absX = std::abs(normal.x);
            const float absY = std::abs(normal.y);
            const float absZ = std::abs(normal.z);

            Vec3 axis{0.0f, 0.0f, 1.0f};
            if(absX <= absY && absX <= absZ) {
                axis = Vec3{1.0f, 0.0f, 0.0f};
            } else if(absY <= absZ) {
                axis = Vec3{0.0f, 1.0f, 0.0f};
            }

            outTangent  = Normalize(Cross(normal, axis));
            outBinormal = Cross(normal, outTangent);
        }

        //! [0,1] の値をバイトへ詰めます。
        //! @param  [in] value 値
        //! @return バイト
        u8 EncodeLinear(float value) {
            return static_cast<u8>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! AO を焼きます。
    //------------------------------------------------------------------------
    bool BakeAoMap(const BakeGBuffer&      gbuffer,
                   const RockField&        fullField,
                   const RockParams&       params,
                   ImageBuffer&            outImage,
                   const ProgressCallback& progress,
                   const CancelToken*      cancel,
                   AoBakeStats*            outStats) {
        const int size = gbuffer.GetSize();
        if(size <= 0) {
            return false;
        }

        // 未被覆は「遮蔽なし」。埋め損ねても暗くならない側に倒しておく
        outImage.Resize(size, size);
        outImage.Fill(255u, 255u, 255u, 255u);

        const int   rayCount = std::clamp(params.aoRayCount, 1, kMaxRayCount);
        const float distance = std::max(params.aoDistance, 0.0f);

        AoBakeStats stats{};
        stats.minOcclusion = 1.0f;

        if(distance <= 0.0f) {
            // 距離 0 は「AO を切る」意味。真っ白のまま返す
            if(outStats) {
                stats.minOcclusion  = 1.0f;
                stats.meanOcclusion = 1.0f;
                *outStats           = stats;
            }
            return true;
        }

        const float bias = std::max(fullField.GetBoundingRadius() * kRayBias, 1e-5f);

        double occlusionSum = 0.0;
        u64    rayTotal     = 0;

        for(int y = 0; y < size; ++y) {
            // 中断の確認は行単位。ここは一番長い段なので、行ごとに見ないと
            // スライダを離したときの取り消しが効かない
            if(cancel && cancel->IsCancelled()) {
                return false;
            }

            for(int x = 0; x < size; ++x) {
                if(!gbuffer.IsCovered(x, y)) {
                    continue;
                }

                const BakeSample& sample = gbuffer.GetSample(x, y);

                //------------------------------------------------------------
                // 真の表面とその法線。Normal ベイクと同じ求め方にしてある。
                // ローポリの補間法線で撃つと、細かい凹凸の AO が出ない
                //------------------------------------------------------------
                const Vec3 surface = fullField.ProjectToSurface(sample.position);
                const Vec3 normal  = Normalize(fullField.Gradient(surface, sample.texelWorldSize));

                // 表面から法線方向へ浮かせた点から撃つ
                const Vec3 origin = surface + normal * bias;

                Vec3 tangent;
                Vec3 binormal;
                BuildBasis(normal, tangent, binormal);

                const float jitterU = TexelJitter(x, y, params.seed);
                const float jitterV = TexelJitter(x, y, params.seed ^ 0xA5A5A5A5u);

                int occluded = 0;
                int cast     = 0;

                for(int i = 0; i < rayCount; ++i) {
                    //--------------------------------------------------------
                    // 余弦分布で半球をサンプルする。
                    //
                    // AO の積分は余弦重みつきなので、分布の方に重みを持たせて
                    // おけば単純な平均で済む。一様分布にして後から cos を
                    // 掛けるより、同じ本数でムラが小さい
                    //--------------------------------------------------------
                    float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(rayCount) + jitterU;
                    float v = RadicalInverse2(static_cast<u32>(i)) + jitterV;

                    u -= std::floor(u);
                    v -= std::floor(v);

                    const float radius = std::sqrt(u);
                    const float phi    = v * 6.2831853f;

                    const Vec3 direction = tangent * (radius * std::cos(phi)) +
                                           binormal * (radius * std::sin(phi)) +
                                           normal * std::sqrt(std::max(1.0f - u, 0.0f));

                    float hitT = 0.0f;
                    if(fullField.Raycast1D(origin, Normalize(direction), 0.0f, distance, kMarchSteps, hitT)) {
                        ++occluded;
                    }
                    ++cast;
                }

                const float occlusion =
                    1.0f - static_cast<float>(occluded) / static_cast<float>(std::max(cast, 1));

                const u8 encoded = EncodeLinear(occlusion);
                outImage.SetPixel(x, y, encoded, encoded, encoded, 255u);

                stats.minOcclusion = std::min(stats.minOcclusion, occlusion);
                occlusionSum += occlusion;
                rayTotal += static_cast<u64>(cast);
                ++stats.texelsWritten;
            }

            ReportProgress(progress, static_cast<float>(y + 1) / static_cast<float>(size), "Bake: AO");
        }

        if(stats.texelsWritten > 0) {
            stats.meanOcclusion       = static_cast<float>(occlusionSum / static_cast<double>(stats.texelsWritten));
            stats.averageRaysPerTexel = static_cast<float>(static_cast<double>(rayTotal) /
                                                           static_cast<double>(stats.texelsWritten));
        } else {
            stats.minOcclusion = 1.0f;
        }

        if(outStats) {
            *outStats = stats;
        }
        return true;
    }

}    // namespace RockCore
