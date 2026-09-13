//----------------------------------------------------------------------------
//! @file   RockExporter.cpp
//! @brief  書き出し窓口の実装
//----------------------------------------------------------------------------
#include <RockExport/RockExporter.hpp>

#include <RockExport/FbxExporter.hpp>
#include <RockExport/GlbWriter.hpp>

#include <RockCore/Io/RockParamsJson.hpp>

#include <algorithm>
#include <cctype>

// 名前空間 RockExport
namespace RockExport {

    namespace {

        //--------------------------------------------------------------------
        //! 拡張子を小文字で取り出します（先頭のドットは含みません）。
        //! @param  [in] path 対象のパス
        //! @return 拡張子。無ければ空文字列
        //--------------------------------------------------------------------
        std::string GetLowerExtension(const std::string& path) {
            const size_t dot = path.find_last_of('.');

            // パス区切りより手前のドットは拡張子と見なさない
            // （"C:\rock.v2\out" のようなディレクトリ名を誤検出しないため）
            const size_t sep = path.find_last_of("/\\");
            if(dot == std::string::npos || (sep != std::string::npos && dot < sep)) {
                return "";
            }

            std::string ext = path.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext;
        }

        //--------------------------------------------------------------------
        //! 出力パスの拡張子を差し替えます。
        //! @param  [in] path      元のパス
        //! @param  [in] extension 新しい拡張子（ドットを含む。例: ".json"）
        //! @return 差し替え後のパス
        //--------------------------------------------------------------------
        std::string ReplaceExtension(const std::string& path, const std::string& extension) {
            const size_t dot = path.find_last_of('.');
            const size_t sep = path.find_last_of("/\\");

            if(dot == std::string::npos || (sep != std::string::npos && dot < sep)) {
                return path + extension;
            }
            return path.substr(0, dot) + extension;
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! パスの拡張子から書き出し形式を判定します。
    //------------------------------------------------------------------------
    bool DetectExportFormat(const std::string& path, RockExportFormat& outFormat) {
        const std::string ext = GetLowerExtension(path);

        if(ext == "glb") {
            outFormat = RockExportFormat::Glb;
            return true;
        }
        if(ext == "fbx") {
            outFormat = RockExportFormat::Fbx;
            return true;
        }
        return false;
    }

    //------------------------------------------------------------------------
    //! Pipeline の焼き上がり結果を書き出します。
    //------------------------------------------------------------------------
    bool ExportRock(const RockCore::Pipeline& pipeline, const std::string& outputPath, std::string* outError) {
        RockExportFormat format{};
        if(!DetectExportFormat(outputPath, format)) {
            if(outError) {
                *outError = "unknown export extension (expected .glb or .fbx): " + outputPath;
            }
            return false;
        }

        const RockCore::MeshBuilder& mesh = pipeline.GetMesh();

        bool modelOk = false;
        switch(format) {
        case RockExportFormat::Glb:
            modelOk = WriteGlb(mesh,
                               pipeline.GetAlbedoMap(),
                               pipeline.GetNormalMap(),
                               pipeline.GetMetallicRoughnessMap(),
                               pipeline.GetAoMap(),
                               outputPath,
                               outError);
            break;

        case RockExportFormat::Fbx:
            modelOk = WriteFbx(mesh, pipeline.GetAlbedoMap(), pipeline.GetNormalMap(), outputPath, outError);
            break;
        }

        if(!modelOk) {
            return false;
        }

        //----------------------------------------------------------------------
        // rock.json を添える。失敗はモデルの書き出し成功を覆さない
        // （形式は書けているので、パラメータの控えが無い程度で成否を反転させない）
        //----------------------------------------------------------------------
        std::string jsonError;
        const std::string jsonPath = ReplaceExtension(outputPath, ".json");
        if(!RockCore::SaveRockParamsToFile(pipeline.GetParams(), jsonPath, &jsonError) && outError) {
            *outError = "model exported, but the params sidecar failed: " + jsonError;
        }

        return true;
    }

}    // namespace RockExport
