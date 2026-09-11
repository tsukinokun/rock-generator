//----------------------------------------------------------------------------
//! @file   Perlin.cpp
//! @brief  3次元の勾配ノイズの実装
//----------------------------------------------------------------------------
#include <RockCore/Noise/Perlin.hpp>

#include <RockCore/Random/Pcg32.hpp>

#include <cmath>
#include <numeric>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //--------------------------------------------------------------------
        //! Perlin の 5 次補間曲線です。1階・2階微分が端点で 0 になるため、
        //! 格子境界で折れ目が出ません。
        //! @param  [in] t 0〜1 の値
        //! @return 滑らかにした値
        //--------------------------------------------------------------------
        float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

        //--------------------------------------------------------------------
        //! 線形補間します。
        //! @param  [in] a 始点
        //! @param  [in] b 終点
        //! @param  [in] t 補間係数
        //! @return 補間結果
        //--------------------------------------------------------------------
        float Mix(float a, float b, float t) { return a + (b - a) * t; }

    }    // namespace

    //------------------------------------------------------------------------
    //! 順列表をシードから作ります。
    //------------------------------------------------------------------------
    Perlin::Perlin(u64 seed) {
        std::array<u8, 256> base{};
        for(int i = 0; i < 256; ++i) {
            base[static_cast<size_t>(i)] = static_cast<u8>(i);
        }

        // Fisher-Yates。std::shuffle を使わないのは、結果が標準ライブラリの
        // 実装に依存してしまい「同じシードなら同じ岩」が環境をまたいで保証できないため
        Pcg32 rng(seed);
        for(u32 i = 255; i > 0; --i) {
            const u32 j = rng.NextBelow(i + 1);
            std::swap(base[i], base[j]);
        }

        for(size_t i = 0; i < 512; ++i) {
            m_permutation[i] = base[i & 255];
        }
    }

    //------------------------------------------------------------------------
    //! 指定位置のノイズ値を求めます。
    //------------------------------------------------------------------------
    float Perlin::Sample(const Vec3& p) const {
        const float fx = std::floor(p.x);
        const float fy = std::floor(p.y);
        const float fz = std::floor(p.z);

        const int xi = static_cast<int>(fx) & 255;
        const int yi = static_cast<int>(fy) & 255;
        const int zi = static_cast<int>(fz) & 255;

        const float tx = p.x - fx;
        const float ty = p.y - fy;
        const float tz = p.z - fz;

        const float u = Fade(tx);
        const float v = Fade(ty);
        const float w = Fade(tz);

        const u8 a  = m_permutation[static_cast<size_t>(xi)];
        const u8 b  = m_permutation[static_cast<size_t>(xi) + 1];
        const u8 aa = m_permutation[static_cast<size_t>(a) + static_cast<size_t>(yi)];
        const u8 ab = m_permutation[static_cast<size_t>(a) + static_cast<size_t>(yi) + 1];
        const u8 ba = m_permutation[static_cast<size_t>(b) + static_cast<size_t>(yi)];
        const u8 bb = m_permutation[static_cast<size_t>(b) + static_cast<size_t>(yi) + 1];

        const size_t z0 = static_cast<size_t>(zi);
        const size_t z1 = z0 + 1;

        const float x00 = Mix(Gradient(m_permutation[static_cast<size_t>(aa) + z0], {tx, ty, tz}),
                              Gradient(m_permutation[static_cast<size_t>(ba) + z0], {tx - 1.0f, ty, tz}), u);
        const float x10 = Mix(Gradient(m_permutation[static_cast<size_t>(ab) + z0], {tx, ty - 1.0f, tz}),
                              Gradient(m_permutation[static_cast<size_t>(bb) + z0], {tx - 1.0f, ty - 1.0f, tz}), u);
        const float x01 = Mix(Gradient(m_permutation[static_cast<size_t>(aa) + z1], {tx, ty, tz - 1.0f}),
                              Gradient(m_permutation[static_cast<size_t>(ba) + z1], {tx - 1.0f, ty, tz - 1.0f}), u);
        const float x11 = Mix(Gradient(m_permutation[static_cast<size_t>(ab) + z1], {tx, ty - 1.0f, tz - 1.0f}),
                              Gradient(m_permutation[static_cast<size_t>(bb) + z1], {tx - 1.0f, ty - 1.0f, tz - 1.0f}), u);

        // 正規化係数。勾配が 12 方向の単位でない斜めベクトルなので、
        // 生の値は [-1,1] をわずかに超える。おおむね収めるために 1/0.866 を掛ける
        return Mix(Mix(x00, x10, v), Mix(x01, x11, v), w) * 1.1547f;
    }

    //------------------------------------------------------------------------
    //! 格子点の勾配と位置ベクトルの内積を求めます。
    //------------------------------------------------------------------------
    float Perlin::Gradient(u8 hash, const Vec3& d) {
        // 立方体の12辺方向を勾配に使う（Perlin の改良版と同じ選び方）
        switch(hash & 15) {
        case 0:  return d.x + d.y;
        case 1:  return -d.x + d.y;
        case 2:  return d.x - d.y;
        case 3:  return -d.x - d.y;
        case 4:  return d.x + d.z;
        case 5:  return -d.x + d.z;
        case 6:  return d.x - d.z;
        case 7:  return -d.x - d.z;
        case 8:  return d.y + d.z;
        case 9:  return -d.y + d.z;
        case 10: return d.y - d.z;
        case 11: return -d.y - d.z;
        case 12: return d.x + d.y;
        case 13: return -d.y + d.z;
        case 14: return -d.x + d.y;
        default: return -d.y - d.z;
        }
    }

}    // namespace RockCore
