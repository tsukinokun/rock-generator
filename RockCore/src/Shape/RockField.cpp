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
        //! 多項式の smooth minimum。
        //!
        //! min をそのまま使うと破断面どうしの境目が数学的に鋭い折れ目になる。
        //! 実物の岩は角から先に風化して丸くなるので、ここを滑らかに繋ぐと
        //! 「割れたて」と「風化した露岩」が k 1本で連続に繋がる。
        //!
        //! k -> 0 で通常の min に一致する。
        //!
        //! @param  [in] a 片方の値
        //! @param  [in] b もう片方の値
        //! @param  [in] k 丸める幅
        //! @return 滑らかに繋いだ最小値
        //--------------------------------------------------------------------
        float SmoothMin(float a, float b, float k) {
            if(k <= 0.0f) {
                return std::min(a, b);
            }

            const float h = std::max(k - std::abs(a - b), 0.0f) / k;
            return std::min(a, b) - h * h * k * 0.25f;
        }

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

        //--------------------------------------------------------------------
        // 破断面。これが無いと fBm の丸い塊にしかならない
        //--------------------------------------------------------------------
        CutPlaneSettings planeSettings{};
        planeSettings.count    = params.planeCutCount;
        planeSettings.depth    = params.planeCutDepth;
        planeSettings.axisBias = params.planeAxisBias;
        planeSettings.radius   = m_radius;

        m_cutPlanes = BuildCutPlanes(planeSettings, params.seed);

        m_edgeRounding = m_radius * std::max(params.edgeRounding, 0.0f);

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
    //! ノイズ抜きの半径を求めます。
    //------------------------------------------------------------------------
    float RockField::BaseRadius(const Vec3& dir) const {
        // まず外接球。カットされていない方向はここに当たって丸くなる
        float radius = m_radius;

        //--------------------------------------------------------------------
        // 破断面を畳む。
        //
        // 半空間 dot(p, n) <= d の内側にいるレイが平面を抜ける距離は
        // d / dot(dir, n)。dot が 0 へ近づくと発散するので、外接球の
        // 数倍でクランプしてから smooth min へ渡す
        //
        // 畳む順は平面番号の昇順に固定する。smooth min は結合的でないので、
        // 順序を変えると結果が変わって決定論が崩れる
        //--------------------------------------------------------------------
        constexpr float kFarFactor = 4.0f;
        const float     farLimit   = m_radius * kFarFactor;

        for(const CutPlane& plane : m_cutPlanes) {
            const float facing = Dot(dir, plane.normal);
            if(facing <= 1e-4f) {
                // 平面の裏側へ向かうレイは、その平面には当たらない
                continue;
            }

            const float hit = std::min(plane.distance / facing, farLimit);
            radius          = SmoothMin(radius, hit, m_edgeRounding);
        }

        // 平面を重ねるほど smooth min の縮みが積もる。痩せすぎを止める
        return std::max(radius, m_radius * 0.05f);
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
        return std::max(BaseRadius(dir) * (1.0f + displacement), m_radius * 0.05f);
    }

    //------------------------------------------------------------------------
    //! 方向 dir の表面上の点を求めます。
    //------------------------------------------------------------------------
    Vec3 RockField::SurfacePoint(const Vec3& dir) const {
        return Mul(m_anisoScale, dir * Radius(dir));
    }

    //------------------------------------------------------------------------
    //! 周囲と比べてどれだけ窪んでいるかを返します。
    //------------------------------------------------------------------------
    float RockField::Relief(const Vec3& dir, float angularRadius) const {
        const float radius = Radius(dir);
        if(radius <= 0.0f || angularRadius <= 0.0f) {
            return 0.0f;
        }

        //--------------------------------------------------------------------
        // dir に直交する基底を作る。
        //
        // 成分の絶対値が最小の軸を選んでから外積を取る。dir に近い軸を選ぶと
        // 外積が 0 に潰れて基底が作れない
        //--------------------------------------------------------------------
        const float absX = std::abs(dir.x);
        const float absY = std::abs(dir.y);
        const float absZ = std::abs(dir.z);

        Vec3 axis{0.0f, 0.0f, 1.0f};
        if(absX <= absY && absX <= absZ) {
            axis = Vec3{1.0f, 0.0f, 0.0f};
        } else if(absY <= absZ) {
            axis = Vec3{0.0f, 1.0f, 0.0f};
        }

        const Vec3 tangent  = Normalize(Cross(dir, axis));
        const Vec3 binormal = Cross(dir, tangent);

        //--------------------------------------------------------------------
        // 周囲を 4 方位で測る。
        //
        // 8 方位にしても値はほとんど変わらないのに評価回数が倍になる。
        // ここは 1 テクセルにつき毎回走るので、4 で足りるなら 4 にする
        //--------------------------------------------------------------------
        const float offset = std::tan(std::min(angularRadius, 1.0f));

        float sum = 0.0f;
        for(int i = 0; i < 4; ++i) {
            const float angle = static_cast<float>(i) * 1.5707963f;

            const Vec3 around = tangent * (std::cos(angle) * offset) + binormal * (std::sin(angle) * offset);
            sum += Radius(Normalize(dir + around));
        }

        return (sum * 0.25f - radius) / radius;
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
