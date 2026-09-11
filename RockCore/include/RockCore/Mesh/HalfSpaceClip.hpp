//----------------------------------------------------------------------------
//! @file   HalfSpaceClip.hpp
//! @brief  三角形メッシュを半空間で切り、切り口を塞ぐ
//! @detail 破断面を**メッシュの実エッジ**として持たせるためのものです。
//!
//!         半径関数だけで平らな面を作ることもできますが、それだとテッセレーションが
//!         稜線に沿わないため、シルエットが階段状にギザつきます。三角形の辺を
//!         稜線に一致させるにはメッシュ側を切るしかありません。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Mesh/MeshBuilder.hpp>

#include <RockCore/Math/Vec.hpp>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! メッシュを半空間 dot(p, normal) <= distance の内側だけに切り詰めます。
    //!
    //! 切り口には新しい面（平らな破断面）が張られるので、閉じたメッシュの
    //! ままになります。normals と uvs は位相が変わるため捨てられます。
    //!
    //! メッシュが平面の完全に内側にある場合は何もしません。
    //!
    //! 切り口は重心からの扇ではなく**同心リング**で張ります。扇にすると
    //! 中心から縁までの長い辺ができ、後段の SubdivideToEdgeLength が
    //! その1辺のために全体を細分割してしまって三角形数が跳ね上がります。
    //!
    //! @param  [in,out] mesh              対象のメッシュ
    //! @param  [in]     planeNormal       外向きの単位法線
    //! @param  [in]     planeDistance     原点から平面までの距離
    //! @param  [in]     targetEdgeLength  切り口を張るときの目標エッジ長。
    //!                                    0 以下なら単純な扇にする
    //! @return 実際に切ったら true
    //------------------------------------------------------------------------
    bool ClipMeshByPlane(MeshBuilder& mesh, const Vec3& planeNormal, float planeDistance, float targetEdgeLength);

}    // namespace RockCore
