//----------------------------------------------------------------------------
//! @file   PngWriter.cpp
//! @brief  PNG 符号化の実装
//----------------------------------------------------------------------------
#include <RockExport/PngWriter.hpp>

#include <array>
#include <cstring>

using RockCore::u32;
using RockCore::u64;
using RockCore::u8;

// 名前空間 RockExport
namespace RockExport {

    namespace {

        //--------------------------------------------------------------------
        //! PNG チャンクの CRC32（IEEE 802.3、反射多項式 0xEDB88320）を求めます。
        //! チャンク type の4バイトとデータをまとめて渡します。
        //!
        //! @param  [in] data 対象のバイト列
        //! @return CRC32
        //--------------------------------------------------------------------
        u32 Crc32(const std::vector<u8>& data) {
            static const std::array<u32, 256> table = [] {
                std::array<u32, 256> t{};
                for(u32 i = 0; i < 256; ++i) {
                    u32 c = i;
                    for(int bit = 0; bit < 8; ++bit) {
                        c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                    }
                    t[i] = c;
                }
                return t;
            }();

            u32 crc = 0xFFFFFFFFu;
            for(const u8 byte : data) {
                crc = table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
            }
            return crc ^ 0xFFFFFFFFu;
        }

        //--------------------------------------------------------------------
        //! zlib ストリームの末尾に付く Adler-32 チェックサムを求めます。
        //! @param  [in] data 対象のバイト列（圧縮前の生データ）
        //! @return Adler-32
        //--------------------------------------------------------------------
        u32 Adler32(const std::vector<u8>& data) {
            constexpr u32 kMod = 65521u;

            u32 a = 1u;
            u32 b = 0u;

            // 桁あふれの前に定期的に mod を取る素直な実装。
            // 1 バイトごとに mod すると遅いが、ここは exe 内蔵ツールの
            // 書き出し1回きりなので速度は問題にならない
            for(const u8 byte : data) {
                a = (a + byte) % kMod;
                b = (b + a) % kMod;
            }
            return (b << 16) | a;
        }

        //! ビッグエンディアンで 32bit 値を追加します（PNG のチャンク長・CRC 用）。
        //! @param  [in,out] out 追加先
        //! @param  [in]     value 値
        void AppendU32Big(std::vector<u8>& out, u32 value) {
            out.push_back(static_cast<u8>((value >> 24) & 0xFFu));
            out.push_back(static_cast<u8>((value >> 16) & 0xFFu));
            out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
            out.push_back(static_cast<u8>(value & 0xFFu));
        }

        //! リトルエンディアンで 16bit 値を追加します（deflate の格納ブロック用）。
        //! @param  [in,out] out 追加先
        //! @param  [in]     value 値
        void AppendU16Little(std::vector<u8>& out, u32 value) {
            out.push_back(static_cast<u8>(value & 0xFFu));
            out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
        }

        //--------------------------------------------------------------------
        //! 1つの PNG チャンクを追加します（長さ + 種別 + データ + CRC）。
        //! @param  [in,out] out  追加先
        //! @param  [in]     type 4文字の種別（"IHDR" 等）
        //! @param  [in]     data チャンクのデータ
        //--------------------------------------------------------------------
        void AppendChunk(std::vector<u8>& out, const char type[4], const std::vector<u8>& data) {
            AppendU32Big(out, static_cast<u32>(data.size()));

            std::vector<u8> typeAndData;
            typeAndData.reserve(4 + data.size());
            typeAndData.insert(typeAndData.end(), type, type + 4);
            typeAndData.insert(typeAndData.end(), data.begin(), data.end());

            out.insert(out.end(), typeAndData.begin(), typeAndData.end());
            AppendU32Big(out, Crc32(typeAndData));
        }

        //--------------------------------------------------------------------
        //! 生データを「格納（無圧縮）」ブロックだけの zlib ストリームへ包みます。
        //!
        //! deflate の格納ブロックは必ずバイト境界から始まり、
        //! バイト境界で終わる（RFC 1951 3.2.4）。ヘッダの残り5ビットは
        //! 未使用で 0 を書けばよいので、ビット単位のパッキングが要らない。
        //!
        //! @param  [in] raw 圧縮前の生データ
        //! @return zlib ストリーム（ヘッダ + 格納ブロック列 + Adler-32）
        //--------------------------------------------------------------------
        std::vector<u8> BuildStoredZlibStream(const std::vector<u8>& raw) {
            std::vector<u8> stream;

            //------------------------------------------------------------------
            // zlib ヘッダ（RFC 1950）。CMF=0x78（deflate, 32K窓）、
            // FLG は (CMF*256+FLG) が 31 の倍数になるよう下位5bitを選ぶ。
            // FDICT=0（辞書なし）、FLEVEL は圧縮率のヒントなので 0 のままでよい
            //------------------------------------------------------------------
            constexpr u8 kCmf = 0x78u;

            u8 flg = 0u;
            while((static_cast<u32>(kCmf) * 256u + flg) % 31u != 0u) {
                ++flg;
            }

            stream.push_back(kCmf);
            stream.push_back(flg);

            //------------------------------------------------------------------
            // 格納ブロックへ分割する。1ブロックの最大は 65535 バイト（LEN が
            // 16bit のため）。切りのよい 65000 で刻む
            //------------------------------------------------------------------
            constexpr size_t kMaxBlockSize = 65000;

            size_t offset = 0;
            do {
                const size_t remaining = raw.size() - offset;
                const size_t blockSize = std::min(remaining, kMaxBlockSize);
                const bool   isFinal   = (offset + blockSize) >= raw.size();

                // BFINAL(bit0) | BTYPE=00(bit1-2) | 残りは未使用で 0
                stream.push_back(isFinal ? 0x01u : 0x00u);

                AppendU16Little(stream, static_cast<u32>(blockSize));
                AppendU16Little(stream, static_cast<u32>(blockSize) ^ 0xFFFFu);    // NLEN = LEN の 1の補数

                stream.insert(stream.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                             raw.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));

                offset += blockSize;
            } while(offset < raw.size());

            // raw が空でも、最終ブロックを1つは書く必要がある（BFINAL=1・長さ0）
            if(raw.empty()) {
                stream.push_back(0x01u);
                AppendU16Little(stream, 0u);
                AppendU16Little(stream, 0xFFFFu);
            }

            AppendU32Big(stream, Adler32(raw));
            return stream;
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! RGBA8 の画像を PNG のバイト列へ符号化します。
    //------------------------------------------------------------------------
    std::vector<u8> EncodePng(const RockCore::ImageBuffer& image) {
        if(!image.IsValid()) {
            return {};
        }

        const int width  = image.GetWidth();
        const int height = image.GetHeight();

        //----------------------------------------------------------------------
        // 走査線データを組む。フィルタは常に None（0）。
        // ベイク結果は自然画像ではないので Sub/Up 等の予測フィルタで
        // 得をする場面が少なく、無圧縮ブロックとの相性でも None が最も単純
        //----------------------------------------------------------------------
        const auto&  pixels   = image.GetPixels();
        const size_t rowBytes = static_cast<size_t>(width) * 4u;

        std::vector<u8> raw;
        raw.reserve(static_cast<size_t>(height) * (1u + rowBytes));

        for(int y = 0; y < height; ++y) {
            raw.push_back(0u);    // フィルタタイプ: None

            const size_t rowOffset = static_cast<size_t>(y) * rowBytes;
            raw.insert(raw.end(), pixels.begin() + static_cast<std::ptrdiff_t>(rowOffset),
                      pixels.begin() + static_cast<std::ptrdiff_t>(rowOffset + rowBytes));
        }

        std::vector<u8> png;

        // PNG シグネチャ（8バイト固定）
        static constexpr u8 kSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
        png.insert(png.end(), std::begin(kSignature), std::end(kSignature));

        //----------------------------------------------------------------------
        // IHDR: width, height, bitdepth=8, colortype=6(RGBA), compression=0,
        //       filter=0（適応フィルタなし。行ごとのフィルタ種別とは別物で、
        //       これは「フィルタ方式そのもの」を選ぶフィールドで 0 しかない）,
        //       interlace=0
        //----------------------------------------------------------------------
        std::vector<u8> ihdr;
        AppendU32Big(ihdr, static_cast<u32>(width));
        AppendU32Big(ihdr, static_cast<u32>(height));
        ihdr.push_back(8u);     // bit depth
        ihdr.push_back(6u);     // color type: RGBA
        ihdr.push_back(0u);     // compression method
        ihdr.push_back(0u);     // filter method
        ihdr.push_back(0u);     // interlace method
        AppendChunk(png, "IHDR", ihdr);

        // IDAT: 走査線データを無圧縮 zlib ストリームへ包んだもの
        AppendChunk(png, "IDAT", BuildStoredZlibStream(raw));

        // IEND: データなし
        AppendChunk(png, "IEND", {});

        return png;
    }

}    // namespace RockExport
