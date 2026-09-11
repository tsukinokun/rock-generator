//----------------------------------------------------------------------------
//! @file   OctahedralUnwrapper.cpp
//! @brief  八面体射影による UV 展開の実装
//----------------------------------------------------------------------------
#include <RockCore/Unwrap/OctahedralUnwrapper.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //--------------------------------------------------------------------
        //! 符号を返します。0 は + 扱いにします。
        //!
        //! 八面体射影では軸上（成分が厳密に 0）の方向が正方形の縁へ落ちます。
        //! そこでどちらの縁を選ぶかは任意で、貼り合わせの等価変換で
        //! 行き来できるため、+ に寄せて決め打ちにしています。
        //!
        //! @param  [in] value 対象の値
        //! @return 1.0 か -1.0
        //--------------------------------------------------------------------
        float SignOrPositive(float value) { return (value < 0.0f) ? -1.0f : 1.0f; }

        //--------------------------------------------------------------------
        //! 正方形の縁をまたぐ等価変換。
        //!
        //! 八面体射影は [-1,1]^2 の縁どうしを「ひねって」貼り合わせた形に
        //! なっており、縁 x=+1 を越えると (x,y) は (2-x, -y) と同じ点を指します。
        //! 4辺ぶんと、角を回り込む合成（y を先に適用してから x）を用意します。
        //--------------------------------------------------------------------
        enum class EdgeFlip { None, XPlus, XMinus, YPlus, YMinus };

        //--------------------------------------------------------------------
        //! 1辺ぶんの等価変換を適用します。
        //! @param  [in] p    元の座標
        //! @param  [in] flip 適用する辺
        //! @return 変換後の座標
        //--------------------------------------------------------------------
        Vec2 ApplyEdgeFlip(const Vec2& p, EdgeFlip flip) {
            switch(flip) {
            case EdgeFlip::XPlus:  return {2.0f - p.x, -p.y};
            case EdgeFlip::XMinus: return {-2.0f - p.x, -p.y};
            case EdgeFlip::YPlus:  return {-p.x, 2.0f - p.y};
            case EdgeFlip::YMinus: return {-p.x, -2.0f - p.y};
            default:               return p;
            }
        }

        //--------------------------------------------------------------------
        //! アンカーに最も近い等価位置を選びます。
        //!
        //! 三角形1枚は正方形に比べてごく小さいので、最も近い等価位置が
        //! 必ず正しい展開先になります。角をまたぐ三角形のために
        //! 2辺ぶんの合成も候補へ入れてあります。
        //!
        //! @param  [in] anchor    基準にする座標
        //! @param  [in] candidate 動かす座標
        //! @return アンカーに最も近い等価位置
        //--------------------------------------------------------------------
        Vec2 NearestEquivalent(const Vec2& anchor, const Vec2& candidate) {
            constexpr std::array<EdgeFlip, 5> kFlips = {
                EdgeFlip::None, EdgeFlip::XPlus, EdgeFlip::XMinus, EdgeFlip::YPlus, EdgeFlip::YMinus,
            };

            Vec2  best         = candidate;
            float bestDistance = std::numeric_limits<float>::max();

            for(EdgeFlip first : kFlips) {
                const Vec2 once = ApplyEdgeFlip(candidate, first);

                for(EdgeFlip second : kFlips) {
                    // 同じ辺を2回は無意味（元に戻る）
                    if(second == first && second != EdgeFlip::None) {
                        continue;
                    }

                    const Vec2  twice    = ApplyEdgeFlip(once, second);
                    const Vec2  delta    = twice - anchor;
                    const float distance = delta.x * delta.x + delta.y * delta.y;

                    if(distance < bestDistance) {
                        bestDistance = distance;
                        best         = twice;
                    }
                }
            }

            return best;
        }

        //--------------------------------------------------------------------
        //! 3点の最大の隔たり（辺の長さの最大値）を求めます。
        //! @param  [in] a 1点目
        //! @param  [in] b 2点目
        //! @param  [in] c 3点目
        //! @return 最大の辺長の2乗
        //--------------------------------------------------------------------
        float MaxSpanSq(const Vec2& a, const Vec2& b, const Vec2& c) {
            auto lengthSq = [](const Vec2& v) { return v.x * v.x + v.y * v.y; };
            return std::max(lengthSq(b - a), std::max(lengthSq(c - b), lengthSq(a - c)));
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! 単位方向ベクトルを八面体射影して [-1,1]^2 の座標を返します。
    //------------------------------------------------------------------------
    Vec2 OctahedralEncode(const Vec3& direction) {
        const float l1 = std::abs(direction.x) + std::abs(direction.y) + std::abs(direction.z);
        if(l1 <= 1e-20f) {
            return {0.0f, 0.0f};
        }

        const Vec2 p{direction.x / l1, direction.y / l1};

        if(direction.z >= 0.0f) {
            // 上半球は正方形に内接する菱形の内側へ入る
            return p;
        }

        // 下半球は菱形の外側の4つの三角形へ折り返す
        return {(1.0f - std::abs(p.y)) * SignOrPositive(p.x), (1.0f - std::abs(p.x)) * SignOrPositive(p.y)};
    }

    //------------------------------------------------------------------------
    //! メッシュへ UV を付けます。
    //------------------------------------------------------------------------
    bool OctahedralUnwrapper::Unwrap(MeshBuilder&          mesh,
                                     const UnwrapSettings& settings,
                                     const CancelToken*    cancel,
                                     UnwrapStats*          outStats) {
        // 中断を見る意味が無いほど速い。引数は口を揃えるためだけにある
        (void)cancel;

        if(mesh.positions.empty() || mesh.indices.empty()) {
            return false;
        }

        const u32 originalVertexCount = mesh.GetVertexCount();

        //--------------------------------------------------------------------
        // 1. 全頂点へ素の八面体座標を入れる。
        //    星形立体なので位置をそのまま正規化すれば方向が出る
        //--------------------------------------------------------------------
        std::vector<Vec2> coords(originalVertexCount);
        for(u32 i = 0; i < originalVertexCount; ++i) {
            coords[i] = OctahedralEncode(Normalize(mesh.positions[i]));
        }

        //--------------------------------------------------------------------
        // 2. 正方形の縁をまたぐ三角形だけ頂点を複製して展開する。
        //
        //    三角形は正方形に比べてごく小さいので、UV の隔たりがしきい値を
        //    超えていれば縁をまたいだと断定できる。しきい値は正方形の一辺
        //    （= 2）の 1/4 にしてある
        //--------------------------------------------------------------------
        constexpr float kSeamSpanSq = 0.5f * 0.5f;

        // 複製後の頂点ごとの UV。元の頂点ぶんを先にコピーしておく
        std::vector<Vec2> vertexCoords = coords;

        for(size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const u32 ia = mesh.indices[i];
            const u32 ib = mesh.indices[i + 1];
            const u32 ic = mesh.indices[i + 2];

            // 値で受ける。この直後に vertexCoords へ push_back するため、
            // 参照で持つと再確保で宙に浮く
            const Vec2 a = vertexCoords[ia];
            const Vec2 b = vertexCoords[ib];
            const Vec2 c = vertexCoords[ic];

            if(MaxSpanSq(a, b, c) <= kSeamSpanSq) {
                continue;
            }

            // a を基準に残り2点を隣のタイルへ引き寄せる。
            // 複製した頂点は他の三角形と共有しない（共有すると別の三角形の
            // UV が壊れる）ので、3頂点すべて新規に作る
            const Vec2 unfoldedA = a;
            const Vec2 unfoldedB = NearestEquivalent(unfoldedA, b);
            const Vec2 unfoldedC = NearestEquivalent(unfoldedA, c);

            const u32 sources[3]  = {ia, ib, ic};
            const Vec2 unfolded[3] = {unfoldedA, unfoldedB, unfoldedC};

            for(int corner = 0; corner < 3; ++corner) {
                const u32 source = sources[corner];

                // 自分自身の要素を push_back するので値で取り出してから渡す
                const Vec3 position = mesh.positions[source];
                const Vec3 normal   = mesh.normals.empty() ? Vec3{} : mesh.normals[source];

                const u32 duplicated = static_cast<u32>(mesh.positions.size());
                mesh.positions.push_back(position);
                if(!mesh.normals.empty()) {
                    mesh.normals.push_back(normal);
                }
                vertexCoords.push_back(unfolded[corner]);

                mesh.indices[i + static_cast<size_t>(corner)] = duplicated;
            }
        }

        //--------------------------------------------------------------------
        // 3. 全 UV を [0,1] の正方形へ詰める。
        //
        //    展開した三角形が [-1,1]^2 の外へ出るので、素の 0.5 倍 + 0.5 では
        //    はみ出す。実際の範囲を測ってから、縦横同じ倍率で収める
        //    （倍率を軸ごとに変えるとテクセル密度が異方になり、
        //    Normal マップの TBN が無駄に歪む）
        //--------------------------------------------------------------------
        Vec2 boundsMin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        Vec2 boundsMax{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};

        for(const Vec2& uv : vertexCoords) {
            boundsMin.x = std::min(boundsMin.x, uv.x);
            boundsMin.y = std::min(boundsMin.y, uv.y);
            boundsMax.x = std::max(boundsMax.x, uv.x);
            boundsMax.y = std::max(boundsMax.y, uv.y);
        }

        const float extent = std::max(std::max(boundsMax.x - boundsMin.x, boundsMax.y - boundsMin.y), 1e-6f);

        // 余白はテクセル数で指定されているので、比率へ直してから引く
        const float textureSize = static_cast<float>(std::max(settings.textureSize, 1));
        const float margin      = std::clamp(static_cast<float>(std::max(settings.padding, 0)) / textureSize, 0.0f, 0.25f);
        const float scale       = (1.0f - margin * 2.0f) / extent;

        mesh.uvs.resize(vertexCoords.size());
        for(size_t i = 0; i < vertexCoords.size(); ++i) {
            mesh.uvs[i] = Vec2{(vertexCoords[i].x - boundsMin.x) * scale + margin,
                               (vertexCoords[i].y - boundsMin.y) * scale + margin};
        }

        if(outStats) {
            outStats->methodName  = GetName();
            outStats->vertexCount = mesh.GetVertexCount();

            // チャートという概念が無いので 0 のまま。占有率も測らない
            // （八面体射影は常にテクスチャ全面を使う）
            outStats->chartCount  = 0;
            outStats->utilization = 0.0f;
        }

        return true;
    }

}    // namespace RockCore
