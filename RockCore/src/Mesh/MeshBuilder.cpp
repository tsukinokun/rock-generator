//----------------------------------------------------------------------------
//! @file   MeshBuilder.cpp
//! @brief  三角形メッシュの中間表現の実装
//----------------------------------------------------------------------------
#include <RockCore/Mesh/MeshBuilder.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! 頂点を追加します。
    //------------------------------------------------------------------------
    u32 MeshBuilder::AddVertex(const Vec3& position) {
        positions.push_back(position);
        return static_cast<u32>(positions.size() - 1);
    }

    //------------------------------------------------------------------------
    //! 三角形を追加します。
    //------------------------------------------------------------------------
    void MeshBuilder::AddTriangle(u32 a, u32 b, u32 c) {
        indices.push_back(a);
        indices.push_back(b);
        indices.push_back(c);
    }

    //------------------------------------------------------------------------
    //! バウンディングボックスを求めます。
    //------------------------------------------------------------------------
    void MeshBuilder::ComputeBounds(Vec3& outMin, Vec3& outMax) const {
        if(positions.empty()) {
            outMin = Vec3{0.0f, 0.0f, 0.0f};
            outMax = Vec3{0.0f, 0.0f, 0.0f};
            return;
        }

        constexpr float kBig = std::numeric_limits<float>::max();
        outMin               = Vec3{kBig, kBig, kBig};
        outMax               = Vec3{-kBig, -kBig, -kBig};

        for(const Vec3& p : positions) {
            outMin.x = std::min(outMin.x, p.x);
            outMin.y = std::min(outMin.y, p.y);
            outMin.z = std::min(outMin.z, p.z);
            outMax.x = std::max(outMax.x, p.x);
            outMax.y = std::max(outMax.y, p.y);
            outMax.z = std::max(outMax.z, p.z);
        }
    }

    //------------------------------------------------------------------------
    //! 最も長いエッジの長さを求めます。
    //------------------------------------------------------------------------
    float MeshBuilder::ComputeMaxEdgeLength() const {
        float maxLengthSq = 0.0f;

        for(size_t i = 0; i + 2 < indices.size(); i += 3) {
            const Vec3& a = positions[indices[i]];
            const Vec3& b = positions[indices[i + 1]];
            const Vec3& c = positions[indices[i + 2]];

            maxLengthSq = std::max(maxLengthSq, LengthSq(b - a));
            maxLengthSq = std::max(maxLengthSq, LengthSq(c - b));
            maxLengthSq = std::max(maxLengthSq, LengthSq(a - c));
        }

        return std::sqrt(maxLengthSq);
    }

    //------------------------------------------------------------------------
    //! 中身を空にします。
    //------------------------------------------------------------------------
    void MeshBuilder::Clear() {
        positions.clear();
        normals.clear();
        uvs.clear();
        indices.clear();
    }

    //------------------------------------------------------------------------
    //! normals と uvs を捨てます。
    //------------------------------------------------------------------------
    void MeshBuilder::DiscardAttributes() {
        normals.clear();
        uvs.clear();
    }

    //------------------------------------------------------------------------
    //! 参照されていない頂点を取り除きます。
    //------------------------------------------------------------------------
    u32 MeshBuilder::RemoveUnusedVertices() {
        const u32 vertexCount = GetVertexCount();
        if(vertexCount == 0u) {
            return 0u;
        }

        constexpr u32 kUnused = 0xFFFFFFFFu;

        //--------------------------------------------------------------------
        // 1. 使われている頂点に印を付ける
        //--------------------------------------------------------------------
        std::vector<u32> remap(vertexCount, kUnused);

        for(const u32 index : indices) {
            if(index < vertexCount) {
                remap[index] = 0u;    // 使われている印。番号は次の手順で振る
            }
        }

        //--------------------------------------------------------------------
        // 2. 新しい番号を**頂点番号の昇順で**振る。
        //
        //    添字バッファの出現順に振ってはいけない。その順だと「新しい番号 >
        //    元の番号」になる頂点ができ、手順 4 の詰め直しで、まだ読んでいない
        //    頂点を上書きしてしまう（位置が別の頂点の値に化ける）。
        //    昇順なら必ず 新しい番号 <= 元の番号 になるので、同じ配列の中で
        //    前から詰められる。元の並び順が保たれるので決定論にも都合がよい
        //--------------------------------------------------------------------
        u32 nextIndex = 0u;
        for(u32 vertex = 0u; vertex < vertexCount; ++vertex) {
            if(remap[vertex] != kUnused) {
                remap[vertex] = nextIndex++;
            }
        }

        if(nextIndex == vertexCount) {
            return 0u;
        }

        //--------------------------------------------------------------------
        // 3. 添字を振り直す。範囲外の添字は壊れた入力なので触らずに残す
        //    （ここで直すと原因が隠れる）
        //--------------------------------------------------------------------
        for(u32& index : indices) {
            if(index < vertexCount) {
                index = remap[index];
            }
        }

        //--------------------------------------------------------------------
        // 4. 属性を詰め直す
        //--------------------------------------------------------------------
        const bool hasNormals = (normals.size() == static_cast<size_t>(vertexCount));
        const bool hasUvs     = (uvs.size() == static_cast<size_t>(vertexCount));

        for(u32 old = 0u; old < vertexCount; ++old) {
            const u32 fresh = remap[old];
            if(fresh == kUnused) {
                continue;
            }

            positions[fresh] = positions[old];
            if(hasNormals) {
                normals[fresh] = normals[old];
            }
            if(hasUvs) {
                uvs[fresh] = uvs[old];
            }
        }

        positions.resize(nextIndex);
        if(hasNormals) {
            normals.resize(nextIndex);
        }
        if(hasUvs) {
            uvs.resize(nextIndex);
        }

        return vertexCount - nextIndex;
    }

    //------------------------------------------------------------------------
    //! 近すぎる頂点を溶接します。
    //------------------------------------------------------------------------
    u32 MeshBuilder::WeldCloseVertices(float epsilon) {
        const u32 vertexCount = GetVertexCount();
        if(epsilon <= 0.0f || vertexCount == 0u) {
            return 0u;
        }

        //--------------------------------------------------------------------
        // 格子ハッシュで近傍を引く。格子の一辺を epsilon に取ってあるので、
        // 距離 epsilon 以内の相手は必ず 3x3x3 の近傍セルの中にいる
        //--------------------------------------------------------------------
        const float inverseCell = 1.0f / epsilon;
        const float epsilonSq   = epsilon * epsilon;

        const auto cellKey = [](int x, int y, int z) {
            u64 key = 0xCBF29CE484222325ull;

            const int components[3] = {x, y, z};
            for(const int value : components) {
                key ^= static_cast<u64>(static_cast<u32>(value));
                key *= 0x100000001B3ull;
            }
            return key;
        };

        // セルに入れるのは代表の頂点だけ。代表でない頂点を入れると、
        // 連鎖的な溶接（A-B が近く B-C が近いが A-C は遠い）で結果が
        // 走査順に依存して不安定になる
        std::unordered_map<u64, std::vector<u32>> cells;
        cells.reserve(vertexCount);

        std::vector<u32> remap(vertexCount);

        u32 mergedCount = 0u;

        for(u32 vertex = 0u; vertex < vertexCount; ++vertex) {
            const Vec3 position = positions[vertex];

            const int cellX = static_cast<int>(std::floor(position.x * inverseCell));
            const int cellY = static_cast<int>(std::floor(position.y * inverseCell));
            const int cellZ = static_cast<int>(std::floor(position.z * inverseCell));

            u32  representative = 0u;
            bool found          = false;

            for(int dz = -1; dz <= 1 && !found; ++dz) {
                for(int dy = -1; dy <= 1 && !found; ++dy) {
                    for(int dx = -1; dx <= 1 && !found; ++dx) {
                        const auto cell = cells.find(cellKey(cellX + dx, cellY + dy, cellZ + dz));
                        if(cell == cells.end()) {
                            continue;
                        }

                        for(const u32 candidate : cell->second) {
                            if(LengthSq(positions[candidate] - position) <= epsilonSq) {
                                representative = candidate;
                                found          = true;
                                break;
                            }
                        }
                    }
                }
            }

            if(found) {
                remap[vertex] = representative;
                ++mergedCount;
            } else {
                remap[vertex] = vertex;
                cells[cellKey(cellX, cellY, cellZ)].push_back(vertex);
            }
        }

        if(mergedCount == 0u) {
            return 0u;
        }

        //--------------------------------------------------------------------
        // 代表へ差し替えてから、同じ頂点を 2 つ以上持つ三角形を捨てる。
        // これで辺が縮んだことになり、位相は閉じたまま保たれる
        //--------------------------------------------------------------------
        std::vector<u32> kept;
        kept.reserve(indices.size());

        for(size_t i = 0; i + 2 < indices.size(); i += 3) {
            const u32 a = (indices[i] < vertexCount) ? remap[indices[i]] : indices[i];
            const u32 b = (indices[i + 1] < vertexCount) ? remap[indices[i + 1]] : indices[i + 1];
            const u32 c = (indices[i + 2] < vertexCount) ? remap[indices[i + 2]] : indices[i + 2];

            if(a == b || b == c || c == a) {
                continue;
            }

            kept.push_back(a);
            kept.push_back(b);
            kept.push_back(c);
        }

        indices = std::move(kept);

        // 代表でなくなった頂点はどの三角形からも参照されていないので、
        // 詰め直しはこれに任せる
        RemoveUnusedVertices();

        return mergedCount;
    }

    //------------------------------------------------------------------------
    //! 面積の小さすぎる三角形を取り除きます。
    //------------------------------------------------------------------------
    u32 MeshBuilder::CollapseSliverTriangles(float minArea, float maxEdgeLength, int maxPasses) {
        if(minArea <= 0.0f || maxEdgeLength <= 0.0f || indices.empty()) {
            return 0u;
        }

        const float maxEdgeLengthSq = maxEdgeLength * maxEdgeLength;

        u32 totalCollapsed = 0u;

        for(int pass = 0; pass < maxPasses; ++pass) {
            const u32 vertexCount = GetVertexCount();
            if(vertexCount == 0u) {
                break;
            }

            //----------------------------------------------------------------
            // Union-Find。縮めた辺の両端を同じ組にまとめる。
            // 代表は必ず番号の小さい方にする（結果を走査順に依存させない）
            //----------------------------------------------------------------
            std::vector<u32> parent(vertexCount);
            for(u32 i = 0; i < vertexCount; ++i) {
                parent[i] = i;
            }

            const auto find = [&parent](u32 vertex) {
                u32 root = vertex;
                while(parent[root] != root) {
                    root = parent[root];
                }

                // 経路圧縮。深さが増えると 1 頂点あたりの探索が線形になる
                while(parent[vertex] != root) {
                    const u32 next = parent[vertex];
                    parent[vertex] = root;
                    vertex         = next;
                }
                return root;
            };

            u32 collapsed = 0u;

            for(size_t i = 0; i + 2 < indices.size(); i += 3) {
                if(indices[i] >= vertexCount || indices[i + 1] >= vertexCount || indices[i + 2] >= vertexCount) {
                    continue;
                }

                const u32 ra = find(indices[i]);
                const u32 rb = find(indices[i + 1]);
                const u32 rc = find(indices[i + 2]);

                // すでに潰れている。この後のまとめ直しで消える
                if(ra == rb || rb == rc || rc == ra) {
                    continue;
                }

                const Vec3& a = positions[ra];
                const Vec3& b = positions[rb];
                const Vec3& c = positions[rc];

                if(Length(Cross(b - a, c - a)) * 0.5f > minArea) {
                    continue;
                }

                //------------------------------------------------------------
                // 最短辺を選ぶ。動かす距離がその辺の長さそのものになるので、
                // 一番短いものを選ぶのが形への影響が最も小さい
                //------------------------------------------------------------
                const float lengthAb = LengthSq(b - a);
                const float lengthBc = LengthSq(c - b);
                const float lengthCa = LengthSq(a - c);

                u32   first      = ra;
                u32   second     = rb;
                float shortestSq = lengthAb;

                if(lengthBc < shortestSq) {
                    first      = rb;
                    second     = rc;
                    shortestSq = lengthBc;
                }
                if(lengthCa < shortestSq) {
                    first      = rc;
                    second     = ra;
                    shortestSq = lengthCa;
                }

                // 長い辺しか無い三角形（3 頂点が一直線に並んだだけのもの）は
                // 縮めると形が目に見えて変わる。触らない
                if(shortestSq > maxEdgeLengthSq) {
                    continue;
                }

                const u32 root  = std::min(first, second);
                const u32 child = std::max(first, second);

                parent[child] = root;
                ++collapsed;
            }

            if(collapsed == 0u) {
                break;
            }

            //----------------------------------------------------------------
            // 代表へ差し替えて、潰れた三角形を捨てる。
            // 溶接と同じで、頂点をまとめてから捨てるので位相は閉じたまま
            //----------------------------------------------------------------
            std::vector<u32> kept;
            kept.reserve(indices.size());

            for(size_t i = 0; i + 2 < indices.size(); i += 3) {
                if(indices[i] >= vertexCount || indices[i + 1] >= vertexCount || indices[i + 2] >= vertexCount) {
                    continue;
                }

                const u32 a = find(indices[i]);
                const u32 b = find(indices[i + 1]);
                const u32 c = find(indices[i + 2]);

                if(a == b || b == c || c == a) {
                    continue;
                }

                kept.push_back(a);
                kept.push_back(b);
                kept.push_back(c);
            }

            indices = std::move(kept);
            RemoveUnusedVertices();

            totalCollapsed += collapsed;
        }

        return totalCollapsed;
    }

}    // namespace RockCore
