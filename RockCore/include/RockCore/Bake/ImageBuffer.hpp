//----------------------------------------------------------------------------
//! @file   ImageBuffer.hpp
//! @brief  ベイク結果を入れる RGBA8 の画像
//! @detail 常に RGBA8・リニアのまま持ちます。Normal / MR / AO は
//!         ガンマを掛けてはいけないので、ここで sRGB へ変換する口を
//!         そもそも作りません。sRGB の扱いは書き出し側（RockExport）と
//!         プレビューの SRV フォーマットで決めます。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Types.hpp>

#include <vector>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @class ImageBuffer
    //! RGBA8 の画像
    //------------------------------------------------------------------------
    class ImageBuffer {
    public:
        ImageBuffer() = default;

        //! 指定の大きさで作ります。
        //! @param [in] width  横幅
        //! @param [in] height 高さ
        ImageBuffer(int width, int height) { Resize(width, height); }

        //! 大きさを変えます。中身は 0 で埋められます。
        //! @param [in] width  横幅
        //! @param [in] height 高さ
        void Resize(int width, int height);

        //! 全体を1色で埋めます。
        //! @param [in] r 赤
        //! @param [in] g 緑
        //! @param [in] b 青
        //! @param [in] a アルファ
        void Fill(u8 r, u8 g, u8 b, u8 a);

        //! 1テクセルを書き込みます。範囲外は無視します。
        //! @param [in] x 横位置
        //! @param [in] y 縦位置
        //! @param [in] r 赤
        //! @param [in] g 緑
        //! @param [in] b 青
        //! @param [in] a アルファ
        void SetPixel(int x, int y, u8 r, u8 g, u8 b, u8 a);

        //! 横幅を返します。
        //! @return 横幅
        int GetWidth() const { return m_width; }

        //! 高さを返します。
        //! @return 高さ
        int GetHeight() const { return m_height; }

        //! 画素列を返します。並びは RGBA が横優先。
        //! @return 画素列
        const std::vector<u8>& GetPixels() const { return m_pixels; }

        //! 画素列を返します。
        //! @return 画素列
        std::vector<u8>& GetPixels() { return m_pixels; }

        //! 中身を持っているかを返します。
        //! @return 持っていれば true
        bool IsValid() const { return m_width > 0 && m_height > 0 && !m_pixels.empty(); }

    private:
        int             m_width  = 0;
        int             m_height = 0;
        std::vector<u8> m_pixels;
    };

}    // namespace RockCore
