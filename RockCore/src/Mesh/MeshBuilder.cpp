//----------------------------------------------------------------------------
//! @file   MeshBuilder.cpp
//! @brief  三角形メッシュの中間表現の実装
//----------------------------------------------------------------------------
#include <RockCore/Mesh/MeshBuilder.hpp>

#include <algorithm>
#include <limits>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! 頂点を追加します。
    //------------------------------------------------------------------------
    u32 MeshBuilder::AddVertex(const Vec3& position) {
        positions.push_back(position);
        return static_cast<u32>(positions.size() - 1);
    }

    //------------------------------------------------------------------------
    //! 三角形を追加します。
    //------------------------------------------------------------------------
    void MeshBuilder::AddTriangle(u32 a, u32 b, u32 c) {
        indices.push_back(a);
        indices.push_back(b);
        indices.push_back(c);
    }

    //------------------------------------------------------------------------
    //! バウンディングボックスを求めます。
    //------------------------------------------------------------------------
    void MeshBuilder::ComputeBounds(Vec3& outMin, Vec3& outMax) const {
        if(positions.empty()) {
            outMin = Vec3{0.0f, 0.0f, 0.0f};
            outMax = Vec3{0.0f, 0.0f, 0.0f};
            return;
        }

        constexpr float kBig = std::numeric_limits<float>::max();
        outMin               = Vec3{kBig, kBig, kBig};
        outMax               = Vec3{-kBig, -kBig, -kBig};

        for(const Vec3& p : positions) {
            outMin.x = std::min(outMin.x, p.x);
            outMin.y = std::min(outMin.y, p.y);
            outMin.z = std::min(outMin.z, p.z);
            outMax.x = std::max(outMax.x, p.x);
            outMax.y = std::max(outMax.y, p.y);
            outMax.z = std::max(outMax.z, p.z);
        }
    }

    //------------------------------------------------------------------------
    //! 最も長いエッジの長さを求めます。
    //------------------------------------------------------------------------
    float MeshBuilder::ComputeMaxEdgeLength() const {
        float maxLengthSq = 0.0f;

        for(size_t i = 0; i + 2 < indices.size(); i += 3) {
            const Vec3& a = positions[indices[i]];
            const Vec3& b = positions[indices[i + 1]];
            const Vec3& c = positions[indices[i + 2]];

            maxLengthSq = std::max(maxLengthSq, LengthSq(b - a));
            maxLengthSq = std::max(maxLengthSq, LengthSq(c - b));
            maxLengthSq = std::max(maxLengthSq, LengthSq(a - c));
        }

        return std::sqrt(maxLengthSq);
    }

    //------------------------------------------------------------------------
    //! 中身を空にします。
    //------------------------------------------------------------------------
    void MeshBuilder::Clear() {
        positions.clear();
        normals.clear();
        uvs.clear();
        indices.clear();
    }

    //------------------------------------------------------------------------
    //! normals と uvs を捨てます。
    //------------------------------------------------------------------------
    void MeshBuilder::DiscardAttributes() {
        normals.clear();
        uvs.clear();
    }

}    // namespace RockCore
