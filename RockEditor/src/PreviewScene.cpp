//----------------------------------------------------------------------------
//! @file   PreviewScene.cpp
//! @brief  岩のプレビュー描画の実装
//----------------------------------------------------------------------------
#include <RockEditor/PreviewScene.hpp>

#include <RockEditor/EngineBootstrap.hpp>

#include <Tsukino/Core/Log.hpp>
#include <Tsukino/Core/Math/MathHelper.hpp>
#include <Tsukino/Core/Math/Matrix.hpp>
#include <Tsukino/Engine/Asset/Shader/ShaderAsset.hpp>
#include <Tsukino/GraphicsCommon/Mesh/MeshData.hpp>
#include <Tsukino/GraphicsCommon/State/SamplerType.hpp>
#include <Tsukino/GraphicsCommon/Vertex/VertexPNUV.hpp>
#include <Tsukino/Renderer/ConstantBuffer.hpp>
#include <Tsukino/Renderer/DX11/PipelineFactory.hpp>
#include <Tsukino/Renderer/DrawCommand.hpp>
#include <Tsukino/Renderer/ShaderSlots.hpp>

#include <cmath>
#include <cstring>
#include <vector>

// 名前空間 RockEditor
namespace RockEditor {

    namespace {

        //--------------------------------------------------------------------
        //! 方位角と仰角から単位方向ベクトルを作ります。
        //! @param  [in] yawDeg   方位角（度）
        //! @param  [in] pitchDeg 仰角（度）
        //! @return 単位方向ベクトル
        //--------------------------------------------------------------------
        hlslpp::float3 DirectionFromAngles(float yawDeg, float pitchDeg) {
            const float yaw   = Tsukino::Core::Math::ToRadians(yawDeg);
            const float pitch = Tsukino::Core::Math::ToRadians(pitchDeg);

            const float cosPitch = std::cos(pitch);
            return hlslpp::float3(std::sin(yaw) * cosPitch, std::sin(pitch), std::cos(yaw) * cosPitch);
        }

        //--------------------------------------------------------------------
        //! RockCore のメッシュを GraphicsCommon の MeshData へ詰め替えます。
        //!
        //! 頂点レイアウトは VertexPNUV（position / normal / uv の 32 バイト）。
        //! これがエンジンの .tsm のレイアウトそのもので、ほかは受け付けません。
        //!
        //! @param  [in] mesh 元のメッシュ
        //! @return 詰め替えた MeshData
        //--------------------------------------------------------------------
        Tsukino::GraphicsCommon::MeshData ToEngineMeshData(const RockCore::MeshBuilder& mesh) {
            using Tsukino::GraphicsCommon::VertexPNUV;

            const size_t vertexCount = mesh.positions.size();

            std::vector<VertexPNUV> vertices(vertexCount);
            for(size_t i = 0; i < vertexCount; ++i) {
                const RockCore::Vec3& position = mesh.positions[i];
                vertices[i].position           = DirectX::XMFLOAT3(position.x, position.y, position.z);

                const RockCore::Vec3 normal = (i < mesh.normals.size()) ? mesh.normals[i] : RockCore::Vec3{0.0f, 1.0f, 0.0f};
                vertices[i].normal          = DirectX::XMFLOAT3(normal.x, normal.y, normal.z);

                const RockCore::Vec2 uv = (i < mesh.uvs.size()) ? mesh.uvs[i] : RockCore::Vec2{};
                vertices[i].uv          = DirectX::XMFLOAT2(uv.x, uv.y);
            }

            Tsukino::GraphicsCommon::MeshData meshData{};
            meshData.vertexStride = static_cast<Tsukino::u32>(sizeof(VertexPNUV));
            meshData.vertexCount  = static_cast<Tsukino::u32>(vertexCount);
            meshData.indexCount   = static_cast<Tsukino::u32>(mesh.indices.size());
            meshData.format       = Tsukino::GraphicsCommon::VertexFormat::PositionNormalUV;
            meshData.indices      = mesh.indices;

            meshData.vertexData.resize(vertices.size() * sizeof(VertexPNUV));
            if(!vertices.empty()) {
                std::memcpy(meshData.vertexData.data(), vertices.data(), meshData.vertexData.size());
            }

            RockCore::Vec3 boundsMin{};
            RockCore::Vec3 boundsMax{};
            mesh.ComputeBounds(boundsMin, boundsMax);
            meshData.bounds.min = hlslpp::float3(boundsMin.x, boundsMin.y, boundsMin.z);
            meshData.bounds.max = hlslpp::float3(boundsMax.x, boundsMax.y, boundsMax.z);

            return meshData;
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! パイプラインを用意します。
    //------------------------------------------------------------------------
    bool PreviewScene::Initialize(EngineBootstrap& engine) {
        Tsukino::Asset::AssetManager&          assets  = engine.GetAssetManager();
        const Tsukino::BuiltIn::BuiltInShaders& shaders = engine.GetBuiltInAssets().shaders;

        auto vertexShader = std::static_pointer_cast<Tsukino::Asset::ShaderAsset>(assets.Get(shaders.staticModelVS));
        auto pixelShader  = std::static_pointer_cast<Tsukino::Asset::ShaderAsset>(assets.Get(shaders.gbufferPS));

        if(!vertexShader || !pixelShader) {
            Tsukino::Core::Log::Error("PreviewScene: the GBuffer shaders are unavailable.");
            return false;
        }

        // 不透明なのでディファードの GBuffer パスへ積む。
        // ラスタライザは CULL_NONE なので巻き方向の心配は要らない
        m_pipeline = engine.GetRenderer().GetPipelineFactory()->Create(*vertexShader,
                                                                      *pixelShader,
                                                                      Tsukino::GraphicsCommon::VertexFormat::PositionNormalUV,
                                                                      Tsukino::Renderer::DepthMode::ReadWrite,
                                                                      Tsukino::Renderer::BlendMode::Opaque);
        if(!m_pipeline) {
            Tsukino::Core::Log::Error("PreviewScene: failed to create the GBuffer pipeline.");
            return false;
        }

        return true;
    }

    //------------------------------------------------------------------------
    //! メッシュを GPU へ載せ直します。
    //------------------------------------------------------------------------
    void PreviewScene::UploadMesh(ID3D11Device* device, const RockCore::MeshBuilder& mesh) {
        if(!device || mesh.indices.empty()) {
            return;
        }

        const Tsukino::GraphicsCommon::MeshData meshData = ToEngineMeshData(mesh);

        // 古いバッファは MeshBuffer の ComPtr が上書きで手放す
        m_meshBuffer = Tsukino::Renderer::CreateMeshBuffer(device, meshData);
    }

    //------------------------------------------------------------------------
    //! Normal マップを GPU へ載せ直します。
    //------------------------------------------------------------------------
    void PreviewScene::UploadNormalMap(ID3D11Device* device, const RockCore::ImageBuffer& image) {
        if(!device || !image.IsValid()) {
            return;
        }

        const int size = image.GetWidth();

        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(context.GetAddressOf());
        if(!context) {
            return;
        }

        //--------------------------------------------------------------------
        // 大きさが同じなら作り直さず、中身だけ差し替える。
        // ベイクし直すたびにテクスチャを作り直すとドライバ側の確保が積む
        //--------------------------------------------------------------------
        if(!m_normalMapTexture || m_normalMapSize != size) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width     = static_cast<UINT>(size);
            desc.Height    = static_cast<UINT>(image.GetHeight());
            desc.ArraySize = 1;

            // ミップを持たせる。焼いた法線はテクセル1つぶんまで細かい成分を
            // 含むので、ミップ無しだと岩を小さく映したときや斜めから見たときに
            // 激しくちらつく（エンジンの埋め込みテクスチャ経路も必ずミップを作る）
            desc.MipLevels = 0;    // 0 = フルチェーンを作らせる

            // 【リニアのまま貼る】sRGB にすると法線が二重ガンマで壊れる。
            // ディスクを経由しないのは、エンジンの単体テクスチャ経路が
            // WIC_FLAGS_FORCE_SRGB で無条件に sRGB 化してしまうため
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

            desc.SampleDesc.Count = 1;
            desc.Usage            = D3D11_USAGE_DEFAULT;

            // GenerateMips は下位ミップへ描き込むので RENDER_TARGET が要る
            desc.BindFlags      = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            desc.MiscFlags      = D3D11_RESOURCE_MISC_GENERATE_MIPS;

            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            if(FAILED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf()))) {
                Tsukino::Core::Log::Error("PreviewScene: failed to create the normal map texture.");
                return;
            }

            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
            if(FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf()))) {
                Tsukino::Core::Log::Error("PreviewScene: failed to create the normal map SRV.");
                return;
            }

            m_normalMapTexture = texture;
            m_normalMapSrv     = srv;
            m_normalMapSize    = size;
        }

        // 最上位ミップだけ書き込み、残りは GPU に作らせる
        context->UpdateSubresource(m_normalMapTexture.Get(),
                                   0,
                                   nullptr,
                                   image.GetPixels().data(),
                                   static_cast<UINT>(size) * 4u,
                                   0);
        context->GenerateMips(m_normalMapSrv.Get());
    }

    //------------------------------------------------------------------------
    //! カメラとライトをレンダラへ設定します。
    //------------------------------------------------------------------------
    void PreviewScene::SubmitCameraAndLight(EngineBootstrap& engine, const PreviewViewSettings& view, float boundingRadius) {
        Tsukino::Renderer::Renderer& renderer = engine.GetRenderer();
        Tsukino::Core::Window&       window   = engine.GetWindow();

        const float width  = static_cast<float>(window.GetWidth());
        const float height = static_cast<float>(window.GetHeight());
        if(width <= 0.0f || height <= 0.0f) {
            return;
        }

        const float  distance = boundingRadius * view.cameraDistance;
        const hlslpp::float3 eye = DirectionFromAngles(view.cameraYawDeg, view.cameraPitchDeg) * distance;
        const hlslpp::float3 target(0.0f, 0.0f, 0.0f);
        const hlslpp::float3 up(0.0f, 1.0f, 0.0f);

        const float nearZ = std::max(boundingRadius * 0.05f, 0.01f);
        const float farZ  = distance + boundingRadius * 8.0f;

        Tsukino::Renderer::CBufferScene scene{};
        scene.view = Tsukino::Core::Math::matrix::lookAtLH(eye, target, up);

        // 【リバースZ】far を先、near を後に渡す。通常の順で渡すと何も映らない
        // （CameraSystem.cpp:90-91 と同じ呼び方）
        scene.projection = Tsukino::Core::Math::matrix::perspectiveFovLH(Tsukino::Core::Math::ToRadians(view.fovDeg),
                                                                        width / height,
                                                                        farZ,
                                                                        nearZ);

        scene.viewProj    = hlslpp::mul(scene.view, scene.projection);
        scene.invViewProj = hlslpp::inverse(scene.viewProj);
        scene.cameraPos   = hlslpp::float4(eye.x, eye.y, eye.z, 1.0f);

        renderer.SetWorldCameraMatrix(scene);

        // Overlay（ImGui）は 2D の直交投影を見る
        Tsukino::Renderer::CBufferScene overlay{};
        overlay.view       = Tsukino::Core::Math::matrix::identity();
        overlay.projection = Tsukino::Core::Math::matrix::orthographicOffCenterLH(0.0f, width, 0.0f, height, 0.0f, 1.0f);
        overlay.viewProj   = hlslpp::mul(overlay.view, overlay.projection);
        overlay.invViewProj = hlslpp::inverse(overlay.viewProj);
        renderer.SetOverlayCameraMatrix(overlay);

        //--------------------------------------------------------------------
        // ライトは必ずカメラより後。シャドウの投影範囲がカメラ位置を
        // 中心に決まるため（Renderer.hpp:312-316）
        //--------------------------------------------------------------------
        const hlslpp::float3 sunDirection   = DirectionFromAngles(view.lightYawDeg, view.lightPitchDeg);
        const hlslpp::float3 lightDirection = hlslpp::normalize(-sunDirection);
        renderer.SetDirectionalLight(lightDirection, hlslpp::float3(1.0f, 0.97f, 0.92f), view.lightIntensity);

        //--------------------------------------------------------------------
        // 大気散乱のパラメータ。SetSkyPipeline だけでは定数バッファが 0 のままで
        // 空が真っ黒になるので、毎フレーム太陽方向と一緒に送る。
        // 値は SkyAtmosphereComponent の既定値と同じ
        //--------------------------------------------------------------------
        Tsukino::Renderer::CBufferSky sky{};
        sky.rayleighScattering = 1.0f;
        sky.mieScattering      = 0.1f;
        sky.mieAnisotropy      = 0.76f;
        sky.sunIntensity       = 20.0f;
        sky.atmosphereHeight   = 8000.0f;
        sky.planetRadius       = 6371000.0f;
        sky.sunDiskSize        = 0.02f;
        sky.padding0           = 0.0f;
        sky.groundColor        = hlslpp::float4(0.1f, 0.08f, 0.05f, 0.0f);

        // 太陽方向はライトの進行方向の逆（SkyAtmosphereSystem.cpp:56 と同じ）
        sky.sunDirection = hlslpp::float4(sunDirection.x, sunDirection.y, sunDirection.z, 0.0f);

        renderer.SetSkyParameters(sky);
    }

    //------------------------------------------------------------------------
    //! 岩の描画コマンドを積みます。
    //------------------------------------------------------------------------
    void PreviewScene::SubmitRock(Tsukino::Renderer::Renderer& renderer, const RockCore::RockParams& params) {
        if(!m_pipeline || !HasMesh()) {
            return;
        }

        using Tsukino::Renderer::SRVSlot;

        // null の SRV はサンプル結果が 0 になる（Material.hpp:14-17）。
        // 5スロット全部を必ず埋める
        ID3D11ShaderResourceView* white      = renderer.GetWhiteTextureSRV();
        ID3D11ShaderResourceView* flatNormal = renderer.GetFlatNormalTextureSRV();

        const bool useBakedNormal = m_normalMapVisible && m_normalMapSrv;

        Tsukino::Renderer::Material& material = renderer.AllocMaterial();
        material.SetPipeline(m_pipeline.get());
        material.SetSampler(renderer.GetSampler(Tsukino::GraphicsCommon::SamplerType::AnisotropicWrap));
        material.SetTexture(SRVSlot::Albedo, white);
        material.SetTexture(SRVSlot::Normal, useBakedNormal ? m_normalMapSrv.Get() : flatNormal);
        material.SetTexture(SRVSlot::MetallicRoughness, white);
        material.SetTexture(SRVSlot::Emissive, white);
        material.SetTexture(SRVSlot::AO, white);

        // テクスチャ値は cbuffer 定数との乗算なので、MR を貼らない Phase 1 では
        // ここの metallic / roughness がそのまま効く
        Tsukino::Renderer::CBufferMaterial& materialData = renderer.AllocMaterialData();
        materialData.baseColor  = hlslpp::float4(params.baseColor.x, params.baseColor.y, params.baseColor.z, 1.0f);
        materialData.emissive   = hlslpp::float3(0.0f, 0.0f, 0.0f);
        materialData.metallic   = params.metallic;
        materialData.roughness  = params.roughness;
        materialData.specular   = 0.5f;
        materialData.rimColor   = hlslpp::float4(0.0f, 0.0f, 0.0f, 0.0f);
        materialData.rimParams  = hlslpp::float4(1.0f, 0.0f, 0.0f, 0.0f);

        Tsukino::Renderer::DrawCommand command{};
        command.mesh         = &m_meshBuffer;
        command.transform    = Tsukino::Core::Math::matrix::identity();
        command.material     = &material;
        command.materialData = &materialData;
        command.pass         = Tsukino::Renderer::RenderPass::GBuffer;

        renderer.PushDrawCommand(command);
    }

}    // namespace RockEditor
