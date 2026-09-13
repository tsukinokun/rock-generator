//----------------------------------------------------------------------------
//! @file   GlbWriter.cpp
//! @brief  GLB 書き出しの実装
//----------------------------------------------------------------------------
#include <RockExport/GlbWriter.hpp>

#include <RockExport/PngWriter.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>

using RockCore::u32;
using RockCore::u8;
using RockCore::Vec2;
using RockCore::Vec3;

// 名前空間 RockExport
namespace RockExport {

    namespace {

        // glTF のコンポーネント型（accessor.componentType）
        constexpr int kComponentFloat        = 5126;
        constexpr int kComponentUnsignedInt  = 5125;

        // bufferView.target
        constexpr int kTargetArrayBuffer        = 34962;
        constexpr int kTargetElementArrayBuffer = 34963;

        //--------------------------------------------------------------------
        //! JSON 文字列を組み立てるための最小限のヘルパー。
        //!
        //! glTF の JSON は構造が固定で分かっているので、汎用 JSON ライブラリを
        //! 引き込むより、フォーマットを直接組んだ方が単純で見通しがよい
        //! （Tools/make_test_glb.py の json.dumps 相当をここでは手で書く）。
        //--------------------------------------------------------------------
        class JsonBuilder {
        public:
            //! 文字列値をエスケープして書きます（バックスラッシュと引用符のみ。
            //! ここで使う文字列はファイル名程度で制御文字を含まないため十分）。
            //! @param  [in] value 元の文字列
            //! @return エスケープ後の文字列
            static std::string Escape(const std::string& value) {
                std::string escaped;
                escaped.reserve(value.size());
                for(const char c : value) {
                    if(c == '"' || c == '\\') {
                        escaped.push_back('\\');
                    }
                    escaped.push_back(c);
                }
                return escaped;
            }

            //! float を JSON の数値として書きます。
            //! @param  [in] value 値
            //! @return 数値の文字列表現
            static std::string Number(float value) {
                // float32 の丸め込みなので %.9g で往復精度は足りる
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
                return buffer;
            }
        };

        //--------------------------------------------------------------------
        //! バッファへバイト列を追加し、対応する bufferView の JSON 片を返します。
        //!
        //! 4バイト境界へ揃えてから追加します。float / u32 のアクセサは
        //! 4バイト単位なので、これで全アクセサがコンポーネントサイズに
        //! 整列します（glTF 仕様の推奨どおり）。
        //--------------------------------------------------------------------
        class BufferBuilder {
        public:
            //! バイト列を追加します。
            //! @param  [in] bytes  追加するデータ
            //! @param  [in] target bufferView.target。0 なら省略（画像用）
            //! @return この bufferView の番号
            int AddView(const std::vector<u8>& bytes, int target) {
                Pad4();

                const size_t offset = m_blob.size();
                m_blob.insert(m_blob.end(), bytes.begin(), bytes.end());

                Entry entry{};
                entry.byteOffset = offset;
                entry.byteLength = bytes.size();
                entry.target     = target;
                m_views.push_back(entry);

                return static_cast<int>(m_views.size()) - 1;
            }

            //! 蓄積したバイナリ本体を返します（4バイト境界まで 0 で埋めてから）。
            //! @return バイナリ本体
            std::vector<u8> TakeBlob() {
                Pad4();
                return std::move(m_blob);
            }

            //! bufferViews の JSON 配列を返します。
            //! @return JSON 断片（角括弧を含む）
            std::string BuildViewsJson() const {
                std::string json = "[";
                for(size_t i = 0; i < m_views.size(); ++i) {
                    const Entry& e = m_views[i];
                    if(i > 0) {
                        json += ",";
                    }
                    json += "{\"buffer\":0,\"byteOffset\":" + std::to_string(e.byteOffset) +
                           ",\"byteLength\":" + std::to_string(e.byteLength);
                    if(e.target != 0) {
                        json += ",\"target\":" + std::to_string(e.target);
                    }
                    json += "}";
                }
                json += "]";
                return json;
            }

        private:
            struct Entry {
                size_t byteOffset = 0;
                size_t byteLength = 0;
                int    target     = 0;
            };

            void Pad4() {
                while(m_blob.size() % 4 != 0) {
                    m_blob.push_back(0u);
                }
            }

            std::vector<u8> m_blob;
            std::vector<Entry> m_views;
        };

        //! Vec3 の配列をリトルエンディアンの float バイト列へ変換します。
        //! @param  [in] values 元データ
        //! @return バイト列
        std::vector<u8> PackVec3(const std::vector<Vec3>& values) {
            std::vector<u8> bytes(values.size() * 12);
            std::memcpy(bytes.data(), values.data(), bytes.size());
            return bytes;
        }

        //! Vec2 の配列をリトルエンディアンの float バイト列へ変換します。
        //! @param  [in] values 元データ
        //! @return バイト列
        std::vector<u8> PackVec2(const std::vector<Vec2>& values) {
            std::vector<u8> bytes(values.size() * 8);
            std::memcpy(bytes.data(), values.data(), bytes.size());
            return bytes;
        }

        //! u32 の配列をリトルエンディアンのバイト列へ変換します。
        //! @param  [in] values 元データ
        //! @return バイト列
        std::vector<u8> PackU32(const std::vector<u32>& values) {
            std::vector<u8> bytes(values.size() * 4);
            std::memcpy(bytes.data(), values.data(), bytes.size());
            return bytes;
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! GLB を書き出します。
    //------------------------------------------------------------------------
    bool WriteGlb(const RockCore::MeshBuilder& mesh,
                 const RockCore::ImageBuffer& albedo,
                 const RockCore::ImageBuffer& normal,
                 const RockCore::ImageBuffer& metallicRoughness,
                 const RockCore::ImageBuffer& ao,
                 const std::string&           outputPath,
                 std::string*                 outError) {
        const auto fail = [outError](const std::string& reason) {
            if(outError) {
                *outError = reason;
            }
            return false;
        };

        if(mesh.positions.empty() || mesh.indices.empty()) {
            return fail("empty mesh");
        }
        if(mesh.normals.size() != mesh.positions.size() || mesh.uvs.size() != mesh.positions.size()) {
            return fail("mesh is missing normals or uvs (export after Unwrap, not before)");
        }
        if(!albedo.IsValid() || !normal.IsValid() || !metallicRoughness.IsValid() || !ao.IsValid()) {
            return fail("one of the four baked maps is empty");
        }

        BufferBuilder buffer;

        //----------------------------------------------------------------------
        // 頂点属性。POSITION は min/max が glTF の必須フィールド
        //----------------------------------------------------------------------
        const int posView = buffer.AddView(PackVec3(mesh.positions), kTargetArrayBuffer);
        const int nrmView = buffer.AddView(PackVec3(mesh.normals), kTargetArrayBuffer);
        const int uvView  = buffer.AddView(PackVec2(mesh.uvs), kTargetArrayBuffer);
        const int idxView = buffer.AddView(PackU32(mesh.indices), kTargetElementArrayBuffer);

        Vec3 posMin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max()};
        Vec3 posMax{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                   -std::numeric_limits<float>::max()};
        for(const Vec3& p : mesh.positions) {
            posMin.x = std::min(posMin.x, p.x);
            posMin.y = std::min(posMin.y, p.y);
            posMin.z = std::min(posMin.z, p.z);
            posMax.x = std::max(posMax.x, p.x);
            posMax.y = std::max(posMax.y, p.y);
            posMax.z = std::max(posMax.z, p.z);
        }

        //----------------------------------------------------------------------
        // 4枚のテクスチャを PNG へ符号化して埋め込む
        //----------------------------------------------------------------------
        const std::vector<u8> albedoPng = EncodePng(albedo);
        const std::vector<u8> normalPng = EncodePng(normal);
        const std::vector<u8> mrPng     = EncodePng(metallicRoughness);
        const std::vector<u8> aoPng     = EncodePng(ao);

        const int albedoView = buffer.AddView(albedoPng, 0);
        const int normalView = buffer.AddView(normalPng, 0);
        const int mrView     = buffer.AddView(mrPng, 0);
        const int aoView     = buffer.AddView(aoPng, 0);

        const std::vector<u8> blob = buffer.TakeBlob();

        //----------------------------------------------------------------------
        // JSON を組み立てる。
        //
        // 手で組んでいるのは Assimp を経由すると metallicRoughnessTexture /
        // occlusionTexture が書けないため（GlbWriter.hpp の説明を参照）。
        // ここで書く4つのテクスチャ参照キーが glTF 仕様どおりの名前であることが
        // 唯一かつ最大の目的
        //----------------------------------------------------------------------
        std::string json;
        json += "{";
        json += "\"asset\":{\"version\":\"2.0\",\"generator\":\"RockForge GlbWriter\"},";
        json += "\"scene\":0,";
        json += "\"scenes\":[{\"nodes\":[0]}],";
        json += "\"nodes\":[{\"mesh\":0,\"name\":\"Rock\"}],";
        json += "\"meshes\":[{\"name\":\"RockMesh\",\"primitives\":[{"
               "\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},"
               "\"indices\":3,\"material\":0}]}],";

        //------------------------------------------------------------------
        // マテリアル。baseColorFactor / metallicFactor / roughnessFactor は
        // すべて 1（テクスチャの値をそのまま使う）。乗算のもう片方の定数を
        // 動かすのはエンジン側の cbuffer なので、ここでは中立値で固定する
        //------------------------------------------------------------------
        json += "\"materials\":[{\"name\":\"RockMaterial\",\"pbrMetallicRoughness\":{"
               "\"baseColorTexture\":{\"index\":0},"
               "\"metallicRoughnessTexture\":{\"index\":2},"
               "\"metallicFactor\":1.0,\"roughnessFactor\":1.0},"
               "\"normalTexture\":{\"index\":1},"
               "\"occlusionTexture\":{\"index\":3}}],";

        json += "\"textures\":[{\"source\":0},{\"source\":1},{\"source\":2},{\"source\":3}],";
        json += "\"images\":[";
        json += "{\"bufferView\":" + std::to_string(albedoView) + ",\"mimeType\":\"image/png\",\"name\":\"albedo\"},";
        json += "{\"bufferView\":" + std::to_string(normalView) + ",\"mimeType\":\"image/png\",\"name\":\"normal\"},";
        json += "{\"bufferView\":" + std::to_string(mrView) +
               ",\"mimeType\":\"image/png\",\"name\":\"metallicRoughness\"},";
        json += "{\"bufferView\":" + std::to_string(aoView) + ",\"mimeType\":\"image/png\",\"name\":\"ao\"}";
        json += "],";

        json += "\"accessors\":[";
        json += "{\"bufferView\":" + std::to_string(posView) + ",\"componentType\":" +
               std::to_string(kComponentFloat) + ",\"count\":" + std::to_string(mesh.positions.size()) +
               ",\"type\":\"VEC3\",\"min\":[" + JsonBuilder::Number(posMin.x) + "," + JsonBuilder::Number(posMin.y) +
               "," + JsonBuilder::Number(posMin.z) + "],\"max\":[" + JsonBuilder::Number(posMax.x) + "," +
               JsonBuilder::Number(posMax.y) + "," + JsonBuilder::Number(posMax.z) + "]},";
        json += "{\"bufferView\":" + std::to_string(nrmView) + ",\"componentType\":" +
               std::to_string(kComponentFloat) + ",\"count\":" + std::to_string(mesh.normals.size()) +
               ",\"type\":\"VEC3\"},";
        json += "{\"bufferView\":" + std::to_string(uvView) + ",\"componentType\":" +
               std::to_string(kComponentFloat) + ",\"count\":" + std::to_string(mesh.uvs.size()) +
               ",\"type\":\"VEC2\"},";
        json += "{\"bufferView\":" + std::to_string(idxView) + ",\"componentType\":" +
               std::to_string(kComponentUnsignedInt) + ",\"count\":" + std::to_string(mesh.indices.size()) +
               ",\"type\":\"SCALAR\"}";
        json += "],";

        json += "\"bufferViews\":" + buffer.BuildViewsJson() + ",";
        json += "\"samplers\":[{\"wrapS\":10497,\"wrapT\":10497}],";
        json += "\"buffers\":[{\"byteLength\":" + std::to_string(blob.size()) + "}]";
        json += "}";

        //----------------------------------------------------------------------
        // GLB コンテナへ詰める。JSON チャンクは空白(0x20)、BIN チャンクは
        // 0 で 4バイト境界まで埋める（glTF 2.0 仕様の GLB コンテナ規則）
        //----------------------------------------------------------------------
        std::vector<u8> jsonBytes(json.begin(), json.end());
        while(jsonBytes.size() % 4 != 0) {
            jsonBytes.push_back(0x20u);
        }

        const u32 totalLength =
            static_cast<u32>(12 + 8 + jsonBytes.size() + 8 + blob.size());

        std::vector<u8> out;
        out.reserve(totalLength);

        const auto appendU32Little = [&out](u32 value) {
            out.push_back(static_cast<u8>(value & 0xFFu));
            out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
            out.push_back(static_cast<u8>((value >> 16) & 0xFFu));
            out.push_back(static_cast<u8>((value >> 24) & 0xFFu));
        };
        const auto appendTag = [&out](const char tag[4]) { out.insert(out.end(), tag, tag + 4); };

        appendTag("glTF");
        appendU32Little(2u);    // glTF バージョン
        appendU32Little(totalLength);

        appendU32Little(static_cast<u32>(jsonBytes.size()));
        appendTag("JSON");
        out.insert(out.end(), jsonBytes.begin(), jsonBytes.end());

        appendU32Little(static_cast<u32>(blob.size()));
        appendTag("BIN\0");
        out.insert(out.end(), blob.begin(), blob.end());

        std::ofstream file(outputPath, std::ios::binary);
        if(!file) {
            return fail("cannot open output file: " + outputPath);
        }
        file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        if(!file) {
            return fail("write failed: " + outputPath);
        }

        return true;
    }

}    // namespace RockExport
