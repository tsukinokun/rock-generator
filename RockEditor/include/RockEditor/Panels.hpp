//----------------------------------------------------------------------------
//! @file   Panels.hpp
//! @brief  ImGui のパネル群
//! @detail パネルは状態を持たず、参照で受けた値を直接書き換えます。
//!         「どのパラメータが変わったか」を追いかける仕組みは置きません。
//!         Pipeline 側がパラメータのハッシュで段ごとに差分を判定するので、
//!         UI が変更通知を持つと二重管理になります。
//----------------------------------------------------------------------------
#pragma once
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
        RockCore::RockMeshStats                                                    mesh{};
        RockCore::NormalBakeStats                                                  normal{};
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
    //! 保存・読み込みのパネルを描きます。
    //! @param [in,out] params 岩のパラメータ
    //! @param [in,out] ui     UI の状態
    //------------------------------------------------------------------------
    void DrawIoPanel(RockCore::RockParams& params, EditorUiState& ui);

}    // namespace RockEditor
