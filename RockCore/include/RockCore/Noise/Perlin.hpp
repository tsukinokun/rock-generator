//----------------------------------------------------------------------------
//! @file   Perlin.hpp
//! @brief  3次元の勾配ノイズ
//! @detail 格子点ごとの勾配ベクトルを順列表から引く、素直な Perlin ノイズです。
//!         順列表はシードから作るため、同じシードなら必ず同じ形になります。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Math/Vec.hpp>
#include <RockCore/Types.hpp>

#include <array>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @class Perlin
    //! シード付きの 3D Perlin ノイズ
    //------------------------------------------------------------------------
    class Perlin {
    public:
        //! 順列表をシードから作ります。
        //! @param [in] seed シード
        explicit Perlin(u64 seed);

        //! 指定位置のノイズ値を求めます。
        //! @param  [in] p 評価位置
        //! @return おおむね [-1, 1] の値
        float Sample(const Vec3& p) const;

    private:
        //! 格子点の勾配と位置ベクトルの内積を求めます。
        //! @param  [in] hash 格子点のハッシュ値
        //! @param  [in] d    格子点から評価位置へのベクトル
        //! @return 内積
        static float Gradient(u8 hash, const Vec3& d);

        //! 順列表。256 要素を2周ぶん持つことで添字の折り返し計算を省く
        std::array<u8, 512> m_permutation{};
    };

}    // namespace RockCore
