//----------------------------------------------------------------------------
//! @file   EngineBootstrap.cpp
//! @brief  Window と Renderer だけでエンジンを起こす実装
//----------------------------------------------------------------------------
#include <RockEditor/EngineBootstrap.hpp>

#include <Tsukino/Core/Log.hpp>
#include <Tsukino/Engine/Asset/Shader/ShaderAsset.hpp>
#include <Tsukino/Renderer/ConstantBuffer.hpp>

#include <objbase.h>

// 名前空間 RockEditor
namespace RockEditor {

    namespace {

        //--------------------------------------------------------------------
        //! ハンドルから ShaderAsset を引きます。
        //! @param  [in] assetManager アセットマネージャ
        //! @param  [in] handle       シェーダーアセットのハンドル
        //! @return ShaderAsset。見つからなければ nullptr
        //--------------------------------------------------------------------
        const Tsukino::Asset::ShaderAsset* GetShader(Tsukino::Asset::AssetManager& assetManager,
                                                     Tsukino::Asset::AssetHandle   handle) {
            auto asset = std::static_pointer_cast<Tsukino::Asset::ShaderAsset>(assetManager.Get(handle));
            return asset.get();
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! 後片付けします。
    //------------------------------------------------------------------------
    EngineBootstrap::~EngineBootstrap() {
        // 明示的に順序どおり落とす。unique_ptr の破棄順は宣言の逆なので
        // このまま任せてもよいが、CoUninitialize をその後に置く必要がある
        m_builtinAssets.reset();
        m_assetManager.reset();
        m_renderer.reset();
        m_window.reset();

        if(m_comInitialized) {
            CoUninitialize();
            m_comInitialized = false;
        }
    }

    //------------------------------------------------------------------------
    //! エンジンを起こします。
    //------------------------------------------------------------------------
    bool EngineBootstrap::Initialize(const std::string& title, int width, int height) {
        //--------------------------------------------------------------------
        // COM。WIC（DirectXTex のテクスチャ読み込み）が要求する
        //--------------------------------------------------------------------
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if(FAILED(hr)) {
            Tsukino::Core::Log::Error("Failed to initialize COM library.");
            return false;
        }
        m_comInitialized = true;

        //--------------------------------------------------------------------
        // アセット。BuiltInAssets がビルトインのシェーダーとフォントを読む
        //--------------------------------------------------------------------
        m_assetManager = std::make_unique<Tsukino::Asset::AssetManager>();
        m_assetManager->Initialize();

        m_builtinAssets = std::make_unique<Tsukino::BuiltIn::BuiltInAssets>();
        m_builtinAssets->Initialize(m_assetManager.get());

        //--------------------------------------------------------------------
        // ウィンドウ
        //--------------------------------------------------------------------
        m_window = std::make_unique<Tsukino::Core::Window>();
        if(!m_window->Create(title, width, height)) {
            Tsukino::Core::Log::Error("Failed to create the editor window.");
            return false;
        }

        //--------------------------------------------------------------------
        // レンダラ。
        //
        // SetShadowPipeline は呼ばない。Renderer::Initialize が shaderSet から
        // 内部で CreateShadowPipelines() を呼んで作るため不要
        // （エンジン内にも呼び出し元は1つも無い）
        //--------------------------------------------------------------------
        Tsukino::Renderer::RendererShaderSet shaderSet{};
        if(!BuildShaderSet(shaderSet)) {
            Tsukino::Core::Log::Error("Failed to resolve the built-in shaders. The engine asset root may be missing.");
            return false;
        }

        m_renderer = std::make_unique<Tsukino::Renderer::Renderer>();
        if(!m_renderer->Initialize(m_window->GetHWND(),
                                   static_cast<uint32_t>(m_window->GetWidth()),
                                   static_cast<uint32_t>(m_window->GetHeight()),
                                   shaderSet)) {
            Tsukino::Core::Log::Error("Failed to initialize the renderer.");
            return false;
        }

        // リサイズはレンダラへ転送する。これを繋がないとウィンドウを
        // 広げたときスワップチェインが付いてこない
        Tsukino::Renderer::Renderer* renderer = m_renderer.get();
        m_window->SetResizeCallback([renderer](int newWidth, int newHeight) {
            renderer->Resize(static_cast<uint32_t>(newWidth), static_cast<uint32_t>(newHeight));
        });

        SetupSky();

        Tsukino::Core::Log::Info("RockEditor: engine bootstrap finished.");
        return true;
    }

    //------------------------------------------------------------------------
    //! ビルトインシェーダーから RendererShaderSet を組み立てます。
    //------------------------------------------------------------------------
    bool EngineBootstrap::BuildShaderSet(Tsukino::Renderer::RendererShaderSet& outShaderSet) {
        Tsukino::Asset::AssetManager&         assets  = *m_assetManager;
        const Tsukino::BuiltIn::BuiltInShaders& shaders = m_builtinAssets->shaders;

        outShaderSet.debugVS    = GetShader(assets, shaders.debugVS);
        outShaderSet.debugPS    = GetShader(assets, shaders.debugPS);
        outShaderSet.tonemapVS  = GetShader(assets, shaders.tonemapVS);
        outShaderSet.tonemapPS  = GetShader(assets, shaders.tonemapPS);

        outShaderSet.shadowStaticVS = GetShader(assets, shaders.shadowStaticVS);

        // 罠: 受け側のフィールド名は shadowSkeletalVS だが、
        // BuiltInShaders 側の名前は shadowVS（ここだけ名前が揃っていない）
        outShaderSet.shadowSkeletalVS = GetShader(assets, shaders.shadowVS);
        outShaderSet.shadowPS         = GetShader(assets, shaders.shadowPS);

        outShaderSet.lightingPS   = GetShader(assets, shaders.lightingPS);
        outShaderSet.motionBlurPS = GetShader(assets, shaders.motionBlurPS);
        outShaderSet.fogPS        = GetShader(assets, shaders.fogPS);

        outShaderSet.ambientParticleVS = GetShader(assets, shaders.ambientParticleVS);
        outShaderSet.ambientParticlePS = GetShader(assets, shaders.ambientParticlePS);

        // 1本でも欠けていたらレンダラの初期化が通らない。ここで止めて
        // 「アセットが見つからない」と明示した方が原因にたどり着きやすい
        const void* required[] = {
            outShaderSet.debugVS,          outShaderSet.debugPS,
            outShaderSet.tonemapVS,        outShaderSet.tonemapPS,
            outShaderSet.shadowStaticVS,   outShaderSet.shadowSkeletalVS,
            outShaderSet.shadowPS,         outShaderSet.lightingPS,
            outShaderSet.motionBlurPS,     outShaderSet.fogPS,
            outShaderSet.ambientParticleVS, outShaderSet.ambientParticlePS,
        };
        for(const void* shader : required) {
            if(!shader) {
                return false;
            }
        }
        return true;
    }

    //------------------------------------------------------------------------
    //! 大気散乱の空を張ります。
    //------------------------------------------------------------------------
    void EngineBootstrap::SetupSky() {
        const Tsukino::Asset::ShaderAsset* skyVS = GetShader(*m_assetManager, m_builtinAssets->shaders.skyVS);
        const Tsukino::Asset::ShaderAsset* skyPS = GetShader(*m_assetManager, m_builtinAssets->shaders.skyPS);

        if(!skyVS || !skyPS) {
            // 空が無くても岩は見える。背景は SetClearColor の単色になる
            Tsukino::Core::Log::Warn("RockEditor: sky shaders are unavailable. Falling back to a flat background.");
            m_renderer->SetClearColor(0.08f, 0.09f, 0.11f, 1.0f);
            return;
        }

        m_renderer->SetSkyPipeline(skyVS, skyPS);
    }

}    // namespace RockEditor
