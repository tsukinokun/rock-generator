//----------------------------------------------------------------------------
//! @file   Mat3.hpp
//! @brief  3x3 行列（行優先・行ベクトル規約）
//! @detail 規約を HLSL 側へ意図的に揃えてあります。エンジンの PBR.hlsli は
//!         `float3x3 TBN = float3x3(T, B, N); mul(tangentNormal, TBN)` と書いており、
//!         これは「行に T,B,N を並べた行優先行列」に「行ベクトルを左から掛ける」形です。
//!         ここで列優先の行列型を使うと、Normal ベイクのエンコードが静かに
//!         転置された結果になり、ライトを回しても凹凸が追従しません。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Math/Vec.hpp>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @struct Mat3
    //! 3x3 行列。row[i] が i 番目の行
    //------------------------------------------------------------------------
    struct Mat3 {
        Vec3 row[3];

        //! 行を並べて行列を作ります。
        //! @param  [in] r0 0行目
        //! @param  [in] r1 1行目
        //! @param  [in] r2 2行目
        //! @return 組み立てた行列
        static constexpr Mat3 FromRows(const Vec3& r0, const Vec3& r1, const Vec3& r2) { return Mat3{{r0, r1, r2}}; }
    };

    //! 行ベクトルに行列を右から掛けます（HLSL の `mul(v, M)` と同じ）。
    //! @param  [in] v 行ベクトル
    //! @param  [in] m 行列
    //! @return v * m
    constexpr Vec3 operator*(const Vec3& v, const Mat3& m) {
        return m.row[0] * v.x + m.row[1] * v.y + m.row[2] * v.z;
    }

    //! 行列式を求めます。
    //! @param  [in] m 対象の行列
    //! @return 行列式
    constexpr float Determinant(const Mat3& m) {
        return Dot(m.row[0], Cross(m.row[1], m.row[2]));
    }

    //------------------------------------------------------------------------
    //! 厳密な逆行列を求めます。余因子展開なので近似は一切入りません。
    //!
    //! 行列式が 0 に近いときは単位行列を返します。Normal ベイクでは
    //! 「UV が潰れたテクセル」がこれに当たり、呼び出し側は (0,0,1) を書いて
    //! dilate に任せるため、ここで失敗を報告する必要はありません。
    //!
    //! @param  [in] m 対象の行列
    //! @return 逆行列
    //------------------------------------------------------------------------
    constexpr Mat3 Inverse(const Mat3& m) {
        const Vec3 c0 = Cross(m.row[1], m.row[2]);
        const Vec3 c1 = Cross(m.row[2], m.row[0]);
        const Vec3 c2 = Cross(m.row[0], m.row[1]);

        const float det = Dot(m.row[0], c0);
        if(det > -1e-20f && det < 1e-20f) {
            return Mat3::FromRows({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
        }

        // 余因子行列は「余因子ベクトルを列に並べたもの」なので、
        // 行を持つこの型では転置して詰める
        const float invDet = 1.0f / det;
        return Mat3::FromRows({c0.x * invDet, c1.x * invDet, c2.x * invDet},
                              {c0.y * invDet, c1.y * invDet, c2.y * invDet},
                              {c0.z * invDet, c1.z * invDet, c2.z * invDet});
    }

}    // namespace RockCore
