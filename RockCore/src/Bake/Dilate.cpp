//----------------------------------------------------------------------------
//! @file   Dilate.cpp
//! @brief  被覆マスクに沿った画像の膨張の実装
//----------------------------------------------------------------------------
#include <RockCore/Bake/Dilate.hpp>

#include <algorithm>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //! 8近傍のオフセット
        constexpr int kNeighborOffsets[8][2] = {
            {-1, -1}, {0, -1}, {1, -1},
            {-1,  0},          {1,  0},
            {-1,  1}, {0,  1}, {1,  1},
        };

    }    // namespace

    //------------------------------------------------------------------------
    //! 被覆マスクの外側へ色を膨張させます。
    //------------------------------------------------------------------------
    void DilateImage(ImageBuffer& image, const std::vector<u8>& coverage, int passes) {
        const int width  = image.GetWidth();
        const int height = image.GetHeight();

        if(width <= 0 || height <= 0 || passes <= 0) {
            return;
        }

        const size_t texelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        if(coverage.size() != texelCount) {
            return;
        }

        std::vector<u8>& pixels = image.GetPixels();

        std::vector<u8> mask = coverage;
        std::vector<u8> newlyCovered;

        for(int pass = 0; pass < passes; ++pass) {
            newlyCovered.assign(texelCount, 0u);

            bool changed = false;

            for(int y = 0; y < height; ++y) {
                for(int x = 0; x < width; ++x) {
                    const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                    if(mask[index] != 0u) {
                        continue;
                    }

                    // 覆われている近傍の平均を取る。最初に見つけた1つをコピーする
                    // 方が速いが、走査順で結果が変わるため平均にしてある
                    int sum[4]        = {0, 0, 0, 0};
                    int neighborCount = 0;

                    for(const auto& offset : kNeighborOffsets) {
                        const int nx = x + offset[0];
                        const int ny = y + offset[1];
                        if(nx < 0 || ny < 0 || nx >= width || ny >= height) {
                            continue;
                        }

                        const size_t neighbor = static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx);
                        if(mask[neighbor] == 0u) {
                            continue;
                        }

                        for(int channel = 0; channel < 4; ++channel) {
                            sum[channel] += pixels[neighbor * 4u + static_cast<size_t>(channel)];
                        }
                        ++neighborCount;
                    }

                    if(neighborCount == 0) {
                        continue;
                    }

                    for(int channel = 0; channel < 4; ++channel) {
                        pixels[index * 4u + static_cast<size_t>(channel)] =
                            static_cast<u8>(sum[channel] / neighborCount);
                    }

                    // マスクはパスの終わりにまとめて更新する。ここで立てると
                    // 同じパスの後続テクセルが今書いた色を拾い、走査順に依存する
                    newlyCovered[index] = 1u;
                    changed             = true;
                }
            }

            if(!changed) {
                // これ以上広がる先が無い
                break;
            }

            for(size_t i = 0; i < texelCount; ++i) {
                if(newlyCovered[i] != 0u) {
                    mask[i] = 1u;
                }
            }
        }
    }

}    // namespace RockCore
