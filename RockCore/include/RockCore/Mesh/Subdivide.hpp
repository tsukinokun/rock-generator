//----------------------------------------------------------------------------
//! @file   Subdivide.hpp
//! @brief  三角形の 1-to-4 細分割
//! @detail Icosphere から切り離してあるのは、Phase 3 で半空間クリップ後の
//!         三角形にも同じ処理を掛けるためです。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Mesh/MeshBuilder.hpp>

#include <RockCore/Math/Vec.hpp>

#include <functional>

// 名前空間 RockCore
namespace RockCore {

    //! 新しく作った中点をどこへ置くかを決める関数。
    //! 球面へ戻す／半径関数の表面へ乗せる、といった用途に使います。
    using VertexProjector = std::function<Vec3(const Vec3&)>;

    //------------------------------------------------------------------------
    //! 全三角形を1回だけ 1-to-4 細分割します。
    //!
    //! エッジの中点は共有されるよう辞書で引き当てます（同じエッジを2回
    //! 作ると頂点が重複し、法線の平均化とUV展開が壊れる）。
    //! normals と uvs は位相が変わるため捨てられます。
    //!
    //! @param [in,out] mesh      対象のメッシュ
    //! @param [in]     projector 中点の置き場所を決める関数。nullptr なら素の中点
    //------------------------------------------------------------------------
    void SubdivideOnce(MeshBuilder& mesh, const VertexProjector& projector = nullptr);

    //------------------------------------------------------------------------
    //! 最長エッジが目標を下回るまで細分割を繰り返します。
    //!
    //! @param  [in,out] mesh              対象のメッシュ
    //! @param  [in]     targetEdgeLength  目標のエッジ長。0 以下なら何もしない
    //! @param  [in]     maxIterations     繰り返しの上限（三角形数の暴走を防ぐ）
    //! @param  [in]     projector         中点の置き場所を決める関数
    //! @return 実際に細分割した回数
    //------------------------------------------------------------------------
    int SubdivideToEdgeLength(MeshBuilder&           mesh,
                              float                  targetEdgeLength,
                              int                    maxIterations,
                              const VertexProjector& projector = nullptr);

}    // namespace RockCore
