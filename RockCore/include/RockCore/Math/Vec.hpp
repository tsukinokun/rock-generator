//----------------------------------------------------------------------------
//! @file   Vec.hpp
//! @brief  RockCore 内で使う最小のベクトル型
//! @detail エンジンの hlsl++ ではなく自前の型を持ちます。RockCore を
//!         エンジンから切り離しておくと、CLI とテストがそのまま成立します。
//----------------------------------------------------------------------------
#pragma once
#include <cmath>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @struct Vec2
    //! 2要素のベクトル
    //------------------------------------------------------------------------
    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;

        constexpr Vec2() = default;
        constexpr Vec2(float inX, float inY) : x(inX), y(inY) {}
    };

    constexpr Vec2 operator+(const Vec2& a, const Vec2& b) { return {a.x + b.x, a.y + b.y}; }
    constexpr Vec2 operator-(const Vec2& a, const Vec2& b) { return {a.x - b.x, a.y - b.y}; }
    constexpr Vec2 operator*(const Vec2& v, float s) { return {v.x * s, v.y * s}; }
    constexpr Vec2 operator*(float s, const Vec2& v) { return {v.x * s, v.y * s}; }

    //! 2次元の外積（スカラー）を求めます。
    //! @param  [in] a 左辺のベクトル
    //! @param  [in] b 右辺のベクトル
    //! @return a と b が張る平行四辺形の符号付き面積
    constexpr float Cross2(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

    //------------------------------------------------------------------------
    //! @struct Vec3
    //! 3要素のベクトル
    //------------------------------------------------------------------------
    struct Vec3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        constexpr Vec3() = default;
        constexpr Vec3(float inX, float inY, float inZ) : x(inX), y(inY), z(inZ) {}

        //! 添字でアクセスします。（勾配のテトラ差分で軸を回すのに使う）
        //! @param  [in] index 0=x, 1=y, 2=z
        //! @return 該当成分への参照
        float&       operator[](int index) { return (&x)[index]; }
        const float& operator[](int index) const { return (&x)[index]; }
    };

    constexpr Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    constexpr Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    constexpr Vec3 operator-(const Vec3& v) { return {-v.x, -v.y, -v.z}; }
    constexpr Vec3 operator*(const Vec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
    constexpr Vec3 operator*(float s, const Vec3& v) { return {v.x * s, v.y * s, v.z * s}; }
    constexpr Vec3 operator/(const Vec3& v, float s) { return {v.x / s, v.y / s, v.z / s}; }

    //! 成分ごとの積を求めます。（異方スケールを掛けるのに使う）
    //! @param  [in] a 左辺のベクトル
    //! @param  [in] b 右辺のベクトル
    //! @return 成分ごとに掛け合わせた結果
    constexpr Vec3 Mul(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }

    //! 内積を求めます。
    //! @param  [in] a 左辺のベクトル
    //! @param  [in] b 右辺のベクトル
    //! @return 内積
    constexpr float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    //! 外積を求めます。
    //! @param  [in] a 左辺のベクトル
    //! @param  [in] b 右辺のベクトル
    //! @return a × b
    constexpr Vec3 Cross(const Vec3& a, const Vec3& b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    //! 長さの2乗を求めます。
    //! @param  [in] v 対象のベクトル
    //! @return 長さの2乗
    constexpr float LengthSq(const Vec3& v) { return Dot(v, v); }

    //! 長さを求めます。
    //! @param  [in] v 対象のベクトル
    //! @return 長さ
    inline float Length(const Vec3& v) { return std::sqrt(LengthSq(v)); }

    //! 正規化します。長さ 0 のときは (0,0,1) を返します。
    //! @param  [in] v 対象のベクトル
    //! @return 単位ベクトル
    inline Vec3 Normalize(const Vec3& v) {
        const float lenSq = LengthSq(v);
        if(lenSq <= 1e-20f) {
            // 縮退。呼び出し側で分岐させるより、法線として無害な +Z を返す方が扱いやすい
            return {0.0f, 0.0f, 1.0f};
        }
        return v / std::sqrt(lenSq);
    }

    //! 線形補間します。
    //! @param  [in] a 始点
    //! @param  [in] b 終点
    //! @param  [in] t 補間係数
    //! @return 補間結果
    constexpr Vec3 Lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

}    // namespace RockCore
