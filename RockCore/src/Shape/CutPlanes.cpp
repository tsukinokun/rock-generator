//----------------------------------------------------------------------------
//! @file   CutPlanes.cpp
//! @brief  破断面の生成の実装
//----------------------------------------------------------------------------
#include <RockCore/Shape/CutPlanes.hpp>

#include <RockCore/Random/Pcg32.hpp>

#include <algorithm>
#include <cmath>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //! 平面用のサブシード番号。
        //!
        //! ノイズ層は層番号 * 1024 + 7 を使っている（NoiseStack.cpp）ので、
        //! そこと衝突しない大きな定数を選んである。これを分けておかないと、
        //! ノイズのスライダを動かすたびに破断面まで変わって形が落ち着かない
        constexpr u32 kCutPlaneSeedIndex = 900001u;

        //! 平面の枚数の上限。これを超えると smin の縮みが積もって形が痩せる
        constexpr int kMaxPlaneCount = 16;

        //--------------------------------------------------------------------
        //! 球面上の一様な単位ベクトルを 1 つ取り出します。
        //!
        //! z を [-1,1] の一様乱数にすると球面上で一様になる（円筒投影の等積性）。
        //! 極に偏る「角度を2つ振る」やり方は使わない。
        //!
        //! @param  [in,out] rng 乱数生成器
        //! @return 単位ベクトル
        //--------------------------------------------------------------------
        Vec3 SampleUnitSphere(Pcg32& rng) {
            const float z     = rng.NextSigned();
            const float theta = rng.NextFloat() * 6.2831853f;
            const float r     = std::sqrt(std::max(1.0f - z * z, 0.0f));
            return {r * std::cos(theta), z, r * std::sin(theta)};
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! 破断面を作ります。
    //------------------------------------------------------------------------
    std::vector<CutPlane> BuildCutPlanes(const CutPlaneSettings& settings, u64 seed) {
        const int count = std::clamp(settings.count, 0, kMaxPlaneCount);

        std::vector<CutPlane> planes;
        if(count <= 0 || settings.radius <= 0.0f) {
            return planes;
        }

        planes.reserve(static_cast<size_t>(count));

        Pcg32 rng(DeriveSeed(seed, kCutPlaneSeedIndex));

        const float axisBias = std::clamp(settings.axisBias, 0.0f, 1.0f);
        const float depth    = std::clamp(settings.depth, 0.0f, 0.9f);

        //--------------------------------------------------------------------
        //! ほぼ同じ平面が既にあるかを見ます。
        //!
        //! 重なった平面は形に何も足さないうえ、メッシュのクリップを壊す。
        //! 1枚目で切った跡の頂点がそのまま2枚目の平面にも乗ってしまい、
        //! 切り口の境界を集めるときに「平面上にある頂点」が大量に混ざって、
        //! 断面の多角形が滅茶苦茶になる（穴が開く・辺が3枚以上の面に共有される）
        //--------------------------------------------------------------------
        auto isDuplicate = [&planes, &settings](const Vec3& normal, float distance) {
            constexpr float kParallelCosine = 0.95f;
            const float     minGap          = settings.radius * 0.10f;

            for(const CutPlane& existing : planes) {
                if(Dot(existing.normal, normal) > kParallelCosine &&
                   std::abs(existing.distance - distance) < minGap) {
                    return true;
                }
            }
            return false;
        };

        // 棄却のたびに引き直すので、無限に粘らないよう試行回数に上限を置く
        int attempts = 0;

        for(int i = 0; i < count; ++i) {
            Vec3 normal = SampleUnitSphere(rng);

            //----------------------------------------------------------------
            // 軸バイアス。法線を上下（±Y）へ寄せると、上下に平らな面を持つ
            // 板状の岩になる。符号は元の法線の向きを尊重して、上下どちらの
            // 面になるかは乱数任せにする
            //----------------------------------------------------------------
            if(axisBias > 0.0f) {
                const Vec3 axis = {0.0f, (normal.y < 0.0f) ? -1.0f : 1.0f, 0.0f};
                normal          = Normalize(Lerp(normal, axis, axisBias));
            }

            //----------------------------------------------------------------
            // 食い込む深さ。
            //
            // 一様分布 [0, depth] にすると半分の平面が浅すぎてほとんど切らず、
            // 枚数を増やしても面が増えない。範囲を [depth/2, depth] へ寄せて
            // どの平面も意味のある深さで切るようにする。
            //
            // 下限のクランプは星形を守るため。原点がすべての半空間の内側に
            // 残らないと半径関数が負になって破綻する
            //----------------------------------------------------------------
            const float bite     = depth * (0.5f + 0.5f * rng.NextFloat());
            const float distance = std::max(settings.radius * (1.0f - bite), settings.radius * 0.25f);

            if(isDuplicate(normal, distance)) {
                constexpr int kMaxAttempts = 64;
                if(++attempts < kMaxAttempts) {
                    --i;    // 引き直す
                }
                continue;
            }

            planes.push_back(CutPlane{normal, distance});
        }

        return planes;
    }

}    // namespace RockCore
