//----------------------------------------------------------------------------
//! @file   RockMesher.cpp
//! @brief  半径関数から岩のメッシュを組み立てる実装
//----------------------------------------------------------------------------
#include <RockCore/Shape/RockMesher.hpp>

#include <RockCore/Mesh/HalfSpaceClip.hpp>
#include <RockCore/Mesh/Icosphere.hpp>
#include <RockCore/Mesh/Subdivide.hpp>

#include <algorithm>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //! 細分割回数の上限。7 で三角形が約 32 万枚になるので、
        //! ここを超えさせるとプレビューの対話性が失われる
        constexpr int kMaxSubdivisions = 7;

        //! 細分割回数の下限。
        //!
        //! 八面体射影の UV 展開は「三角形1枚が UV 空間で十分小さい」ことを
        //! 前提にシームを検出している（OctahedralUnwrapper.cpp の kSeamSpanSq）。
        //! 細分割 2 未満だと素の正二十面体の三角形が UV 空間で大きすぎ、
        //! 全三角形がシーム扱いになって展開が壊れる
        constexpr int kMinSubdivisions = 2;

    }    // namespace

    //------------------------------------------------------------------------
    //! メッシュが表現できる周波数の上限（ナイキスト）を求めます。
    //------------------------------------------------------------------------
    float ComputeMeshNyquistFrequency(const RockParams& params) {
        const float edgeLength = std::max(params.targetEdgeLength, 1e-4f);
        return std::max(params.radius / (2.0f * edgeLength), 0.0f);
    }

    //------------------------------------------------------------------------
    //! 岩のメッシュを組み立てます。
    //------------------------------------------------------------------------
    void BuildRockMesh(const RockParams& params, const RockField& lowField, MeshBuilder& outMesh, RockMeshStats* outStats) {
        //--------------------------------------------------------------------
        // 1. 目標エッジ長を満たす細かさの単位球を作る。
        //
        //    粗く作ってから細分割で追い込む手もあるが、そうするとクリップの
        //    切り口が後段の細分割に巻き込まれて三角形数が跳ね上がる。
        //    先に必要な細かさまで作っておき、切り口はリングで目標エッジ長に
        //    合わせて張る（ClipMeshByPlane）方が総数が読める
        //--------------------------------------------------------------------
        const int estimated = EstimateIcosphereSubdivisions(params.targetEdgeLength, params.radius, kMaxSubdivisions);
        const int baseLevel = std::clamp(std::max(estimated, params.baseSubdivision), kMinSubdivisions, kMaxSubdivisions);

        BuildIcosphere(outMesh, baseLevel);

        //--------------------------------------------------------------------
        // 2. 外接球の大きさへ広げる。
        //
        //    ここで BaseRadius へ射影してはいけない。BaseRadius は稜線を
        //    smooth min で丸めた後の形なので、その上から鋭い平面で切ると
        //    丸めた分の内側をもう一度削ってしまう。
        //    クリップには「丸める前の鋭い平面」を見せて、丸めは手順 4 の
        //    射影に任せる
        //--------------------------------------------------------------------
        for(Vec3& position : outMesh.positions) {
            position = position * params.radius;
        }

        //--------------------------------------------------------------------
        // 3. 破断面で切る。
        //
        //    ここが Phase 3 の肝。半径関数側だけで平らにするとテッセレーションが
        //    稜線に沿わず、シルエットが階段状にギザつく。三角形の辺を稜線に
        //    一致させるにはメッシュを実際に切るしかない
        //--------------------------------------------------------------------
        for(const CutPlane& plane : lowField.GetCutPlanes()) {
            ClipMeshByPlane(outMesh, plane.normal, plane.distance, params.targetEdgeLength);
        }

        //--------------------------------------------------------------------
        //    クリップの後片付け。2 つある。
        //
        //    1) 孤立した頂点を捨てる。平面の外側にあった頂点は、参照する
        //       三角形が消えても positions に残り続ける
        //    2) 近すぎる頂点を溶接して、針のような三角形を畳む。交点が
        //       既存の頂点のすぐ近くに落ちると面積 1e-7 未満の三角形が残る
        //
        //    どちらも描画結果には出ないので長く放置していたが、xatlas は
        //    この 2 つを「どのチャートにも属さない頂点」として返してくる。
        //    その UV は (0,0) なので、通すとアトラスの原点へ伸びる巨大な
        //    三角形ができてベイクが壊れる。
        //
        //    溶接の幅は目標エッジ長の 0.3%。面積で言えば
        //    0.035 * 1e-4 / 2 = 1.75e-6 で、xatlas のしきい値 1.19e-7 の
        //    15 倍。距離としては 0.1mm なので目では見えない
        //--------------------------------------------------------------------
        constexpr float kWeldRatio = 0.003f;

        outMesh.WeldCloseVertices(params.targetEdgeLength * kWeldRatio);

        //--------------------------------------------------------------------
        //    溶接で落としきれない分を面積で掃除する。
        //
        //    実測で残るのは「3 頂点が互いに 2e-4 ほど離れた極小の三角形」で、
        //    頂点どうしは溶接の幅より離れているので溶接では畳めない。
        //    溶接の幅をそこまで広げると本来の形まで削れるので、面積で選んで
        //    最短辺だけを縮める。
        //
        //    面積のしきい値は目標エッジ長の 2 乗の 1/1000。0.035 なら 1.2e-6 で、
        //    xatlas が面を弾く 1.19e-7 の 10 倍。
        //
        //    最短辺の上限は、一直線に並んだだけの長い三角形を縮めて形を
        //    変えてしまわないための保険。目標の 5% にしていたときは
        //    「辺 1.8e-3 と 3.7e-3 で高さ 1.5e-5」という三角形が上限を
        //    わずかに超えて 1 枚だけ残った。10% にしてある。目標エッジ長が
        //    0.035 なら 3.5mm で、1m の岩の上では見えない
        //--------------------------------------------------------------------
        constexpr float kSliverAreaRatio = 1e-3f;
        constexpr float kSliverEdgeRatio = 0.10f;
        constexpr int   kSliverPasses    = 4;

        outMesh.CollapseSliverTriangles(params.targetEdgeLength * params.targetEdgeLength * kSliverAreaRatio,
                                        params.targetEdgeLength * kSliverEdgeRatio,
                                        kSliverPasses);

        outMesh.RemoveUnusedVertices();

        //--------------------------------------------------------------------
        // 4. 念のための細分割。
        //
        //    手順 1 と 3 で目標エッジ長は満たしているはずなので、普通は
        //    0 回で抜ける。深く切って切り口が大きいときの保険として残す。
        //
        //    中点は BaseRadius（ノイズ抜き）へ射影する。ノイズ込みで射影すると
        //    中点が動いて破断面の平らさが崩れるうえ、細分割のたびに形が
        //    変わってしまう
        //--------------------------------------------------------------------
        const VertexProjector toBaseSurface = [&lowField](const Vec3& p) {
            const Vec3 direction = Normalize(p);
            return direction * lowField.BaseRadius(direction);
        };

        //--------------------------------------------------------------------
        // しきい値を目標より緩めてある。
        //
        // SubdivideToEdgeLength は「最長の辺」を見るが、切り口のリングを
        // 繋ぐ四角形の対角線は sqrt(周方向^2 + 半径方向^2) になり、
        // どちらの辺も目標を満たしていても対角線だけが目標を超える。
        // 素の目標値で判定すると、その対角線 1 本のためにメッシュ全体が
        // 4 倍に膨らむ。ここは病的な場合の保険であって、実際の細かさは
        // 手順 1 の細分割レベルとリングの間隔が決めている
        //--------------------------------------------------------------------
        constexpr float kSafetyNetSlack = 1.6f;

        const int extraLevels =
            SubdivideToEdgeLength(outMesh, params.targetEdgeLength * kSafetyNetSlack, kMaxSubdivisions, toBaseSurface);

        //--------------------------------------------------------------------
        // 5. ノイズと異方スケールを載せて、解析勾配で法線を作る
        //--------------------------------------------------------------------
        const u32 vertexCount = outMesh.GetVertexCount();
        outMesh.normals.resize(vertexCount);

        // 勾配の差分幅はメッシュの解像度に合わせる。頂点間隔より細かく測ると
        // メッシュには載っていない高周波を法線だけが拾い、陰影がざらつく
        const float gradientStep = std::max(params.targetEdgeLength, 1e-4f);

        for(u32 i = 0; i < vertexCount; ++i) {
            const Vec3 direction = Normalize(outMesh.positions[i]);
            const Vec3 surface   = lowField.SurfacePoint(direction);

            outMesh.positions[i] = surface;

            // 法線は解析勾配。F は内側が負・外側が正なので、勾配はそのまま
            // 外向きを指す（反転させないこと）
            outMesh.normals[i] = Normalize(lowField.Gradient(surface, gradientStep));
        }

        //--------------------------------------------------------------------
        // 6. 結果を返す。
        //
        //    稜線のハードエッジ化（SplitHardEdges）はここでは**やらない**。
        //    UV 展開の後段へ回してある。先に分割すると稜線の両側が別頂点に
        //    なって法線シームが生まれ、xatlas がそこでチャートを切るため、
        //    破断面の数だけチャートが増えてアトラスの詰め込みが崩れる
        //--------------------------------------------------------------------
        if(outStats) {
            outStats->subdivisions     = baseLevel + extraLevels;
            outStats->vertexCount      = outMesh.GetVertexCount();
            outStats->triangleCount    = outMesh.GetTriangleCount();
            outStats->actualEdgeLength = outMesh.ComputeMaxEdgeLength();
            outStats->cutPlaneCount    = static_cast<u32>(lowField.GetCutPlanes().size());
        }
    }

}    // namespace RockCore
