//----------------------------------------------------------------------------
//! @file   Worley.cpp
//! @brief  Worley ノイズの実装
//----------------------------------------------------------------------------
#include <RockCore/Noise/Worley.hpp>

#include <algorithm>
#include <cmath>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //--------------------------------------------------------------------
        //! セル番号とシードから 64bit のハッシュを作ります。
        //!
        //! 負のセル番号も扱うので、いったん符号なしへ変換してから混ぜる。
        //!
        //! @param  [in] seed  シード
        //! @param  [in] cellX セルの X 番号
        //! @param  [in] cellY セルの Y 番号
        //! @param  [in] cellZ セルの Z 番号
        //! @return ハッシュ値
        //--------------------------------------------------------------------
        u64 HashCell(u64 seed, int cellX, int cellY, int cellZ) {
            u64 value = seed;
            value = (value ^ static_cast<u64>(static_cast<u32>(cellX))) * 0x9E3779B97F4A7C15ULL;
            value = (value ^ static_cast<u64>(static_cast<u32>(cellY))) * 0xC2B2AE3D27D4EB4FULL;
            value = (value ^ static_cast<u64>(static_cast<u32>(cellZ))) * 0x165667B19E3779F9ULL;

            // SplitMix64 の finalizer で撹拌する。隣り合うセルの値が
            // 相関すると、割れ目が格子に沿った縞に見えてしまう
            value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
            value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
            return value ^ (value >> 31);
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! セルの大きさと溝の幅を決めます。
    //------------------------------------------------------------------------
    Worley::Worley(u64 seed, float frequency, float width)
        : m_seed(seed)
        , m_frequency(std::max(frequency, 0.0f))
        , m_invWidth(1.0f / std::max(width, 1e-3f)) {}

    //------------------------------------------------------------------------
    //! セルの特徴点を求めます。
    //------------------------------------------------------------------------
    Vec3 Worley::FeaturePoint(int cellX, int cellY, int cellZ) const {
        //--------------------------------------------------------------------
        // ハッシュから直接3成分を切り出す。
        //
        // ここは1サンプルにつき 27 回（3x3x3 の近傍ぶん）呼ばれ、ベイクでは
        // テクセルごとに5回サンプルするので、1024x1024 なら1億回を超える。
        // Pcg32 を毎回構築していたときは、これだけでベイクが数分かかっていた。
        //
        // HashCell が SplitMix64 の finalizer まで通してあるので、
        // ビットは十分に撹拌されている。21 ビットずつ切り出せば
        // 特徴点の分解能としては過剰なくらい
        //--------------------------------------------------------------------
        const u64 hash = HashCell(m_seed, cellX, cellY, cellZ);

        constexpr u64   kMask = 0x1FFFFFull;    // 21 ビット
        constexpr float kInv  = 1.0f / 2097151.0f;

        // セルの真ん中へ寄せる。完全な一様分布にすると特徴点どうしが
        // 極端に近づく組が出て、そこだけ不自然に細長い溝になる
        constexpr float kMargin = 0.15f;
        constexpr float kSpan   = 1.0f - kMargin * 2.0f;

        return {kMargin + static_cast<float>(hash & kMask) * kInv * kSpan,
                kMargin + static_cast<float>((hash >> 21) & kMask) * kInv * kSpan,
                kMargin + static_cast<float>((hash >> 42) & kMask) * kInv * kSpan};
    }

    //------------------------------------------------------------------------
    //! 割れ目のマスクを求めます。
    //------------------------------------------------------------------------
    float Worley::SampleCrack(const Vec3& p) const {
        const Vec3 scaled{p.x * m_frequency, p.y * m_frequency, p.z * m_frequency};

        const float baseX = std::floor(scaled.x);
        const float baseY = std::floor(scaled.y);
        const float baseZ = std::floor(scaled.z);

        const int cellX = static_cast<int>(baseX);
        const int cellY = static_cast<int>(baseY);
        const int cellZ = static_cast<int>(baseZ);

        // セル内の位置
        const Vec3 local{scaled.x - baseX, scaled.y - baseY, scaled.z - baseZ};

        //--------------------------------------------------------------------
        // 3x3x3 の近傍から F1（最近傍）と F2（2番目）を探す。
        // 特徴点をセルの内側へ寄せてあるので、この範囲の外に
        // より近い点が来ることはない
        //--------------------------------------------------------------------
        float nearest       = 1e30f;
        float secondNearest = 1e30f;

        for(int dz = -1; dz <= 1; ++dz) {
            for(int dy = -1; dy <= 1; ++dy) {
                for(int dx = -1; dx <= 1; ++dx) {
                    const Vec3 feature = FeaturePoint(cellX + dx, cellY + dy, cellZ + dz);

                    const Vec3 offset{feature.x + static_cast<float>(dx) - local.x,
                                      feature.y + static_cast<float>(dy) - local.y,
                                      feature.z + static_cast<float>(dz) - local.z};

                    const float distance = Length(offset);

                    if(distance < nearest) {
                        secondNearest = nearest;
                        nearest       = distance;
                    } else if(distance < secondNearest) {
                        secondNearest = distance;
                    }
                }
            }
        }

        //--------------------------------------------------------------------
        // F2 - F1 はセル境界で 0 へ落ちる。1 から引いて境界だけを立てる。
        // これを半径から引くと、境界に沿って細い溝が彫れる
        //--------------------------------------------------------------------
        const float edgeDistance = (secondNearest - nearest) * m_invWidth;
        return 1.0f - std::clamp(edgeDistance, 0.0f, 1.0f);
    }

}    // namespace RockCore
