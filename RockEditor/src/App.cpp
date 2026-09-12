//----------------------------------------------------------------------------
//! @file   App.cpp
//! @brief  エディタ本体の実装
//----------------------------------------------------------------------------
#include <RockEditor/App.hpp>

#include <RockEditor/Localization.hpp>

#include <Tsukino/Core/Log.hpp>

#include <imgui.h>

#include <chrono>

// 名前空間 RockEditor
namespace RockEditor {

    namespace {

        //! ウィンドウの初期サイズ
        constexpr int kWindowWidth  = 1600;
        constexpr int kWindowHeight = 900;

    }    // namespace

    //------------------------------------------------------------------------
    //! 起動してメインループを回します。
    //------------------------------------------------------------------------
    int App::Run() {
        // これを呼ばないと Log は OutputDebugStringA にしか出ない。
        // つまりデバッガの外から初期化失敗が完全に見えなくなるので、
        // 何よりも先に呼ぶ
        Tsukino::Core::Log::SetLogFile("Logs/RockGenerator.log");

        if(!m_engine.Initialize("RockGenerator", kWindowWidth, kWindowHeight)) {
            return 1;
        }

        if(!m_imgui.Initialize(m_engine.GetWindow(), m_engine.GetRenderer())) {
            return 1;
        }

        if(!m_preview.Initialize(m_engine)) {
            return 1;
        }

        //--------------------------------------------------------------------
        // 言語を決める。
        //
        // ImGui の初期化より後に置いているのは、日本語フォントを読めたかを
        // 見てから決めたいため。設定ファイルが無いのは初回起動なので正常系で、
        // そのときだけ OS の UI 言語から決める
        //--------------------------------------------------------------------
        if(!LoadEditorSettings(m_settings, kEditorSettingsFileName)) {
            m_settings.language = DetectSystemLanguage();
        }
        if(m_settings.language == Language::Japanese && !m_imgui.HasJapaneseFont()) {
            // 日本語フォントが無い環境。選んでも "?" しか出ないので落とす
            m_settings.language = Language::English;
        }
        SetLanguage(m_settings.language);


        auto previousTime = std::chrono::steady_clock::now();

        while(m_engine.GetWindow().ProcessMessages()) {
            const auto                                  now     = std::chrono::steady_clock::now();
            const std::chrono::duration<float>          elapsed = now - previousTime;
            previousTime                                        = now;

            const float deltaTime = elapsed.count();

            ConsumeResult();
            RenderFrame(deltaTime);
            ScheduleWork();
        }

        // ワーカーを確実に畳む。m_jobs のデストラクタでも待つが、
        // 明示しておく方が「ここで待つ」という意図が読める
        m_jobs.RequestCancel();

        Tsukino::Core::Log::Info("RockEditor: shutting down.");
        Tsukino::Core::Log::CloseLogFile();
        return 0;
    }

    //------------------------------------------------------------------------
    //! 1フレーム描画します。
    //------------------------------------------------------------------------
    void App::RenderFrame(float deltaTime) {
        Tsukino::Renderer::Renderer& renderer = m_engine.GetRenderer();

        // b0 の timeParams を進める。毎フレーム1回
        renderer.AdvanceFrameTime(deltaTime);

        m_imgui.BeginFrame();
        m_imgui.BeginDockSpace();
        BuildUi();

        // カメラ → ライトの順で設定する。
        // SetDirectionalLight はシャドウの投影範囲をカメラ位置から決めるため、
        // 逆順にすると影の位置がずれる
        m_preview.SubmitCameraAndLight(m_engine, m_view, m_boundingRadius);
        m_preview.SubmitRock(renderer, m_editParams);

        // ImGui は Overlay パスへ積む。Render() は内部で Present まで
        // 済ませるので、Render() の後に描いても画面には出ない
        m_imgui.SubmitDrawCommand(renderer);

        renderer.Render();
    }

    //------------------------------------------------------------------------
    //! UI を構築します。
    //------------------------------------------------------------------------
    void App::BuildUi() {
        DrawShapePanel(m_editParams);
        DrawMaterialPanel(m_editParams);
        DrawViewPanel(m_view);
        DrawStatsPanel(m_snapshot, m_jobs.IsBusy(), m_jobs.GetProgress(), m_jobs.GetLabel());

        // 言語が変わった瞬間だけ書き出す。毎フレーム書くわけにはいかない
        if(DrawSettingsPanel(m_editParams, m_ui, m_settings, m_imgui.HasJapaneseFont())) {
            SetLanguage(m_settings.language);
            SaveEditorSettings(m_settings, kEditorSettingsFileName);
        }

        if(m_ui.showImGuiDemo) {
            ImGui::ShowDemoWindow(&m_ui.showImGuiDemo);
        }

        // スライダを掴んでいる間は形だけを更新し、離したらベイクまで走らせる。
        // これが「スライダを動かすと形が変わる」体感の正体
        m_editing = ImGui::IsAnyItemActive();
    }

    //------------------------------------------------------------------------
    //! パラメータの差分を見て、必要なら生成を投げます。
    //------------------------------------------------------------------------
    void App::ScheduleWork() {
        //--------------------------------------------------------------------
        // 走っている間は Pipeline に一切触らない（パラメータの代入も含む）。
        // 操作中なら中断を伝えて、空いてから作り直す
        //--------------------------------------------------------------------
        if(m_jobs.IsBusy()) {
            if(m_editing) {
                m_jobs.RequestCancel();
            }
            return;
        }

        // 操作中は Unwrap まで。UV が無いとベイク済みのマップを貼る座標が
        // 取れないので、Mesh で止めずに Unwrap までは進めておく
        const RockCore::PipelineStage target =
            m_editing ? RockCore::PipelineStage::Unwrap : RockCore::PipelineStage::BakeAo;

        // 形を動かしている最中は、前の形で焼いたマップを貼らない
        m_preview.SetBakedMapsVisible(!m_editing);

        m_pipeline.GetMutableParams() = m_editParams;
        m_pipeline.SetTargetStage(target);

        //--------------------------------------------------------------------
        // UV 展開を八面体射影へ落とす条件は2つ。
        //
        //   1. 操作中。xatlas は数百ms〜数秒かかるので、スライダを掴んでいる間に
        //      走らせると形の更新がそこで待たされる
        //   2. まだ一度も結果が出ていないとき。起動直後にいきなり xatlas を
        //      走らせると、その間ずっと何も映らない。Debug ビルドでは数分に
        //      なることもあり、固まったのと区別が付かない。
        //      まず八面体射影で形を出し、空いてから焼き直す
        //
        // どちらも離せば（= 結果が出れば）選ばれた方式で焼き直される。
        // ハッシュには「実際に使う方式」が入っているので、切り替わった瞬間に
        // Unwrap が dirty になる
        //--------------------------------------------------------------------
        m_pipeline.SetFastPreview(m_editing || !m_snapshot.valid);

        if(m_pipeline.IsUpToDate()) {
            return;
        }

        m_jobs.Submit([this](const RockCore::ProgressCallback& progress, const RockCore::CancelToken* cancel) {
            m_pipeline.Update(progress, cancel);
        });
    }

    //------------------------------------------------------------------------
    //! 完了した結果を GPU と写しへ取り込みます。
    //------------------------------------------------------------------------
    void App::ConsumeResult() {
        if(!m_jobs.PollCompletion()) {
            return;
        }

        //--------------------------------------------------------------------
        // ここはワーカーが止まっている区間なので Pipeline を読んでよい。
        //
        // リビジョンは「その段が完走したときだけ」上がる。中断されたベイクは
        // リビジョンを上げないので、作りかけの画像を貼ってしまうことがない
        //--------------------------------------------------------------------
        ID3D11Device* device = m_engine.GetRenderer().GetDevice();

        if(m_pipeline.GetMeshRevision() != m_uploadedMeshRevision) {
            m_preview.UploadMesh(device, m_pipeline.GetMesh());
            m_uploadedMeshRevision = m_pipeline.GetMeshRevision();
        }

        if(m_pipeline.GetNormalMapRevision() != m_uploadedNormalRevision) {
            m_preview.UploadNormalMap(device, m_pipeline.GetNormalMap());
            m_uploadedNormalRevision = m_pipeline.GetNormalMapRevision();
        }

        if(m_pipeline.GetSurfaceMapRevision() != m_uploadedSurfaceRevision) {
            m_preview.UploadSurfaceMaps(device, m_pipeline.GetAlbedoMap(), m_pipeline.GetMetallicRoughnessMap());
            m_uploadedSurfaceRevision = m_pipeline.GetSurfaceMapRevision();
        }

        if(m_pipeline.GetAoMapRevision() != m_uploadedAoRevision) {
            m_preview.UploadAoMap(device, m_pipeline.GetAoMap());
            m_uploadedAoRevision = m_pipeline.GetAoMapRevision();
        }

        m_boundingRadius = m_pipeline.GetBoundingRadius();

        // UI が読むのはこの写しだけ
        m_snapshot.mesh           = m_pipeline.GetMeshStats();
        m_snapshot.unwrap         = m_pipeline.GetUnwrapStats();
        m_snapshot.normal         = m_pipeline.GetNormalBakeStats();
        m_snapshot.surface        = m_pipeline.GetSurfaceBakeStats();
        m_snapshot.ao             = m_pipeline.GetAoBakeStats();
        m_snapshot.coveredTexels  = m_pipeline.GetBakeGBuffer().GetCoveredTexelCount();
        m_snapshot.textureSize    = m_pipeline.GetBakeGBuffer().GetSize();
        m_snapshot.boundingRadius = m_boundingRadius;
        m_snapshot.valid          = true;

        for(RockCore::u32 i = 0; i < RockCore::kPipelineStageCount; ++i) {
            m_snapshot.stages[i] = m_pipeline.GetStageStatus(static_cast<RockCore::PipelineStage>(i));
        }
    }

}    // namespace RockEditor
