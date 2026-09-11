//----------------------------------------------------------------------------
//! @file   Icosphere.cpp
//! @brief  正二十面体を細分割した単位球メッシュの実装
//----------------------------------------------------------------------------
#include <RockCore/Mesh/Icosphere.hpp>

#include <RockCore/Mesh/Subdivide.hpp>

#include <algorithm>
#include <cmath>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //! 正二十面体の辺長（単位球に外接させたとき）。細分割回数の見積もりに使う
        constexpr float kIcosahedronEdgeLength = 1.0514622f;

        //--------------------------------------------------------------------
        //! 正二十面体の12頂点と20三角形を書き込みます。
        //! @param [out] outMesh 書き込み先
        //--------------------------------------------------------------------
        void BuildIcosahedron(MeshBuilder& outMesh) {
            // 黄金比。(0, ±1, ±t) の巡回で12頂点が作れる
            const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;

            const Vec3 vertices[12] = {
                {-1.0f, t, 0.0f},  {1.0f, t, 0.0f},  {-1.0f, -t, 0.0f}, {1.0f, -t, 0.0f},
                {0.0f, -1.0f, t},  {0.0f, 1.0f, t},  {0.0f, -1.0f, -t}, {0.0f, 1.0f, -t},
                {t, 0.0f, -1.0f},  {t, 0.0f, 1.0f},  {-t, 0.0f, -1.0f}, {-t, 0.0f, 1.0f},
            };

            for(const Vec3& v : vertices) {
                outMesh.AddVertex(Normalize(v));
            }

            // 外向きの巻き方向に揃えてある
            const u32 faces[20][3] = {
                {0, 11, 5},  {0, 5, 1},   {0, 1, 7},   {0, 7, 10},  {0, 10, 11},
                {1, 5, 9},   {5, 11, 4},  {11, 10, 2}, {10, 7, 6},  {7, 1, 8},
                {3, 9, 4},   {3, 4, 2},   {3, 2, 6},   {3, 6, 8},   {3, 8, 9},
                {4, 9, 5},   {2, 4, 11},  {6, 2, 10},  {8, 6, 7},   {9, 8, 1},
            };

            for(const auto& face : faces) {
                outMesh.AddTriangle(face[0], face[1], face[2]);
            }
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! 単位球の正二十面体メッシュを作ります。
    //------------------------------------------------------------------------
    void BuildIcosphere(MeshBuilder& outMesh, int subdivisions) {
        outMesh.Clear();
        BuildIcosahedron(outMesh);

        // 中点は球面へ押し戻す。これをしないと細分割するほど多面体のままになる
        const VertexProjector toUnitSphere = [](const Vec3& p) { return Normalize(p); };

        const int count = std::clamp(subdivisions, 0, 8);
        for(int i = 0; i < count; ++i) {
            SubdivideOnce(outMesh, toUnitSphere);
        }
    }

    //------------------------------------------------------------------------
    //! 目標エッジ長を満たす細分割回数を見積もります。
    //------------------------------------------------------------------------
    int EstimateIcosphereSubdivisions(float targetEdgeLength, float sphereRadius, int maxSubdivisions) {
        if(targetEdgeLength <= 0.0f || sphereRadius <= 0.0f) {
            return 0;
        }

        float edgeLength = kIcosahedronEdgeLength * sphereRadius;

        int level = 0;
        while(level < maxSubdivisions && edgeLength > targetEdgeLength) {
            edgeLength *= 0.5f;
            ++level;
        }
        return level;
    }

}    // namespace RockCore
