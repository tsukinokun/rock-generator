//----------------------------------------------------------------------------
//! @file   XAtlasUnwrapper.cpp
//! @brief  xatlas による UV 展開の実装
//----------------------------------------------------------------------------
#include <RockCore/Unwrap/XAtlasUnwrapper.hpp>

#include <xatlas.h>

#include <algorithm>
#include <cmath>
#include <utility>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        // xatlas へは配列の先頭アドレスとストライドを渡すので、
        // Vec2 / Vec3 が詰まっていることが前提になる
        static_assert(sizeof(Vec3) == sizeof(float) * 3, "Vec3 にパディングがあると xatlas へ直接渡せない");
        static_assert(sizeof(Vec2) == sizeof(float) * 2, "Vec2 にパディングがあると xatlas へ直接渡せない");

        //! アトラスが 1 枚に収まらなかったときの詰め直しの回数
        constexpr int kMaxPackAttempts = 5;

        //--------------------------------------------------------------------
        //! xatlas の標準出力へのログを止めます。
        //!
        //! xatlas は既定で警告を printf で吐く。RockCli は検証結果を
        //! 標準出力で読ませるツールなので、そこへ混ざると読めなくなる。
        //!
        //! SetPrint は xatlas 側のグローバル設定で、RockCore の
        //! 「グローバル状態を持たない」方針から外れて見える。これは
        //! 生成結果に一切影響しない 3rd party のログ設定であり、
        //! 何度呼んでも同じ状態になる（冪等）ため例外として許容している。
        //--------------------------------------------------------------------
        void SilenceXAtlasOnce() {
            // 関数内 static はスレッドセーフに一度だけ初期化される
            static const bool silenced = [] {
                xatlas::SetPrint(nullptr, false);
                return true;
            }();
            (void)silenced;
        }

        //--------------------------------------------------------------------
        //! @class AtlasHandle
        //! xatlas::Atlas の所有権を持つだけのもの
        //!
        //! 失敗経路が多いので、Destroy を手で書くと必ずどこかで漏れる
        //--------------------------------------------------------------------
        class AtlasHandle {
        public:
            AtlasHandle() : m_atlas(xatlas::Create()) {}
            ~AtlasHandle() {
                if(m_atlas) {
                    xatlas::Destroy(m_atlas);
                }
            }

            AtlasHandle(const AtlasHandle&)            = delete;
            AtlasHandle& operator=(const AtlasHandle&) = delete;

            //! アトラスを返します。
            //! @return アトラス。作れていなければ nullptr
            xatlas::Atlas* Get() const { return m_atlas; }

        private:
            xatlas::Atlas* m_atlas = nullptr;
        };

        //--------------------------------------------------------------------
        //! xatlas から呼ばれる進捗コールバック。
        //!
        //! false を返すと xatlas が処理を打ち切る。これが唯一の中断手段で、
        //! 戻り値を無視すると数秒の処理をスライダ操作中に止められなくなる。
        //!
        //! xatlas は内部でスレッドを起こすので、この関数は任意のスレッドから
        //! 呼ばれる。CancelToken は atomic なのでそのまま読んでよい。
        //!
        //! @param  [in] category xatlas の処理段階（使わない）
        //! @param  [in] progress 進み具合（使わない）
        //! @param  [in] userData CancelToken へのポインタ。nullptr でもよい
        //! @return 続行するなら true
        //--------------------------------------------------------------------
        bool OnXAtlasProgress(xatlas::ProgressCategory category, int progress, void* userData) {
            (void)category;
            (void)progress;

            const auto* cancel = static_cast<const CancelToken*>(userData);
            return (cancel == nullptr) || !cancel->IsCancelled();
        }

        //--------------------------------------------------------------------
        //! 中断が要求されているかを返します。
        //! @param  [in] cancel 中断フラグ。nullptr でもよい
        //! @return 要求されていれば true
        //--------------------------------------------------------------------
        bool IsCancelled(const CancelToken* cancel) { return (cancel != nullptr) && cancel->IsCancelled(); }

    }    // namespace

    //------------------------------------------------------------------------
    //! メッシュへ UV を付けます。
    //------------------------------------------------------------------------
    bool XAtlasUnwrapper::Unwrap(MeshBuilder&          mesh,
                                 const UnwrapSettings& settings,
                                 const CancelToken*    cancel,
                                 UnwrapStats*          outStats) {
        //--------------------------------------------------------------------
        // 失敗経路が多いので、理由を記録してから false を返す口を用意する。
        // 「xatlas が落ちた」だけでは、詰め込みが溢れたのか中断されたのか
        // 区別できず、切り分けに困る
        //--------------------------------------------------------------------
        const auto fail = [outStats](const char* reason) {
            if(outStats) {
                outStats->failureReason = reason;
            }
            return false;
        };

        if(mesh.positions.empty() || mesh.indices.empty()) {
            return fail("empty mesh");
        }

        SilenceXAtlasOnce();

        AtlasHandle handle;
        if(handle.Get() == nullptr) {
            return fail("xatlas::Create failed");
        }

        xatlas::Atlas* atlas = handle.Get();

        // const_cast は xatlas の口が void* なだけで、中では読み取りにしか使わない
        xatlas::SetProgressCallback(atlas, &OnXAtlasProgress, const_cast<CancelToken*>(cancel));

        //--------------------------------------------------------------------
        // 1. メッシュを渡す。
        //
        //    法線も渡す。xatlas はチャートを育てるときに面の法線が平均から
        //    どれだけ外れるかを見るので、渡さないと破断面をまたいだチャートが
        //    でき、そこだけ歪みが大きくなる
        //--------------------------------------------------------------------
        xatlas::MeshDecl decl{};
        decl.vertexCount          = mesh.GetVertexCount();
        decl.vertexPositionData   = mesh.positions.data();
        decl.vertexPositionStride = static_cast<u32>(sizeof(Vec3));

        if(mesh.normals.size() == mesh.positions.size()) {
            decl.vertexNormalData   = mesh.normals.data();
            decl.vertexNormalStride = static_cast<u32>(sizeof(Vec3));
        }

        decl.indexCount  = static_cast<u32>(mesh.indices.size());
        decl.indexData   = mesh.indices.data();
        decl.indexFormat = xatlas::IndexFormat::UInt32;

        const xatlas::AddMeshError addError = xatlas::AddMesh(atlas, decl, 1u);
        if(addError != xatlas::AddMeshError::Success) {
            return fail(xatlas::StringForEnum(addError));
        }
        xatlas::AddMeshJoin(atlas);

        //--------------------------------------------------------------------
        // 2. チャートへ切る。
        //
        //    既定値のままにしてある。法線シームの重みを上げて破断面ごとに
        //    強制的に切る手もあるが、この段のメッシュには法線シームが無い
        //    （ハードエッジ化は UV 展開の後段で行う）ので効かない。
        //    稜線で切るかどうかは normalDeviationWeight が面の法線差から
        //    判断する
        //--------------------------------------------------------------------
        xatlas::ComputeCharts(atlas, xatlas::ChartOptions{});

        if(IsCancelled(cancel)) {
            return fail("cancelled while computing charts");
        }

        // ここで atlas->chartCount を見てはいけない。Atlas のメンバが埋まるのは
        // PackCharts の後で、ComputeCharts 直後はまだ 0 のまま
        // （xatlas.h の Atlas の注釈「Populated after charts are packed」）

        //--------------------------------------------------------------------
        // 3. テクスチャ 1 枚へ詰める。
        //
        //    resolution を指定して texelsPerUnit を 0 にすると、xatlas が
        //    「だいたいこの解像度に収まる」密度を見積もってくれる。
        //    ただし見積もりなので溢れて 2 枚目ができることがある。
        //    焼き先は 1 枚しか無いので、溢れたら密度を縮めて詰め直す
        //--------------------------------------------------------------------
        xatlas::PackOptions packOptions{};
        packOptions.padding       = static_cast<u32>(std::max(settings.padding, 1));
        packOptions.resolution    = static_cast<u32>(std::max(settings.textureSize, 1));
        packOptions.texelsPerUnit = std::max(settings.texelsPerUnit, 0.0f);
        packOptions.bilinear      = true;
        packOptions.createImage   = false;

        for(int attempt = 0; attempt < kMaxPackAttempts; ++attempt) {
            xatlas::PackCharts(atlas, packOptions);

            if(IsCancelled(cancel)) {
                return fail("cancelled while packing charts");
            }
            if(atlas->atlasCount == 1u) {
                break;
            }
            if(atlas->atlasCount == 0u) {
                return fail("packing produced no atlas");
            }

            //----------------------------------------------------------------
            // n 枚に溢れたなら面積が n 倍あるということ。密度は長さの次元なので
            // 1/sqrt(n) へ縮める。0.95 は見積もりの誤差ぶんの余裕で、
            // これが無いと境界付近で何度も溢れて試行を使い切る
            //----------------------------------------------------------------
            const float shrink = 0.95f / std::sqrt(static_cast<float>(atlas->atlasCount));

            packOptions.texelsPerUnit = atlas->texelsPerUnit * shrink;
        }

        if(atlas->atlasCount != 1u) {
            return fail("the charts do not fit into a single atlas");
        }
        if(atlas->meshCount == 0u || atlas->chartCount == 0u) {
            return fail("no output mesh");
        }

        //--------------------------------------------------------------------
        // 4. 結果を取り出す。
        //
        //    xref は「この出力頂点がどの入力頂点から来たか」。シームで
        //    複製された頂点は同じ xref を指すので、位置と法線はそこから
        //    引けばよい。出力の頂点数は入力より増える
        //--------------------------------------------------------------------
        const xatlas::Mesh& packed = atlas->meshes[0];

        if(packed.vertexCount == 0u || packed.indexCount == 0u) {
            return fail("the output mesh is empty");
        }

        const bool hasNormals     = (mesh.normals.size() == mesh.positions.size());
        const u32  sourceVertices = mesh.GetVertexCount();

        MeshBuilder result;
        result.positions.resize(packed.vertexCount);
        result.uvs.resize(packed.vertexCount);
        if(hasNormals) {
            result.normals.resize(packed.vertexCount);
        }

        const float invWidth  = 1.0f / static_cast<float>(std::max(atlas->width, 1u));
        const float invHeight = 1.0f / static_cast<float>(std::max(atlas->height, 1u));

        for(u32 i = 0; i < packed.vertexCount; ++i) {
            const xatlas::Vertex& vertex = packed.vertexArray[i];

            //----------------------------------------------------------------
            // チャートに入らなかった頂点は uv が (0,0) のまま返る。
            // それを通すと UV 空間に原点まで伸びた巨大な三角形ができて
            // ベイクが壊れるので、1 つでもあれば失敗として呼び出し側へ返し、
            // 八面体射影へ落としてもらう
            //----------------------------------------------------------------
            if(vertex.chartIndex < 0) {
                return fail("a vertex was left out of every chart");
            }
            if(vertex.atlasIndex != 0) {
                return fail("a vertex landed on a second atlas");
            }
            if(vertex.xref >= sourceVertices) {
                return fail("xref points outside the input mesh");
            }

            result.positions[i] = mesh.positions[vertex.xref];
            if(hasNormals) {
                result.normals[i] = mesh.normals[vertex.xref];
            }

            // xatlas の uv はテクセル単位。[0,1] へ直す。
            // 端の丸めで 1 をわずかに超えることがあるのでクランプする
            result.uvs[i] = Vec2{std::clamp(vertex.uv[0] * invWidth, 0.0f, 1.0f),
                                 std::clamp(vertex.uv[1] * invHeight, 0.0f, 1.0f)};
        }

        result.indices.assign(packed.indexArray, packed.indexArray + packed.indexCount);

        mesh = std::move(result);

        if(outStats) {
            outStats->methodName  = GetName();
            outStats->chartCount  = packed.chartCount;
            outStats->vertexCount = mesh.GetVertexCount();
            outStats->utilization = atlas->utilization[0];
        }

        return true;
    }

}    // namespace RockCore
