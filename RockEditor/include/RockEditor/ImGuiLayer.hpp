//----------------------------------------------------------------------------
//! @file   ImGuiLayer.hpp
//! @brief  ImGui の初期化と、Overlay パスへの積み込み
//! @detail エンジンへの変更は一切要りません。必要なフックはすべて公開済みです。
//!
//!         描画は RenderPass::Overlay の DrawCommand::customDraw に積みます。
//!         Renderer::Render() は内部で Present まで済ませてしまうので、
//!         Render() の後に ImGui を描いても画面には出ません。
//!
//!         入力は Window::SetMessageCallback から受けますが、このフックは
//!         WM_KEYFIRST..WM_KEYLAST と WM_MOUSEFIRST..WM_MOUSELAST しか
//!         転送しません（Window.cpp:293-298）。足りないぶんは
//!         ImGuiLayer 側で3点補っています。詳細は .cpp のコメント。
//----------------------------------------------------------------------------
#pragma once
#include <Tsukino/Core/Window.hpp>
#include <Tsukino/Renderer/Renderer.hpp>

// 名前空間 RockEditor
namespace RockEditor {

    //------------------------------------------------------------------------
    //! @class ImGuiLayer
    //! ImGui のコンテキストとエンジンへの橋渡しを持つもの
    //------------------------------------------------------------------------
    class ImGuiLayer {
    public:
        ImGuiLayer() = default;
        ~ImGuiLayer();

        ImGuiLayer(const ImGuiLayer&)            = delete;
        ImGuiLayer& operator=(const ImGuiLayer&) = delete;

        //------------------------------------------------------------------
        //! ImGui を初期化し、ウィンドウのメッセージを繋ぎます。
        //!
        //! Window::SetMessageCallback は単一スロットですが、
        //! EngineIntegration を初期化していないので空いています。
        //!
        //! @param  [in] window   ウィンドウ
        //! @param  [in] renderer レンダラ
        //! @return 成功したら true
        //------------------------------------------------------------------
        bool Initialize(Tsukino::Core::Window& window, Tsukino::Renderer::Renderer& renderer);

        //! フレームを開始します。この後にパネルを構築します。
        void BeginFrame();

        //------------------------------------------------------------------
        //! 描画コマンドを Overlay パスへ積みます。
        //!
        //! Renderer::Render() を呼ぶ前に呼ぶこと。
        //!
        //! @param [in] renderer レンダラ
        //------------------------------------------------------------------
        void SubmitDrawCommand(Tsukino::Renderer::Renderer& renderer);

        //! 画面全体を覆うドックスペースを敷きます。
        void BeginDockSpace();

        //------------------------------------------------------------------
        //! 日本語が出せるフォントを読み込めたかを返します。
        //!
        //! 読めていない環境で日本語を選ぶと全部 "?" になるので、
        //! UI 側で言語の選択肢を無効化するのに使います。
        //!
        //! @return 読み込めていれば true
        //------------------------------------------------------------------
        bool HasJapaneseFont() const { return m_hasJapaneseFont; }

    private:
        Tsukino::Core::Window* m_window      = nullptr;
        bool                   m_initialized = false;
        bool                   m_layoutBuilt = false;    //!< 既定のドック配置を組んだか
        bool                   m_hasJapaneseFont = false;
    };

}    // namespace RockEditor
