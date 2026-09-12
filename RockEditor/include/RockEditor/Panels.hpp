//----------------------------------------------------------------------------
//! @file   Panels.hpp
//! @brief  ImGui のパネル群
//! @detail パネルは状態を持たず、参照で受けた値を直接書き換えます。
//!         「どのパラメータが変わったか」を追いかける仕組みは置きません。
//!         Pipeline 側がパラメータのハッシュで段ごとに差分を判定するので、
//!         UI が変更通知を持つと二重管理になります。
//----------------------------------------------------------------------------
#pragma once
#include <RockEditor/EditorSettings.hpp>
#include <RockEditor/PreviewScene.hpp>

#include <RockCore/Bake/UvRasterizer.hpp>
#include <RockCore/Pipeline/Pipeline.hpp>
#include <RockCore/Shape/RockParams.hpp>

#include <array>
#include <string>

// 名前空間 RockEditor
namespace RockEditor {

    //------------------------------------------------------------------------
    //! @struct PipelineSnapshot
    //! StatsPanel が表示するための、生成結果の写し
    //!
    //! ワーカースレッドが Pipeline を書き換えている最中に UI が中身を読むと
    //! データ競合になります。完了時に一度だけ写しを取り、UI はこちらだけを
    //! 読むことで読み書きを分けています。
    //------------------------------------------------------------------------
    struct PipelineSnapshot {
        RockCore::RockMeshStats                                                  mesh{};
        RockCore::UnwrapStats                                                    unwrap{};
        RockCore::NormalBakeStats                                                normal{};
        RockCore::SurfaceBakeStats                                               surface{};
        RockCore::AoBakeStats                                                    ao{};
        std::array<RockCore::PipelineStageStatus, RockCore::kPipelineStageCount> stages{};

        RockCore::u32 coveredTexels   = 0;
        int           textureSize     = 0;
        float         boundingRadius  = 1.0f;
        bool          valid           = false;
    };

    //------------------------------------------------------------------------
    //! @struct EditorUiState
    //! UI 自身の状態（保存しない一時的なもの）
    //------------------------------------------------------------------------
    struct EditorUiState {
        bool        showImGuiDemo = false;
        std::string ioStatus;              //!< 保存／読み込みの結果メッセージ
        std::string paramsFilePath = "rock.json";
    };

    //------------------------------------------------------------------------
    //! 形のパネルを描きます。
    //! @param [in,out] params 岩のパラメータ
    //------------------------------------------------------------------------
    void DrawShapePanel(RockCore::RockParams& params);

    //------------------------------------------------------------------------
    //! マテリアルとベイク設定のパネルを描きます。
    //! @param [in,out] params 岩のパラメータ
    //------------------------------------------------------------------------
    void DrawMaterialPanel(RockCore::RockParams& params);

    //------------------------------------------------------------------------
    //! 視点とライトのパネルを描きます。
    //! @param [in,out] view 視点の設定
    //------------------------------------------------------------------------
    void DrawViewPanel(PreviewViewSettings& view);

    //------------------------------------------------------------------------
    //! 生成結果のパネルを描きます。
    //! @param [in] snapshot 生成結果の写し
    //! @param [in] busy     ワーカーが走っているか
    //! @param [in] progress 進捗（0〜1）
    //! @param [in] label    現在の作業名
    //------------------------------------------------------------------------
    void DrawStatsPanel(const PipelineSnapshot& snapshot, bool busy, float progress, const std::string& label);

    //------------------------------------------------------------------------
    //! 設定と保存・読み込みのパネルを描きます。
    //!
    //! 言語が変わったかを返すのは、変わった瞬間だけ設定ファイルへ書きたい
    //! ためです（毎フレーム書くわけにはいかない）。
    //!
    //! @param  [in,out] params                岩のパラメータ
    //! @param  [in,out] ui                    UI の状態
    //! @param  [in,out] settings              エディタの設定
    //! @param  [in]     japaneseFontAvailable 日本語フォントが読めているか
    //! @return 言語が変わったら true
    //------------------------------------------------------------------------
    bool DrawSettingsPanel(RockCore::RockParams& params,
                           EditorUiState&        ui,
                           EditorSettings&       settings,
                           bool                  japaneseFontAvailable);

}    // namespace RockEditor
