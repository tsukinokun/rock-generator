//----------------------------------------------------------------------------
//! @file   App.hpp
//! @brief  エディタ本体
//! @detail スレッドとデータの持ち方に一点だけ約束があります。
//!
//!         Pipeline はワーカースレッドが書き換えるため、走っている間は
//!         メインスレッドから一切触りません（パラメータの代入も結果の読みも）。
//!         UI が編集するのは App 側の複製（m_editParams）で、ワーカーが
//!         空いたときにまとめて Pipeline へ渡します。結果は完了時に
//!         PipelineSnapshot へ写し、UI はその写しだけを読みます。
//!
//!         この分け方にしているのは、ロックで守ると「重いベイク中ずっと
//!         UI がロック待ちする」か「ロックの粒度を細かくして取り落とす」の
//!         どちらかになるためです。
//----------------------------------------------------------------------------
#pragma once
#include <RockEditor/EditorSettings.hpp>
#include <RockEditor/EngineBootstrap.hpp>
#include <RockEditor/ImGuiLayer.hpp>
#include <RockEditor/JobRunner.hpp>
#include <RockEditor/Panels.hpp>
#include <RockEditor/PreviewScene.hpp>

#include <RockCore/Pipeline/Pipeline.hpp>
#include <RockCore/Shape/RockParams.hpp>

// 名前空間 RockEditor
namespace RockEditor {

    //------------------------------------------------------------------------
    //! @class App
    //! エディタ全体をまとめるもの
    //------------------------------------------------------------------------
    class App {
    public:
        App()  = default;
        ~App() = default;

        App(const App&)            = delete;
        App& operator=(const App&) = delete;

        //! 起動してメインループを回します。
        //! @return 終了コード
        int Run();

    private:
        //! UI を構築します。
        void BuildUi();

        //! パラメータの差分を見て、必要なら生成を投げます。
        void ScheduleWork();

        //! 完了した結果を GPU と写しへ取り込みます。
        void ConsumeResult();

        //! 1フレーム描画します。
        //! @param [in] deltaTime 前フレームからの経過秒
        void RenderFrame(float deltaTime);

        EngineBootstrap m_engine;
        ImGuiLayer      m_imgui;
        PreviewScene    m_preview;

        //! @note 【この宣言順は破棄順序の設計であり、並べ替えてはならない】
        //!       m_jobs のデストラクタはワーカーの終了を待つ。そのワーカーは
        //!       m_pipeline を触っているため、m_pipeline より後に宣言して
        //!       先に破棄させる
        RockCore::Pipeline m_pipeline;
        JobRunner          m_jobs;

        //! UI が編集する複製。ワーカーが空いたときに Pipeline へ渡す
        RockCore::RockParams m_editParams{};

        PreviewViewSettings m_view{};
        EditorUiState       m_ui{};
        PipelineSnapshot    m_snapshot{};

        //! 起動をまたいで覚えておく設定（今は言語だけ）
        EditorSettings m_settings{};

        //! GPU へ載せ済みのリビジョン。これが変わったときだけ載せ直す
        RockCore::u32 m_uploadedMeshRevision    = 0;
        RockCore::u32 m_uploadedNormalRevision  = 0;
        RockCore::u32 m_uploadedSurfaceRevision = 0;
        RockCore::u32 m_uploadedAoRevision      = 0;

        //! 岩の外接半径。カメラ距離の基準に使う
        float m_boundingRadius = 1.0f;

        //! 直前のフレームでユーザーが UI を操作していたか
        bool m_editing = false;
    };

}    // namespace RockEditor
