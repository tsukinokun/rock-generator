//----------------------------------------------------------------------------
//! @file   PreviewScene.hpp
//! @brief  岩のプレビュー描画
//! @detail 必ずエンジンの PBR.hlsli 経路を通します。ddx/ddy から TBN を
//!         再構成する実物が見えるので、これがそのまま Normal マップ規約の
//!         回帰テストになります（自前でプレビュー用シェーダーを書いたら
//!         その検証価値が消えます）。
//!
//!         ベイク結果はディスクを経由せず、Renderer::GetDevice() から直接
//!         SRV を作って貼ります。エンジンの単体テクスチャ経路
//!         （TextureImporter.cpp:38-45）は WIC_FLAGS_FORCE_SRGB で無条件に
//!         sRGB 扱いにするため、PNG を経由すると法線が二重ガンマで壊れます。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Bake/ImageBuffer.hpp>
#include <RockCore/Mesh/MeshBuilder.hpp>
#include <RockCore/Shape/RockParams.hpp>

#include <Tsukino/Renderer/DX11/MeshBuffer.hpp>
#include <Tsukino/Renderer/DX11/PipelineState.hpp>
#include <Tsukino/Renderer/Renderer.hpp>

#include <d3d11.h>
#include <wrl/client.h>

#include <memory>

// 名前空間 RockEditor
namespace RockEditor {

    class EngineBootstrap;    // 前方宣言

    //------------------------------------------------------------------------
    //! @struct PreviewViewSettings
    //! カメラとライトの向き（ViewPanel が触る）
    //------------------------------------------------------------------------
    struct PreviewViewSettings {
        float cameraYawDeg   = 35.0f;
        float cameraPitchDeg = 18.0f;
        float cameraDistance = 2.7f;    //!< 岩の外接半径に対する倍率
        float fovDeg         = 45.0f;

        //! ライトの向き（degree）。Phase 2 の検証でここを回す。
        //! 既定はカメラ（yaw 35）の少し右上から当てて、起動直後の絵で
        //! 凹凸が読めるようにしてある
        float lightYawDeg   = 55.0f;
        float lightPitchDeg = 45.0f;

        float lightIntensity = 3.0f;

        //! ワイヤフレームを重ねる（細分割の確認用）
        bool drawWireframe = false;
    };

    //------------------------------------------------------------------------
    //! @class PreviewScene
    //! GPU 側のプレビュー資源を持つもの
    //------------------------------------------------------------------------
    class PreviewScene {
    public:
        PreviewScene() = default;
        ~PreviewScene() = default;

        PreviewScene(const PreviewScene&)            = delete;
        PreviewScene& operator=(const PreviewScene&) = delete;

        //! パイプラインを用意します。
        //! @param  [in] engine エンジン
        //! @return 成功したら true
        bool Initialize(EngineBootstrap& engine);

        //------------------------------------------------------------------
        //! メッシュを GPU へ載せ直します。
        //!
        //! 毎フレームではなく、Pipeline のメッシュリビジョンが変わったときだけ
        //! 呼びます（頂点バッファの作り直しは安くない）。
        //!
        //! @param [in] device デバイス
        //! @param [in] mesh   載せるメッシュ
        //------------------------------------------------------------------
        void UploadMesh(ID3D11Device* device, const RockCore::MeshBuilder& mesh);

        //------------------------------------------------------------------
        //! Normal マップを GPU へ載せ直します。
        //!
        //! DXGI_FORMAT_R8G8B8A8_UNORM（リニア）で作ります。sRGB にすると
        //! 法線が二重ガンマで壊れます。
        //!
        //! @param [in] device デバイス
        //! @param [in] image  載せる画像
        //------------------------------------------------------------------
        void UploadNormalMap(ID3D11Device* device, const RockCore::ImageBuffer& image);

        //------------------------------------------------------------------
        //! Albedo と MetallicRoughness を GPU へ載せ直します。
        //!
        //! **Albedo だけ sRGB の SRV で作ります。** ベイク側が sRGB で
        //! 符号化して書いているので、ここをリニアにすると色が持ち上がって
        //! 白っぽくなります。MR はリニアのままです（ラフネスに
        //! ガンマを掛けたら意味が変わる）。
        //!
        //! 2 枚は必ず同時に焼けるので、載せるのも 1 回で受けます。
        //!
        //! @param [in] device             デバイス
        //! @param [in] albedo             Albedo（sRGB 符号化済み）
        //! @param [in] metallicRoughness  MetallicRoughness（リニア）
        //------------------------------------------------------------------
        void UploadSurfaceMaps(ID3D11Device*                device,
                               const RockCore::ImageBuffer& albedo,
                               const RockCore::ImageBuffer& metallicRoughness);

        //------------------------------------------------------------------
        //! AO を GPU へ載せ直します。リニアのままです。
        //! @param [in] device デバイス
        //! @param [in] image  載せる画像
        //------------------------------------------------------------------
        void UploadAoMap(ID3D11Device* device, const RockCore::ImageBuffer& image);

        //------------------------------------------------------------------
        //! カメラとライトをレンダラへ設定します。
        //!
        //! 呼ぶ順序に意味があります。SetDirectionalLight は
        //! SetWorldCameraMatrix より後でなければなりません
        //! （シャドウの投影範囲がカメラ位置を中心に決まるため）。
        //!
        //! @param [in] engine          エンジン
        //! @param [in] view            カメラとライトの設定
        //! @param [in] boundingRadius  岩の外接半径
        //------------------------------------------------------------------
        void SubmitCameraAndLight(EngineBootstrap& engine, const PreviewViewSettings& view, float boundingRadius);

        //------------------------------------------------------------------
        //! 岩の描画コマンドを積みます。
        //!
        //! Material と CBufferMaterial は毎フレーム Alloc し直します
        //! （Renderer::Render() の末尾でキューごと Clear されるため、
        //! フレームを跨いでポインタを持てません）。
        //!
        //! @param [in] renderer レンダラ
        //! @param [in] params   マテリアルに使うパラメータ
        //------------------------------------------------------------------
        void SubmitRock(Tsukino::Renderer::Renderer& renderer, const RockCore::RockParams& params);

        //! メッシュを載せてあるかを返します。
        //! @return 載せてあれば true
        bool HasMesh() const { return m_meshBuffer.indexCount > 0; }

        //------------------------------------------------------------------
        //! 焼いたマップを貼るかを切り替えます。
        //!
        //! 形を動かしている最中は、前の形で焼いたマップが残っていて実際の
        //! 凹凸と噛み合いません。そのまま貼ると法線が砂嵐のように見え、
        //! 窪みの汚れも見当違いの場所に出ます。その間は平坦な法線と
        //! cbuffer の単色へ落とします。
        //!
        //! @param [in] visible 貼るなら true
        //------------------------------------------------------------------
        void SetBakedMapsVisible(bool visible) { m_bakedMapsVisible = visible; }

    private:
        //------------------------------------------------------------------
        //! @struct BakedTexture
        //! ベイク結果1枚ぶんの GPU 資源
        //------------------------------------------------------------------
        struct BakedTexture {
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
            Microsoft::WRL::ComPtr<ID3D11Texture2D>          texture;
            int                                              size = 0;
        };

        //------------------------------------------------------------------
        //! ベイク結果を GPU へ載せます。
        //!
        //! @param [in]     device デバイス
        //! @param [in]     image  載せる画像
        //! @param [in]     srgb   sRGB の SRV で作るなら true
        //! @param [in,out] target 載せ先
        //------------------------------------------------------------------
        void UploadBakedTexture(ID3D11Device* device, const RockCore::ImageBuffer& image, bool srgb, BakedTexture& target);

        //! GBuffer パス用のパイプライン。メッシュが変わっても作り直さない
        std::shared_ptr<Tsukino::Renderer::PipelineState> m_pipeline;

        Tsukino::Renderer::MeshBuffer m_meshBuffer{};

        BakedTexture m_normalMap{};
        BakedTexture m_albedoMap{};
        BakedTexture m_metallicRoughnessMap{};
        BakedTexture m_aoMap{};

        bool m_bakedMapsVisible = true;
    };

}    // namespace RockEditor
