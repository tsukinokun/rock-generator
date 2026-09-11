//----------------------------------------------------------------------------
//! @file   Panels.cpp
//! @brief  ImGui のパネル群の実装
//----------------------------------------------------------------------------
#include <RockEditor/Panels.hpp>

#include <RockCore/Io/RockParamsJson.hpp>
#include <RockCore/Random/Pcg32.hpp>
#include <RockCore/Shape/RockMesher.hpp>

#include <imgui.h>

#include <cstdio>
#include <iterator>

// 名前空間 RockEditor
namespace RockEditor {

    namespace {

        //! テクスチャの一辺に選ばせる値。
        //!
        //! エンジンは 2048 超を縮小し4の倍数へ丸める（ModelImporter.cpp:395-414）。
        //! しかもその縮小はアスペクト比を保たないので、正方形の 3 択に絞る
        constexpr int kTextureSizes[] = {512, 1024, 2048};

        //--------------------------------------------------------------------
        //! 符号なし 64bit のシードを ImGui で編集します。
        //!
        //! ImGui に u64 のスライダは無いので、32bit 2本ではなく
        //! 「数値入力 + 乱数ボタン」で扱う。
        //!
        //! @param [in,out] seed シード
        //--------------------------------------------------------------------
        void DrawSeedControl(RockCore::u64& seed) {
            int shown = static_cast<int>(seed & 0x7FFFFFFFull);
            if(ImGui::InputInt("Seed", &shown)) {
                seed = static_cast<RockCore::u64>(shown < 0 ? -shown : shown);
            }

            ImGui::SameLine();
            if(ImGui::Button("Randomize")) {
                // 中身は何でもよいが、連番にすると似た形が並ぶので撹拌する
                seed = RockCore::DeriveSeed(seed, 1u) & 0x7FFFFFFFull;
            }
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! 形のパネルを描きます。
    //------------------------------------------------------------------------
    void DrawShapePanel(RockCore::RockParams& params) {
        if(!ImGui::Begin("Shape")) {
            ImGui::End();
            return;
        }

        DrawSeedControl(params.seed);

        ImGui::SliderFloat("Radius (m)", &params.radius, 0.05f, 3.0f, "%.3f");

        ImGui::SliderFloat3("Aniso scale", &params.anisoScale.x, 0.1f, 2.0f, "%.2f");
        ImGui::SetItemTooltip("(1, 0.25, 1) で板状の岩になる");

        ImGui::Separator();

        ImGui::SliderFloat("Target edge (m)", &params.targetEdgeLength, 0.005f, 0.2f, "%.4f");
        ImGui::SetItemTooltip("メッシュの目標エッジ長。小さくすると三角形が増える");

        ImGui::SliderInt("Base subdivision", &params.baseSubdivision, 2, 7);

        // メッシュが表現できる周波数の上限。ここを超える成分は
        // 頂点変位ではなく Normal マップへ回る
        const float nyquist = RockCore::ComputeMeshNyquistFrequency(params);
        ImGui::Text("Mesh nyquist frequency: %.1f", nyquist);
        ImGui::SetItemTooltip("この周波数を超えるノイズは頂点に載せず、Normal マップへ焼く");

        ImGui::Separator();
        ImGui::TextUnformatted("Noise layers");

        for(size_t i = 0; i < params.noiseLayers.size(); ++i) {
            RockCore::NoiseLayerParams& layer = params.noiseLayers[i];

            char label[32];
            std::snprintf(label, sizeof(label), "Layer %zu", i);

            ImGui::PushID(static_cast<int>(i));
            if(ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Enabled", &layer.enabled);
                ImGui::SliderFloat("Amplitude", &layer.amplitude, 0.0f, 0.6f, "%.3f");
                ImGui::SliderFloat("Frequency", &layer.fbm.frequency, 0.2f, 12.0f, "%.2f");
                ImGui::SliderInt("Octaves", &layer.fbm.octaves, 1, 10);
                ImGui::SliderFloat("Lacunarity", &layer.fbm.lacunarity, 1.5f, 3.0f, "%.2f");
                ImGui::SliderFloat("Gain", &layer.fbm.gain, 0.2f, 0.8f, "%.2f");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        if(ImGui::Button("Add layer")) {
            params.noiseLayers.push_back(RockCore::NoiseLayerParams{});
        }
        ImGui::SameLine();
        if(ImGui::Button("Remove layer") && params.noiseLayers.size() > 1) {
            params.noiseLayers.pop_back();
        }

        ImGui::Separator();
        ImGui::TextDisabled("Worley / Ridged / plane cuts: Phase 3");

        ImGui::End();
    }

    //------------------------------------------------------------------------
    //! マテリアルとベイク設定のパネルを描きます。
    //------------------------------------------------------------------------
    void DrawMaterialPanel(RockCore::RockParams& params) {
        if(!ImGui::Begin("Material / Bake")) {
            ImGui::End();
            return;
        }

        // 色とラフネスは下流のベイク段を無効化しない。
        // ここを動かしても Unwrap と Normal ベイクは走らない
        ImGui::ColorEdit3("Base color", &params.baseColor.x);
        ImGui::SliderFloat("Roughness", &params.roughness, 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("Metallic is fixed at 0 (rock)");

        ImGui::Separator();

        int sizeIndex = 1;
        for(int i = 0; i < static_cast<int>(std::size(kTextureSizes)); ++i) {
            if(kTextureSizes[i] == params.textureSize) {
                sizeIndex = i;
            }
        }
        if(ImGui::Combo("Texture size", &sizeIndex, "512\0" "1024\0" "2048\0")) {
            params.textureSize = kTextureSizes[sizeIndex];
        }
        ImGui::SetItemTooltip("エンジンが 2048 超を縮小し4の倍数へ丸めるため、正方形の3択のみ");

        ImGui::SliderInt("Dilate passes", &params.dilatePasses, 0, 24);
        ImGui::SetItemTooltip("被覆マスクの膨張。チャート境界の滲み対策");

        ImGui::Separator();
        ImGui::TextDisabled("Albedo / MR bake: Phase 4   AO bake: Phase 5");

        ImGui::End();
    }

    //------------------------------------------------------------------------
    //! 視点とライトのパネルを描きます。
    //------------------------------------------------------------------------
    void DrawViewPanel(PreviewViewSettings& view) {
        if(!ImGui::Begin("View")) {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("Camera");
        ImGui::SliderFloat("Yaw", &view.cameraYawDeg, -180.0f, 180.0f, "%.0f deg");
        ImGui::SliderFloat("Pitch", &view.cameraPitchDeg, -85.0f, 85.0f, "%.0f deg");
        ImGui::SliderFloat("Distance", &view.cameraDistance, 1.2f, 6.0f, "%.2f x");
        ImGui::SliderFloat("FOV", &view.fovDeg, 20.0f, 90.0f, "%.0f deg");

        ImGui::Separator();
        ImGui::TextUnformatted("Light");

        // Phase 2 の検証はここを回して凹凸の陰影が追従するかを見る。
        // 追従すれば C++ の符号化とシェーダの ApplyNormalMap が整合している
        ImGui::SliderFloat("Light yaw", &view.lightYawDeg, -180.0f, 180.0f, "%.0f deg");
        ImGui::SliderFloat("Light pitch", &view.lightPitchDeg, 5.0f, 89.0f, "%.0f deg");
        ImGui::SliderFloat("Intensity", &view.lightIntensity, 0.0f, 10.0f, "%.2f");

        ImGui::TextDisabled("ライトを回して凹凸の陰影が追従すれば TBN は整合している");

        ImGui::End();
    }

    //------------------------------------------------------------------------
    //! 生成結果のパネルを描きます。
    //------------------------------------------------------------------------
    void DrawStatsPanel(const PipelineSnapshot& snapshot, bool busy, float progress, const std::string& label) {
        if(!ImGui::Begin("Stats")) {
            ImGui::End();
            return;
        }

        if(busy) {
            ImGui::Text("Working: %s", label.empty() ? "..." : label.c_str());
            ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
        } else {
            ImGui::TextUnformatted("Idle");
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
        }

        ImGui::Separator();

        if(!snapshot.valid) {
            ImGui::TextUnformatted("No result yet.");
            ImGui::End();
            return;
        }

        ImGui::Text("Vertices : %u", snapshot.mesh.vertexCount);
        ImGui::Text("Triangles: %u", snapshot.mesh.triangleCount);
        ImGui::Text("Subdiv   : %d", snapshot.mesh.subdivisions);
        ImGui::Text("Max edge : %.4f m", snapshot.mesh.actualEdgeLength);

        ImGui::Separator();

        const int texelTotal = snapshot.textureSize * snapshot.textureSize;
        const float coverage = (texelTotal > 0) ? (static_cast<float>(snapshot.coveredTexels) / static_cast<float>(texelTotal)) : 0.0f;
        ImGui::Text("UV coverage   : %.1f %%", coverage * 100.0f);
        ImGui::Text("Baked texels  : %u", snapshot.normal.texelsWritten);
        ImGui::Text("Degenerate TBN: %u", snapshot.normal.degenerateTexels);

        ImGui::Separator();

        //--------------------------------------------------------------------
        // 段ごとの所要時間と実行回数。
        //
        // 色スライダを動かしたときに Unwrap と Bake の Runs が増えないことを
        // ここで確かめる。これがダーティ判定が効いている証拠
        //--------------------------------------------------------------------
        if(ImGui::BeginTable("stages", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Stage");
            ImGui::TableSetupColumn("ms");
            ImGui::TableSetupColumn("Runs");
            ImGui::TableSetupColumn("Dirty");
            ImGui::TableHeadersRow();

            for(RockCore::u32 i = 0; i < RockCore::kPipelineStageCount; ++i) {
                const RockCore::PipelineStageStatus& status = snapshot.stages[i];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(RockCore::GetPipelineStageName(static_cast<RockCore::PipelineStage>(i)));
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", status.milliseconds);
                ImGui::TableNextColumn();
                ImGui::Text("%u", status.runCount);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(status.dirty ? "yes" : "-");
            }

            ImGui::EndTable();
        }

        ImGui::End();
    }

    //------------------------------------------------------------------------
    //! 保存・読み込みのパネルを描きます。
    //------------------------------------------------------------------------
    void DrawIoPanel(RockCore::RockParams& params, EditorUiState& ui) {
        if(!ImGui::Begin("Parameters I/O")) {
            ImGui::End();
            return;
        }

        char pathBuffer[260];
        std::snprintf(pathBuffer, sizeof(pathBuffer), "%s", ui.paramsFilePath.c_str());
        if(ImGui::InputText("File", pathBuffer, sizeof(pathBuffer))) {
            ui.paramsFilePath = pathBuffer;
        }

        if(ImGui::Button("Save")) {
            std::string error;
            if(RockCore::SaveRockParamsToFile(params, ui.paramsFilePath, &error)) {
                ui.ioStatus = "Saved: " + ui.paramsFilePath;
            } else {
                ui.ioStatus = "Save failed: " + error;
            }
        }

        ImGui::SameLine();
        if(ImGui::Button("Load")) {
            std::string error;
            if(RockCore::LoadRockParamsFromFile(params, ui.paramsFilePath, &error)) {
                ui.ioStatus = "Loaded: " + ui.paramsFilePath;
            } else {
                ui.ioStatus = "Load failed: " + error;
            }
        }

        if(!ui.ioStatus.empty()) {
            ImGui::TextWrapped("%s", ui.ioStatus.c_str());
        }

        ImGui::Separator();
        ImGui::Checkbox("ImGui demo window", &ui.showImGuiDemo);

        ImGui::End();
    }

}    // namespace RockEditor
