//----------------------------------------------------------------------------
//! @file   SurfaceBaker.cpp
//! @brief  Albedo と MetallicRoughness のベイクの実装
//----------------------------------------------------------------------------
#include <RockCore/Bake/SurfaceBaker.hpp>

#include <RockCore/Noise/Fbm.hpp>
#include <RockCore/Random/Pcg32.hpp>

#include <algorithm>
#include <cmath>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //--------------------------------------------------------------------
        //! 斑のノイズ用のサブシード番号。
        //!
        //! ノイズ層は 層番号 * 1024 + 7、破断面は 900001 を使っている。
        //! そのどちらとも衝突しない値を選んである。ここを形のノイズと
        //! 共有してはいけない。色と凹凸が完全に連動して作り物に見える
        //--------------------------------------------------------------------
        constexpr u32 kColorSeedIndex = 800011u;

        //--------------------------------------------------------------------
        //! 窪みを測る角度（半径に対する比率）。
        //!
        //! 世界の長さで固定していることに意味がある。テクセルの大きさに
        //! 合わせると、テクスチャの解像度を変えただけで汚れの太さが
        //! 変わってしまう。0.04 は既定の割れ目の幅とおおむね同じ
        //--------------------------------------------------------------------
        constexpr float kCavityAngularRadius = 0.04f;

        //--------------------------------------------------------------------
        //! 窪み具合を [0,1] へ写すときの倍率。
        //!
        //! RockField::Relief は「半径に対する比率」を返す。**実測で決めた値**で、
        //! 既定の岩の割れ目の底がおよそ 0.017（--verify-core の max relief）。
        //! その逆数に取ってあるので、一番深い割れ目でちょうど 1.0 へ届く。
        //!
        //! 最初 20 にしていたときは最深部でも 0.34 にしかならず、暗くなる幅が
        //! 15% で目では分からなかった。ここは勘で決めずに、ノイズ層の構成を
        //! 変えたら max relief を見て取り直すこと
        //--------------------------------------------------------------------
        constexpr float kReliefToCavity = 55.0f;

        //! 窪みが最も深いところの明るさの倍率。0 にすると真っ黒になって嘘くさい
        constexpr float kCavityFloor = 0.35f;

        //! これを超えたら「窪み」として数える（統計用のしきい値）
        constexpr float kCavityCountThreshold = 0.25f;

        //--------------------------------------------------------------------
        //! リニアの値を sRGB のバイトへ符号化します。
        //!
        //! sRGB の規格どおりの式。近似の pow(x, 1/2.2) を使うと暗部が
        //! 目に見えてずれる（黒に近いほど誤差が乗る）ので、直線部分を含む
        //! 正しい式で書く。
        //!
        //! @param  [in] value リニアの値
        //! @return sRGB のバイト
        //--------------------------------------------------------------------
        u8 EncodeSrgb(float value) {
            const float clamped = std::clamp(value, 0.0f, 1.0f);

            const float encoded = (clamped <= 0.0031308f)
                                      ? (clamped * 12.92f)
                                      : (1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f);

            return static_cast<u8>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
        }

        //! [0,1] の値をバイトへ詰めます。
        //! @param  [in] value 値
        //! @return バイト
        u8 EncodeLinear(float value) {
            return static_cast<u8>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

        //! 線形補間します。
        //! @param  [in] a 始点
        //! @param  [in] b 終点
        //! @param  [in] t 係数
        //! @return 補間結果
        float Mix(float a, float b, float t) { return a + (b - a) * t; }

        //! 明度を求めます（Rec.709）。
        //! @param  [in] color 色
        //! @return 明度
        float Luminance(const Vec3& color) {
            return color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! Albedo と MetallicRoughness を焼きます。
    //------------------------------------------------------------------------
    bool BakeSurfaceMaps(const BakeGBuffer&      gbuffer,
                         const RockField&        fullField,
                         const RockParams&       params,
                         ImageBuffer&            outAlbedo,
                         ImageBuffer&            outMetallicRoughness,
                         const ProgressCallback& progress,
                         const CancelToken*      cancel,
                         SurfaceBakeStats*       outStats) {
        const int size = gbuffer.GetSize();
        if(size <= 0) {
            return false;
        }

        //--------------------------------------------------------------------
        // 未被覆テクセルの既定値。
        //
        // dilate が後から埋めるが、埋め損ねても破綻しない値にしておく。
        // Albedo は基本色、MR は基準ラフネスそのもの
        //--------------------------------------------------------------------
        outAlbedo.Resize(size, size);
        outAlbedo.Fill(EncodeSrgb(params.baseColor.x),
                       EncodeSrgb(params.baseColor.y),
                       EncodeSrgb(params.baseColor.z),
                       255u);

        outMetallicRoughness.Resize(size, size);
        outMetallicRoughness.Fill(255u, EncodeLinear(params.roughness), 0u, 255u);

        //--------------------------------------------------------------------
        // 斑のノイズ。
        //
        // 帯域制限を掛けない（maxFrequency = 0）。色はメッシュの頂点には
        // 載らないので、ナイキストで切る理由が無い。テクスチャの解像度が
        // 許すかぎり細かくてよい
        //--------------------------------------------------------------------
        FbmParams mottleParams{};
        mottleParams.frequency  = std::max(params.colorNoiseFrequency, 0.0f);
        mottleParams.octaves    = 4;
        mottleParams.lacunarity = 2.0f;
        mottleParams.gain       = 0.5f;

        const Fbm mottle(mottleParams, DeriveSeed(params.seed, kColorSeedIndex), 0.0f);

        const float variation       = std::clamp(params.colorVariation, 0.0f, 1.0f);
        const float cavityDarkening = std::clamp(params.cavityDarkening, 0.0f, 1.0f);
        const float baseRoughness   = std::clamp(params.roughness, 0.0f, 1.0f);
        const float cavityRoughness = std::clamp(params.cavityRoughness, -1.0f, 1.0f);
        const float metallic        = std::clamp(params.metallic, 0.0f, 1.0f);

        SurfaceBakeStats stats{};

        stats.minLuminance = 1.0f;
        stats.maxLuminance = 0.0f;

        u32 cavityTexels = 0;

        for(int y = 0; y < size; ++y) {
            // 中断の確認は行単位。Normal ベイクと同じ理由
            if(cancel && cancel->IsCancelled()) {
                return false;
            }

            for(int x = 0; x < size; ++x) {
                if(!gbuffer.IsCovered(x, y)) {
                    continue;
                }

                const BakeSample& sample = gbuffer.GetSample(x, y);

                // 異方スケールを戻した空間の方向。ノイズも半径関数も
                // この方向で評価する
                const Vec3 direction = fullField.DirectionOf(sample.position);

                //------------------------------------------------------------
                // 1. 斑。2 色を混ぜる。
                //
                //    混合比を「基本色 100%」から引く形にしてある。
                //    variation = 0 で基本色の単色へ退化し、1 で 2 色を端から
                //    端まで使う。
                //
                //    0.5 を中心に振ると variation = 0 が「2 色のちょうど中間」に
                //    なってしまい、強さを 0 にしたのに基本色が出ない
                //------------------------------------------------------------
                const float noise = mottle.Sample(direction);
                const float blend = std::clamp(Mix(1.0f, 0.5f + noise * 0.5f, variation), 0.0f, 1.0f);

                Vec3 color{Mix(params.secondaryColor.x, params.baseColor.x, blend),
                           Mix(params.secondaryColor.y, params.baseColor.y, blend),
                           Mix(params.secondaryColor.z, params.baseColor.z, blend)};

                //------------------------------------------------------------
                // 2. 窪みの汚れ。
                //
                //    割れ目の内側と出っぱりの頂点を区別する唯一の信号。
                //    ここが入らないと、形をどれだけ作り込んでも粘土に見える
                //------------------------------------------------------------
                const float relief = fullField.Relief(direction, kCavityAngularRadius);
                const float cavity = std::clamp(relief * kReliefToCavity, 0.0f, 1.0f);

                stats.maxRelief = std::max(stats.maxRelief, relief);

                if(cavity >= kCavityCountThreshold) {
                    ++cavityTexels;
                }

                const float shade = Mix(1.0f, kCavityFloor, cavity * cavityDarkening);

                color = color * shade;

                //------------------------------------------------------------
                // 3. 書き込み
                //------------------------------------------------------------
                outAlbedo.SetPixel(x, y, EncodeSrgb(color.x), EncodeSrgb(color.y), EncodeSrgb(color.z), 255u);

                const float roughness = std::clamp(baseRoughness + cavity * cavityRoughness, 0.0f, 1.0f);

                // R は未使用。Phase 5 で AO を入れる余地として 255 のまま置く
                outMetallicRoughness.SetPixel(x, y, 255u, EncodeLinear(roughness), EncodeLinear(metallic), 255u);

                const float luminance = Luminance(color);

                stats.minLuminance = std::min(stats.minLuminance, luminance);
                stats.maxLuminance = std::max(stats.maxLuminance, luminance);
                ++stats.texelsWritten;
            }

            ReportProgress(progress, static_cast<float>(y + 1) / static_cast<float>(size), "Bake: Color");
        }

        if(stats.texelsWritten > 0) {
            stats.cavityRatio = static_cast<float>(cavityTexels) / static_cast<float>(stats.texelsWritten);
        } else {
            stats.minLuminance = 0.0f;
            stats.maxLuminance = 0.0f;
        }

        if(outStats) {
            *outStats = stats;
        }
        return true;
    }

}    // namespace RockCore
