//----------------------------------------------------------------------------
//! @file   TangentFrame.cpp
//! @brief  エンジンの TBN 行列と符号化の実装
//----------------------------------------------------------------------------
#include <RockCore/Bake/TangentFrame.hpp>

#include <algorithm>
#include <cmath>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //! 符号化に失敗したときに書く値。タンジェント空間で面の法線そのもの
        constexpr Vec3 kFlatNormal{0.0f, 0.0f, 1.0f};

        //--------------------------------------------------------------------
        //! フレームが潰れていると見なす行列式のしきい値。
        //!
        //! BuildEngineTbn は invmax で max(|T|,|B|) を 1 に揃え、3行目は
        //! 単位法線なので、行列式は必ず [-1, 1] に収まる。つまりこの値は
        //! そのまま「条件数 1e4 まで許す」という相対しきい値になる。
        //!
        //! T と B は互いに直交していないので、UV が極端に異方な場所や
        //! dPdu と dPdv の接平面成分が平行に近い場所では行列式が 0 へ寄る。
        //! そこで厳密逆行列を取ると float では桁が消し飛び、正規化前の
        //! ベクトルが 1e-6 程度まで縮んで向きが無意味になる
        //! （1e-12 のような絶対しきい値では、この破綻を素通しする）。
        //!
        //! 1e-4 なら符号化誤差は 1e-3 程度に収まり、8bit 量子化の
        //! 1/255 ≒ 4e-3 より十分小さい
        //--------------------------------------------------------------------
        constexpr float kMinFrameDeterminant = 1e-4f;

    }    // namespace

    //------------------------------------------------------------------------
    //! エンジンが PS 内で組むのと同じ TBN 行列を作ります。
    //------------------------------------------------------------------------
    Mat3 BuildEngineTbn(const Vec3& normal, const Vec3& dPdu, const Vec3& dPdv) {
        // det の符号だけが残る。大きさは invmax で消えるので ±1 で足りる
        // （ヘッダの導出を参照）
        const float sign = (Dot(Cross(dPdu, dPdv), normal) < 0.0f) ? -1.0f : 1.0f;

        const Vec3 tangent  = Cross(dPdv, normal) * sign;
        const Vec3 binormal = Cross(normal, dPdu) * sign;

        // PBR.hlsli の invmax をそのまま写す。
        //
        // 絶対値のしきい値と比べてはいけない（シェーダ側も 0 との比較に直してある）。
        // ここでは s = ±1 を使っていて |T| が 1 前後になるため実際にクランプへ
        // 触ることは無いが、式を食い違わせないために同じ形にしておく
        const float maxLenSq = std::max(Dot(tangent, tangent), Dot(binormal, binormal));
        const float invmax   = (maxLenSq > 0.0f) ? (1.0f / std::sqrt(maxLenSq)) : 0.0f;

        return Mat3::FromRows(tangent * invmax, binormal * invmax, normal);
    }

    //------------------------------------------------------------------------
    //! TBN が潰れていて符号化できないかを返します。
    //------------------------------------------------------------------------
    bool IsEngineTbnDegenerate(const Mat3& tbn) {
        return std::abs(Determinant(tbn)) < kMinFrameDeterminant;
    }

    //------------------------------------------------------------------------
    //! ワールド法線をタンジェント空間の法線へ符号化します。
    //------------------------------------------------------------------------
    Vec3 EncodeTangentNormal(const Mat3& tbn, const Vec3& worldNormal) {
        // フレームが潰れている場所では厳密逆行列でも解が意味を持たない。
        // 平坦な法線を書いて dilate に任せる
        if(IsEngineTbnDegenerate(tbn)) {
            return kFlatNormal;
        }

        // n_world = normalize(tn * M) を tn について解く。
        // normalize が掛かるので tn の正のスケールは自由に取れる
        const Vec3 tangentNormal = worldNormal * Inverse(tbn);

        if(LengthSq(tangentNormal) < 1e-20f) {
            return kFlatNormal;
        }
        return Normalize(tangentNormal);
    }

    //------------------------------------------------------------------------
    //! タンジェント空間の法線を RGB へ詰めます。
    //------------------------------------------------------------------------
    void PackTangentNormal(const Vec3& tangentNormal, unsigned char outRgb[3]) {
        for(int i = 0; i < 3; ++i) {
            const float encoded = tangentNormal[i] * 0.5f + 0.5f;
            outRgb[i]           = static_cast<unsigned char>(std::clamp(encoded * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }

}    // namespace RockCore
