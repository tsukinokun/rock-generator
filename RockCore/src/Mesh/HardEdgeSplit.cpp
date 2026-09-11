//----------------------------------------------------------------------------
//! @file   HardEdgeSplit.cpp
//! @brief  クリース角での頂点分割の実装
//----------------------------------------------------------------------------
#include <RockCore/Mesh/HardEdgeSplit.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

// 名前空間 RockCore
namespace RockCore {

    namespace {

        //--------------------------------------------------------------------
        //! 面法線を面積で重み付けしたベクトルを求めます。
        //!
        //! 正規化しないのは、後で平均を取るときに面積の重みをそのまま
        //! 使いたいため（外積の長さが面積の2倍になっている）。
        //!
        //! @param  [in] a 1つ目の頂点
        //! @param  [in] b 2つ目の頂点
        //! @param  [in] c 3つ目の頂点
        //! @return 面積で重み付けした面法線
        //--------------------------------------------------------------------
        Vec3 ComputeWeightedFaceNormal(const Vec3& a, const Vec3& b, const Vec3& c) {
            return Cross(b - a, c - a);
        }

        //--------------------------------------------------------------------
        //! エッジを頂点番号の組で表すキーを作ります。
        //! @param  [in] a 片方の頂点番号
        //! @param  [in] b もう片方の頂点番号
        //! @return エッジのキー
        //--------------------------------------------------------------------
        u64 MakeEdgeKey(u32 a, u32 b) {
            const u32 lo = std::min(a, b);
            const u32 hi = std::max(a, b);
            return (static_cast<u64>(lo) << 32) | static_cast<u64>(hi);
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! クリース角を超えるエッジで頂点を分割します。
    //------------------------------------------------------------------------
    u32 SplitHardEdges(MeshBuilder& mesh, float creaseAngleDeg) {
        const u32 triangleCount = mesh.GetTriangleCount();
        const u32 vertexCount   = mesh.GetVertexCount();

        if(triangleCount == 0 || mesh.normals.size() != mesh.positions.size()) {
            return 0;
        }

        const float creaseCosine = std::cos(std::clamp(creaseAngleDeg, 0.0f, 180.0f) * 3.14159265f / 180.0f);

        //--------------------------------------------------------------------
        // 面法線
        //--------------------------------------------------------------------
        std::vector<Vec3> weightedNormals(triangleCount);
        std::vector<Vec3> unitNormals(triangleCount);

        for(u32 t = 0; t < triangleCount; ++t) {
            const size_t base = static_cast<size_t>(t) * 3u;

            weightedNormals[t] = ComputeWeightedFaceNormal(mesh.positions[mesh.indices[base]],
                                                           mesh.positions[mesh.indices[base + 1]],
                                                           mesh.positions[mesh.indices[base + 2]]);
            unitNormals[t]     = Normalize(weightedNormals[t]);
        }

        //--------------------------------------------------------------------
        // 頂点ごとの隣接面
        //--------------------------------------------------------------------
        std::vector<std::vector<u32>> incidentFaces(vertexCount);
        for(u32 t = 0; t < triangleCount; ++t) {
            const size_t base = static_cast<size_t>(t) * 3u;
            for(int corner = 0; corner < 3; ++corner) {
                incidentFaces[mesh.indices[base + static_cast<size_t>(corner)]].push_back(t);
            }
        }

        //--------------------------------------------------------------------
        // エッジごとの隣接面。スムージンググループを繋ぐのに使う
        //--------------------------------------------------------------------
        std::unordered_map<u64, std::vector<u32>> edgeFaces;
        edgeFaces.reserve(static_cast<size_t>(triangleCount) * 2u);

        for(u32 t = 0; t < triangleCount; ++t) {
            const size_t base = static_cast<size_t>(t) * 3u;
            for(int corner = 0; corner < 3; ++corner) {
                const u32 a = mesh.indices[base + static_cast<size_t>(corner)];
                const u32 b = mesh.indices[base + static_cast<size_t>((corner + 1) % 3)];
                edgeFaces[MakeEdgeKey(a, b)].push_back(t);
            }
        }

        //--------------------------------------------------------------------
        // 頂点ごとに、隣接面をスムージンググループへ分ける。
        //
        // 同じ頂点を共有する面どうしでも、その間のエッジがクリースなら
        // 別グループにする。ここは Union-Find ではなく素朴な幅優先で足りる
        //（1頂点あたりの隣接面はせいぜい6〜8枚）
        //--------------------------------------------------------------------
        u32 splitCount = 0;

        // (元の頂点, グループ番号) → 新しい頂点番号
        std::unordered_map<u64, u32> groupVertex;

        std::vector<int> faceGroup;
        std::vector<u32> queue;

        for(u32 v = 0; v < vertexCount; ++v) {
            const std::vector<u32>& faces = incidentFaces[v];
            if(faces.empty()) {
                continue;
            }

            faceGroup.assign(faces.size(), -1);

            int groupCount = 0;
            for(size_t start = 0; start < faces.size(); ++start) {
                if(faceGroup[start] >= 0) {
                    continue;
                }

                const int group = groupCount++;
                faceGroup[start] = group;

                queue.clear();
                queue.push_back(static_cast<u32>(start));

                while(!queue.empty()) {
                    const u32 currentLocal = queue.back();
                    queue.pop_back();

                    const u32 currentFace = faces[currentLocal];

                    for(size_t otherLocal = 0; otherLocal < faces.size(); ++otherLocal) {
                        if(faceGroup[otherLocal] >= 0) {
                            continue;
                        }

                        const u32 otherFace = faces[otherLocal];

                        // この頂点まわりで隣り合っている（辺を共有している）か
                        bool shareEdge = false;
                        for(int corner = 0; corner < 3 && !shareEdge; ++corner) {
                            const size_t base = static_cast<size_t>(currentFace) * 3u;
                            const u32    a    = mesh.indices[base + static_cast<size_t>(corner)];
                            const u32    b    = mesh.indices[base + static_cast<size_t>((corner + 1) % 3)];
                            if(a != v && b != v) {
                                continue;
                            }

                            const auto found = edgeFaces.find(MakeEdgeKey(a, b));
                            if(found == edgeFaces.end()) {
                                continue;
                            }
                            shareEdge = std::find(found->second.begin(), found->second.end(), otherFace) !=
                                        found->second.end();
                        }

                        if(!shareEdge) {
                            continue;
                        }

                        // クリースを越えたら別グループのまま残す
                        if(Dot(unitNormals[currentFace], unitNormals[otherFace]) < creaseCosine) {
                            continue;
                        }

                        faceGroup[otherLocal] = group;
                        queue.push_back(static_cast<u32>(otherLocal));
                    }
                }
            }

            if(groupCount <= 1) {
                // 稜線に乗っていない頂点。解析勾配の法線をそのまま残す。
                // ここを面法線の平均に置き換えると、丸い部分が低テッセレーションで
                // カクついて見える
                continue;
            }

            //----------------------------------------------------------------
            // 2つ以上に分かれた頂点だけ複製する。
            // グループ 0 は元の頂点をそのまま使い、1 以降を新しく作る
            //----------------------------------------------------------------
            for(int group = 0; group < groupCount; ++group) {
                Vec3 accumulated{0.0f, 0.0f, 0.0f};
                for(size_t local = 0; local < faces.size(); ++local) {
                    if(faceGroup[local] == group) {
                        accumulated = accumulated + weightedNormals[faces[local]];
                    }
                }

                const Vec3 groupNormal = Normalize(accumulated);

                u32 targetVertex = v;
                if(group > 0) {
                    // push_back で再確保されるので、元の要素は値で取り出してから渡す
                    const Vec3 position = mesh.positions[v];
                    const Vec2 uv       = mesh.uvs.empty() ? Vec2{} : mesh.uvs[v];

                    targetVertex = mesh.AddVertex(position);
                    mesh.normals.push_back(groupNormal);

                    // UV は複製元と同じ値を持たせる。
                    //
                    // 稜線の両側は UV 空間でも隣り合う別の三角形なので、
                    // 同じ UV を共有させても重なりは起きない。位置と UV が
                    // 同じで法線だけが違う頂点が並ぶ、という状態になる
                    if(!mesh.uvs.empty()) {
                        mesh.uvs.push_back(uv);
                    }
                    ++splitCount;
                } else {
                    mesh.normals[v] = groupNormal;
                }

                groupVertex[(static_cast<u64>(v) << 32) | static_cast<u64>(static_cast<u32>(group))] = targetVertex;
            }

            // この頂点を参照している面の添字を、グループごとの頂点へ差し替える
            for(size_t local = 0; local < faces.size(); ++local) {
                const u32    face = faces[local];
                const size_t base = static_cast<size_t>(face) * 3u;

                const u64 key      = (static_cast<u64>(v) << 32) | static_cast<u64>(static_cast<u32>(faceGroup[local]));
                const u32 replaced = groupVertex[key];

                for(int corner = 0; corner < 3; ++corner) {
                    if(mesh.indices[base + static_cast<size_t>(corner)] == v) {
                        mesh.indices[base + static_cast<size_t>(corner)] = replaced;
                    }
                }
            }

            groupVertex.clear();
        }

        return splitCount;
    }

}    // namespace RockCore
