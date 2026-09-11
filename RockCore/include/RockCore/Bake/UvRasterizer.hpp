//----------------------------------------------------------------------------
//! @file   UvRasterizer.hpp
//! @brief  UV 空間のラスタライズ（ベイク用の G-Buffer）
//! @detail 三角形を UV 空間で走査し、テクセルごとにローポリ側の位置・法線・
//!         UV 微分を記録します。これが全ベイク（Normal / AO / Albedo / MR）の
//!         共通の入力になります。
//!
//!         2パス構成です。
//!           1. テクセル中心が三角形の内側にあるもの
//!           2. 未被覆のうち、テクセルの箱が三角形と重なるものを最近傍で埋める
//!         2 が無いとチャート境界の 1px が必ず欠けます。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Mesh/MeshBuilder.hpp>

#include <RockCore/Math/Vec.hpp>
#include <RockCore/Types.hpp>

#include <vector>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @struct BakeSample
    //! 1テクセルぶんのローポリ側の情報
    //------------------------------------------------------------------------
    struct BakeSample {
        Vec3 position;    //!< ローポリ表面の位置
        Vec3 normal;      //!< 補間済みの頂点法線。面法線ではない（シェーダがそうしているため）
        Vec3 dPdu;        //!< 位置の u 方向微分。三角形内で定数
        Vec3 dPdv;        //!< 位置の v 方向微分。三角形内で定数
        float texelWorldSize = 0.0f;    //!< このテクセルが覆うワールドの大きさ
        u32   triangleIndex  = 0;
    };

    //------------------------------------------------------------------------
    //! @class BakeGBuffer
    //! UV 空間にラスタライズした結果
    //------------------------------------------------------------------------
    class BakeGBuffer {
    public:
        //! 大きさを変えます。中身は未被覆で初期化されます。
        //! @param [in] size 一辺のテクセル数
        void Resize(int size);

        //! 一辺のテクセル数を返します。
        //! @return 一辺のテクセル数
        int GetSize() const { return m_size; }

        //! テクセルの情報を返します。
        //! @param  [in] x 横位置
        //! @param  [in] y 縦位置
        //! @return テクセルの情報
        const BakeSample& GetSample(int x, int y) const { return m_samples[Index(x, y)]; }

        //! テクセルが覆われているかを返します。
        //! @param  [in] x 横位置
        //! @param  [in] y 縦位置
        //! @return 覆われていれば true
        bool IsCovered(int x, int y) const { return m_coverage[Index(x, y)] != 0u; }

        //! 被覆マスクを返します。dilate が同じマスクを全マップへ使います。
        //! @return 被覆マスク（0 = 未被覆）
        const std::vector<u8>& GetCoverage() const { return m_coverage; }

        //! 覆われたテクセルの数を返します。
        //! @return 覆われたテクセル数
        u32 GetCoveredTexelCount() const { return m_coveredTexelCount; }

        //! テクセルへ情報を書き込みます。既に覆われていれば何もしません。
        //! @param [in] x      横位置
        //! @param [in] y      縦位置
        //! @param [in] sample 書き込む情報
        void WriteIfUncovered(int x, int y, const BakeSample& sample);

    private:
        //! 添字を求めます。
        //! @param  [in] x 横位置
        //! @param  [in] y 縦位置
        //! @return 配列の添字
        size_t Index(int x, int y) const { return static_cast<size_t>(y) * static_cast<size_t>(m_size) + static_cast<size_t>(x); }

        int                     m_size = 0;
        std::vector<BakeSample> m_samples;
        std::vector<u8>         m_coverage;
        u32                     m_coveredTexelCount = 0;
    };

    //------------------------------------------------------------------------
    //! メッシュを UV 空間へラスタライズします。
    //!
    //! mesh は uvs と normals を持っていること（UV 展開の後に呼ぶ）。
    //!
    //! @param  [in]  mesh       対象のメッシュ
    //! @param  [in]  size       一辺のテクセル数
    //! @param  [out] outGBuffer 書き込み先
    //! @return 1テクセルでも覆えたら true
    //------------------------------------------------------------------------
    bool RasterizeUv(const MeshBuilder& mesh, int size, BakeGBuffer& outGBuffer);

}    // namespace RockCore
