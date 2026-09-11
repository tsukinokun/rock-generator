//----------------------------------------------------------------------------
//! @file   TangentFrame.hpp
//! @brief  エンジンの TBN 行列と、タンジェント空間への符号化
//! @detail 【ここを間違えると Normal マップが静かに壊れる】
//!
//!         エンジンは頂点に接線を持たず、PS 内で ddx/ddy から TBN を
//!         再構成します（PBR.hlsli:155-174 の ApplyNormalMap）。
//!
//!             float3 dp1 = ddx(worldPos);   float3 dp2 = ddy(worldPos);
//!             float2 duv1 = ddx(uv);        float2 duv2 = ddy(uv);
//!             float3 dp2perp = cross(dp2, N);
//!             float3 dp1perp = cross(N, dp1);
//!             float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
//!             float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
//!             float maxLenSq = max(dot(T,T), dot(B,B));
//!             float invmax   = (maxLenSq > 0.0f) ? rsqrt(maxLenSq) : 0.0f;
//!             float3x3 TBN = float3x3(T * invmax, B * invmax, N);
//!             return normalize(mul(tangentNormal, TBN));
//!
//!         この T と B は互いに直交していません（各々 N には直交する）。
//!         さらに共通のスカラ invmax でしか正規化されないため、
//!         「T,B,N は正規直交」と仮定して dot で符号化すると、UV が異方な
//!         場所で法線が必ずずれます。3x3 を厳密に逆行列する必要があります。
//!
//!         ── ddx/ddy を再現する必要はない（導出） ──
//!
//!         dp1 = dPdu*duv1.x + dPdv*duv1.y、dp2 = dPdu*duv2.x + dPdv*duv2.y を
//!         代入して整理すると、det = duv1.x*duv2.y - duv1.y*duv2.x として
//!
//!             T = det * cross(dPdv, N)
//!             B = det * cross(N, dPdu)
//!
//!         になります。invmax は T,B の二乗長から作られるので、共通因子
//!         |det| は T*invmax / B*invmax から完全に消えます。
//!         残るのは det の符号だけで、これは
//!         「UV チャートが面の向きに対してミラーかどうか」に等しく、
//!         視点に依らず sign(dot(cross(dPdu, dPdv), N)) で求まります。
//!
//!         よって画面微分なしの閉形式で厳密に一致します。
//!
//!         なお |det| を落としてあることには副次的な効き目があります。
//!         シェーダ側は元々 invmax のしきい値を絶対値（1e-8f）で持っており、
//!         det が小さい（＝オブジェクトが画面上で大きい）ときに T,B が
//!         丸ごと 0 へ潰れてノーマルマップが無効化される不具合がありました。
//!         こちらは |T| が 1 前後になるため、その領域に入りません。
//!         シェーダ側も 0 との比較へ直してあります
//!         （TsukinoEngine: [Fix] ApplyNormalMap の commit）。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Math/Mat3.hpp>
#include <RockCore/Math/Vec.hpp>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! エンジンが PS 内で組むのと同じ TBN 行列を作ります。
    //!
    //! 返る行列は行に (T*invmax, B*invmax, N) を並べた形で、
    //! `tangentNormal * M` が HLSL の `mul(tangentNormal, TBN)` に一致します。
    //!
    //! @param  [in] normal 補間済みの頂点法線（面法線ではない）
    //! @param  [in] dPdu   位置の u 方向微分
    //! @param  [in] dPdv   位置の v 方向微分
    //! @return TBN 行列
    //------------------------------------------------------------------------
    Mat3 BuildEngineTbn(const Vec3& normal, const Vec3& dPdu, const Vec3& dPdv);

    //------------------------------------------------------------------------
    //! TBN が潰れていて符号化できないかを返します。
    //!
    //! しきい値は TangentFrame.cpp に1箇所だけ置いてあります。同じ判定を
    //! 呼び出し側（ベイカの統計など）で二重に書かないためのものです。
    //!
    //! @param  [in] tbn BuildEngineTbn が返した行列
    //! @return 潰れていれば true
    //------------------------------------------------------------------------
    bool IsEngineTbnDegenerate(const Mat3& tbn);

    //------------------------------------------------------------------------
    //! ワールド法線をタンジェント空間の法線へ符号化します。
    //!
    //! `normalize(tn * M) = worldNormal` を tn について厳密に解きます。
    //! 厳密逆行列なので異方 UV でも誤差はなく、残るのは BC3 の量子化だけです。
    //!
    //! @param  [in] tbn         BuildEngineTbn が返した行列
    //! @param  [in] worldNormal 焼きたいワールド法線（単位ベクトル）
    //! @return タンジェント空間の法線（単位ベクトル）。縮退時は (0,0,1)
    //------------------------------------------------------------------------
    Vec3 EncodeTangentNormal(const Mat3& tbn, const Vec3& worldNormal);

    //------------------------------------------------------------------------
    //! タンジェント空間の法線を RGB へ詰めます。
    //! @param  [in]  tangentNormal タンジェント空間の法線
    //! @param  [out] outRgb        0〜255 の3成分
    //------------------------------------------------------------------------
    void PackTangentNormal(const Vec3& tangentNormal, unsigned char outRgb[3]);

}    // namespace RockCore
