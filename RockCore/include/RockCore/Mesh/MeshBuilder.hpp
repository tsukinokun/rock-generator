//----------------------------------------------------------------------------
//! @file   MeshBuilder.hpp
//! @brief  編集しやすい形の三角形メッシュ
//! @detail エンジンの MeshData（生バイト列 + VertexFormat）とは別物です。
//!         RockCore を GraphicsCommon から切り離しておくために自前の表現を
//!         持ち、GPU 向けへの詰め替えは RockEditor 側で行います。
//!
//!         頂点属性は position / normal / uv0 だけです。エンジンの .tsm が
//!         それしか持たないため（頂点カラーも接線も複数UVも非対応）、
//!         ここで余計に持っても出口がありません。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Math/Vec.hpp>
#include <RockCore/Types.hpp>

#include <vector>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @struct MeshBuilder
    //! 三角形メッシュの中間表現
    //------------------------------------------------------------------------
    struct MeshBuilder {
        std::vector<Vec3> positions;
        std::vector<Vec3> normals;    //!< 空のこともある（細分割の直後など）
        std::vector<Vec2> uvs;        //!< 空のこともある（UV 展開前）
        std::vector<u32>  indices;

        //! 頂点を追加します。
        //! @param  [in] position 頂点位置
        //! @return 追加した頂点の番号
        u32 AddVertex(const Vec3& position);

        //! 三角形を追加します。
        //! @param [in] a 1つ目の頂点番号
        //! @param [in] b 2つ目の頂点番号
        //! @param [in] c 3つ目の頂点番号
        void AddTriangle(u32 a, u32 b, u32 c);

        //! 頂点数を返します。
        //! @return 頂点数
        u32 GetVertexCount() const { return static_cast<u32>(positions.size()); }

        //! 三角形数を返します。
        //! @return 三角形数
        u32 GetTriangleCount() const { return static_cast<u32>(indices.size() / 3); }

        //! バウンディングボックスを求めます。空なら両端が 0 になります。
        //! @param [out] outMin 最小側
        //! @param [out] outMax 最大側
        void ComputeBounds(Vec3& outMin, Vec3& outMax) const;

        //! 最も長いエッジの長さを求めます。細分割の停止判定に使います。
        //! @return 最長エッジ長
        float ComputeMaxEdgeLength() const;

        //! 中身を空にします。確保済み領域は手放しません。
        void Clear();

        //! normals と uvs を捨てます。位相を変える操作の前に呼びます。
        void DiscardAttributes();
    };

}    // namespace RockCore
