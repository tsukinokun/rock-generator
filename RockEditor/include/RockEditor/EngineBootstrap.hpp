//----------------------------------------------------------------------------
//! @file   EngineBootstrap.hpp
//! @brief  Window と Renderer だけでエンジンを起こすもの
//! @detail EngineIntegration / Scene / ECS は使いません。このツールに要るのは
//!         「ウィンドウ」と「DX11 のレンダラ」だけで、ECS を通すと
//!         プレビュー用のエンティティ定義や Prefab 登録が必要になって
//!         本題から遠のきます。
//!
//!         手本は EngineIntegration.cpp:150-314。そこから ECS / Scene /
//!         Audio / Physics / PrefabFactory を抜いたものがここの中身です。
//----------------------------------------------------------------------------
#pragma once
#include <Tsukino/BuiltIn/BuiltInAssets.hpp>
#include <Tsukino/Core/Window.hpp>
#include <Tsukino/Engine/Asset/AssetManager.hpp>
#include <Tsukino/Renderer/Renderer.hpp>

#include <memory>
#include <string>

// 名前空間 RockEditor
namespace RockEditor {

    //------------------------------------------------------------------------
    //! @class EngineBootstrap
    //! エンジンの最小構成を所有するもの
    //------------------------------------------------------------------------
    class EngineBootstrap {
    public:
        EngineBootstrap() = default;
        ~EngineBootstrap();

        EngineBootstrap(const EngineBootstrap&)            = delete;
        EngineBootstrap& operator=(const EngineBootstrap&) = delete;

        //------------------------------------------------------------------
        //! エンジンを起こします。
        //!
        //! 失敗した理由は Log へ出ます。Log::SetLogFile を先に呼んでおくこと
        //! （呼ばないと OutputDebugStringA にしか出ず、デバッガの外から
        //! 初期化失敗が完全に見えません）。
        //!
        //! @param  [in] title  ウィンドウタイトル
        //! @param  [in] width  クライアント幅
        //! @param  [in] height クライアント高さ
        //! @return 成功したら true
        //------------------------------------------------------------------
        bool Initialize(const std::string& title, int width, int height);

        //! ウィンドウを返します。
        //! @return ウィンドウ
        Tsukino::Core::Window& GetWindow() { return *m_window; }

        //! レンダラを返します。
        //! @return レンダラ
        Tsukino::Renderer::Renderer& GetRenderer() { return *m_renderer; }

        //! アセットマネージャを返します。
        //! @return アセットマネージャ
        Tsukino::Asset::AssetManager& GetAssetManager() { return *m_assetManager; }

        //! ビルトインアセットを返します。
        //! @return ビルトインアセット
        Tsukino::BuiltIn::BuiltInAssets& GetBuiltInAssets() { return *m_builtinAssets; }

    private:
        //! ビルトインシェーダーから RendererShaderSet を組み立てます。
        //! @param  [out] outShaderSet 組み立て先
        //! @return 全てのシェーダーが揃ったら true
        bool BuildShaderSet(Tsukino::Renderer::RendererShaderSet& outShaderSet);

        //! 大気散乱の空を張ります。失敗しても致命的ではありません。
        void SetupSky();

        //! @note 【この宣言順は破棄順序の設計であり、並べ替えてはならない】
        //!       EngineIntegration.hpp の順序に合わせてある。
        //!       Renderer は Window の HWND から作ったスワップチェインを持ち、
        //!       アセット（テクスチャ／シェーダー）は Renderer のデバイスから
        //!       作った GPU リソースを持つため、
        //!       BuiltInAssets → AssetManager → Renderer → Window の順に
        //!       破棄されなければならない（= 宣言はその逆順）
        std::unique_ptr<Tsukino::Core::Window>           m_window;
        std::unique_ptr<Tsukino::Renderer::Renderer>     m_renderer;
        std::unique_ptr<Tsukino::Asset::AssetManager>    m_assetManager;
        std::unique_ptr<Tsukino::BuiltIn::BuiltInAssets> m_builtinAssets;

        bool m_comInitialized = false;    // CoInitializeEx に成功したか（CoUninitialize の要否判定用）
    };

}    // namespace RockEditor
