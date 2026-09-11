//----------------------------------------------------------------------------
//! @file   ImageBuffer.cpp
//! @brief  RGBA8 画像の実装
//----------------------------------------------------------------------------
#include <RockCore/Bake/ImageBuffer.hpp>

#include <algorithm>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! 大きさを変えます。
    //------------------------------------------------------------------------
    void ImageBuffer::Resize(int width, int height) {
        m_width  = std::max(width, 0);
        m_height = std::max(height, 0);
        m_pixels.assign(static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4u, 0u);
    }

    //------------------------------------------------------------------------
    //! 全体を1色で埋めます。
    //------------------------------------------------------------------------
    void ImageBuffer::Fill(u8 r, u8 g, u8 b, u8 a) {
        for(size_t i = 0; i + 3 < m_pixels.size(); i += 4) {
            m_pixels[i]     = r;
            m_pixels[i + 1] = g;
            m_pixels[i + 2] = b;
            m_pixels[i + 3] = a;
        }
    }

    //------------------------------------------------------------------------
    //! 1テクセルを書き込みます。
    //------------------------------------------------------------------------
    void ImageBuffer::SetPixel(int x, int y, u8 r, u8 g, u8 b, u8 a) {
        if(x < 0 || y < 0 || x >= m_width || y >= m_height) {
            return;
        }

        const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)) * 4u;
        m_pixels[offset]     = r;
        m_pixels[offset + 1] = g;
        m_pixels[offset + 2] = b;
        m_pixels[offset + 3] = a;
    }

}    // namespace RockCore
