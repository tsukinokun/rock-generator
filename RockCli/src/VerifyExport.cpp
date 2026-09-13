//----------------------------------------------------------------------------
//! @file   VerifyExport.cpp
//! @brief  RockExport の書き出し検証（--verify-export）
//! @detail RockCore の純粋性テスト（VerifyCore.cpp）とはあえて分けてあります。
//!         こちらは Assimp を実際に使う検証で、GPU もウィンドウも要りませんが
//!         RockCore の話ではないためです。
//!
//!         「書けたはず」では終わらせません。書き出したファイルを
//!         Assimp（エンジンが実際に使うのと同じインポート後処理フラグ）で
//!         読み直し、期待したテクスチャスロットに実際に届くかを検証します。
//!         GlbWriter を自前で書いた理由そのものが、この検証結果です。
//----------------------------------------------------------------------------
#include <RockCore/Pipeline/Pipeline.hpp>

#include <RockExport/RockExporter.hpp>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

    int g_failures = 0;

    void Check(bool condition, const char* label) {
        std::printf("%s %s\n", condition ? "[ ok ]" : "[FAIL]", label);
        if(!condition) {
            ++g_failures;
        }
    }

    //--------------------------------------------------------------------
    // エンジンが実際に使っているのと同じ後処理フラグ
    // （TsukinoEngine/Tsukino.Engine/src/Asset/Model/ModelImporter.cpp:206-207）。
    // ここを揃えないと「読めた」の意味が無い
    //--------------------------------------------------------------------
    constexpr unsigned int kEngineImportFlags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                                aiProcess_FlipUVs | aiProcess_LimitBoneWeights |
                                                aiProcess_OptimizeMeshes;

    //! 指定の型のテクスチャが index 0 に届いているかを返します。
    //! @param  [in] material 対象のマテリアル
    //! @param  [in] type     調べる aiTextureType
    //! @return 届いていれば true
    bool HasTexture(const aiMaterial* material, aiTextureType type) {
        aiString path;
        return material->GetTexture(type, 0, &path) == AI_SUCCESS;
    }

    //--------------------------------------------------------------------
    //! GLB の書き出し→再インポートを検証します。
    //!
    //! ここが GlbWriter.hpp の主張の裏取りです。DIFFUSE_ROUGHNESS と
    //! AMBIENT_OCCLUSION は、Assimp 自身の glTF2 エクスポータを使うと
    //! 落ちる（Phase 0 / Phase 6 で実測済み）。自前で JSON を書いた
    //! GlbWriter ならこの2つも届くはず、というのがこの検証の主眼
    //--------------------------------------------------------------------
    void TestGlbRoundTrip(const RockCore::Pipeline& pipeline, const std::string& path) {
        std::string error;
        Check(RockExport::ExportRock(pipeline, path, &error), "glb export completes");

        Assimp::Importer importer;
        const aiScene*   scene = importer.ReadFile(path, kEngineImportFlags);

        Check(scene != nullptr, "glb re-imports through Assimp with the engine's post-process flags");
        if(!scene) {
            std::printf("       import error: %s\n", importer.GetErrorString());
            return;
        }

        const bool hasMesh = scene->mNumMeshes > 0 && scene->mMeshes[0]->mNumVertices > 0;
        Check(hasMesh, "glb mesh has vertices after reimport");
        if(hasMesh) {
            std::printf("       reimported vertices=%u triangles=%u\n", scene->mMeshes[0]->mNumVertices,
                        scene->mMeshes[0]->mNumFaces);
        }

        Check(scene->mNumMaterials > 0, "glb has a material");
        if(scene->mNumMaterials == 0) {
            return;
        }

        const aiMaterial* material = scene->mMaterials[0];

        Check(HasTexture(material, aiTextureType_DIFFUSE), "glb: albedo lands on DIFFUSE");
        Check(HasTexture(material, aiTextureType_NORMALS), "glb: normal lands on NORMALS");

        //----------------------------------------------------------------
        // metallicRoughnessTexture / occlusionTexture は、Assimp の glTF2
        // インポータでは aiTextureType_DIFFUSE_ROUGHNESS /
        // aiTextureType_AMBIENT_OCCLUSION へは分類されない（glTF の
        // pbrmaterial.h 側にそれ専用の aiTextureType が無いため）。
        // 実際に届く先は UNKNOWN[0] と LIGHTMAP[0]（このテストで実測して
        // 確認した事実）。ModelImporter.cpp 側はこの2つへのフォールバックを
        // 持つよう Phase 6 で直したので、ここでは「フォールバック先に
        // 届いているか」を検証する。DIFFUSE_ROUGHNESS / AMBIENT_OCCLUSION
        // 側に直接届いていたら、それは Assimp の挙動が変わった合図なので
        // 気付けるよう別に記録しておく
        //----------------------------------------------------------------
        const bool mrDirect   = HasTexture(material, aiTextureType_DIFFUSE_ROUGHNESS);
        const bool mrFallback = HasTexture(material, aiTextureType_UNKNOWN);
        Check(mrDirect || mrFallback, "glb: metallic-roughness lands on DIFFUSE_ROUGHNESS or its UNKNOWN fallback");
        if(mrFallback && !mrDirect) {
            std::printf("       (as expected: metallic-roughness landed on UNKNOWN, not DIFFUSE_ROUGHNESS)\n");
        }

        const bool aoDirect   = HasTexture(material, aiTextureType_AMBIENT_OCCLUSION);
        const bool aoFallback = HasTexture(material, aiTextureType_LIGHTMAP);
        Check(aoDirect || aoFallback, "glb: AO lands on AMBIENT_OCCLUSION or its LIGHTMAP fallback");
        if(aoFallback && !aoDirect) {
            std::printf("       (as expected: AO landed on LIGHTMAP, not AMBIENT_OCCLUSION)\n");
        }
    }

    //--------------------------------------------------------------------
    //! FBX の書き出し→再インポートを検証します。
    //!
    //! MR / AO は最初から書いていない（FbxExporter.hpp を参照）。
    //! ここで確かめるのは Albedo / Normal がちゃんと届くことだけ
    //--------------------------------------------------------------------
    void TestFbxRoundTrip(const RockCore::Pipeline& pipeline, const std::string& path) {
        std::string error;
        Check(RockExport::ExportRock(pipeline, path, &error), "fbx export completes");

        Assimp::Importer importer;
        const aiScene*   scene = importer.ReadFile(path, kEngineImportFlags);

        Check(scene != nullptr, "fbx re-imports through Assimp with the engine's post-process flags");
        if(!scene) {
            std::printf("       import error: %s\n", importer.GetErrorString());
            return;
        }

        const bool hasMesh = scene->mNumMeshes > 0 && scene->mMeshes[0]->mNumVertices > 0;
        Check(hasMesh, "fbx mesh has vertices after reimport");
        if(hasMesh) {
            std::printf("       reimported vertices=%u triangles=%u\n", scene->mMeshes[0]->mNumVertices,
                        scene->mMeshes[0]->mNumFaces);
        }

        Check(scene->mNumMaterials > 0, "fbx has a material");
        if(scene->mNumMaterials == 0) {
            return;
        }

        const aiMaterial* material = scene->mMaterials[0];

        Check(HasTexture(material, aiTextureType_DIFFUSE), "fbx: albedo lands on DIFFUSE");
        Check(HasTexture(material, aiTextureType_NORMALS), "fbx: normal lands on NORMALS");

        std::printf("       (metallic-roughness / AO are intentionally not written to FBX -- see FbxExporter.hpp)\n");
    }

}    // namespace

//----------------------------------------------------------------------------
//! RockExport の書き出し検証を実行します。
//! @return 全て通れば 0、失敗があれば 1
//----------------------------------------------------------------------------
int RunExportVerification() {
    namespace fs = std::filesystem;

    // 一時フォルダへ書く。CI や別マシンでも書き込み権限が確実にある場所
    const fs::path outDir = fs::temp_directory_path() / "RockGeneratorExportVerify";

    std::error_code ec;
    fs::create_directories(outDir, ec);

    //--------------------------------------------------------------------
    // 小さめのテクスチャで焼く。この検証はスロットの割り当てとメッシュの
    // 往復を見るだけで、ベイクの品質そのものは --verify-core の役目
    //--------------------------------------------------------------------
    RockCore::Pipeline pipeline;
    pipeline.GetMutableParams().textureSize      = 128;
    pipeline.GetMutableParams().targetEdgeLength = 0.09f;
    pipeline.SetTargetStage(RockCore::PipelineStage::BakeAo);

    Check(pipeline.Update(nullptr, nullptr), "the pipeline completes before export");

    std::printf("--- GLB ---\n");
    TestGlbRoundTrip(pipeline, (outDir / "verify.glb").string());

    std::printf("--- FBX ---\n");
    TestFbxRoundTrip(pipeline, (outDir / "verify.fbx").string());

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
