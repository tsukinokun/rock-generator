//----------------------------------------------------------------------------
//! @file   FbxExporter.cpp
//! @brief  FBX 書き出しの実装
//----------------------------------------------------------------------------
#include <RockExport/FbxExporter.hpp>

#include <RockExport/PngWriter.hpp>

#include <assimp/Exporter.hpp>
#include <assimp/scene.h>

#include <cstring>
#include <memory>

using RockCore::u8;
using RockCore::u32;
using RockCore::Vec3;

// 名前空間 RockExport
namespace RockExport {

    namespace {

        //--------------------------------------------------------------------
        //! PNG バイト列を aiScene の埋め込みテクスチャとして追加します。
        //!
        //! mHeight=0 は「圧縮済み画像」を意味し、achFormatHint がその形式名
        //! （"png"）になる（texture.h の仕様）。Phase0RoundTrip.cpp の
        //! BuildScene と同じ手順で、実際に埋め込みが機能することを
        //! Phase 0 で確認済み。
        //!
        //! @param  [in]     png  PNG のバイト列
        //! @param  [in]     name デバッグ用のファイル名
        //! @return 新しく確保した aiTexture（呼び出し側が scene->mTextures へ入れる）
        //--------------------------------------------------------------------
        aiTexture* MakeEmbeddedTexture(const std::vector<u8>& png, const char* name) {
            aiTexture* texture = new aiTexture();
            texture->mWidth    = static_cast<unsigned int>(png.size());
            texture->mHeight   = 0u;    // 0 = 圧縮済み。生ピクセルではなくファイルのバイト列そのまま
            std::strcpy(texture->achFormatHint, "png");

            texture->pcData = reinterpret_cast<aiTexel*>(new char[png.size()]);
            std::memcpy(texture->pcData, png.data(), png.size());

            texture->mFilename.Set(name);
            return texture;
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! FBX を書き出します。
    //------------------------------------------------------------------------
    bool WriteFbx(const RockCore::MeshBuilder& mesh,
                 const RockCore::ImageBuffer& albedo,
                 const RockCore::ImageBuffer& normal,
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
        if(mesh.indices.size() % 3 != 0) {
            return fail("index count is not a multiple of 3 (mesh is not pure triangles)");
        }
        if(!albedo.IsValid() || !normal.IsValid()) {
            return fail("albedo or normal map is empty");
        }

        const u32 vertexCount   = mesh.GetVertexCount();
        const u32 triangleCount = mesh.GetTriangleCount();

        //----------------------------------------------------------------------
        // aiScene を手で組み立てる。
        //
        // std::unique_ptr で持つ。Assimp::Exporter::Export は所有権を取らないので
        // （呼び出し側が持ち続ける）、例外や早期リターンでも確実に delete するために
        // ここでスコープを抜けたら自動で片付くようにする。aiScene のデストラクタが
        // mMeshes / mMaterials / mTextures / mRootNode を再帰的に delete する
        // （Assimp 標準の所有権規約。Phase0RoundTrip.cpp と同じ組み方）
        //----------------------------------------------------------------------
        std::unique_ptr<aiScene> scene(new aiScene());

        //----------------------------------------------------------------------
        // メッシュ
        //----------------------------------------------------------------------
        aiMesh* aimesh       = new aiMesh();
        aimesh->mNumVertices = vertexCount;
        aimesh->mVertices    = new aiVector3D[vertexCount];
        aimesh->mNormals     = new aiVector3D[vertexCount];

        aimesh->mNumUVComponents[0] = 2;
        aimesh->mTextureCoords[0]   = new aiVector3D[vertexCount];

        for(u32 v = 0; v < vertexCount; ++v) {
            const Vec3& p = mesh.positions[v];
            const Vec3& n = mesh.normals[v];

            aimesh->mVertices[v] = aiVector3D(p.x, p.y, p.z);
            aimesh->mNormals[v]  = aiVector3D(n.x, n.y, n.z);

            // V を反転する。ExportProfile::flipV(Fbx)=true。
            // FBX 経路は aiProcess_FlipUVs の反転が往復で相殺されないことを
            // Phase 0 で確認済み（GLB は相殺されるので反転しない）
            const RockCore::Vec2& uv = mesh.uvs[v];
            aimesh->mTextureCoords[0][v] = aiVector3D(uv.x, 1.0f - uv.y, 0.0f);
        }

        aimesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
        aimesh->mNumFaces       = triangleCount;
        aimesh->mFaces          = new aiFace[triangleCount];

        for(u32 t = 0; t < triangleCount; ++t) {
            aiFace& face      = aimesh->mFaces[t];
            face.mNumIndices  = 3;
            face.mIndices     = new unsigned int[3];
            face.mIndices[0]  = mesh.indices[static_cast<size_t>(t) * 3 + 0];
            face.mIndices[1]  = mesh.indices[static_cast<size_t>(t) * 3 + 1];
            face.mIndices[2]  = mesh.indices[static_cast<size_t>(t) * 3 + 2];
        }

        aimesh->mMaterialIndex = 0;

        scene->mNumMeshes = 1;
        scene->mMeshes    = new aiMesh*[1]{aimesh};

        //----------------------------------------------------------------------
        // マテリアル。埋め込みテクスチャは "*0" / "*1" で参照する
        // （Phase0RoundTrip.cpp と同じ規約）
        //----------------------------------------------------------------------
        aiMaterial* material = new aiMaterial();

        aiString matName("RockMaterial");
        material->AddProperty(&matName, AI_MATKEY_NAME);

        // レガシー Phong モデルの拡散反射色を白にしておく。既定値のままだと
        // グレーが乗ってテクスチャの色が正しく見えない DCC がある
        aiColor3D white(1.0f, 1.0f, 1.0f);
        material->AddProperty(&white, 1, AI_MATKEY_COLOR_DIFFUSE);

        aiString diffusePath("*0");
        material->AddProperty(&diffusePath, AI_MATKEY_TEXTURE_DIFFUSE(0));

        aiString normalPath("*1");
        material->AddProperty(&normalPath, AI_MATKEY_TEXTURE_NORMALS(0));

        scene->mNumMaterials = 1;
        scene->mMaterials    = new aiMaterial*[1]{material};

        //----------------------------------------------------------------------
        // 埋め込みテクスチャ本体
        //----------------------------------------------------------------------
        scene->mNumTextures = 2;
        scene->mTextures    = new aiTexture*[2];
        scene->mTextures[0] = MakeEmbeddedTexture(EncodePng(albedo), "albedo.png");
        scene->mTextures[1] = MakeEmbeddedTexture(EncodePng(normal), "normal.png");

        //----------------------------------------------------------------------
        // ルートノード
        //----------------------------------------------------------------------
        scene->mRootNode             = new aiNode();
        scene->mRootNode->mName.Set("Rock");
        scene->mRootNode->mNumMeshes = 1;
        scene->mRootNode->mMeshes    = new unsigned int[1]{0};

        //----------------------------------------------------------------------
        // 書き出す。"fbx" はバイナリ FBX（"fbxa" はアスキー）
        //----------------------------------------------------------------------
        Assimp::Exporter exporter;
        if(exporter.Export(scene.get(), "fbx", outputPath) != AI_SUCCESS) {
            return fail(std::string("Assimp export failed: ") + exporter.GetErrorString());
        }

        return true;
    }

}    // namespace RockExport
