//----------------------------------------------------------------------------
//! @file   Pipeline.hpp
//! @brief  段階ごとのダーティ判定付きの生成パイプライン
//! @detail 色スライダを動かしたときに UV 展開と AO が再実行されないこと、
//!         これが体感速度をほぼ決めます。後付けが面倒なので最初から入れてあります。
//!
//!             Params ─┬→ [Field]      ~0ms
//!                     ├→ [Mesh]       50-300ms
//!                     ├→ [Unwrap]     0.5-3s
//!                     ├→ [BakeNormal] 0.3-2s
//!                     ├→ [BakeColor]  0.2-1s
//!                     └→ [BakeAo]     5-60s        ★ここが一番重い
//!
//!         各段は「自分が依存するパラメータのハッシュ」を持ちます。上流の
//!         ハッシュを自分のハッシュへ混ぜているので、無効化の伝播を
//!         手で書く必要がありません（書くと必ずどこかで漏れる）。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Bake/AoBaker.hpp>
#include <RockCore/Bake/ImageBuffer.hpp>
#include <RockCore/Bake/NormalBaker.hpp>
#include <RockCore/Bake/SurfaceBaker.hpp>
#include <RockCore/Bake/UvRasterizer.hpp>
#include <RockCore/Mesh/MeshBuilder.hpp>
#include <RockCore/Pipeline/CancelToken.hpp>
#include <RockCore/Shape/RockField.hpp>
#include <RockCore/Shape/RockMesher.hpp>
#include <RockCore/Shape/RockParams.hpp>
#include <RockCore/Unwrap/IUnwrapper.hpp>

#include <RockCore/Types.hpp>

#include <array>
#include <memory>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @enum  PipelineStage
    //! パイプラインの段階。上流から順に並んでいる
    //------------------------------------------------------------------------
    enum class PipelineStage : u32 {
        Field = 0,     //!< 半径関数（低帯域と全帯域の2つ）
        Mesh,          //!< 頂点と三角形
        Unwrap,        //!< UV 展開と UV 空間のラスタライズ
        BakeNormal,    //!< Normal マップ
        BakeColor,     //!< Albedo と MetallicRoughness
        BakeAo,        //!< AO
        Count,
    };

    //! 段階の総数
    constexpr u32 kPipelineStageCount = static_cast<u32>(PipelineStage::Count);

    //! 段階の表示名を返します。
    //! @param  [in] stage 段階
    //! @return 表示名
    const char* GetPipelineStageName(PipelineStage stage);

    //------------------------------------------------------------------------
    //! @struct PipelineStageStatus
    //! 1段ぶんの状態。StatsPanel がそのまま表示する
    //------------------------------------------------------------------------
    struct PipelineStageStatus {
        bool  dirty        = true;
        float milliseconds = 0.0f;
        u32   runCount     = 0;    //!< 実行された回数。「再実行されていない」ことの確認に使う
    };

    //------------------------------------------------------------------------
    //! @class Pipeline
    //! 岩の生成手順をまとめたもの
    //------------------------------------------------------------------------
    class Pipeline {
    public:
        Pipeline();
        ~Pipeline();

        Pipeline(const Pipeline&)            = delete;
        Pipeline& operator=(const Pipeline&) = delete;

        //! パラメータを参照します。
        //! @return 現在のパラメータ
        const RockParams& GetParams() const { return m_params; }

        //! パラメータを書き換えられる形で参照します。
        //!
        //! 書き換えた後に Update を呼べば、差分のある段だけが走ります
        //! （ハッシュ比較なので変更通知は要りません）。
        //!
        //! @return 現在のパラメータ
        RockParams& GetMutableParams() { return m_params; }

        //! どの段まで実行するかを指定します。
        //! @param [in] stage 実行したい最終段
        void SetTargetStage(PipelineStage stage) { m_targetStage = stage; }

        //------------------------------------------------------------------
        //! 対話中かどうかを伝えます。
        //!
        //! true の間は UV 展開を八面体射影へ強制します。xatlas は数百ms〜
        //! 数秒かかるので、スライダを掴んでいる間に走らせると形の更新が
        //! そこで待たされて操作感が死ぬためです。
        //!
        //! これはパラメータではありませんが、ハッシュには「実際に使う方式」を
        //! 混ぜてあります。そうしないとスライダを離したときに
        //! 「Unwrap は完了済み」と判断されて xatlas が永遠に走りません。
        //!
        //! @param [in] fast 対話中なら true
        //------------------------------------------------------------------
        void SetFastPreview(bool fast) { m_fastPreview = fast; }

        //------------------------------------------------------------------
        //! 差分のある段を順に実行します。
        //!
        //! 中断された段はハッシュを記録しないので、次の呼び出しでやり直します。
        //!
        //! @param  [in] progress 進捗の通知先。空でもよい
        //! @param  [in] cancel   中断フラグ。nullptr でもよい
        //! @return 目標の段まで完走したら true
        //------------------------------------------------------------------
        bool Update(const ProgressCallback& progress, const CancelToken* cancel);

        //! 全段を無効化します。シード変更などで作り直したいときに使います。
        void InvalidateAll();

        //------------------------------------------------------------------
        //! 目標の段まで実行し終えているかを返します。
        //!
        //! 現在のパラメータからハッシュを取り直すので const ではありません。
        //! 呼び出し側はこれを見て「投げるべきか」を判断します。
        //!
        //! @return 何も走らせる必要が無ければ true
        //------------------------------------------------------------------
        bool IsUpToDate();

        //! 段の状態を返します。
        //! @param  [in] stage 段階
        //! @return 段の状態
        const PipelineStageStatus& GetStageStatus(PipelineStage stage) const {
            return m_status[static_cast<size_t>(stage)];
        }

        //! メッシュを返します。UV 展開とハードエッジ化まで済んだもの。
        //! @return メッシュ
        const MeshBuilder& GetMesh() const { return m_mesh; }

        //------------------------------------------------------------------
        //! UV 展開前のメッシュを返します。
        //!
        //! 破断面のクリップが位相を壊していないかを調べるのはこちら。
        //! GetMesh() の方はシームとハードエッジで頂点が複製されているので、
        //! 「頂点番号で多様体か」を見たいときは必ずこちらを使います。
        //!
        //! @return UV 展開前のメッシュ
        //------------------------------------------------------------------
        const MeshBuilder& GetBaseMesh() const { return m_baseMesh; }

        //! メッシュ生成の数値を返します。
        //! @return メッシュ生成の数値
        const RockMeshStats& GetMeshStats() const { return m_meshStats; }

        //! Normal マップを返します。
        //! @return Normal マップ
        const ImageBuffer& GetNormalMap() const { return m_normalMap; }

        //! Normal ベイクの数値を返します。
        //! @return Normal ベイクの数値
        const NormalBakeStats& GetNormalBakeStats() const { return m_normalStats; }

        //! Albedo マップを返します。バイト列は sRGB で符号化されています。
        //! @return Albedo マップ
        const ImageBuffer& GetAlbedoMap() const { return m_albedoMap; }

        //! MetallicRoughness マップを返します。G=ラフネス、B=メタリック。
        //! @return MetallicRoughness マップ
        const ImageBuffer& GetMetallicRoughnessMap() const { return m_metallicRoughnessMap; }

        //! Albedo / MR ベイクの数値を返します。
        //! @return ベイクの数値
        const SurfaceBakeStats& GetSurfaceBakeStats() const { return m_surfaceStats; }

        //! AO マップを返します。R チャンネルだけを使います。
        //! @return AO マップ
        const ImageBuffer& GetAoMap() const { return m_aoMap; }

        //! AO ベイクの数値を返します。
        //! @return ベイクの数値
        const AoBakeStats& GetAoBakeStats() const { return m_aoStats; }

        //! UV 展開の数値を返します。
        //! @return UV 展開の数値
        const UnwrapStats& GetUnwrapStats() const { return m_unwrapStats; }

        //! UV 空間のラスタライズ結果を返します。
        //! @return ラスタライズ結果
        const BakeGBuffer& GetBakeGBuffer() const { return m_gbuffer; }

        //! メッシュが更新された回数を返します。GPU バッファの作り直し判定に使います。
        //! @return 更新回数
        u32 GetMeshRevision() const { return m_meshRevision; }

        //! Normal マップが更新された回数を返します。SRV の作り直し判定に使います。
        //! @return 更新回数
        u32 GetNormalMapRevision() const { return m_normalMapRevision; }

        //! Albedo / MR が更新された回数を返します。2枚は必ず同時に焼けるので1つで足ります。
        //! @return 更新回数
        u32 GetSurfaceMapRevision() const { return m_surfaceMapRevision; }

        //! AO マップが更新された回数を返します。
        //! @return 更新回数
        u32 GetAoMapRevision() const { return m_aoMapRevision; }

        //! 岩が収まる球の半径を返します。カメラ距離の基準に使います。
        //! @return 外接半径。まだ場が無ければ params の半径を返す
        float GetBoundingRadius() const;

    private:
        //! 各段が依存するパラメータのハッシュを計算し直します。
        void RecomputeHashes();

        //! 実際に使う UV 展開の方式を返します。対話中は八面体射影へ落とします。
        //! @return 展開方式
        UnwrapMethod GetEffectiveUnwrapMethod() const;

        //------------------------------------------------------------------
        //! UV 展開とハードエッジ化を行います。
        //!
        //! @param  [in] cancel 中断フラグ。nullptr でもよい
        //! @return 中断されずに終わったら true
        //------------------------------------------------------------------
        bool RunUnwrap(const CancelToken* cancel);

        RockParams m_params{};

        PipelineStage m_targetStage = PipelineStage::BakeAo;

        bool m_fastPreview = false;

        std::array<PipelineStageStatus, kPipelineStageCount> m_status{};

        //! 今のパラメータから求めたハッシュ
        std::array<u64, kPipelineStageCount> m_currentHash{};

        //! 最後に完走した時点のハッシュ。違っていればその段は dirty
        std::array<u64, kPipelineStageCount> m_completedHash{};

        //! @note 【この宣言順は破棄順序の設計であり、並べ替えてはならない】
        //!       m_mesh / m_gbuffer は m_lowField から作られた値を持つだけで
        //!       参照は保持しないが、読み手が依存の向きを誤解しないよう
        //!       上流（場）を先に宣言している
        std::unique_ptr<RockField> m_lowField;
        std::unique_ptr<RockField> m_fullField;

        //--------------------------------------------------------------------
        //! Mesh 段の出力。UV も無く、法線も稜線で分割されていない素の形。
        //!
        //! これを別に持っているのは、Unwrap 段が必ず「展開前の状態」から
        //! 始められるようにするため。展開もハードエッジ化も頂点を複製するので、
        //! 同じメッシュへ二度かけると頂点が倍々に増えていく。
        //! テクスチャサイズだけを変えたときは Mesh 段が走らないので、
        //! 写しが無いと実際にそうなる
        //--------------------------------------------------------------------
        MeshBuilder m_baseMesh{};

        MeshBuilder   m_mesh{};
        RockMeshStats m_meshStats{};
        UnwrapStats   m_unwrapStats{};

        BakeGBuffer     m_gbuffer{};
        ImageBuffer     m_normalMap{};
        NormalBakeStats m_normalStats{};

        ImageBuffer      m_albedoMap{};
        ImageBuffer      m_metallicRoughnessMap{};
        SurfaceBakeStats m_surfaceStats{};

        //--------------------------------------------------------------------
        //! AO 専用の G-Buffer。
        //!
        //! AO だけ解像度を落として焼くので、他のマップとは別に持つ。
        //! 倍率が 1 のときも共有せず作り直す。共有すると「倍率を 1 へ
        //! 戻したときだけ経路が変わる」という分岐が増え、そこが必ず腐る
        //--------------------------------------------------------------------
        BakeGBuffer m_aoGBuffer{};

        ImageBuffer m_aoMap{};
        AoBakeStats m_aoStats{};

        u32 m_meshRevision       = 0;
        u32 m_normalMapRevision  = 0;
        u32 m_surfaceMapRevision = 0;
        u32 m_aoMapRevision      = 0;
    };

}    // namespace RockCore
