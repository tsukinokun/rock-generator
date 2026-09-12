//----------------------------------------------------------------------------
//! @file   Localization.hpp
//! @brief  UI 文字列の英日切り替え
//! @detail 【ラベルを変えると ImGui の ID が変わることに注意】
//!
//!         ImGui はウィジェットとウィンドウの ID をラベル文字列のハッシュから
//!         作ります。素朴に訳文へ差し替えると ID まで変わってしまい、
//!         言語を切り替えた瞬間にドック配置が崩れ、スライダのドラッグや
//!         TreeNode の開閉状態も失われます。
//!
//!         そこで訳文には最初から `###<安定キー>` を埋め込んであります。
//!         ImHashStr（imgui.cpp:2572-2598）は `###` を見つけるとハッシュを
//!         そこから開始し直すため、
//!
//!             "Shape###shape"  と  "形状###shape"  は同じ ID
//!
//!         になります。Begin も DockBuilderDockWindow（imgui.cpp:20995）も
//!         同じ ImHashStr を通るので、ウィンドウ名にもそのまま効きます。
//!
//!         `##` ではなく `###` を使うこと。`##` は前半も ID に含めるので、
//!         前半（＝訳文）が変われば ID も変わり、意味がありません。
//!
//!         表示専用のもの（ImGui::Text の書式文字列など）は ID を持たないので
//!         `###` は付けていません。
//!
//!         RockCore には置きません。RockCore が UI を知らないことが
//!         CLI とテストの成立条件だからです。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Pipeline/Pipeline.hpp>

// 名前空間 RockEditor
namespace RockEditor {

    //------------------------------------------------------------------------
    //! @enum  Language
    //! UI の言語
    //------------------------------------------------------------------------
    enum class Language : int {
        English = 0,
        Japanese,
        Count,
    };

    //------------------------------------------------------------------------
    //! @enum  UiText
    //! UI に出る文字列の種類。
    //!
    //! @note 【Localization.cpp の kEnglish / kJapanese と並び順を必ず一致
    //!       させること】個数の違いは static_assert で落ちますが、順序の
    //!       取り違えは落ちません。起動時の VerifyLocalizationTables() が
    //!       `###` キーの食い違いだけは拾います。
    //------------------------------------------------------------------------
    enum class UiText : int {
        //-- ウィンドウ ------------------------------------------------------
        WindowShape,
        WindowMaterial,
        WindowView,
        WindowStats,
        WindowSettings,

        //-- 形状パネル ------------------------------------------------------
        LabelSeed,
        ButtonRandomize,
        LabelRadius,
        LabelAnisoScale,
        TipAnisoScale,
        LabelTargetEdge,
        TipTargetEdge,
        LabelBaseSubdivision,
        FormatNyquist,
        TipNyquist,
        HeadingNoiseLayers,
        FormatLayerName,
        LabelEnabled,
        LabelAmplitude,
        LabelFrequency,
        LabelOctaves,
        LabelLacunarity,
        LabelGain,
        ButtonAddLayer,
        ButtonRemoveLayer,

        //-- マテリアル / ベイクパネル ---------------------------------------
        LabelBaseColor,
        LabelRoughness,
        NoteMetallicFixed,
        LabelTextureSize,
        TipTextureSize,
        LabelDilatePasses,
        TipDilatePasses,
        NotePhase45Bake,

        //-- 視点パネル ------------------------------------------------------
        HeadingCamera,
        LabelYaw,
        LabelPitch,
        LabelDistance,
        LabelFov,
        HeadingLight,
        LabelLightYaw,
        LabelLightPitch,
        LabelIntensity,
        NoteTbnCheck,

        //-- 統計パネル ------------------------------------------------------
        FormatWorking,
        StatusIdle,
        StatusNoResult,
        FormatVertices,
        FormatTriangles,
        FormatSubdivisions,
        FormatMaxEdge,
        FormatUvCoverage,
        FormatBakedTexels,
        FormatDegenerateTbn,
        ColumnStage,
        ColumnMilliseconds,
        ColumnRuns,
        ColumnDirty,
        ValueDirtyYes,
        ValueDirtyNo,

        //-- 設定 / 入出力パネル ---------------------------------------------
        HeadingSettings,
        LabelLanguage,
        TipLanguageFontMissing,
        HeadingParamsIo,
        LabelFilePath,
        ButtonSave,
        ButtonLoad,
        PrefixSaved,
        PrefixSaveFailed,
        PrefixLoaded,
        PrefixLoadFailed,
        LabelImGuiDemo,

        //-- パイプラインの段名 ----------------------------------------------
        StageField,
        StageMesh,
        StageUnwrap,
        StageBakeNormal,

        //-- 破断面（Phase 3）------------------------------------------------
        //
        // 末尾へ足しているのは意図的。途中へ差し込むと kEnglish / kJapanese の
        // 並びを手で揃え直すことになり、ずれても static_assert では捕まらない
        LabelPreset,
        PresetHint,
        PresetWeathered,
        PresetAngular,
        PresetBoulder,
        PresetSlab,

        HeadingFracture,
        LabelPlaneCutCount,
        TipPlaneCutCount,
        LabelPlaneCutDepth,
        LabelPlaneAxisBias,
        TipPlaneAxisBias,
        LabelEdgeRounding,
        TipEdgeRounding,
        LabelCreaseAngle,
        TipCreaseAngle,

        LabelLayerKind,
        KindFbm,
        KindWorley,
        KindRidged,
        LabelSharpness,
        TipSharpness,

        FormatCutPlanes,
        FormatSplitVertices,

        //-- UV 展開 ---------------------------------------------------------
        HeadingUnwrap,
        LabelUnwrapMethod,
        TipUnwrapMethod,
        MethodOctahedral,
        MethodXAtlas,
        LabelTexelsPerUnit,
        TipTexelsPerUnit,
        LabelUvPadding,
        TipUvPadding,

        FormatUnwrapMethod,
        FormatChartCount,
        FormatAtlasUtilization,
        NoteUnwrapFellBack,

        //-- マテリアル ------------------------------------------------------
        LabelSecondaryColor,
        TipSecondaryColor,
        LabelColorVariation,
        LabelColorNoiseFrequency,
        TipColorNoiseFrequency,
        LabelCavityDarkening,
        TipCavityDarkening,
        LabelCavityRoughness,
        TipCavityRoughness,

        FormatCavityRatio,
        FormatLuminanceRange,

        StageBakeColor,

        Count,
    };

    //------------------------------------------------------------------------
    //! 表示する言語を設定します。
    //! @param [in] language 言語
    //------------------------------------------------------------------------
    void SetLanguage(Language language);

    //------------------------------------------------------------------------
    //! 現在の言語を返します。
    //! @return 言語
    //------------------------------------------------------------------------
    Language GetLanguage();

    //------------------------------------------------------------------------
    //! 現在の言語の文字列を返します。
    //!
    //! 返る文字列は静的な寿命を持つので、そのまま ImGui へ渡せます。
    //!
    //! @param  [in] text 文字列の種類
    //! @return 訳文（ID 用の ### 接尾辞を含むことがある）
    //------------------------------------------------------------------------
    const char* Tr(UiText text);

    //------------------------------------------------------------------------
    //! パイプラインの段の表示名を返します。
    //!
    //! RockCore::GetPipelineStageName() は英語の識別子としてログと CLI が
    //! 使うので据え置き、UI 表示用はこちらで別に持ちます。
    //!
    //! @param  [in] stage 段
    //! @return 表示名
    //------------------------------------------------------------------------
    const char* Tr(RockCore::PipelineStage stage);

    //------------------------------------------------------------------------
    //! 翻訳テーブルの整合を検査します。起動時に1度だけ呼びます。
    //!
    //! 見るのは2点です。
    //!   - どちらのテーブルにも nullptr が無いこと
    //!   - 同じ添字なら `###` 以降が一致すること
    //!
    //! 2つ目が要です。ここがずれると「日本語にした瞬間だけドックが崩れる」
    //! という再現しづらい壊れ方をします。
    //!
    //! @return 問題が無ければ true。失敗内容は Log へ英語で出る
    //------------------------------------------------------------------------
    bool VerifyLocalizationTables();

}    // namespace RockEditor
