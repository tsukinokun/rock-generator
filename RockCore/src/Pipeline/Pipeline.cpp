//----------------------------------------------------------------------------
//! @file   Pipeline.cpp
//! @brief  段階ごとのダーティ判定付き生成パイプラインの実装
//----------------------------------------------------------------------------
#include <RockCore/Pipeline/Pipeline.hpp>

#include <RockCore/Bake/Dilate.hpp>
#include <RockCore/Mesh/HardEdgeSplit.hpp>
#include <RockCore/Unwrap/OctahedralUnwrapper.hpp>
#include <RockCore/Unwrap/XAtlasUnwrapper.hpp>

#include <chrono>
#include <type_traits>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //--------------------------------------------------------------------
        //! @class Hasher
        //! パラメータをバイト列として潰して 64bit へ混ぜるもの
        //!
        //! 正確な衝突耐性は要らない。必要なのは「同じパラメータなら同じ値」
        //! 「1ビット違えば別の値」だけなので FNV-1a で足りる
        //--------------------------------------------------------------------
        class Hasher {
        public:
            //! 生のバイト列を混ぜます。
            //! @param [in] data  先頭アドレス
            //! @param [in] bytes バイト数
            void Raw(const void* data, size_t bytes) {
                const auto* cursor = static_cast<const unsigned char*>(data);
                for(size_t i = 0; i < bytes; ++i) {
                    m_value ^= static_cast<u64>(cursor[i]);
                    m_value *= 0x100000001B3ULL;
                }
            }

            //! 値を混ぜます。浮動小数もビット列として扱います。
            //! @param [in] value 混ぜる値
            template <class T>
            void Mix(const T& value) {
                static_assert(std::is_trivially_copyable_v<T>, "Hasher::Mix は trivially copyable な型だけを受ける");
                Raw(&value, sizeof(T));
            }

            //! ハッシュ値を返します。
            //! @return ハッシュ値
            u64 Get() const { return m_value; }

        private:
            u64 m_value = 0xCBF29CE484222325ULL;
        };

        //--------------------------------------------------------------------
        //! ノイズ層のパラメータを混ぜます。
        //!
        //! 構造体をまるごと Raw で潰すとパディングの不定値まで拾うため、
        //! フィールドを1つずつ混ぜる。
        //!
        //! @param [in,out] hasher 混ぜ先
        //! @param [in]     layers ノイズ層
        //--------------------------------------------------------------------
        void MixNoiseLayers(Hasher& hasher, const std::vector<NoiseLayerParams>& layers) {
            hasher.Mix(layers.size());
            for(const NoiseLayerParams& layer : layers) {
                hasher.Mix(layer.enabled);
                hasher.Mix(layer.kind);
                hasher.Mix(layer.amplitude);
                hasher.Mix(layer.sharpness);
                hasher.Mix(layer.fbm.frequency);
                hasher.Mix(layer.fbm.octaves);
                hasher.Mix(layer.fbm.lacunarity);
                hasher.Mix(layer.fbm.gain);
            }
        }

        //--------------------------------------------------------------------
        //! 現在時刻を取ります。
        //! @return 現在時刻
        //--------------------------------------------------------------------
        std::chrono::steady_clock::time_point Now() { return std::chrono::steady_clock::now(); }

        //--------------------------------------------------------------------
        //! 経過ミリ秒を求めます。
        //! @param  [in] begin 計測開始時刻
        //! @return 経過ミリ秒
        //--------------------------------------------------------------------
        float ElapsedMilliseconds(const std::chrono::steady_clock::time_point& begin) {
            const std::chrono::duration<float, std::milli> elapsed = Now() - begin;
            return elapsed.count();
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! 段階の表示名を返します。
    //------------------------------------------------------------------------
    const char* GetPipelineStageName(PipelineStage stage) {
        switch(stage) {
        case PipelineStage::Field:      return "Field";
        case PipelineStage::Mesh:       return "Mesh";
        case PipelineStage::Unwrap:     return "Unwrap";
        case PipelineStage::BakeNormal: return "Bake: Normal";
        case PipelineStage::BakeColor:  return "Bake: Color";
        default:                        return "?";
        }
    }

    //------------------------------------------------------------------------
    //! 既定のパラメータで作ります。
    //------------------------------------------------------------------------
    Pipeline::Pipeline() {
        InvalidateAll();
    }

    //------------------------------------------------------------------------
    //! 後片付けします。
    //------------------------------------------------------------------------
    Pipeline::~Pipeline() = default;

    //------------------------------------------------------------------------
    //! 全段を無効化します。
    //------------------------------------------------------------------------
    void Pipeline::InvalidateAll() {
        // 完走済みハッシュを潰すだけでよい。現在のハッシュと必ず食い違うので
        // 全段が dirty になる
        m_completedHash.fill(0u);
        RecomputeHashes();
    }

    //------------------------------------------------------------------------
    //! 目標の段まで実行し終えているかを返します。
    //------------------------------------------------------------------------
    bool Pipeline::IsUpToDate() {
        RecomputeHashes();

        if(!m_lowField || !m_fullField) {
            return false;
        }

        for(u32 i = 0; i <= static_cast<u32>(m_targetStage) && i < kPipelineStageCount; ++i) {
            if(m_status[i].dirty) {
                return false;
            }
        }
        return true;
    }

    //------------------------------------------------------------------------
    //! 岩が収まる球の半径を返します。
    //------------------------------------------------------------------------
    float Pipeline::GetBoundingRadius() const {
        return m_lowField ? m_lowField->GetBoundingRadius() : m_params.radius;
    }

    //------------------------------------------------------------------------
    //! 各段が依存するパラメータのハッシュを計算し直します。
    //------------------------------------------------------------------------
    void Pipeline::RecomputeHashes() {
        //--------------------------------------------------------------------
        // Field: 半径関数そのもの。メッシュ解像度から決まる帯域制限も含むので
        //        targetEdgeLength にも依存する
        //--------------------------------------------------------------------
        Hasher fieldHasher;
        fieldHasher.Mix(m_params.seed);
        fieldHasher.Mix(m_params.radius);
        fieldHasher.Mix(m_params.anisoScale);
        fieldHasher.Mix(m_params.targetEdgeLength);
        fieldHasher.Mix(m_params.bakeMaxFrequency);

        // 破断面は半径関数そのものなので Field 段に属する。
        // ここへ混ぜ忘れるとスライダを動かしても形が変わらない
        fieldHasher.Mix(m_params.planeCutCount);
        fieldHasher.Mix(m_params.planeCutDepth);
        fieldHasher.Mix(m_params.planeAxisBias);
        fieldHasher.Mix(m_params.edgeRounding);
        MixNoiseLayers(fieldHasher, m_params.noiseLayers);
        m_currentHash[static_cast<size_t>(PipelineStage::Field)] = fieldHasher.Get();

        //--------------------------------------------------------------------
        // Mesh: 上流 + 三角形の分割設定
        //--------------------------------------------------------------------
        Hasher meshHasher;
        meshHasher.Mix(m_currentHash[static_cast<size_t>(PipelineStage::Field)]);
        meshHasher.Mix(m_params.baseSubdivision);
        m_currentHash[static_cast<size_t>(PipelineStage::Mesh)] = meshHasher.Get();

        //--------------------------------------------------------------------
        // Unwrap: 上流 + 展開方式とテクスチャの大きさ
        //--------------------------------------------------------------------
        Hasher unwrapHasher;
        unwrapHasher.Mix(m_currentHash[static_cast<size_t>(PipelineStage::Mesh)]);

        // params.unwrapMethod ではなく「実際に使う方式」を混ぜる。
        // 対話中は八面体射影へ落ちるので、そのまま混ぜるとスライダを
        // 離した瞬間に「もう完了している」と判断されて xatlas が走らない
        unwrapHasher.Mix(GetEffectiveUnwrapMethod());
        unwrapHasher.Mix(m_params.texelsPerUnit);
        unwrapHasher.Mix(m_params.uvPadding);
        unwrapHasher.Mix(m_params.textureSize);

        // クリース角は法線と頂点の複製にしか効かない。ハードエッジ化は
        // UV 展開の後段に置いてあるので Unwrap 段に属する
        unwrapHasher.Mix(m_params.creaseAngleDeg);
        m_currentHash[static_cast<size_t>(PipelineStage::Unwrap)] = unwrapHasher.Get();

        //--------------------------------------------------------------------
        // BakeNormal: 上流 + dilate のパス数。
        //
        // baseColor や roughness は混ぜない。これが「色を動かしても
        // Normal ベイクが走らない」ことの実体で、Phase 4 で色の段を足しても
        // ここは変わらない
        //--------------------------------------------------------------------
        Hasher normalHasher;
        normalHasher.Mix(m_currentHash[static_cast<size_t>(PipelineStage::Unwrap)]);
        normalHasher.Mix(m_params.dilatePasses);
        m_currentHash[static_cast<size_t>(PipelineStage::BakeNormal)] = normalHasher.Get();

        //--------------------------------------------------------------------
        // BakeColor: 上流 + マテリアル。
        //
        // 上流に混ぜるのは **Unwrap** であって BakeNormal ではない。
        // この 2 つは兄弟で、どちらも UV にしか依存しない。段の並びは
        // 実行順なので BakeNormal が先に来るが、依存関係ではない。
        // ここへ BakeNormal を混ぜると、色を変えただけで Normal の
        // ハッシュ経由の連鎖が起きて意味のない再実行が増える
        //--------------------------------------------------------------------
        Hasher colorHasher;
        colorHasher.Mix(m_currentHash[static_cast<size_t>(PipelineStage::Unwrap)]);
        colorHasher.Mix(m_params.baseColor);
        colorHasher.Mix(m_params.secondaryColor);
        colorHasher.Mix(m_params.colorVariation);
        colorHasher.Mix(m_params.colorNoiseFrequency);
        colorHasher.Mix(m_params.cavityDarkening);
        colorHasher.Mix(m_params.roughness);
        colorHasher.Mix(m_params.cavityRoughness);
        colorHasher.Mix(m_params.metallic);

        // 窪みの信号は半径関数から取るので、形が変われば色も焼き直す。
        // seed は Field のハッシュ経由で Unwrap まで伝わっている
        colorHasher.Mix(m_params.dilatePasses);
        m_currentHash[static_cast<size_t>(PipelineStage::BakeColor)] = colorHasher.Get();

        //--------------------------------------------------------------------
        // dirty 判定。上流のハッシュを自分へ混ぜてあるので、
        // 無効化の伝播を手で書く必要がない
        //--------------------------------------------------------------------
        for(size_t i = 0; i < kPipelineStageCount; ++i) {
            m_status[i].dirty = (m_currentHash[i] != m_completedHash[i]);
        }
    }

    //------------------------------------------------------------------------
    //! 実際に使う UV 展開の方式を返します。
    //------------------------------------------------------------------------
    UnwrapMethod Pipeline::GetEffectiveUnwrapMethod() const {
        return m_fastPreview ? UnwrapMethod::Octahedral : m_params.unwrapMethod;
    }

    //------------------------------------------------------------------------
    //! UV 展開とハードエッジ化を行います。
    //------------------------------------------------------------------------
    bool Pipeline::RunUnwrap(const CancelToken* cancel) {
        //--------------------------------------------------------------------
        // 必ず展開前の写しから始める。
        //
        // 展開もハードエッジ化も頂点を複製するので、一度通したメッシュへ
        // もう一度かけると頂点が際限なく増える。テクスチャサイズを変えただけの
        // ときは Mesh 段が走らないため、ここで写し直さないと実際にそうなる
        //--------------------------------------------------------------------
        m_mesh = m_baseMesh;

        m_unwrapStats = UnwrapStats{};

        UnwrapSettings settings{};
        settings.textureSize   = m_params.textureSize;
        settings.padding       = m_params.uvPadding;
        settings.texelsPerUnit = m_params.texelsPerUnit;

        bool unwrapped = false;

        if(GetEffectiveUnwrapMethod() == UnwrapMethod::XAtlas) {
            XAtlasUnwrapper unwrapper;
            unwrapped = unwrapper.Unwrap(m_mesh, settings, cancel, &m_unwrapStats);

            //----------------------------------------------------------------
            // 中断と失敗を区別する。中断ならやり直したいので false を返すが、
            // 失敗（チャートが1枚に収まらない等）は八面体射影で焼き切る。
            // ここで諦めるとプレビューが真っ平らな法線のままになる
            //----------------------------------------------------------------
            if(!unwrapped && cancel && cancel->IsCancelled()) {
                return false;
            }
            if(!unwrapped) {
                // 理由は残す。これを落とすと「なぜ落ちたか」が
                // どこにも出なくなり、切り分けができない
                const char* reason = m_unwrapStats.failureReason;

                m_mesh                      = m_baseMesh;
                m_unwrapStats               = UnwrapStats{};
                m_unwrapStats.fellBack      = true;
                m_unwrapStats.failureReason = reason;
            }
        }

        if(!unwrapped) {
            OctahedralUnwrapper unwrapper;
            unwrapper.Unwrap(m_mesh, settings, cancel, &m_unwrapStats);
        }

        //--------------------------------------------------------------------
        // 稜線をハードエッジにする。
        //
        // **展開より後に行うこと。** 先に分割すると稜線の両側が別頂点になって
        // 法線シームが生まれ、xatlas がそこでチャートを切るため、破断面の数だけ
        // チャートが増えてアトラスの詰め込みが崩れる。
        // 後に行えば「位置と UV が同じで法線だけ違う頂点」が増えるだけで、
        // UV アトラスには一切影響しない
        //--------------------------------------------------------------------
        m_unwrapStats.splitVertexCount = SplitHardEdges(m_mesh, m_params.creaseAngleDeg);
        m_unwrapStats.vertexCount      = m_mesh.GetVertexCount();

        return true;
    }

    //------------------------------------------------------------------------
    //! 差分のある段を順に実行します。
    //------------------------------------------------------------------------
    bool Pipeline::Update(const ProgressCallback& progress, const CancelToken* cancel) {
        RecomputeHashes();

        const auto shouldRun = [this](PipelineStage stage) {
            if(static_cast<u32>(stage) > static_cast<u32>(m_targetStage)) {
                return false;
            }

            // 場がまだ無いなら、ハッシュが一致していても作り直す。
            // 初回と、ハッシュが偶然 0 になった場合の保険
            if(stage == PipelineStage::Field && (!m_lowField || !m_fullField)) {
                return true;
            }
            return m_status[static_cast<size_t>(stage)].dirty;
        };

        const auto markCompleted = [this](PipelineStage stage, float milliseconds) {
            const size_t index            = static_cast<size_t>(stage);
            m_completedHash[index]        = m_currentHash[index];
            m_status[index].dirty         = false;
            m_status[index].milliseconds  = milliseconds;
            ++m_status[index].runCount;
        };

        //--------------------------------------------------------------------
        // Field
        //--------------------------------------------------------------------
        if(shouldRun(PipelineStage::Field)) {
            const auto begin = Now();

            // r_low はメッシュのナイキストで打ち切る。r_full は全帯域。
            // その差分が Normal マップへ焼かれる高周波ディテールになる
            m_lowField  = std::make_unique<RockField>(m_params, ComputeMeshNyquistFrequency(m_params));
            m_fullField = std::make_unique<RockField>(m_params, m_params.bakeMaxFrequency);

            markCompleted(PipelineStage::Field, ElapsedMilliseconds(begin));
            ReportProgress(progress, 1.0f, "Field");
        }

        //--------------------------------------------------------------------
        // Mesh
        //--------------------------------------------------------------------
        if(shouldRun(PipelineStage::Mesh)) {
            if(!m_lowField) {
                return false;
            }
            const auto begin = Now();

            // 出力は m_baseMesh。m_mesh（UV 展開済み）は Unwrap 段が作る
            BuildRockMesh(m_params, *m_lowField, m_baseMesh, &m_meshStats);

            markCompleted(PipelineStage::Mesh, ElapsedMilliseconds(begin));
            ReportProgress(progress, 1.0f, "Mesh");
        }

        //--------------------------------------------------------------------
        // Unwrap（UV 展開 + UV 空間のラスタライズ）
        //
        // 展開はシームで頂点を複製するためメッシュそのものが変わる。
        // プレビューの頂点バッファも作り直す必要があるのでリビジョンを上げる
        //--------------------------------------------------------------------
        if(shouldRun(PipelineStage::Unwrap)) {
            const auto begin = Now();

            ReportProgress(progress, 0.0f, "Unwrap");

            if(!RunUnwrap(cancel)) {
                // 中断された。ハッシュを記録しないので次回やり直す
                return false;
            }
            ++m_meshRevision;

            RasterizeUv(m_mesh, m_params.textureSize, m_gbuffer);

            markCompleted(PipelineStage::Unwrap, ElapsedMilliseconds(begin));
            ReportProgress(progress, 1.0f, "Unwrap");
        }

        //--------------------------------------------------------------------
        // BakeNormal
        //--------------------------------------------------------------------
        if(shouldRun(PipelineStage::BakeNormal)) {
            if(!m_fullField) {
                return false;
            }
            const auto begin = Now();

            if(!BakeNormalMap(m_gbuffer, *m_fullField, m_normalMap, progress, cancel, &m_normalStats)) {
                // 中断された。ハッシュを記録しないので次回やり直す
                return false;
            }

            DilateImage(m_normalMap, m_gbuffer.GetCoverage(), m_params.dilatePasses);
            ++m_normalMapRevision;

            markCompleted(PipelineStage::BakeNormal, ElapsedMilliseconds(begin));
        }

        //--------------------------------------------------------------------
        // BakeColor
        //--------------------------------------------------------------------
        if(shouldRun(PipelineStage::BakeColor)) {
            if(!m_fullField) {
                return false;
            }
            const auto begin = Now();

            if(!BakeSurfaceMaps(m_gbuffer,
                                *m_fullField,
                                m_params,
                                m_albedoMap,
                                m_metallicRoughnessMap,
                                progress,
                                cancel,
                                &m_surfaceStats)) {
                // 中断された。ハッシュを記録しないので次回やり直す
                return false;
            }

            DilateImage(m_albedoMap, m_gbuffer.GetCoverage(), m_params.dilatePasses);
            DilateImage(m_metallicRoughnessMap, m_gbuffer.GetCoverage(), m_params.dilatePasses);
            ++m_surfaceMapRevision;

            markCompleted(PipelineStage::BakeColor, ElapsedMilliseconds(begin));
        }

        return true;
    }

}    // namespace RockCore
