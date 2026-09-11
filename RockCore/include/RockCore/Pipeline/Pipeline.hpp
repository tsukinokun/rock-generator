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
//!                     ├→ [BakeAo]     1-10s        （Phase 5）
//!                     └→ [BakeColor]  50-200ms     （Phase 4）
//!
//!         各段は「自分が依存するパラメータのハッシュ」を持ちます。上流の
//!         ハッシュを自分のハッシュへ混ぜているので、無効化の伝播を
//!         手で書く必要がありません（書くと必ずどこかで漏れる）。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Bake/ImageBuffer.hpp>
#include <RockCore/Bake/NormalBaker.hpp>
#include <RockCore/Bake/UvRasterizer.hpp>
#include <RockCore/Mesh/MeshBuilder.hpp>
#include <RockCore/Pipeline/CancelToken.hpp>
#include <RockCore/Shape/RockField.hpp>
#include <RockCore/Shape/RockMesher.hpp>
#include <RockCore/Shape/RockParams.hpp>

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

        //! メッシュを返します。
        //! @return メッシュ
        const MeshBuilder& GetMesh() const { return m_mesh; }

        //! メッシュ生成の数値を返します。
        //! @return メッシュ生成の数値
        const RockMeshStats& GetMeshStats() const { return m_meshStats; }

        //! Normal マップを返します。
        //! @return Normal マップ
        const ImageBuffer& GetNormalMap() const { return m_normalMap; }

        //! Normal ベイクの数値を返します。
        //! @return Normal ベイクの数値
        const NormalBakeStats& GetNormalBakeStats() const { return m_normalStats; }

        //! UV 空間のラスタライズ結果を返します。
        //! @return ラスタライズ結果
        const BakeGBuffer& GetBakeGBuffer() const { return m_gbuffer; }

        //! メッシュが更新された回数を返します。GPU バッファの作り直し判定に使います。
        //! @return 更新回数
        u32 GetMeshRevision() const { return m_meshRevision; }

        //! Normal マップが更新された回数を返します。SRV の作り直し判定に使います。
        //! @return 更新回数
        u32 GetNormalMapRevision() const { return m_normalMapRevision; }

        //! 岩が収まる球の半径を返します。カメラ距離の基準に使います。
        //! @return 外接半径。まだ場が無ければ params の半径を返す
        float GetBoundingRadius() const;

    private:
        //! 各段が依存するパラメータのハッシュを計算し直します。
        void RecomputeHashes();

        RockParams m_params{};

        PipelineStage m_targetStage = PipelineStage::BakeNormal;

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

        MeshBuilder   m_mesh{};
        RockMeshStats m_meshStats{};

        BakeGBuffer     m_gbuffer{};
        ImageBuffer     m_normalMap{};
        NormalBakeStats m_normalStats{};

        u32 m_meshRevision      = 0;
        u32 m_normalMapRevision = 0;
    };

}    // namespace RockCore
