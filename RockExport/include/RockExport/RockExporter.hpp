//----------------------------------------------------------------------------
//! @file   RockExporter.hpp
//! @brief  Pipeline の焼き上がり結果を書き出す窓口
//! @detail RockCli と RockEditor の両方がここだけを呼びます。
//!         「拡張子で形式を選ぶ」「GlbWriter/FbxExporter のどちらを叩くか」
//!         「rock.json を並べて置く」という判断をここへ集めておくことで、
//!         呼び出し側は Pipeline と出力パスだけ知っていればよくなります。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Pipeline/Pipeline.hpp>
#include <RockCore/Types.hpp>

#include <string>

// 名前空間 RockExport
namespace RockExport {

    //------------------------------------------------------------------------
    //! @enum  RockExportFormat
    //! 書き出す形式
    //------------------------------------------------------------------------
    enum class RockExportFormat : RockCore::u8 {
        Glb,    //!< エンジン向けの主経路（自前ライタ）
        Fbx,    //!< DCC 相互運用用（Assimp）
    };

    //------------------------------------------------------------------------
    //! パスの拡張子から書き出し形式を判定します。
    //! @param  [in]  path       出力パス
    //! @param  [out] outFormat 判定結果
    //! @return 対応する拡張子（.glb / .fbx）なら true
    //------------------------------------------------------------------------
    bool DetectExportFormat(const std::string& path, RockExportFormat& outFormat);

    //------------------------------------------------------------------------
    //! Pipeline の焼き上がり結果を書き出します。
    //!
    //! pipeline は事前に PipelineStage::BakeAo まで完走していること
    //! （呼び出し側の責務。ここでは Update() を呼びません — 重い処理を
    //! 呼び出し側の知らないタイミングで走らせないためです）。
    //!
    //! 出力パスと同じ場所へ、拡張子だけ .json に替えたファイル名で
    //! RockParams も書き出します。同じシード・パラメータから
    //! 作り直せるようにするための添え物で、失敗してもモデルの
    //! 書き出し結果には影響させません（警告として outError に積むだけ）。
    //!
    //! @param  [in]  pipeline   焼き上がった Pipeline
    //! @param  [in]  outputPath 出力パス。拡張子で形式を判定する
    //! @param  [out] outError   失敗理由、または非致命的な警告。nullptr でもよい
    //! @return モデルの書き出しに成功したら true（rock.json の失敗は含まない）
    //------------------------------------------------------------------------
    bool ExportRock(const RockCore::Pipeline& pipeline, const std::string& outputPath, std::string* outError);

}    // namespace RockExport
