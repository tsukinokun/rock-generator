//----------------------------------------------------------------------------
//! @file   UvRasterizer.cpp
//! @brief  UV 空間ラスタライズの実装
//----------------------------------------------------------------------------
#include <RockCore/Bake/UvRasterizer.hpp>

#include <algorithm>
#include <cmath>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //--------------------------------------------------------------------
        //! @struct TriangleSetup
        //! 三角形1枚ぶんの、テクセル走査で繰り返し使う値
        //--------------------------------------------------------------------
        struct TriangleSetup {
            Vec2 uv[3];
            Vec3 position[3];
            Vec3 normal[3];
            Vec3 dPdu;
            Vec3 dPdv;
            float texelWorldSize = 0.0f;
            float invArea        = 0.0f;    //!< UV 空間の符号付き面積の逆数（重心座標用）
            bool  valid          = false;
        };

        //--------------------------------------------------------------------
        //! 三角形の定数項を求めます。
        //!
        //! dPdu / dPdv は UV の 2x2 ヤコビアンを逆行列して求めます。
        //! 位置は UV に対して線形なので、これは三角形内で定数です
        //! （= シェーダの ddx(worldPos)/ddx(uv) と厳密に一致する量）。
        //!
        //! @param  [in] mesh          対象のメッシュ
        //! @param  [in] triangleIndex 三角形の番号
        //! @param  [in] size          一辺のテクセル数
        //! @return 組み立てた定数項
        //--------------------------------------------------------------------
        TriangleSetup SetupTriangle(const MeshBuilder& mesh, u32 triangleIndex, int size) {
            TriangleSetup setup{};

            const size_t base = static_cast<size_t>(triangleIndex) * 3u;
            for(int corner = 0; corner < 3; ++corner) {
                const u32 vertex         = mesh.indices[base + static_cast<size_t>(corner)];
                setup.uv[corner]       = mesh.uvs[vertex];
                setup.position[corner] = mesh.positions[vertex];
                setup.normal[corner]   = mesh.normals[vertex];
            }

            const Vec2 duv1 = setup.uv[1] - setup.uv[0];
            const Vec2 duv2 = setup.uv[2] - setup.uv[0];

            const float area = Cross2(duv1, duv2);
            if(std::abs(area) < 1e-12f) {
                // UV が潰れている。被覆させずに未被覆のまま残し、dilate に任せる
                return setup;
            }

            setup.invArea = 1.0f / area;

            const Vec3 dp1 = setup.position[1] - setup.position[0];
            const Vec3 dp2 = setup.position[2] - setup.position[0];

            // [dp1; dp2] = [duv1; duv2] * [dPdu; dPdv] を dPdu / dPdv について解く
            setup.dPdu = (dp1 * duv2.y - dp2 * duv1.y) * setup.invArea;
            setup.dPdv = (dp2 * duv1.x - dp1 * duv2.x) * setup.invArea;

            // 1テクセルが覆うワールドの大きさ。勾配の差分幅 h にそのまま使う。
            // これを合わせておくと、ノイズが自動的にターゲット解像度へ
            // 帯域制限され、ミップのちらつきが抑えられる
            const float texelStep  = 1.0f / static_cast<float>(size);
            setup.texelWorldSize   = std::max(Length(setup.dPdu), Length(setup.dPdv)) * texelStep;

            setup.valid = true;
            return setup;
        }

        //--------------------------------------------------------------------
        //! 重心座標を求めます。
        //! @param  [in] setup 三角形の定数項
        //! @param  [in] p     UV 空間の点
        //! @param  [out] outW 3つの重み
        //--------------------------------------------------------------------
        void ComputeBarycentric(const TriangleSetup& setup, const Vec2& p, float outW[3]) {
            const Vec2 v0 = setup.uv[1] - setup.uv[0];
            const Vec2 v1 = setup.uv[2] - setup.uv[0];
            const Vec2 v2 = p - setup.uv[0];

            // p = uv0 + w1*v0 + w2*v1 を外積で解く。
            // Cross2(v2, v1) = w1 * area、Cross2(v0, v2) = w2 * area
            outW[1] = Cross2(v2, v1) * setup.invArea;
            outW[2] = Cross2(v0, v2) * setup.invArea;
            outW[0] = 1.0f - outW[1] - outW[2];
        }

        //--------------------------------------------------------------------
        //! 重心座標を三角形の内側へ押し込めます。
        //!
        //! チャート境界の 1px を埋めるためのもので、厳密な最近傍点ではなく
        //! 「はみ出した重みを切って正規化する」近似です。テクセル1つぶんの
        //! ずれしか生まないので、dilate の前処理としては十分です。
        //!
        //! @param [in,out] w 3つの重み
        //--------------------------------------------------------------------
        void ClampBarycentric(float w[3]) {
            float sum = 0.0f;
            for(int i = 0; i < 3; ++i) {
                w[i] = std::clamp(w[i], 0.0f, 1.0f);
                sum += w[i];
            }

            if(sum > 1e-8f) {
                const float inv = 1.0f / sum;
                for(int i = 0; i < 3; ++i) {
                    w[i] *= inv;
                }
            } else {
                w[0] = 1.0f;
                w[1] = 0.0f;
                w[2] = 0.0f;
            }
        }

        //--------------------------------------------------------------------
        //! 重心座標から BakeSample を作ります。
        //! @param  [in] setup         三角形の定数項
        //! @param  [in] w             3つの重み
        //! @param  [in] triangleIndex 三角形の番号
        //! @return 作った BakeSample
        //--------------------------------------------------------------------
        BakeSample MakeSample(const TriangleSetup& setup, const float w[3], u32 triangleIndex) {
            BakeSample sample{};
            sample.position = setup.position[0] * w[0] + setup.position[1] * w[1] + setup.position[2] * w[2];

            // 補間した頂点法線をそのまま使う。シェーダの ApplyNormalMap も
            // 面法線ではなく補間済みの法線を受け取るため、ここで面法線にすると
            // TBN が食い違う
            sample.normal = Normalize(setup.normal[0] * w[0] + setup.normal[1] * w[1] + setup.normal[2] * w[2]);

            sample.dPdu           = setup.dPdu;
            sample.dPdv           = setup.dPdv;
            sample.texelWorldSize = setup.texelWorldSize;
            sample.triangleIndex  = triangleIndex;
            return sample;
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! 大きさを変えます。
    //------------------------------------------------------------------------
    void BakeGBuffer::Resize(int size) {
        m_size = std::max(size, 0);

        const size_t count = static_cast<size_t>(m_size) * static_cast<size_t>(m_size);
        m_samples.assign(count, BakeSample{});
        m_coverage.assign(count, 0u);
        m_coveredTexelCount = 0;
    }

    //------------------------------------------------------------------------
    //! テクセルへ情報を書き込みます。
    //------------------------------------------------------------------------
    void BakeGBuffer::WriteIfUncovered(int x, int y, const BakeSample& sample) {
        if(x < 0 || y < 0 || x >= m_size || y >= m_size) {
            return;
        }

        const size_t index = Index(x, y);
        if(m_coverage[index] != 0u) {
            return;
        }

        m_samples[index]  = sample;
        m_coverage[index] = 1u;
        ++m_coveredTexelCount;
    }

    //------------------------------------------------------------------------
    //! メッシュを UV 空間へラスタライズします。
    //------------------------------------------------------------------------
    bool RasterizeUv(const MeshBuilder& mesh, int size, BakeGBuffer& outGBuffer) {
        outGBuffer.Resize(size);

        if(size <= 0 || mesh.indices.empty() || mesh.uvs.size() != mesh.positions.size() ||
           mesh.normals.size() != mesh.positions.size()) {
            return false;
        }

        const u32   triangleCount = mesh.GetTriangleCount();
        const float texels        = static_cast<float>(size);

        // 定数項は2パスで使い回す。三角形ごとに2回組み立てるのは無駄
        std::vector<TriangleSetup> setups(triangleCount);
        for(u32 t = 0; t < triangleCount; ++t) {
            setups[t] = SetupTriangle(mesh, t, size);
        }

        //--------------------------------------------------------------------
        // パス1: テクセル中心が三角形の内側にあるもの
        //--------------------------------------------------------------------
        for(u32 t = 0; t < triangleCount; ++t) {
            const TriangleSetup& setup = setups[t];
            if(!setup.valid) {
                continue;
            }

            float minU = setup.uv[0].x;
            float maxU = setup.uv[0].x;
            float minV = setup.uv[0].y;
            float maxV = setup.uv[0].y;
            for(int corner = 1; corner < 3; ++corner) {
                minU = std::min(minU, setup.uv[corner].x);
                maxU = std::max(maxU, setup.uv[corner].x);
                minV = std::min(minV, setup.uv[corner].y);
                maxV = std::max(maxV, setup.uv[corner].y);
            }

            const int x0 = std::max(0, static_cast<int>(std::floor(minU * texels)));
            const int x1 = std::min(size - 1, static_cast<int>(std::ceil(maxU * texels)));
            const int y0 = std::max(0, static_cast<int>(std::floor(minV * texels)));
            const int y1 = std::min(size - 1, static_cast<int>(std::ceil(maxV * texels)));

            for(int y = y0; y <= y1; ++y) {
                for(int x = x0; x <= x1; ++x) {
                    const Vec2 center{(static_cast<float>(x) + 0.5f) / texels, (static_cast<float>(y) + 0.5f) / texels};

                    float w[3];
                    ComputeBarycentric(setup, center, w);

                    if(w[0] < 0.0f || w[1] < 0.0f || w[2] < 0.0f) {
                        continue;
                    }

                    outGBuffer.WriteIfUncovered(x, y, MakeSample(setup, w, t));
                }
            }
        }

        //--------------------------------------------------------------------
        // パス2: 未被覆のうち、テクセルの箱が三角形と重なるものを最近傍で埋める。
        //
        // パス1の走査範囲を1テクセル広げ、まだ空いているところだけを埋める。
        // これが無いとチャート境界の 1px が必ず欠け、ミップの粗いレベルで
        // 背景色がにじみ出る
        //--------------------------------------------------------------------
        for(u32 t = 0; t < triangleCount; ++t) {
            const TriangleSetup& setup = setups[t];
            if(!setup.valid) {
                continue;
            }

            float minU = setup.uv[0].x;
            float maxU = setup.uv[0].x;
            float minV = setup.uv[0].y;
            float maxV = setup.uv[0].y;
            for(int corner = 1; corner < 3; ++corner) {
                minU = std::min(minU, setup.uv[corner].x);
                maxU = std::max(maxU, setup.uv[corner].x);
                minV = std::min(minV, setup.uv[corner].y);
                maxV = std::max(maxV, setup.uv[corner].y);
            }

            const int x0 = std::max(0, static_cast<int>(std::floor(minU * texels)) - 1);
            const int x1 = std::min(size - 1, static_cast<int>(std::ceil(maxU * texels)) + 1);
            const int y0 = std::max(0, static_cast<int>(std::floor(minV * texels)) - 1);
            const int y1 = std::min(size - 1, static_cast<int>(std::ceil(maxV * texels)) + 1);

            for(int y = y0; y <= y1; ++y) {
                for(int x = x0; x <= x1; ++x) {
                    if(outGBuffer.IsCovered(x, y)) {
                        continue;
                    }

                    const Vec2 center{(static_cast<float>(x) + 0.5f) / texels, (static_cast<float>(y) + 0.5f) / texels};

                    float w[3];
                    ComputeBarycentric(setup, center, w);
                    ClampBarycentric(w);

                    outGBuffer.WriteIfUncovered(x, y, MakeSample(setup, w, t));
                }
            }
        }

        return outGBuffer.GetCoveredTexelCount() > 0;
    }

}    // namespace RockCore
