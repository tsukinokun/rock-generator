//-------------------------------------------------------------
//! @file   Phase0RoundTrip.cpp
//! @brief  Assimp のエクスポート→再インポートで、PBRの5スロットが
//!         どう写るかを実測するための使い捨てハーネス。
//-------------------------------------------------------------
// TsukinoEngine の ModelImporter は、Assimp のマテリアルから
//   DIFFUSE / NORMALS / DIFFUSE_ROUGHNESS / EMISSIVE / AMBIENT_OCCLUSION
// の5つを index 0 で読む（ModelImporter.cpp:529-533, 485）。
//
// 一方 Assimp 5.0.1 の pbrmaterial.h には
//   BASE_COLOR_TEXTURE           = aiTextureType_DIFFUSE, 1   ← index が 1
//   METALLICROUGHNESS_TEXTURE    = aiTextureType_UNKNOWN, 0   ← 型が違う
// と書かれており、glTF 経由では複数のスロットが落ちるはずである。
//
// 「はず」で設計を決めたくないので、実際に書いて読み直し、
// どの aiTextureType の何番に入ったかを総当たりでダンプする。
// あわせて aiProcess_FlipUVs による UV の V 反転も観測する。
//
// ビルド: tools/build_phase0.bat

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

    //-------------------------------------------------------------
    //! エンジンが ModelImporter.cpp:206-207 で使っているのと同じ後処理フラグ。
    //! ここを揃えないと実測の意味がない
    //-------------------------------------------------------------
    constexpr unsigned int kEngineImportFlags =
        aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_LimitBoneWeights | aiProcess_OptimizeMeshes;

    //-------------------------------------------------------------
    //! エンジンが実際に読む5スロット（ModelImporter.cpp:529-533）
    //-------------------------------------------------------------
    struct EngineSlot {
        const char*  label;
        aiTextureType type;
    };

    constexpr EngineSlot kEngineSlots[] = {
        {"albedoMap           ", aiTextureType_DIFFUSE},
        {"normalMap           ", aiTextureType_NORMALS},
        {"metallicRoughnessMap", aiTextureType_DIFFUSE_ROUGHNESS},
        {"emissiveMap         ", aiTextureType_EMISSIVE},
        {"aoMap               ", aiTextureType_AMBIENT_OCCLUSION},
    };

    //! aiTextureType の名前（0〜18 = aiTextureType_UNKNOWN まで）
    const char* TextureTypeName(unsigned int t) {
        static const char* kNames[] = {"NONE",       "DIFFUSE",      "SPECULAR",        "AMBIENT",
                                       "EMISSIVE",   "HEIGHT",       "NORMALS",         "SHININESS",
                                       "OPACITY",    "DISPLACEMENT", "LIGHTMAP",        "REFLECTION",
                                       "BASE_COLOR", "NORMAL_CAMERA","EMISSION_COLOR",  "METALNESS",
                                       "DIFFUSE_ROUGHNESS", "AMBIENT_OCCLUSION", "UNKNOWN"};
        constexpr unsigned int kCount = sizeof(kNames) / sizeof(kNames[0]);
        return t < kCount ? kNames[t] : "?";
    }

    //-------------------------------------------------------------
    //! ファイルを丸ごとメモリへ読む
    //-------------------------------------------------------------
    std::vector<char> ReadFileBytes(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if(!file) {
            std::cerr << "  [!] cannot open " << path << "\n";
            return {};
        }
        const std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<char> bytes(static_cast<size_t>(size));
        file.read(bytes.data(), size);
        return bytes;
    }

    //-------------------------------------------------------------
    //! 検証用の最小シーンを組み立てる
    //! @param textureDir  テスト用PNGの置き場所
    //! @param embed       true なら "*N" 形式の埋め込みテクスチャにする
    //! @note  解放しない（使い捨てハーネスなのでプロセス終了に任せる）。
    //!        aiScene のデストラクタは配列を delete[] するため、
    //!        中途半端に所有権を渡すと二重解放になりやすい
    //-------------------------------------------------------------
    aiScene* BuildScene(const std::string& textureDir, bool embed) {
        // スロットの並びは kEngineSlots と対応させる
        const char* kFiles[] = {"albedo.png", "normal.png", "mr.png", "emissive.png", "ao.png"};
        constexpr unsigned int kSlotCount = 5;

        aiScene* scene = new aiScene();

        //--------------------------------------------------------------
        // メッシュ: UV の向きを見たいので、四隅で UV が違う板を1枚置く
        //--------------------------------------------------------------
        aiMesh* mesh         = new aiMesh();
        mesh->mNumVertices   = 4;
        mesh->mVertices      = new aiVector3D[4]{{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}};
        mesh->mNormals       = new aiVector3D[4]{{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}};
        mesh->mNumUVComponents[0] = 2;
        mesh->mTextureCoords[0]   = new aiVector3D[4]{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        mesh->mPrimitiveTypes     = aiPrimitiveType_TRIANGLE;
        mesh->mNumFaces           = 2;
        mesh->mFaces              = new aiFace[2];
        const unsigned int kIndices[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for(unsigned int f = 0; f < 2; ++f) {
            mesh->mFaces[f].mNumIndices = 3;
            mesh->mFaces[f].mIndices    = new unsigned int[3]{kIndices[f][0], kIndices[f][1], kIndices[f][2]};
        }
        mesh->mMaterialIndex = 0;

        scene->mNumMeshes = 1;
        scene->mMeshes    = new aiMesh*[1]{mesh};

        //--------------------------------------------------------------
        // マテリアル: エンジンが読む5つの型へ、そのまま素直に入れてみる
        //--------------------------------------------------------------
        aiMaterial* material = new aiMaterial();
        aiString    matName("Phase0Material");
        material->AddProperty(&matName, AI_MATKEY_NAME);

        for(unsigned int i = 0; i < kSlotCount; ++i) {
            aiString texPath;
            if(embed) {
                texPath.Set("*" + std::to_string(i));
            } else {
                texPath.Set(kFiles[i]);
            }
            material->AddProperty(&texPath, AI_MATKEY_TEXTURE(kEngineSlots[i].type, 0));
        }

        scene->mNumMaterials = 1;
        scene->mMaterials    = new aiMaterial*[1]{material};

        //--------------------------------------------------------------
        // 埋め込みテクスチャ（PNGのバイト列をそのまま持たせる圧縮形式）
        //--------------------------------------------------------------
        if(embed) {
            scene->mNumTextures = kSlotCount;
            scene->mTextures    = new aiTexture*[kSlotCount];
            for(unsigned int i = 0; i < kSlotCount; ++i) {
                const std::vector<char> bytes = ReadFileBytes(textureDir + "/" + kFiles[i]);

                aiTexture* tex = new aiTexture();
                tex->mWidth    = static_cast<unsigned int>(bytes.size());
                tex->mHeight   = 0;    // 0 = 圧縮済み（achFormatHint の形式でそのまま入っている）
                std::strcpy(tex->achFormatHint, "png");
                tex->pcData = reinterpret_cast<aiTexel*>(new char[bytes.size()]);
                std::memcpy(tex->pcData, bytes.data(), bytes.size());
                tex->mFilename.Set(kFiles[i]);

                scene->mTextures[i] = tex;
            }
        }

        //--------------------------------------------------------------
        // ルートノード
        //--------------------------------------------------------------
        scene->mRootNode             = new aiNode();
        scene->mRootNode->mName.Set("root");
        scene->mRootNode->mNumMeshes = 1;
        scene->mRootNode->mMeshes    = new unsigned int[1]{0};

        return scene;
    }

    //-------------------------------------------------------------
    //! 再インポートした結果をダンプする
    //-------------------------------------------------------------
    void DumpImported(const std::string& path) {
        Assimp::Importer importer;
        const aiScene*   scene = importer.ReadFile(path, kEngineImportFlags);

        if(!scene) {
            std::cout << "  IMPORT FAILED: " << importer.GetErrorString() << "\n";
            return;
        }

        std::cout << "  meshes=" << scene->mNumMeshes << "  materials=" << scene->mNumMaterials
                  << "  embeddedTextures=" << scene->mNumTextures << "\n";

        //--------------------------------------------------------------
        // UV: 書き出す前は (0,0)(1,0)(1,1)(0,1)。FlipUVs が掛かると V が反転する
        //--------------------------------------------------------------
        if(scene->mNumMeshes > 0 && scene->mMeshes[0]->mTextureCoords[0]) {
            const aiMesh* m = scene->mMeshes[0];
            std::cout << "  uv[0..3] =";
            for(unsigned int v = 0; v < m->mNumVertices && v < 4; ++v) {
                std::cout << " (" << m->mTextureCoords[0][v].x << "," << m->mTextureCoords[0][v].y << ")";
            }
            std::cout << "\n";
        }

        for(unsigned int mi = 0; mi < scene->mNumMaterials; ++mi) {
            const aiMaterial* mat = scene->mMaterials[mi];

            //--------------------------------------------------------------
            // 総当たり: どの型の何番に何が入ったか
            //--------------------------------------------------------------
            std::cout << "  --- material " << mi << ": all texture types ---\n";
            for(unsigned int t = 0; t <= aiTextureType_UNKNOWN; ++t) {
                const unsigned int count = mat->GetTextureCount(static_cast<aiTextureType>(t));
                for(unsigned int idx = 0; idx < count; ++idx) {
                    aiString p;
                    mat->GetTexture(static_cast<aiTextureType>(t), idx, &p);
                    std::cout << "      " << TextureTypeName(t) << "[" << idx << "] = " << p.C_Str() << "\n";
                }
            }

            //--------------------------------------------------------------
            // エンジンが実際に読む位置に、何が届くか
            //--------------------------------------------------------------
            std::cout << "  --- what TsukinoEngine would read (index 0) ---\n";
            for(const EngineSlot& slot : kEngineSlots) {
                aiString    p;
                const bool  ok = mat->GetTexture(slot.type, 0, &p) == AI_SUCCESS;
                std::cout << "      " << slot.label << " : " << (ok ? p.C_Str() : "(EMPTY -- LOST)") << "\n";
            }
        }
    }

    //-------------------------------------------------------------
    //! 1形式ぶんの往復を実行する
    //-------------------------------------------------------------
    void RoundTrip(const std::string& formatId, const std::string& outPath, const std::string& textureDir, bool embed) {
        std::cout << "\n==================================================================\n";
        std::cout << " format=" << formatId << "  embed=" << (embed ? "yes" : "no") << "  -> " << outPath << "\n";
        std::cout << "==================================================================\n";

        aiScene* scene = BuildScene(textureDir, embed);

        Assimp::Exporter exporter;
        if(exporter.Export(scene, formatId, outPath) != AI_SUCCESS) {
            std::cout << "  EXPORT FAILED: " << exporter.GetErrorString() << "\n";
            return;
        }

        std::ifstream written(outPath, std::ios::binary | std::ios::ate);
        std::cout << "  exported ok, " << (written ? static_cast<long long>(written.tellg()) : -1) << " bytes\n";

        DumpImported(outPath);
    }

}    // namespace

int main(int argc, char** argv) {
    //--------------------------------------------------------------
    // --dump <file> : 任意のファイルをエンジンと同じフラグで読み、
    //                 スロットの入り方だけを見るモード。
    //                 自前で組んだ glTF が Assimp にどう解釈されるかの確認に使う
    //--------------------------------------------------------------
    if(argc > 2 && std::string(argv[1]) == "--dump") {
        std::cout << "dump: " << argv[2] << "\n";
        DumpImported(argv[2]);
        return 0;
    }

    const std::string textureDir = (argc > 1) ? argv[1] : "testdata";
    const std::string outDir     = (argc > 2) ? argv[2] : "testdata/out";

    std::cout << "Assimp export/import round trip\n";
    std::cout << "  textureDir = " << textureDir << "\n";
    std::cout << "  outDir     = " << outDir << "\n";

    //--------------------------------------------------------------
    // 利用可能なエクスポータの一覧（DLLに何が入っているかの確認も兼ねる）
    //--------------------------------------------------------------
    Assimp::Exporter   probe;
    const size_t       formatCount = probe.GetExportFormatCount();
    std::cout << "\navailable exporters (" << formatCount << "):\n";
    for(size_t i = 0; i < formatCount; ++i) {
        const aiExportFormatDesc* desc = probe.GetExportFormatDescription(i);
        std::cout << "  " << desc->id << "  (" << desc->description << ")\n";
    }

    RoundTrip("glb2", outDir + "/rock_embed.glb", textureDir, true);
    RoundTrip("glb2", outDir + "/rock_extern.glb", textureDir, false);
    RoundTrip("fbx", outDir + "/rock_extern.fbx", textureDir, false);
    RoundTrip("fbx", outDir + "/rock_embed.fbx", textureDir, true);

    return 0;
}
