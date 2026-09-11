//----------------------------------------------------------------------------
//! @file   HalfSpaceClip.cpp
//! @brief  半空間クリップと切り口の穴埋めの実装
//----------------------------------------------------------------------------
#include <RockCore/Mesh/HalfSpaceClip.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //--------------------------------------------------------------------
        //! エッジを頂点番号の組で表すキーを作ります。
        //!
        //! Subdivide.cpp と同じ手。隣り合う三角形が同じ交点頂点を共有しないと、
        //! 切り口の境界が繋がらず穴が塞がらない。
        //!
        //! @param  [in] a 片方の頂点番号
        //! @param  [in] b もう片方の頂点番号
        //! @return エッジのキー
        //--------------------------------------------------------------------
        u64 MakeEdgeKey(u32 a, u32 b) {
            const u32 lo = std::min(a, b);
            const u32 hi = std::max(a, b);
            return (static_cast<u64>(lo) << 32) | static_cast<u64>(hi);
        }

        //--------------------------------------------------------------------
        //! 法線に直交する正規直交基底を作ります。
        //! @param  [in]  normal  単位法線
        //! @param  [out] outAxisU 1本目の軸
        //! @param  [out] outAxisV 2本目の軸
        //--------------------------------------------------------------------
        void BuildPlaneBasis(const Vec3& normal, Vec3& outAxisU, Vec3& outAxisV) {
            // 法線と平行になりにくい軸を選んでから外積を取る
            const Vec3 helper = (std::abs(normal.y) < 0.9f) ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};

            outAxisU = Normalize(Cross(helper, normal));
            outAxisV = Cross(normal, outAxisU);
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! メッシュを半空間の内側だけに切り詰めます。
    //------------------------------------------------------------------------
    bool ClipMeshByPlane(MeshBuilder& mesh, const Vec3& planeNormal, float planeDistance, float targetEdgeLength) {
        if(mesh.indices.empty()) {
            return false;
        }

        const u32 originalVertexCount = mesh.GetVertexCount();

        //--------------------------------------------------------------------
        // 頂点ごとの符号付き距離。正なら平面の外側
        //--------------------------------------------------------------------
        std::vector<float> signedDistance(originalVertexCount);

        float maxDistance = 0.0f;
        for(u32 i = 0; i < originalVertexCount; ++i) {
            signedDistance[i] = Dot(mesh.positions[i], planeNormal) - planeDistance;
            maxDistance       = std::max(maxDistance, signedDistance[i]);
        }

        // 平面が完全にメッシュの外なら何もしない
        const float epsilon = std::max(std::abs(planeDistance), 1.0f) * 1e-5f;
        if(maxDistance <= epsilon) {
            return false;
        }

        mesh.DiscardAttributes();

        const std::vector<u32> sourceIndices = mesh.indices;
        mesh.indices.clear();
        mesh.indices.reserve(sourceIndices.size());

        //--------------------------------------------------------------------
        // 平面上に乗っている頂点の集合。交点として新しく作ったものと、
        // 元から平面上にあったものの両方を入れる。切り口の境界を
        // 拾うときにこれを引く
        //--------------------------------------------------------------------
        std::unordered_set<u32> onPlane;

        for(u32 i = 0; i < originalVertexCount; ++i) {
            if(std::abs(signedDistance[i]) <= epsilon) {
                onPlane.insert(i);
            }
        }

        std::unordered_map<u64, u32> intersectionCache;

        //--------------------------------------------------------------------
        //! 辺の交点を作る（すでにあれば使い回す）
        //--------------------------------------------------------------------
        auto getIntersection = [&](u32 a, u32 b) {
            const u64 key = MakeEdgeKey(a, b);

            const auto found = intersectionCache.find(key);
            if(found != intersectionCache.end()) {
                return found->second;
            }

            const float sa = signedDistance[a];
            const float sb = signedDistance[b];

            // sa と sb は符号が違うので分母が 0 になることはない
            const float t = sa / (sa - sb);

            //----------------------------------------------------------------
            // 交点が端点とほぼ重なるときは、新しい頂点を作らずその端点を使う。
            //
            // 片方の頂点がちょうど平面に乗っているとここに来る。素直に交点を
            // 作ると、その頂点と 1e-7 しか離れていない別頂点ができて、
            // 面積 0 の三角形と、見た目は同じ位置なのに番号が違う頂点が生まれる
            //----------------------------------------------------------------
            //----------------------------------------------------------------
            // しきい値は辺に対する比率。
            //
            // ここを大きくして「針のような三角形」を根本から防ごうとしては
            // いけない。1e-2 まで上げて試したところ、切り口の多角形が次々に
            // 線分へ潰れて境界のループが繋がらなくなり、断面に穴が開いた
            // （三角形 40228 → 21193、既定のプリセットは 1313 枚まで崩壊）。
            // この値はあくまで「ちょうど平面に乗っている頂点」を拾うための
            // もので、細い三角形の掃除は後段の WeldCloseVertices が行う
            //----------------------------------------------------------------
            constexpr float kSnap = 1e-4f;

            u32 index = 0;
            if(t <= kSnap) {
                index = a;
            } else if(t >= 1.0f - kSnap) {
                index = b;
            } else {
                index = mesh.AddVertex(Lerp(mesh.positions[a], mesh.positions[b], t));
            }

            intersectionCache[key] = index;
            onPlane.insert(index);
            return index;
        };

        //--------------------------------------------------------------------
        // 切り口の境界となる有向辺。塞ぐときに向きを反転して使う
        //--------------------------------------------------------------------
        std::vector<u32> boundaryVertices;

        //--------------------------------------------------------------------
        // 三角形ごとに Sutherland-Hodgman で内側を残す
        //--------------------------------------------------------------------
        std::vector<u32> polygon;
        polygon.reserve(4);

        for(size_t i = 0; i + 2 < sourceIndices.size(); i += 3) {
            const u32 triangle[3] = {sourceIndices[i], sourceIndices[i + 1], sourceIndices[i + 2]};

            int outsideCount = 0;
            for(const u32 vertex : triangle) {
                if(signedDistance[vertex] > epsilon) {
                    ++outsideCount;
                }
            }

            if(outsideCount == 0) {
                // まるごと内側。切っていないので境界にもならない。
                // ここで拾ってしまうと、平面にたまたま乗っている辺を
                // 切り口と誤認して扇が歪む
                mesh.AddTriangle(triangle[0], triangle[1], triangle[2]);
                continue;
            }

            if(outsideCount == 3) {
                continue;
            }

            polygon.clear();

            for(int corner = 0; corner < 3; ++corner) {
                const u32 current = triangle[corner];
                const u32 next    = triangle[(corner + 1) % 3];

                const bool currentInside = signedDistance[current] <= epsilon;
                const bool nextInside    = signedDistance[next] <= epsilon;

                if(currentInside) {
                    polygon.push_back(current);
                }

                // 片方だけが外側なら、その辺の上に交点ができる
                if(currentInside != nextInside) {
                    polygon.push_back(getIntersection(current, next));
                }
            }

            //----------------------------------------------------------------
            // 交点のスナップで同じ頂点が続けて並ぶことがある。畳んでおかないと
            // 面積 0 の三角形になる（環状なので先頭と末尾の重なりも見る）
            //----------------------------------------------------------------
            polygon.erase(std::unique(polygon.begin(), polygon.end()), polygon.end());
            if(polygon.size() >= 2 && polygon.front() == polygon.back()) {
                polygon.pop_back();
            }

            //----------------------------------------------------------------
            // 切り口の境界を拾う。両端が平面上にある辺がそれに当たる。
            //
            // 三角形が潰れて線分になった場合（頂点2つ）でも、その辺は
            // 切り口の縁なので拾う。ここを飛ばすと断面に隙間が開く
            //----------------------------------------------------------------
            const size_t cornerCount = polygon.size();
            for(size_t k = 0; k < cornerCount; ++k) {
                const u32 a = polygon[k];
                const u32 b = polygon[(k + 1) % cornerCount];

                if(a != b && onPlane.count(a) > 0 && onPlane.count(b) > 0) {
                    boundaryVertices.push_back(a);
                    boundaryVertices.push_back(b);
                }

                // 2頂点しか無いときは往復させない
                if(cornerCount == 2) {
                    break;
                }
            }

            if(polygon.size() < 3) {
                // まるごと外側か、線分まで潰れた。面としては残さない
                continue;
            }

            // 巻き方向を保ったまま扇で三角化する
            for(size_t k = 1; k + 1 < polygon.size(); ++k) {
                mesh.AddTriangle(polygon[0], polygon[k], polygon[k + 1]);
            }
        }

        //--------------------------------------------------------------------
        // 切り口を塞ぐ。
        //
        // 切る対象が凸（球と半空間の交わり）なので、断面は必ず 1 つの凸多角形に
        // なる。有向辺を繋いで環をたどるより、境界の頂点を平面上の角度で
        // 並べ替えて扇にする方が短く、退化にも強い
        //--------------------------------------------------------------------
        //--------------------------------------------------------------------
        // 境界の有向辺を繋いでループを作る。
        //
        // 角度で並べ替える手もあるが、同じ角度の頂点や、断面がわずかに
        // 凸でない場合に順序を取り違えて、縁の辺が1本ずれたまま塞がる。
        // その結果できる「開いた辺」は数万本のうち数本なので目視では気付けない。
        // 実際の繋がりをたどれば構成上ずれようがない
        //--------------------------------------------------------------------
        std::unordered_map<u32, u32> nextOnBoundary;
        nextOnBoundary.reserve(boundaryVertices.size());

        for(size_t i = 0; i + 1 < boundaryVertices.size(); i += 2) {
            nextOnBoundary[boundaryVertices[i]] = boundaryVertices[i + 1];
        }

        if(nextOnBoundary.size() < 3) {
            // 平面がかすっただけ。塞ぐ面が作れないが、穴も開いていない
            return true;
        }

        std::vector<u32> capVertices;
        capVertices.reserve(nextOnBoundary.size());

        {
            const u32 start = nextOnBoundary.begin()->first;

            u32 current = start;
            for(size_t step = 0; step <= nextOnBoundary.size(); ++step) {
                capVertices.push_back(current);

                const auto found = nextOnBoundary.find(current);
                if(found == nextOnBoundary.end()) {
                    break;
                }

                current = found->second;
                if(current == start) {
                    break;
                }
            }
        }

        // たどり切れなかった（＝単一の閉ループになっていない）。
        // 起きないはずだが、起きたら塞がずに抜ける方が、間違った面を
        // 張って非多様体にするよりまし
        if(capVertices.size() != nextOnBoundary.size()) {
            return true;
        }

        Vec3 center{0.0f, 0.0f, 0.0f};
        for(u32 index : capVertices) {
            center = center + mesh.positions[index];
        }
        center = center * (1.0f / static_cast<float>(capVertices.size()));

        Vec3 axisU{};
        Vec3 axisV{};
        BuildPlaneBasis(planeNormal, axisU, axisV);

        //--------------------------------------------------------------------
        // ループの向きを揃える。
        //
        // 平面内での符号付き面積が正なら planeNormal から見て反時計回り。
        // 後段は「反時計回りに (current, next, center)」で外向きになる前提で
        // 張るので、負なら反転させる
        //--------------------------------------------------------------------
        float signedArea = 0.0f;
        for(size_t k = 0; k < capVertices.size(); ++k) {
            const Vec3 a = mesh.positions[capVertices[k]] - center;
            const Vec3 b = mesh.positions[capVertices[(k + 1) % capVertices.size()]] - center;

            signedArea += Dot(a, axisU) * Dot(b, axisV) - Dot(a, axisV) * Dot(b, axisU);
        }

        if(signedArea < 0.0f) {
            std::reverse(capVertices.begin(), capVertices.end());
        }

        //--------------------------------------------------------------------
        // 同心リングで張る。
        //
        // 重心からの単純な扇にすると、中心から縁までの辺が切り口の半径ぶんの
        // 長さになる。後段の SubdivideToEdgeLength は「最長の辺」を見て全体を
        // 細分割するので、その1辺のためにメッシュ全体が 4 倍・16 倍と膨らむ。
        // リングにしておけば全部の辺が目標エッジ長に収まり、余計な細分割が要らない
        //--------------------------------------------------------------------
        const size_t capCount = capVertices.size();

        float maxSpoke = 0.0f;
        for(u32 index : capVertices) {
            maxSpoke = std::max(maxSpoke, Length(mesh.positions[index] - center));
        }

        int ringCount = 1;
        if(targetEdgeLength > 0.0f) {
            // 上限を設けないと、細かい目標値で切り口だけが極端に密になる
            constexpr int kMaxRings = 24;

            ringCount = static_cast<int>(std::ceil(maxSpoke / targetEdgeLength));
            ringCount = std::clamp(ringCount, 1, kMaxRings);
        }

        const u32 centerIndex = mesh.AddVertex(center);

        //--------------------------------------------------------------------
        // 外側のリングから内側へ向かって作る。
        //
        // 巻き方向: capVertices は planeNormal 側から見て反時計回りに
        // 並べてあるので、(current, next, inner) の順に張ると面法線が
        // planeNormal と同じ向き（外向き）になる。逆順にすると裏返って
        // 真っ黒に潰れる
        //--------------------------------------------------------------------
        std::vector<u32> outerRing(capVertices);
        std::vector<u32> innerRing;

        for(int ring = ringCount - 1; ring >= 1; --ring) {
            const float t = static_cast<float>(ring) / static_cast<float>(ringCount);

            innerRing.clear();
            innerRing.reserve(capCount);
            for(u32 index : capVertices) {
                innerRing.push_back(mesh.AddVertex(Lerp(center, mesh.positions[index], t)));
            }

            for(size_t k = 0; k < capCount; ++k) {
                const size_t next = (k + 1) % capCount;

                mesh.AddTriangle(innerRing[k], outerRing[k], outerRing[next]);
                mesh.AddTriangle(innerRing[k], outerRing[next], innerRing[next]);
            }

            outerRing = innerRing;
        }

        // 一番内側のリングと重心を扇で繋ぐ
        for(size_t k = 0; k < capCount; ++k) {
            const size_t next = (k + 1) % capCount;
            mesh.AddTriangle(outerRing[k], outerRing[next], centerIndex);
        }

        return true;
    }

}    // namespace RockCore
