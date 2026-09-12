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
//!           5. xatlas。チャートが重なっていないか、アトラス 1 枚に収まったか、
//!              展開後もハードエッジが残っているか、スレッドを起こしても
//!              決定論が保たれるか
//!           6. Albedo / MR ベイク。窪みの信号が届いているか、窪みが
//!              出っぱりより暗いか、メタリックが 0 のままか、
//!              sRGB で符号化されているか
//!           7. メッシュの健全性。閉じているか、退化が無いか、破断面が
//!              実エッジとして乗っているか（半空間クリップの穴埋めの検証）
//!           8. 4 プリセットが完走し、決定論を保つか
//!           9. RockParams の JSON 往復
//----------------------------------------------------------------------------
#include <RockCore/Bake/Dilate.hpp>
#include <RockCore/Bake/NormalBaker.hpp>
#include <RockCore/Bake/TangentFrame.hpp>
#include <RockCore/Bake/UvRasterizer.hpp>
#include <RockCore/Io/RockParamsJson.hpp>
#include <RockCore/Pipeline/Pipeline.hpp>
#include <RockCore/Random/Pcg32.hpp>
#include <RockCore/Shape/RockPresets.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

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
            pipeline.SetTargetStage(PipelineStage::BakeColor);
            return pipeline.Update(nullptr, nullptr);
        };

        Pipeline a;
        Pipeline b;
        Check(run(a), "pipeline run A completed");
        Check(run(b), "pipeline run B completed");

        Check(a.GetMesh().positions.size() == b.GetMesh().positions.size(), "vertex count matches");
        Check(a.GetMesh().indices == b.GetMesh().indices, "index buffer matches");
        Check(a.GetNormalMap().GetPixels() == b.GetNormalMap().GetPixels(), "normal map is bit identical");
        Check(a.GetAlbedoMap().GetPixels() == b.GetAlbedoMap().GetPixels(), "albedo map is bit identical");
        Check(a.GetMetallicRoughnessMap().GetPixels() == b.GetMetallicRoughnessMap().GetPixels(),
              "metallic-roughness map is bit identical");

        std::printf("       vertices=%u triangles=%u covered texels=%u\n",
                    a.GetMesh().GetVertexCount(),
                    a.GetMesh().GetTriangleCount(),
                    a.GetBakeGBuffer().GetCoveredTexelCount());
    }

    void TestDirtyFlags() {
        Pipeline pipeline;
        pipeline.GetMutableParams().textureSize      = 128;
        pipeline.GetMutableParams().targetEdgeLength = 0.09f;
        pipeline.SetTargetStage(PipelineStage::BakeColor);
        pipeline.Update(nullptr, nullptr);

        const u32 unwrapRuns = pipeline.GetStageStatus(PipelineStage::Unwrap).runCount;
        const u32 bakeRuns   = pipeline.GetStageStatus(PipelineStage::BakeNormal).runCount;
        const u32 colorRuns  = pipeline.GetStageStatus(PipelineStage::BakeColor).runCount;
        const u32 meshRuns   = pipeline.GetStageStatus(PipelineStage::Mesh).runCount;

        //--------------------------------------------------------------------
        // 色とラフネスだけを動かす。
        //
        // Albedo / MR は焼き直すが、そこより上は 1 段も走ってはいけない。
        // これが段ごとのハッシュが効いている証拠で、体感速度をほぼ決める
        //--------------------------------------------------------------------
        pipeline.GetMutableParams().baseColor = Vec3{0.9f, 0.1f, 0.1f};
        pipeline.GetMutableParams().roughness = 0.2f;
        pipeline.Update(nullptr, nullptr);

        Check(pipeline.GetStageStatus(PipelineStage::Unwrap).runCount == unwrapRuns, "color change does not re-run Unwrap");
        Check(pipeline.GetStageStatus(PipelineStage::BakeNormal).runCount == bakeRuns, "color change does not re-run Bake:Normal");
        Check(pipeline.GetStageStatus(PipelineStage::Mesh).runCount == meshRuns, "color change does not re-run Mesh");
        Check(pipeline.GetStageStatus(PipelineStage::BakeColor).runCount == colorRuns + 1, "color change re-runs Bake:Color");

        // 形を動かすと全段走る
        pipeline.GetMutableParams().noiseLayers[0].amplitude = 0.3f;
        pipeline.Update(nullptr, nullptr);

        Check(pipeline.GetStageStatus(PipelineStage::Mesh).runCount == meshRuns + 1, "shape change re-runs Mesh");
        Check(pipeline.GetStageStatus(PipelineStage::Unwrap).runCount == unwrapRuns + 1, "shape change re-runs Unwrap");
        Check(pipeline.GetStageStatus(PipelineStage::BakeNormal).runCount == bakeRuns + 1, "shape change re-runs Bake:Normal");
        Check(pipeline.GetStageStatus(PipelineStage::BakeColor).runCount == colorRuns + 2, "shape change re-runs Bake:Color");
    }

    void TestUnwrapCoverage() {
        Pipeline pipeline;

        // 方式を明示する。既定は xatlas だが、この検査は八面体射影の
        // シーム処理（正方形の縁をまたぐ三角形の複製）を見るためのもの
        pipeline.GetMutableParams().unwrapMethod     = UnwrapMethod::Octahedral;
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

    //------------------------------------------------------------------------
    //! 位置からハッシュを作ります。
    //!
    //! 辺を頂点番号ではなく**位置**で数えるために使います。ハードエッジ化で
    //! 頂点を複製してあるので、番号で見ると稜線の辺が共有されていないように
    //! 見えてしまいます。複製した頂点は位置がビット単位で同じなので、
    //! 位置を鍵にすれば正しく繋がります。
    //!
    //! @param  [in] p 位置
    //! @return ハッシュ値
    //------------------------------------------------------------------------
    u64 HashPosition(const Vec3& p) {
        u64 hash = 0xCBF29CE484222325ull;

        const float components[3] = {p.x, p.y, p.z};
        for(const float value : components) {
            u32 bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));

            hash ^= static_cast<u64>(bits);
            hash *= 0x100000001B3ull;
        }
        return hash;
    }

    //------------------------------------------------------------------------
    //! 辺の鍵を作ります。向きは問いません。
    //! @param  [in] a 片方の位置
    //! @param  [in] b もう片方の位置
    //! @return 辺の鍵
    //------------------------------------------------------------------------
    u64 MakePositionEdgeKey(const Vec3& a, const Vec3& b) {
        const u64 hashA = HashPosition(a);
        const u64 hashB = HashPosition(b);
        return (hashA < hashB) ? (hashA * 31u + hashB) : (hashB * 31u + hashA);
    }

    void TestMeshIntegrity() {
        Pipeline pipeline;
        pipeline.GetMutableParams().textureSize = 128;
        pipeline.SetTargetStage(PipelineStage::Mesh);
        pipeline.Update(nullptr, nullptr);

        // UV 展開前のメッシュを見る。展開とハードエッジ化は頂点を複製するので、
        // クリップが位相を壊していないかを見たいならこちら
        const MeshBuilder&   mesh  = pipeline.GetBaseMesh();
        const RockMeshStats& stats = pipeline.GetMeshStats();

        std::printf("       cut planes=%u vertices=%u triangles=%u\n",
                    stats.cutPlaneCount,
                    mesh.GetVertexCount(),
                    mesh.GetTriangleCount());

        Check(stats.cutPlaneCount > 0, "the default rock actually has fracture planes");

        //--------------------------------------------------------------------
        // 閉じているか。辺はちょうど 2 枚の三角形に共有されていなければならない。
        // クリップの切り口を塞ぎ損ねると即ここで落ちる
        //--------------------------------------------------------------------
        std::unordered_map<u64, int> edgeUseCount;
        edgeUseCount.reserve(mesh.indices.size());

        for(size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            for(int corner = 0; corner < 3; ++corner) {
                const Vec3& a = mesh.positions[mesh.indices[i + static_cast<size_t>(corner)]];
                const Vec3& b = mesh.positions[mesh.indices[i + static_cast<size_t>((corner + 1) % 3)]];

                ++edgeUseCount[MakePositionEdgeKey(a, b)];
            }
        }

        int openEdges   = 0;
        int nonManifold = 0;
        for(const auto& entry : edgeUseCount) {
            if(entry.second == 1) {
                ++openEdges;
            } else if(entry.second > 2) {
                ++nonManifold;
            }
        }

        std::printf("       edges=%zu open=%d non-manifold=%d\n", edgeUseCount.size(), openEdges, nonManifold);
        Check(openEdges == 0, "the mesh is closed (no open edges after clipping)");
        Check(nonManifold == 0, "the mesh is manifold (no edge shared by more than two triangles)");

        //--------------------------------------------------------------------
        // 面積 0 の三角形が無いか
        //--------------------------------------------------------------------
        //--------------------------------------------------------------------
        // しきい値は xatlas に合わせてある。
        //
        // xatlas は面積が FLT_EPSILON 以下の面を「無効な面」として扱い、
        // チャートへ入れずに UV (0,0) のまま返してくる（xatlas.cpp:9173 の
        // kAreaEpsilon）。それを通すとアトラスの原点へ伸びる巨大な三角形が
        // できてベイクが壊れるので、ここを xatlas より緩くしてはいけない。
        //
        // 以前は 1e-12 だった。面積 1e-12〜1e-7 の針のような三角形が残り、
        // 「退化は 0 件」と出ているのに xatlas だけが落ちる状態になっていた
        //--------------------------------------------------------------------
        int   degenerate = 0;
        float minArea    = std::numeric_limits<float>::max();

        // 一番細い三角形の辺の長さ。辺の縮めで直せるのか
        // （＝短い辺があるのか）を判断するために出す
        float worstShortEdge = 0.0f;
        float worstLongEdge  = 0.0f;

        for(size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const Vec3& a = mesh.positions[mesh.indices[i]];
            const Vec3& b = mesh.positions[mesh.indices[i + 1]];
            const Vec3& c = mesh.positions[mesh.indices[i + 2]];

            const float area = Length(Cross(b - a, c - a)) * 0.5f;

            if(area < minArea) {
                minArea = area;

                const float ab = Length(b - a);
                const float bc = Length(c - b);
                const float ca = Length(a - c);

                worstShortEdge = std::min(std::min(ab, bc), ca);
                worstLongEdge  = std::max(std::max(ab, bc), ca);
            }
            if(area <= FLT_EPSILON) {
                ++degenerate;
            }
        }
        std::printf("       degenerate triangles = %d  (min area = %.3e, xatlas rejects <= %.3e)\n",
                    degenerate,
                    minArea,
                    FLT_EPSILON);
        std::printf("       thinnest triangle: shortest edge = %.3e, longest edge = %.3e\n",
                    worstShortEdge,
                    worstLongEdge);
        Check(degenerate == 0, "no triangle is small enough for xatlas to reject");

        //--------------------------------------------------------------------
        // 平らな面が実際にできているか。
        //
        // 隣り合う三角形の法線がほぼ一致する組が一定数あれば、破断面が
        // メッシュの実エッジとして乗っている証拠になる
        //--------------------------------------------------------------------
        const u32 triangleCount = mesh.GetTriangleCount();

        std::vector<Vec3> faceNormals(triangleCount);
        for(u32 t = 0; t < triangleCount; ++t) {
            const size_t base = static_cast<size_t>(t) * 3u;
            faceNormals[t] =
                Normalize(Cross(mesh.positions[mesh.indices[base + 1]] - mesh.positions[mesh.indices[base]],
                                mesh.positions[mesh.indices[base + 2]] - mesh.positions[mesh.indices[base]]));
        }

        std::unordered_map<u64, u32> edgeFirstFace;
        int                          coplanarPairs = 0;
        int                          adjacentPairs = 0;

        for(u32 t = 0; t < triangleCount; ++t) {
            const size_t base = static_cast<size_t>(t) * 3u;
            for(int corner = 0; corner < 3; ++corner) {
                const Vec3& a = mesh.positions[mesh.indices[base + static_cast<size_t>(corner)]];
                const Vec3& b = mesh.positions[mesh.indices[base + static_cast<size_t>((corner + 1) % 3)]];

                const u64  key   = MakePositionEdgeKey(a, b);
                const auto found = edgeFirstFace.find(key);
                if(found == edgeFirstFace.end()) {
                    edgeFirstFace[key] = t;
                    continue;
                }

                ++adjacentPairs;
                if(Dot(faceNormals[t], faceNormals[found->second]) > 0.9995f) {
                    ++coplanarPairs;
                }
            }
        }

        const float coplanarRatio =
            (adjacentPairs > 0) ? (static_cast<float>(coplanarPairs) / static_cast<float>(adjacentPairs)) : 0.0f;
        std::printf("       coplanar adjacent pairs = %.1f %%\n", coplanarRatio * 100.0f);
        Check(coplanarRatio > 0.05f, "flat fracture faces exist in the mesh");
    }

    void TestPresets() {
        for(u32 i = 0; i < kRockPresetCount; ++i) {
            const RockPreset preset = static_cast<RockPreset>(i);

            Pipeline a;
            Pipeline b;

            a.GetMutableParams()             = MakeRockPreset(preset, 4242u);
            b.GetMutableParams()             = MakeRockPreset(preset, 4242u);
            a.GetMutableParams().textureSize = 128;
            b.GetMutableParams().textureSize = 128;
            a.SetTargetStage(PipelineStage::BakeColor);
            b.SetTargetStage(PipelineStage::BakeColor);

            const bool okA = a.Update(nullptr, nullptr);
            const bool okB = b.Update(nullptr, nullptr);

            std::printf("       %-10s tris=%u planes=%u split=%u\n",
                        GetRockPresetName(preset),
                        a.GetMesh().GetTriangleCount(),
                        a.GetMeshStats().cutPlaneCount,
                        a.GetUnwrapStats().splitVertexCount);

            char label[96];
            std::snprintf(label, sizeof(label), "preset %s completes and is deterministic", GetRockPresetName(preset));

            Check(okA && okB && a.GetMesh().indices == b.GetMesh().indices &&
                      a.GetNormalMap().GetPixels() == b.GetNormalMap().GetPixels(),
                  label);
        }
    }

    //------------------------------------------------------------------------
    //! UV 空間で 2 枚以上の三角形に覆われたテクセルの数を数えます。
    //!
    //! チャートが重なると、そこへ別々の面の法線が順に焼かれて後から来た方が
    //! 勝ちます。見た目には「一部の面だけ他人の法線を貼っている」という形で
    //! 出るので気付きにくい。八面体射影は星形保証から重なり得ませんが、
    //! xatlas の詰め込みは余白の取り方次第で重なるため、ここで数えます。
    //!
    //! 辺の上のテクセルは厳密な内側判定（辺関数が3つとも正）から外れるので、
    //! どちらの三角形にも数えません。隣り合う三角形を重なりと誤検出しない
    //! ためにそうしてあります。
    //!
    //! @param  [in] mesh        対象のメッシュ
    //! @param  [in] textureSize テクスチャの一辺
    //! @return 2 回以上覆われたテクセル数
    //------------------------------------------------------------------------
    u32 CountOverlappingTexels(const MeshBuilder& mesh, int textureSize) {
        if(mesh.uvs.size() != mesh.positions.size() || textureSize <= 0) {
            return 0;
        }

        const int       size  = textureSize;
        std::vector<u8> hits(static_cast<size_t>(size) * static_cast<size_t>(size), 0u);

        u32 overlapping = 0;

        const float scale = static_cast<float>(size);

        for(size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const Vec2& uv0 = mesh.uvs[mesh.indices[i]];
            const Vec2& uv1 = mesh.uvs[mesh.indices[i + 1]];
            const Vec2& uv2 = mesh.uvs[mesh.indices[i + 2]];

            const Vec2 p0{uv0.x * scale, uv0.y * scale};
            const Vec2 p1{uv1.x * scale, uv1.y * scale};
            const Vec2 p2{uv2.x * scale, uv2.y * scale};

            // 巻き方向は三角形ごとに違い得る（UV が鏡像のチャートがある）ので、
            // 符号付き面積で揃えてから内外を見る
            const float area = (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
            if(std::abs(area) < 1e-9f) {
                continue;
            }
            const float orient = (area < 0.0f) ? -1.0f : 1.0f;

            const int minX = std::max(static_cast<int>(std::floor(std::min(std::min(p0.x, p1.x), p2.x))), 0);
            const int maxX = std::min(static_cast<int>(std::ceil(std::max(std::max(p0.x, p1.x), p2.x))), size - 1);
            const int minY = std::max(static_cast<int>(std::floor(std::min(std::min(p0.y, p1.y), p2.y))), 0);
            const int maxY = std::min(static_cast<int>(std::ceil(std::max(std::max(p0.y, p1.y), p2.y))), size - 1);

            for(int y = minY; y <= maxY; ++y) {
                for(int x = minX; x <= maxX; ++x) {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;

                    const float e0 = ((p1.x - p0.x) * (py - p0.y) - (px - p0.x) * (p1.y - p0.y)) * orient;
                    const float e1 = ((p2.x - p1.x) * (py - p1.y) - (px - p1.x) * (p2.y - p1.y)) * orient;
                    const float e2 = ((p0.x - p2.x) * (py - p2.y) - (px - p2.x) * (p0.y - p2.y)) * orient;

                    if(e0 <= 0.0f || e1 <= 0.0f || e2 <= 0.0f) {
                        continue;
                    }

                    u8& hit = hits[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)];
                    if(hit == 1u) {
                        ++overlapping;
                    }
                    if(hit < 2u) {
                        ++hit;
                    }
                }
            }
        }

        return overlapping;
    }

    //------------------------------------------------------------------------
    //! 辺がちょうど 2 枚の三角形に共有されているかを調べます。
    //! @param  [in] mesh 対象のメッシュ
    //! @return すべての辺が 2 枚に共有されていれば true
    //------------------------------------------------------------------------
    bool IsClosedByPosition(const MeshBuilder& mesh) {
        std::unordered_map<u64, int> edgeUseCount;
        edgeUseCount.reserve(mesh.indices.size());

        for(size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            for(int corner = 0; corner < 3; ++corner) {
                const Vec3& a = mesh.positions[mesh.indices[i + static_cast<size_t>(corner)]];
                const Vec3& b = mesh.positions[mesh.indices[i + static_cast<size_t>((corner + 1) % 3)]];
                ++edgeUseCount[MakePositionEdgeKey(a, b)];
            }
        }

        for(const auto& entry : edgeUseCount) {
            if(entry.second != 2) {
                return false;
            }
        }
        return true;
    }

    //------------------------------------------------------------------------
    //! テクセル密度のばらつきを測ります。
    //!
    //! 三角形ごとの「UV 面積 / 3D 面積」がテクセル密度の 2 乗にあたります。
    //! これが方向によって大きく変わると、同じ岩なのに面ごとに Normal マップの
    //! 細かさが違って見えます。xatlas を入れた理由がここなので、
    //! 「被覆率は落ちたが密度は揃った」ことを数字で残せるようにしておきます。
    //!
    //! 中央値に対する 5〜95 パーセンタイルの比を返します。1.0 が完全に均一。
    //! 最大／最小ではなく分位点を見るのは、切り口の縁にできる極小の三角形が
    //! 1 枚混じるだけで最大値が跳ね、指標として使えなくなるためです。
    //!
    //! @param  [in] mesh 測るメッシュ
    //! @param  [out] outLow  中央値に対する 5 パーセンタイルの比
    //! @param  [out] outHigh 中央値に対する 95 パーセンタイルの比
    //------------------------------------------------------------------------
    void MeasureTexelDensitySpread(const MeshBuilder& mesh, float& outLow, float& outHigh) {
        outLow  = 0.0f;
        outHigh = 0.0f;

        if(mesh.uvs.size() != mesh.positions.size()) {
            return;
        }

        std::vector<float> density;
        density.reserve(mesh.indices.size() / 3);

        for(size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const u32 ia = mesh.indices[i];
            const u32 ib = mesh.indices[i + 1];
            const u32 ic = mesh.indices[i + 2];

            const float area3d = Length(Cross(mesh.positions[ib] - mesh.positions[ia],
                                              mesh.positions[ic] - mesh.positions[ia]));

            const Vec2 uvAb{mesh.uvs[ib].x - mesh.uvs[ia].x, mesh.uvs[ib].y - mesh.uvs[ia].y};
            const Vec2 uvAc{mesh.uvs[ic].x - mesh.uvs[ia].x, mesh.uvs[ic].y - mesh.uvs[ia].y};

            const float areaUv = std::abs(uvAb.x * uvAc.y - uvAc.x * uvAb.y);

            if(area3d <= 0.0f || areaUv <= 0.0f) {
                continue;
            }
            density.push_back(std::sqrt(areaUv / area3d));
        }

        if(density.size() < 20u) {
            return;
        }

        std::sort(density.begin(), density.end());

        const size_t count  = density.size();
        const float  median = density[count / 2];
        if(median <= 0.0f) {
            return;
        }

        outLow  = density[count / 20] / median;
        outHigh = density[(count * 19) / 20] / median;
    }

    void TestXAtlasUnwrap() {
        constexpr int kTextureSize = 256;

        //--------------------------------------------------------------------
        // 同じ形を 2 つの方式で展開して比べる。
        //
        // 形は共通なので、違うのは UV だけ。被覆率の差がそのまま
        // 「テクセルをどれだけ使えているか」の差になる
        //--------------------------------------------------------------------
        const auto build = [](UnwrapMethod method, Pipeline& pipeline) {
            pipeline.GetMutableParams().unwrapMethod     = method;
            pipeline.GetMutableParams().textureSize      = kTextureSize;
            pipeline.GetMutableParams().targetEdgeLength = 0.05f;
            pipeline.SetTargetStage(PipelineStage::BakeNormal);
            return pipeline.Update(nullptr, nullptr);
        };

        Pipeline octahedral;
        Pipeline atlas;

        const bool okOctahedral = build(UnwrapMethod::Octahedral, octahedral);
        const bool okAtlas      = build(UnwrapMethod::XAtlas, atlas);

        Check(okOctahedral && okAtlas, "both unwrappers complete");

        const UnwrapStats& stats = atlas.GetUnwrapStats();
        const MeshBuilder& mesh  = atlas.GetMesh();

        std::printf("       method=%s charts=%u utilization=%.1f%% split=%u\n",
                    (stats.methodName[0] != '\0') ? stats.methodName : "-",
                    stats.chartCount,
                    stats.utilization * 100.0f,
                    stats.splitVertexCount);

        if(stats.fellBack) {
            std::printf("       fell back because: %s\n",
                        stats.failureReason ? stats.failureReason : "(no reason recorded)");
        }
        Check(!stats.fellBack, "xatlas packed a single atlas without falling back");
        Check(stats.chartCount > 0, "xatlas produced charts");
        Check(mesh.uvs.size() == mesh.positions.size() && mesh.normals.size() == mesh.positions.size(),
              "uv and normal counts match the vertex count");

        bool uvInRange = true;
        for(const Vec2& uv : mesh.uvs) {
            if(uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) {
                uvInRange = false;
                break;
            }
        }
        Check(uvInRange, "all xatlas UVs are inside [0,1]");

        //--------------------------------------------------------------------
        // ハードエッジが残っているか。
        //
        // 分割を展開より後へ移したので、ここが 0 だと「稜線が法線の補間で
        // 丸められた状態」に戻っている。順序を入れ替えた回帰がここで出る
        //--------------------------------------------------------------------
        Check(stats.splitVertexCount > 0, "hard edges are split after the unwrap");

        //--------------------------------------------------------------------
        // 位相。xatlas はシームで頂点を複製するが、面は増やさないので
        // 位置で見れば閉じたままでなければならない
        //--------------------------------------------------------------------
        Check(IsClosedByPosition(mesh), "the unwrapped mesh is still closed by position");

        //--------------------------------------------------------------------
        // チャートの重なり。1 テクセルでも重なると、そこは 2 つの面の
        // どちらか片方の法線しか持てない
        //--------------------------------------------------------------------
        const u32 overlapping = CountOverlappingTexels(mesh, kTextureSize);
        std::printf("       overlapping texels = %u\n", overlapping);
        Check(overlapping == 0u, "no texel is claimed by two charts");

        //--------------------------------------------------------------------
        // 被覆率。xatlas を入れた目的がこれなので数字で残す
        //--------------------------------------------------------------------
        const u32   texels          = static_cast<u32>(kTextureSize) * static_cast<u32>(kTextureSize);
        const float octaCoverage    = static_cast<float>(octahedral.GetBakeGBuffer().GetCoveredTexelCount()) /
                                   static_cast<float>(texels);
        const float atlasCoverage = static_cast<float>(atlas.GetBakeGBuffer().GetCoveredTexelCount()) /
                                    static_cast<float>(texels);

        std::printf("       UV coverage octahedral=%.1f%% xatlas=%.1f%%\n",
                    octaCoverage * 100.0f,
                    atlasCoverage * 100.0f);
        Check(atlasCoverage > 0.3f, "the xatlas atlas covers a reasonable share of the texture");

        //--------------------------------------------------------------------
        // テクセル密度のばらつき。xatlas を入れた理由はここ。
        //
        // 八面体射影は正方形の対角へ向いた面のテクセル密度が落ちるので、
        // 被覆率では勝っていても「面ごとに Normal マップの細かさが違う」
        // という形で出る。被覆率と引き換えに何を得たのかを数字で残す
        //--------------------------------------------------------------------
        float octaLow  = 0.0f;
        float octaHigh = 0.0f;
        MeasureTexelDensitySpread(octahedral.GetMesh(), octaLow, octaHigh);

        float atlasLow  = 0.0f;
        float atlasHigh = 0.0f;
        MeasureTexelDensitySpread(mesh, atlasLow, atlasHigh);

        std::printf("       texel density (5%%..95%% of median) octahedral=%.2f..%.2f xatlas=%.2f..%.2f\n",
                    octaLow,
                    octaHigh,
                    atlasLow,
                    atlasHigh);
        Check((atlasHigh - atlasLow) < (octaHigh - octaLow),
              "xatlas spreads texel density less than octahedral projection");

        //--------------------------------------------------------------------
        // 決定論。xatlas は内部でスレッドを起こすので、ここは念入りに見る。
        // スレッド数に結果が依存していたら、焼き直すたびに法線が変わる
        //--------------------------------------------------------------------
        Pipeline repeat;
        const bool okRepeat = build(UnwrapMethod::XAtlas, repeat);

        Check(okRepeat && repeat.GetMesh().indices == mesh.indices &&
                  repeat.GetMesh().GetVertexCount() == mesh.GetVertexCount() &&
                  repeat.GetNormalMap().GetPixels() == atlas.GetNormalMap().GetPixels(),
              "xatlas is deterministic");
    }

    //------------------------------------------------------------------------
    //! リニアの値を sRGB のバイトへ符号化します（SurfaceBaker.cpp の参照実装）。
    //! @param  [in] value リニアの値
    //! @return sRGB のバイト
    //------------------------------------------------------------------------
    u8 EncodeSrgbReference(float value) {
        const float clamped = std::clamp(value, 0.0f, 1.0f);

        const float encoded =
            (clamped <= 0.0031308f) ? (clamped * 12.92f) : (1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f);

        return static_cast<u8>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
    }

    void TestSurfaceBake() {
        constexpr int kTextureSize = 256;

        Pipeline pipeline;
        pipeline.GetMutableParams().textureSize      = kTextureSize;
        pipeline.GetMutableParams().targetEdgeLength = 0.05f;
        pipeline.SetTargetStage(PipelineStage::BakeColor);

        Check(pipeline.Update(nullptr, nullptr), "the color bake completes");

        const SurfaceBakeStats& stats  = pipeline.GetSurfaceBakeStats();
        const BakeGBuffer&      buffer = pipeline.GetBakeGBuffer();

        const std::vector<u8>& albedo = pipeline.GetAlbedoMap().GetPixels();
        const std::vector<u8>& mr     = pipeline.GetMetallicRoughnessMap().GetPixels();

        std::printf("       texels=%u cavity=%.1f%% luminance=%.3f..%.3f\n",
                    stats.texelsWritten,
                    stats.cavityRatio * 100.0f,
                    stats.minLuminance,
                    stats.maxLuminance);
        // SurfaceBaker の kReliefToCavity はこの値の逆数に取ってある。
        // ノイズ層の構成を変えたらここを見て取り直すこと
        std::printf("       max relief = %.4f\n", stats.maxRelief);

        Check(stats.texelsWritten > 0, "the color bake wrote texels");
        Check(albedo.size() == mr.size() && !albedo.empty(), "both maps have the same size");

        //--------------------------------------------------------------------
        // 窪みの信号が効いているか。
        //
        // ここが 0 なら、色は焼けていても割れ目と出っぱりが区別されていない。
        // 「暗くしたつもりが真っ平ら」という失敗はこれでしか気付けない
        //--------------------------------------------------------------------
        Check(stats.cavityRatio > 0.001f, "the cavity signal reaches the surface");
        Check(stats.maxLuminance > stats.minLuminance + 0.01f, "the albedo is not one flat colour");

        //--------------------------------------------------------------------
        // MetallicRoughness のチャンネル割り当て（glTF 慣例）。
        //
        // B に 0 以外が入ると岩が金属になる。見た目では「妙にてかる」
        // としか分からず原因に辿り着けないので、ここで数える
        //--------------------------------------------------------------------
        u32 metallicTexels = 0;
        u32 minRoughness   = 255u;
        u32 maxRoughness   = 0u;

        // 窪み（＝ラフネスが高い側）と出っぱりで明度を比べるための集計
        double darkSum   = 0.0;
        double brightSum = 0.0;
        u32    darkCount = 0;
        u32    brightCount = 0;

        std::vector<u8> roughnessValues;
        roughnessValues.reserve(static_cast<size_t>(stats.texelsWritten));

        for(int y = 0; y < kTextureSize; ++y) {
            for(int x = 0; x < kTextureSize; ++x) {
                if(!buffer.IsCovered(x, y)) {
                    continue;
                }

                const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(kTextureSize) +
                                      static_cast<size_t>(x)) * 4u;

                if(mr[index + 2] != 0u) {
                    ++metallicTexels;
                }

                const u8 roughness = mr[index + 1];

                minRoughness = std::min(minRoughness, static_cast<u32>(roughness));
                maxRoughness = std::max(maxRoughness, static_cast<u32>(roughness));
                roughnessValues.push_back(roughness);
            }
        }

        std::printf("       roughness byte range = %u..%u\n", minRoughness, maxRoughness);

        Check(metallicTexels == 0, "metallic stays 0 everywhere (rock is never metal)");
        Check(maxRoughness > minRoughness, "roughness varies with the cavity signal");

        //--------------------------------------------------------------------
        // 窪みが実際に暗いか。
        //
        // ラフネスは「基準 + 窪み * 係数」なので、ラフネスの高い側が窪み。
        // その側の明度が低くなければ、2 枚のマップが別々の信号を見ている
        //--------------------------------------------------------------------
        if(!roughnessValues.empty() && maxRoughness > minRoughness) {
            std::vector<u8> sorted = roughnessValues;
            std::sort(sorted.begin(), sorted.end());

            const u8 lowCut  = sorted[sorted.size() / 10];
            const u8 highCut = sorted[(sorted.size() * 9) / 10];

            for(int y = 0; y < kTextureSize; ++y) {
                for(int x = 0; x < kTextureSize; ++x) {
                    if(!buffer.IsCovered(x, y)) {
                        continue;
                    }

                    const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(kTextureSize) +
                                          static_cast<size_t>(x)) * 4u;

                    // sRGB のまま足して構わない。ここで見たいのは大小関係だけ
                    const double luminance = albedo[index] * 0.2126 + albedo[index + 1] * 0.7152 +
                                             albedo[index + 2] * 0.0722;

                    if(mr[index + 1] >= highCut) {
                        darkSum += luminance;
                        ++darkCount;
                    } else if(mr[index + 1] <= lowCut) {
                        brightSum += luminance;
                        ++brightCount;
                    }
                }
            }
        }

        if(darkCount > 0 && brightCount > 0) {
            const double darkMean   = darkSum / static_cast<double>(darkCount);
            const double brightMean = brightSum / static_cast<double>(brightCount);

            std::printf("       mean albedo: cavity=%.1f  raised=%.1f\n", darkMean, brightMean);
            Check(darkMean < brightMean, "cavities are darker than the raised parts");
        } else {
            Check(false, "could not split texels into cavity and raised");
        }

        //--------------------------------------------------------------------
        // sRGB 符号化。
        //
        // 斑と汚れを切れば、全テクセルが基本色そのものになるはず。
        // ここがずれていたら、リニアのまま 8bit へ詰めているか、
        // 二重にガンマを掛けている
        //--------------------------------------------------------------------
        Pipeline flat;
        flat.GetMutableParams().textureSize      = 64;
        flat.GetMutableParams().targetEdgeLength = 0.09f;
        flat.GetMutableParams().colorVariation   = 0.0f;
        flat.GetMutableParams().cavityDarkening  = 0.0f;
        flat.SetTargetStage(PipelineStage::BakeColor);
        flat.Update(nullptr, nullptr);

        const Vec3 expectedColor = flat.GetParams().baseColor;

        const u8 expected[3] = {EncodeSrgbReference(expectedColor.x),
                                EncodeSrgbReference(expectedColor.y),
                                EncodeSrgbReference(expectedColor.z)};

        const std::vector<u8>& flatPixels = flat.GetAlbedoMap().GetPixels();

        bool srgbMatches = !flatPixels.empty();
        for(size_t i = 0; i + 2 < flatPixels.size(); i += 4) {
            if(flatPixels[i] != expected[0] || flatPixels[i + 1] != expected[1] || flatPixels[i + 2] != expected[2]) {
                srgbMatches = false;
                break;
            }
        }

        std::printf("       flat albedo expected (%u,%u,%u)\n", expected[0], expected[1], expected[2]);
        Check(srgbMatches, "a flat colour bakes to the sRGB encoding of the base colour");
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
    std::printf("--- xatlas ---\n");
    TestXAtlasUnwrap();
    std::printf("--- Surface (Albedo / MR) ---\n");
    TestSurfaceBake();
    std::printf("--- Mesh integrity ---\n");
    TestMeshIntegrity();
    std::printf("--- Presets ---\n");
    TestPresets();
    std::printf("--- JSON ---\n");
    TestJsonRoundTrip();

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
