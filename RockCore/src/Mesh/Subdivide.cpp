//----------------------------------------------------------------------------
//! @file   Subdivide.cpp
//! @brief  三角形の 1-to-4 細分割の実装
//----------------------------------------------------------------------------
#include <RockCore/Mesh/Subdivide.hpp>

#include <algorithm>
#include <unordered_map>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //--------------------------------------------------------------------
        //! エッジを頂点番号の組で表すキーを作ります。
        //!
        //! 小さい番号を上位へ置くことで、向きの違う同じエッジが同じキーに
        //! なるようにしています。
        //!
        //! @param  [in] a 片方の頂点番号
        //! @param  [in] b もう片方の頂点番号
        //! @return エッジのキー
        //--------------------------------------------------------------------
        u64 MakeEdgeKey(u32 a, u32 b) {
            const u32 lo = std::min(a, b);
            const u32 hi = std::max(a, b);
            return (static_cast<u64>(lo) << 32) | static_cast<u64>(hi);
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! 全三角形を1回だけ 1-to-4 細分割します。
    //------------------------------------------------------------------------
    void SubdivideOnce(MeshBuilder& mesh, const VertexProjector& projector) {
        if(mesh.indices.empty()) {
            return;
        }

        mesh.DiscardAttributes();

        const std::vector<u32> sourceIndices = mesh.indices;
        mesh.indices.clear();
        mesh.indices.reserve(sourceIndices.size() * 4);

        // 三角形1枚が4枚になるので、新しい頂点はおおよそ元の頂点数ぶん増える
        std::unordered_map<u64, u32> midpointCache;
        midpointCache.reserve(sourceIndices.size());

        auto getMidpoint = [&](u32 a, u32 b) {
            const u64 key = MakeEdgeKey(a, b);

            const auto found = midpointCache.find(key);
            if(found != midpointCache.end()) {
                return found->second;
            }

            Vec3 midpoint = (mesh.positions[a] + mesh.positions[b]) * 0.5f;
            if(projector) {
                midpoint = projector(midpoint);
            }

            const u32 index    = mesh.AddVertex(midpoint);
            midpointCache[key] = index;
            return index;
        };

        for(size_t i = 0; i + 2 < sourceIndices.size(); i += 3) {
            const u32 a = sourceIndices[i];
            const u32 b = sourceIndices[i + 1];
            const u32 c = sourceIndices[i + 2];

            const u32 ab = getMidpoint(a, b);
            const u32 bc = getMidpoint(b, c);
            const u32 ca = getMidpoint(c, a);

            // 巻き方向を元の三角形と揃える（中央の1枚も同じ向きになる）
            mesh.AddTriangle(a, ab, ca);
            mesh.AddTriangle(ab, b, bc);
            mesh.AddTriangle(ca, bc, c);
            mesh.AddTriangle(ab, bc, ca);
        }
    }

    //------------------------------------------------------------------------
    //! 最長エッジが目標を下回るまで細分割を繰り返します。
    //------------------------------------------------------------------------
    int SubdivideToEdgeLength(MeshBuilder&           mesh,
                              float                  targetEdgeLength,
                              int                    maxIterations,
                              const VertexProjector& projector) {
        if(targetEdgeLength <= 0.0f) {
            return 0;
        }

        int iterations = 0;
        while(iterations < maxIterations && mesh.ComputeMaxEdgeLength() > targetEdgeLength) {
            SubdivideOnce(mesh, projector);
            ++iterations;
        }
        return iterations;
    }

}    // namespace RockCore
