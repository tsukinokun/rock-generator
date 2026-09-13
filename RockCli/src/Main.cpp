//----------------------------------------------------------------------------
//! @file   Main.cpp
//! @brief  RockCli のエントリポイント
//! @detail RockCore と RockExport だけを使うコンソールツールです。
//!         ウィンドウも DX11 も要らないことが、そのままコアの純粋性の
//!         検証になっています。
//!
//!         バッチ生成（--seed-range）は Phase 7 で実装します。
//----------------------------------------------------------------------------
#include <RockCore/Io/RockParamsJson.hpp>
#include <RockCore/Pipeline/Pipeline.hpp>

#include <RockExport/RockExporter.hpp>

#include <cstdio>
#include <cstring>
#include <string>

//! VerifyCore.cpp で定義
int RunCoreVerification();

//! VerifyExport.cpp で定義
int RunExportVerification();

namespace {

    //------------------------------------------------------------------------
    //! 使い方を表示します。
    //------------------------------------------------------------------------
    void PrintUsage() {
        std::printf("RockCli - procedural rock generator (command line)\n\n");
        std::printf("  RockCli --verify-core             Run the RockCore numeric regression tests\n");
        std::printf("  RockCli --verify-export           Run the RockExport GLB/FBX round-trip tests\n");
        std::printf("  RockCli --dump-params <file>      Write the default parameters as JSON\n");
        std::printf("  RockCli --export <out.glb|out.fbx> [params.json]\n");
        std::printf("                                     Generate a rock and export it.\n");
        std::printf("                                     Omit params.json to use the defaults.\n");
        std::printf("                                     A sidecar <out>.json is written alongside.\n");
        std::printf("\n");
        std::printf("Batch generation (--seed-range) lands in Phase 7.\n");
    }

    //------------------------------------------------------------------------
    //! 既定のパラメータを JSON として書き出します。
    //! @param  [in] filePath 書き出し先
    //! @return 成功したら 0
    //------------------------------------------------------------------------
    int DumpDefaultParams(const char* filePath) {
        RockCore::RockParams params{};

        std::string error;
        if(!RockCore::SaveRockParamsToFile(params, filePath, &error)) {
            std::fprintf(stderr, "RockCli: %s\n", error.c_str());
            return 1;
        }

        std::printf("wrote %s\n", filePath);
        return 0;
    }

    //------------------------------------------------------------------------
    //! 岩を1個生成して書き出します。
    //!
    //! パラメータの指定が無ければ既定値（RockParams{} = 「風化した露岩」）を
    //! そのまま使います。GUI と違って対話性は要らないので、いきなり
    //! BakeAo まで完走させます（xatlas も AO も、ここでは省略の理由が無い）。
    //!
    //! @param  [in] outputPath 出力パス（拡張子で .glb / .fbx を判定）
    //! @param  [in] paramsPath パラメータの JSON。nullptr なら既定値
    //! @return 成功したら 0
    //------------------------------------------------------------------------
    int ExportCommand(const char* outputPath, const char* paramsPath) {
        RockCore::RockParams params{};

        if(paramsPath) {
            std::string error;
            if(!RockCore::LoadRockParamsFromFile(params, paramsPath, &error)) {
                std::fprintf(stderr, "RockCli: %s\n", error.c_str());
                return 1;
            }
        }

        RockCore::Pipeline pipeline;
        pipeline.GetMutableParams() = params;
        pipeline.SetTargetStage(RockCore::PipelineStage::BakeAo);

        // cancel は渡さない。CLI に中断の概念は無く、最後まで走らせる
        if(!pipeline.Update(nullptr, nullptr)) {
            std::fprintf(stderr, "RockCli: generation did not complete.\n");
            return 1;
        }

        std::string error;
        if(!RockExport::ExportRock(pipeline, outputPath, &error)) {
            std::fprintf(stderr, "RockCli: %s\n", error.c_str());
            return 1;
        }

        // ExportRock はモデルさえ書ければ true を返す。rock.json 側だけ
        // 失敗したときは outError に警告が残っているので、成否は変えずに出す
        if(!error.empty()) {
            std::fprintf(stderr, "RockCli: warning: %s\n", error.c_str());
        }

        std::printf("wrote %s\n", outputPath);
        return 0;
    }

}    // namespace

//----------------------------------------------------------------------------
//! エントリポイントです。
//! @param  [in] argc 引数の数
//! @param  [in] argv 引数の配列
//! @return 終了コード
//----------------------------------------------------------------------------
int main(int argc, char** argv) {
    if(argc < 2) {
        PrintUsage();
        return 0;
    }

    const char* command = argv[1];

    if(std::strcmp(command, "--verify-core") == 0) {
        return RunCoreVerification();
    }

    if(std::strcmp(command, "--verify-export") == 0) {
        return RunExportVerification();
    }

    if(std::strcmp(command, "--dump-params") == 0) {
        if(argc < 3) {
            std::fprintf(stderr, "RockCli: --dump-params requires an output path.\n");
            return 1;
        }
        return DumpDefaultParams(argv[2]);
    }

    if(std::strcmp(command, "--export") == 0) {
        if(argc < 3) {
            std::fprintf(stderr, "RockCli: --export requires an output path.\n");
            return 1;
        }
        const char* paramsPath = (argc >= 4) ? argv[3] : nullptr;
        return ExportCommand(argv[2], paramsPath);
    }

    std::fprintf(stderr, "RockCli: unknown option '%s'.\n\n", command);
    PrintUsage();
    return 1;
}
