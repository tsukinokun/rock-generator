//----------------------------------------------------------------------------
//! @file   NormalBaker.cpp
//! @brief  Normal マップのベイクの実装
//----------------------------------------------------------------------------
#include <RockCore/Bake/NormalBaker.hpp>

#include <RockCore/Bake/TangentFrame.hpp>

#include <algorithm>
#include <cmath>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //! 未被覆テクセルの既定値。タンジェント空間で (0,0,1) を符号化したもの
        constexpr u8 kFlatNormalRgb[3] = {128u, 128u, 255u};

    }    // namespace

    //------------------------------------------------------------------------
    //! Normal マップを焼きます。
    //------------------------------------------------------------------------
    bool BakeNormalMap(const BakeGBuffer&      gbuffer,
                       const RockField&        fullField,
                       ImageBuffer&            outImage,
                       const ProgressCallback& progress,
                       const CancelToken*      cancel,
                       NormalBakeStats*        outStats) {
        const int size = gbuffer.GetSize();
        if(size <= 0) {
            return false;
        }

        outImage.Resize(size, size);
        outImage.Fill(kFlatNormalRgb[0], kFlatNormalRgb[1], kFlatNormalRgb[2], 255u);

        NormalBakeStats stats{};

        for(int y = 0; y < size; ++y) {
            // 中断の確認は行単位。テクセル単位で見るとアトミック読みが
            // ベイクそのものより重くなる
            if(cancel && cancel->IsCancelled()) {
                return false;
            }

            for(int x = 0; x < size; ++x) {
                if(!gbuffer.IsCovered(x, y)) {
                    continue;
                }

                const BakeSample& sample = gbuffer.GetSample(x, y);

                //------------------------------------------------------------
                // 1. ローポリのテクセル位置から、同じ方向の真の表面を引く。
                //    ここにローポリのテッシレーション誤差も含まれるので、
                //    形状誤差と高周波ディテールが同時に焼ける
                //------------------------------------------------------------
                const Vec3 surfaceHi = fullField.ProjectToSurface(sample.position);

                //------------------------------------------------------------
                // 2. 真の表面の法線。差分幅はそのテクセルのワールドサイズ。
                //    F は内側が負・外側が正なので勾配はそのまま外向き
                //------------------------------------------------------------
                const Vec3 normalHi = Normalize(fullField.Gradient(surfaceHi, sample.texelWorldSize));

                //------------------------------------------------------------
                // 3. エンジンの TBN で符号化する（PBR.hlsli:155-174 と同値）
                //------------------------------------------------------------
                const Mat3 tbn           = BuildEngineTbn(sample.normal, sample.dPdu, sample.dPdv);
                const Vec3 tangentNormal = EncodeTangentNormal(tbn, normalHi);

                u8 rgb[3];
                PackTangentNormal(tangentNormal, rgb);
                outImage.SetPixel(x, y, rgb[0], rgb[1], rgb[2], 255u);

                ++stats.texelsWritten;

                // 平坦な法線が書かれたかどうかで数えると、本当に平らな面まで
                // 縮退に数えてしまう。TBN が潰れているかを直接見る
                if(IsEngineTbnDegenerate(tbn)) {
                    ++stats.degenerateTexels;
                }
            }

            ReportProgress(progress, static_cast<float>(y + 1) / static_cast<float>(size), "Bake: Normal");
        }

        if(outStats) {
            *outStats = stats;
        }
        return true;
    }

}    // namespace RockCore
