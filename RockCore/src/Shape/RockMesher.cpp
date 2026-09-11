//----------------------------------------------------------------------------
//! @file   RockMesher.cpp
//! @brief  半径関数から岩のメッシュを組み立てる実装
//----------------------------------------------------------------------------
#include <RockCore/Shape/RockMesher.hpp>

#include <RockCore/Mesh/Icosphere.hpp>

#include <algorithm>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //! 細分割回数の上限。7 で三角形が約 32 万枚になるので、
        //! ここを超えさせるとプレビューの対話性が失われる
        constexpr int kMaxSubdivisions = 7;

        //! 細分割回数の下限。
        //!
        //! 八面体射影の UV 展開は「三角形1枚が UV 空間で十分小さい」ことを
        //! 前提にシームを検出している（OctahedralUnwrapper.cpp の kSeamSpanSq）。
        //! 細分割 2 未満だと素の正二十面体の三角形が UV 空間で大きすぎ、
        //! 全三角形がシーム扱いになって展開が壊れる
        constexpr int kMinSubdivisions = 2;

    }    // namespace

    //------------------------------------------------------------------------
    //! メッシュが表現できる周波数の上限（ナイキスト）を求めます。
    //------------------------------------------------------------------------
    float ComputeMeshNyquistFrequency(const RockParams& params) {
        const float edgeLength = std::max(params.targetEdgeLength, 1e-4f);
        return std::max(params.radius / (2.0f * edgeLength), 0.0f);
    }

    //------------------------------------------------------------------------
    //! 岩のメッシュを組み立てます。
    //------------------------------------------------------------------------
    void BuildRockMesh(const RockParams& params, const RockField& lowField, MeshBuilder& outMesh, RockMeshStats* outStats) {
        //--------------------------------------------------------------------
        // 1. 細分割回数を先に決める。
        //    射影後に最長エッジを測って追加細分割する手もあるが、それだと
        //    形を動かすたびに三角形数が変わってプレビューが落ち着かない
        //--------------------------------------------------------------------
        const int estimated    = EstimateIcosphereSubdivisions(params.targetEdgeLength, params.radius, kMaxSubdivisions);
        const int subdivisions =
            std::clamp(std::max(estimated, params.baseSubdivision), kMinSubdivisions, kMaxSubdivisions);

        //--------------------------------------------------------------------
        // 2. 単位球を作る
        //--------------------------------------------------------------------
        BuildIcosphere(outMesh, subdivisions);

        //--------------------------------------------------------------------
        // 3. 各頂点を表面へ射影する。
        //    この時点の positions[i] は単位ベクトルそのものなので、
        //    方向として直接渡せる（星形表現の都合の良いところ）
        //--------------------------------------------------------------------
        const u32 vertexCount = outMesh.GetVertexCount();

        outMesh.normals.resize(vertexCount);

        // 勾配の差分幅はメッシュの解像度に合わせる。頂点間隔より細かく測ると
        // メッシュには載っていない高周波を法線だけが拾い、陰影がざらつく
        const float gradientStep = std::max(params.targetEdgeLength, 1e-4f);

        for(u32 i = 0; i < vertexCount; ++i) {
            const Vec3 direction = outMesh.positions[i];
            const Vec3 surface   = lowField.SurfacePoint(direction);

            outMesh.positions[i] = surface;

            // 法線は解析勾配。F は内側が負・外側が正なので、勾配はそのまま
            // 外向きを指す（反転させないこと）
            outMesh.normals[i] = Normalize(lowField.Gradient(surface, gradientStep));
        }

        //--------------------------------------------------------------------
        // 4. 結果を返す
        //--------------------------------------------------------------------
        if(outStats) {
            outStats->subdivisions     = subdivisions;
            outStats->vertexCount      = vertexCount;
            outStats->triangleCount    = outMesh.GetTriangleCount();
            outStats->actualEdgeLength = outMesh.ComputeMaxEdgeLength();
        }
    }

}    // namespace RockCore
