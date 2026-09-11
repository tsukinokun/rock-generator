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

#include <cstdio>
#include <cstring>
#include <string>

//! VerifyCore.cpp で定義
int RunCoreVerification();

namespace {

    //------------------------------------------------------------------------
    //! 使い方を表示します。
    //------------------------------------------------------------------------
    void PrintUsage() {
        std::printf("RockCli - procedural rock generator (command line)\n\n");
        std::printf("  RockCli --verify-core          Run the RockCore numeric regression tests\n");
        std::printf("  RockCli --dump-params <file>   Write the default parameters as JSON\n");
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

    if(std::strcmp(command, "--dump-params") == 0) {
        if(argc < 3) {
            std::fprintf(stderr, "RockCli: --dump-params requires an output path.\n");
            return 1;
        }
        return DumpDefaultParams(argv[2]);
    }

    std::fprintf(stderr, "RockCli: unknown option '%s'.\n\n", command);
    PrintUsage();
    return 1;
}
