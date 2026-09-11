//----------------------------------------------------------------------------
//! @file   VerifyCore.cpp
//! @brief  RockCore の数値検証（--verify-core）
//! @detail GPU もウィンドウも要らない回帰テストです。RockCore が
//!         DX11 / ImGui / Windows に依存していないからこそ、CLI だけで
//!         ここまで検証できます（依存を持ち込めばこのテストは成立しません）。
//!
//!         検証するもの:
//!           1. TBN の往復。C++ の EncodeTangentNormal が書いた値を、
//!              PBR.hlsli:155-174 の ApplyNormalMap を逐語で再現した関数へ
//!              通して元のワールド法線に戻るか。画面微分（ddx/ddy）には
//!              任意の 2x2 ヤコビアンを差し込む。これが「画面微分を再現する
//!              必要はない」という TangentFrame.hpp の導出の検証そのもの
//!           2. 決定論。同じ seed から2回生成してビット一致するか
//!           3. ダーティ判定。色だけ変えたときに Unwrap と Bake が走らないか
//!           4. UV 展開とベイク。被覆率・UV の範囲・縮退テクセル数
//!           5. RockParams の JSON 往復
//----------------------------------------------------------------------------
#include <RockCore/Bake/Dilate.hpp>
#include <RockCore/Bake/NormalBaker.hpp>
#include <RockCore/Bake/TangentFrame.hpp>
#include <RockCore/Bake/UvRasterizer.hpp>
#include <RockCore/Io/RockParamsJson.hpp>
#include <RockCore/Pipeline/Pipeline.hpp>
#include <RockCore/Random/Pcg32.hpp>

#include <cmath>
#include <cstdio>
#include <string>

using namespace RockCore;

namespace {

    int g_failures = 0;

    void Check(bool condition, const char* label) {
        std::printf("%s %s\n", condition ? "[ ok ]" : "[FAIL]", label);
        if(!condition) {
            ++g_failures;
        }
    }

    //------------------------------------------------------------------------
    // PBR.hlsli:155-174 の ApplyNormalMap を逐語で移植したもの。
    // 検証用なので、こちらは「画面微分から組む」元の式のまま書く。
    //------------------------------------------------------------------------
    Vec3 ApplyNormalMapReference(const Vec3& N, const Vec3& dp1, const Vec3& dp2, const Vec2& duv1, const Vec2& duv2,
                                 const Vec3& tangentNormal) {
        const Vec3 dp2perp = Cross(dp2, N);
        const Vec3 dp1perp = Cross(N, dp1);
        const Vec3 T       = dp2perp * duv1.x + dp1perp * duv2.x;
        const Vec3 B       = dp2perp * duv1.y + dp1perp * duv2.y;

        const float maxLenSq = std::max(Dot(T, T), Dot(B, B));
        const float invmax   = (maxLenSq > 0.0f) ? (1.0f / std::sqrt(maxLenSq)) : 0.0f;

        // float3x3 TBN = float3x3(T * invmax, B * invmax, N); return normalize(mul(tangentNormal, TBN));
        const Mat3 tbn = Mat3::FromRows(T * invmax, B * invmax, N);
        return Normalize(tangentNormal * tbn);
    }

    void TestTangentFrameRoundTrip() {
        Pcg32 rng(0xABCDEF01u);

        float worstError    = 0.0f;
        int   measuredCount       = 0;
        int   illConditionedCount = 0;
        int   degenerateCount     = 0;

        // 最悪ケースの素性を出す
        float worstRefMaxLenSq = 0.0f;
        float worstCrossLen    = 0.0f;
        float worstScreenDet   = 0.0f;

        for(int trial = 0; trial < 200000; ++trial) {
            const Vec3 N = Normalize(Vec3{rng.NextSigned(), rng.NextSigned(), rng.NextSigned()});

            Vec3 dPdu{rng.NextSigned() * 4.0f, rng.NextSigned() * 4.0f, rng.NextSigned() * 4.0f};
            Vec3 dPdv{rng.NextSigned() * 0.3f, rng.NextSigned() * 0.3f, rng.NextSigned() * 0.3f};

            const float crossLen = Length(Cross(dPdu, dPdv));
            if(crossLen < 1e-3f) {
                continue;
            }

            const Vec3 nWorldHi = Normalize(N + Vec3{rng.NextSigned(), rng.NextSigned(), rng.NextSigned()} * 0.5f);

            const Mat3 tbn = BuildEngineTbn(N, dPdu, dPdv);

            // 潰れたフレームは実装側も (0,0,1) を書いて dilate に任せる領域。
            // 比較の対象外
            if(IsEngineTbnDegenerate(tbn)) {
                ++degenerateCount;
                continue;
            }

            const Vec3 tangentNormal = EncodeTangentNormal(tbn, nWorldHi);

            Vec2 duv1{rng.NextSigned() * 2.0f, rng.NextSigned() * 2.0f};
            Vec2 duv2{rng.NextSigned() * 2.0f, rng.NextSigned() * 2.0f};

            const float screenDet = Cross2(duv1, duv2);
            const float chartSign = (Dot(Cross(dPdu, dPdv), N) < 0.0f) ? -1.0f : 1.0f;

            // 裏面（画面 winding がミラー）は対象外
            if(screenDet * chartSign <= 0.0f) {
                continue;
            }

            // 参照側の T,B の大きさ。しきい値クランプが無くなったので
            // 除外は要らないが、最悪ケースの素性を出すために測っておく
            const Vec3  refT        = Cross(dPdv, N) * screenDet;
            const Vec3  refB        = Cross(N, dPdu) * screenDet;
            const float refMaxLenSq = std::max(Dot(refT, refT), Dot(refB, refB));

            // 画面ヤコビアンが退化しているケースは除く。
            // 実際の ddx/ddy は画素グリッドと UV 写像から出るので、duv1 と duv2 は
            // おおむね直交し同程度の長さになる。det がその積よりはるかに小さい状態は
            // 現実には起きないうえ、参照式の側に激しい桁落ちが出て比較にならない
            const float duv1Len = std::sqrt(duv1.x * duv1.x + duv1.y * duv1.y);
            const float duv2Len = std::sqrt(duv2.x * duv2.x + duv2.y * duv2.y);
            if(std::abs(screenDet) < 0.25f * duv1Len * duv2Len) {
                ++illConditionedCount;
                continue;
            }

            const Vec3 dp1 = dPdu * duv1.x + dPdv * duv1.y;
            const Vec3 dp2 = dPdu * duv2.x + dPdv * duv2.y;

            const Vec3  decoded = ApplyNormalMapReference(N, dp1, dp2, duv1, duv2, tangentNormal);
            const float error   = Length(decoded - nWorldHi);

            ++measuredCount;

            if(error > worstError) {
                worstError       = error;
                worstRefMaxLenSq = refMaxLenSq;
                worstCrossLen    = crossLen;
                worstScreenDet   = screenDet;
            }
        }

        std::printf("       measured=%d  skipped: degenerate frame=%d, ill-conditioned screen jacobian=%d\n",
                    measuredCount, degenerateCount, illConditionedCount);
        std::printf("       worst error = %.3e  (refMaxLenSq=%.3e crossLen=%.3e screenDet=%.3e)\n",
                    worstError, worstRefMaxLenSq, worstCrossLen, worstScreenDet);
        // 判定の基準は 8bit テクスチャの量子化幅（1/255 ≒ 3.9e-3）。
        // 符号化誤差がこれより小さければ、焼いた Normal マップ上では区別できない
        Check(worstError < 2e-3f, "TBN round trip error stays below 8-bit quantization");
    }

    void TestDeterminism() {
        RockParams params{};
        params.textureSize      = 128;
        params.targetEdgeLength = 0.09f;

        auto run = [&](Pipeline& pipeline) {
            pipeline.GetMutableParams() = params;
            pipeline.SetTargetStage(PipelineStage::BakeNormal);
            return pipeline.Update(nullptr, nullptr);
        };

        Pipeline a;
        Pipeline b;
        Check(run(a), "pipeline run A completed");
        Check(run(b), "pipeline run B completed");

        Check(a.GetMesh().positions.size() == b.GetMesh().positions.size(), "vertex count matches");
        Check(a.GetMesh().indices == b.GetMesh().indices, "index buffer matches");
        Check(a.GetNormalMap().GetPixels() == b.GetNormalMap().GetPixels(), "normal map is bit identical");

        std::printf("       vertices=%u triangles=%u covered texels=%u\n",
                    a.GetMesh().GetVertexCount(),
                    a.GetMesh().GetTriangleCount(),
                    a.GetBakeGBuffer().GetCoveredTexelCount());
    }

    void TestDirtyFlags() {
        Pipeline pipeline;
        pipeline.GetMutableParams().textureSize      = 128;
        pipeline.GetMutableParams().targetEdgeLength = 0.09f;
        pipeline.SetTargetStage(PipelineStage::BakeNormal);
        pipeline.Update(nullptr, nullptr);

        const u32 unwrapRuns = pipeline.GetStageStatus(PipelineStage::Unwrap).runCount;
        const u32 bakeRuns   = pipeline.GetStageStatus(PipelineStage::BakeNormal).runCount;
        const u32 meshRuns   = pipeline.GetStageStatus(PipelineStage::Mesh).runCount;

        // 色とラフネスだけを動かす。どの段も走ってはいけない
        pipeline.GetMutableParams().baseColor = Vec3{0.9f, 0.1f, 0.1f};
        pipeline.GetMutableParams().roughness = 0.2f;
        pipeline.Update(nullptr, nullptr);

        Check(pipeline.GetStageStatus(PipelineStage::Unwrap).runCount == unwrapRuns, "color change does not re-run Unwrap");
        Check(pipeline.GetStageStatus(PipelineStage::BakeNormal).runCount == bakeRuns, "color change does not re-run Bake:Normal");
        Check(pipeline.GetStageStatus(PipelineStage::Mesh).runCount == meshRuns, "color change does not re-run Mesh");

        // 形を動かすと全段走る
        pipeline.GetMutableParams().noiseLayers[0].amplitude = 0.3f;
        pipeline.Update(nullptr, nullptr);

        Check(pipeline.GetStageStatus(PipelineStage::Mesh).runCount == meshRuns + 1, "shape change re-runs Mesh");
        Check(pipeline.GetStageStatus(PipelineStage::Unwrap).runCount == unwrapRuns + 1, "shape change re-runs Unwrap");
        Check(pipeline.GetStageStatus(PipelineStage::BakeNormal).runCount == bakeRuns + 1, "shape change re-runs Bake:Normal");
    }

    void TestUnwrapCoverage() {
        Pipeline pipeline;
        pipeline.GetMutableParams().textureSize      = 256;
        pipeline.GetMutableParams().targetEdgeLength = 0.05f;
        pipeline.SetTargetStage(PipelineStage::BakeNormal);
        pipeline.Update(nullptr, nullptr);

        const MeshBuilder& mesh = pipeline.GetMesh();

        bool uvInRange = true;
        for(const Vec2& uv : mesh.uvs) {
            if(uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) {
                uvInRange = false;
                break;
            }
        }
        Check(uvInRange, "all UVs are inside [0,1]");
        Check(mesh.uvs.size() == mesh.positions.size(), "uv count matches vertex count");
        Check(mesh.normals.size() == mesh.positions.size(), "normal count matches vertex count");

        const BakeGBuffer& gbuffer = pipeline.GetBakeGBuffer();
        const u32          texels  = 256u * 256u;
        const float        ratio   = static_cast<float>(gbuffer.GetCoveredTexelCount()) / static_cast<float>(texels);
        std::printf("       UV coverage = %.1f%%\n", ratio * 100.0f);
        Check(ratio > 0.3f, "UV atlas covers a reasonable share of the texture");

        // 法線が全部平坦（= ベイクが効いていない）になっていないこと
        const std::vector<u8>& pixels  = pipeline.GetNormalMap().GetPixels();
        u32                    nonFlat = 0;
        for(size_t i = 0; i + 2 < pixels.size(); i += 4) {
            if(pixels[i] != 128u || pixels[i + 1] != 128u) {
                ++nonFlat;
            }
        }
        std::printf("       non-flat normal texels = %u / %u\n", nonFlat, texels);
        std::printf("       degenerate TBN texels  = %u / %u\n",
                    pipeline.GetNormalBakeStats().degenerateTexels, gbuffer.GetCoveredTexelCount());
        Check(pipeline.GetNormalBakeStats().degenerateTexels * 100u < gbuffer.GetCoveredTexelCount(),
              "degenerate TBN texels are under 1% of the atlas");
        Check(nonFlat > texels / 10u, "the normal map actually carries detail");
    }

    void TestJsonRoundTrip() {
        RockParams original{};
        original.seed                       = 987654321u;
        original.radius                     = 0.73f;
        original.anisoScale                 = Vec3{1.0f, 0.25f, 1.0f};
        original.noiseLayers[0].fbm.octaves = 6;
        original.textureSize                = 2048;

        const std::string json = SaveRockParamsToString(original);

        RockParams  loaded{};
        std::string error;
        const bool  ok = LoadRockParamsFromString(loaded, json, &error);
        if(!ok) {
            std::printf("       json error: %s\n", error.c_str());
        }
        Check(ok, "JSON round trip parses");
        Check(loaded.seed == original.seed, "seed survives JSON");
        Check(loaded.radius == original.radius, "radius survives JSON");
        Check(loaded.anisoScale.y == original.anisoScale.y, "anisoScale survives JSON");
        // 層数を決め打ちしない。既定の層構成を変えるたびにテストが落ちるのは
        // 検証したいこと（往復で保たれるか）とずれている
        Check(loaded.noiseLayers.size() == original.noiseLayers.size() && loaded.noiseLayers[0].fbm.octaves == 6,
              "noise layers survive JSON");
        Check(loaded.textureSize == 2048, "textureSize survives JSON");
    }

}    // namespace

//----------------------------------------------------------------------------
//! RockCore の数値検証を実行します。
//! @return 全て通れば 0、失敗があれば 1
//----------------------------------------------------------------------------
int RunCoreVerification() {
    std::printf("--- TangentFrame ---\n");
    TestTangentFrameRoundTrip();
    std::printf("--- Determinism ---\n");
    TestDeterminism();
    std::printf("--- Dirty flags ---\n");
    TestDirtyFlags();
    std::printf("--- Unwrap / Bake ---\n");
    TestUnwrapCoverage();
    std::printf("--- JSON ---\n");
    TestJsonRoundTrip();

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
