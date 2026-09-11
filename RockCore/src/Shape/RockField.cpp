//----------------------------------------------------------------------------
//! @file   RockField.cpp
//! @brief  星形立体の半径関数の実装
//----------------------------------------------------------------------------
#include <RockCore/Shape/RockField.hpp>

#include <algorithm>
#include <cmath>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //--------------------------------------------------------------------
        //! 正4面体の4頂点方向。勾配の差分に使う。
        //!
        //! 立方体の8隅から交互に4つ取ったもの。和が 0 で、自己外積の和が
        //! 4 * 単位行列になるため、k_i に F(p + h*k_i) を掛けて足すと
        //! 勾配に比例した量が出る。中心差分より2回少ない評価で済む
        //--------------------------------------------------------------------
        constexpr Vec3 kTetra[4] = {
            { 1.0f, -1.0f, -1.0f},
            {-1.0f, -1.0f,  1.0f},
            {-1.0f,  1.0f, -1.0f},
            { 1.0f,  1.0f,  1.0f},
        };

        //! kTetra の長さは sqrt(3) なので、サンプル距離を h に揃えるために掛ける
        constexpr float kInvSqrt3 = 0.57735027f;

        //--------------------------------------------------------------------
        //! 0 割りを避けつつ成分ごとの逆数を求めます。
        //! @param  [in] v 元のベクトル
        //! @return 成分ごとの逆数
        //--------------------------------------------------------------------
        Vec3 SafeReciprocal(const Vec3& v) {
            constexpr float kMinScale = 1e-4f;

            auto invert = [](float value) {
                const float sign = (value < 0.0f) ? -1.0f : 1.0f;
                return sign / std::max(std::abs(value), kMinScale);
            };
            return {invert(v.x), invert(v.y), invert(v.z)};
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! 半径関数を構築します。
    //------------------------------------------------------------------------
    RockField::RockField(const RockParams& params, float maxFrequency)
        : m_noise(params.noiseLayers, params.seed, maxFrequency)
        , m_radius(std::max(params.radius, 1e-3f))
        , m_anisoScale(params.anisoScale)
        , m_invAnisoScale(SafeReciprocal(params.anisoScale)) {

        // 変位の最悪値で包む。各層のノイズは振幅の総和で正規化済みで
        // [-1,1] に収まるため、振幅の絶対値を足せば確実に上回る
        float amplitudeSum = 0.0f;
        for(const NoiseLayerParams& layer : params.noiseLayers) {
            if(layer.enabled) {
                amplitudeSum += std::abs(layer.amplitude);
            }
        }

        const float maxAniso =
            std::max(std::abs(m_anisoScale.x), std::max(std::abs(m_anisoScale.y), std::abs(m_anisoScale.z)));
        m_boundingRadius = m_radius * (1.0f + amplitudeSum) * maxAniso;
    }

    //------------------------------------------------------------------------
    //! 方向 dir の半径を求めます。
    //------------------------------------------------------------------------
    float RockField::Radius(const Vec3& dir) const {
        // ノイズは単位球上で評価する。球面上の点をそのまま 3D ノイズの座標へ
        // 渡せるのが星形表現の楽なところで、極の縮退も継ぎ目も出ない
        const float displacement = m_noise.Sample(dir);

        // 半径が 0 へ潜り込むと星形でなくなる。振幅を 1 より大きくしたときだけ
        // 効く安全弁として下限で止める
        return std::max(m_radius * (1.0f + displacement), m_radius * 0.05f);
    }

    //------------------------------------------------------------------------
    //! 方向 dir の表面上の点を求めます。
    //------------------------------------------------------------------------
    Vec3 RockField::SurfacePoint(const Vec3& dir) const {
        return Mul(m_anisoScale, dir * Radius(dir));
    }

    //------------------------------------------------------------------------
    //! 任意の位置から、その点が属する方向を求めます。
    //------------------------------------------------------------------------
    Vec3 RockField::DirectionOf(const Vec3& p) const {
        return Normalize(Mul(m_invAnisoScale, p));
    }

    //------------------------------------------------------------------------
    //! 任意の位置を、同じ方向の表面上へ射影します。
    //------------------------------------------------------------------------
    Vec3 RockField::ProjectToSurface(const Vec3& p) const {
        return SurfacePoint(DirectionOf(p));
    }

    //------------------------------------------------------------------------
    //! 暗黙関数 F(p) の値を求めます。
    //------------------------------------------------------------------------
    float RockField::Value(const Vec3& p) const {
        // 異方スケールを戻した空間へ移すと、そこでは純粋な星形（半径関数のみ）に
        // なるので F を閉形式で書ける
        const Vec3  q    = Mul(m_invAnisoScale, p);
        const float qLen = Length(q);
        if(qLen <= 1e-8f) {
            // 中心。方向が定まらないので、最も内側という扱いにする
            return -m_radius;
        }
        return qLen - Radius(q / qLen);
    }

    //------------------------------------------------------------------------
    //! F の勾配を正4面体差分で求めます。
    //------------------------------------------------------------------------
    Vec3 RockField::Gradient(const Vec3& p, float h) const {
        const float step = std::max(h, 1e-6f) * kInvSqrt3;

        Vec3 sum{0.0f, 0.0f, 0.0f};
        for(const Vec3& k : kTetra) {
            sum = sum + k * Value(p + k * step);
        }

        // 自己外積の和が 4I なので、勾配は和を 4*step で割った値になる。
        // 呼び出し側は正規化して使うが、スケールを正しく返しておく方が
        // 誤解が起きない
        return sum * (1.0f / (4.0f * step));
    }

}    // namespace RockCore
