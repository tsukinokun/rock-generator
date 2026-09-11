//----------------------------------------------------------------------------
//! @file   Panels.cpp
//! @brief  ImGui のパネル群の実装
//! @detail UI に出る文字列はすべて Tr() 経由です。直接リテラルを渡すと
//!         その箇所だけ言語が切り替わらないので、書き足すときは
//!         Localization.hpp の UiText へ項目を追加してください。
//----------------------------------------------------------------------------
#include <RockEditor/Panels.hpp>

#include <RockEditor/Localization.hpp>

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
        //! 補足の一行を、パネル幅で折り返して描きます。
        //! @param [in] text 表示する文字列
        //--------------------------------------------------------------------
        void DrawNote(const char* text) {
            // 英語は日本語より長くなりがちで、折り返さないとパネル幅で切れる
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", text);
            ImGui::PopTextWrapPos();
        }

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
            if(ImGui::InputInt(Tr(UiText::LabelSeed), &shown)) {
                seed = static_cast<RockCore::u64>(shown < 0 ? -shown : shown);
            }

            ImGui::SameLine();
            if(ImGui::Button(Tr(UiText::ButtonRandomize))) {
                // 中身は何でもよいが、連番にすると似た形が並ぶので撹拌する
                seed = RockCore::DeriveSeed(seed, 1u) & 0x7FFFFFFFull;
            }
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! 形のパネルを描きます。
    //------------------------------------------------------------------------
    void DrawShapePanel(RockCore::RockParams& params) {
        if(!ImGui::Begin(Tr(UiText::WindowShape))) {
            ImGui::End();
            return;
        }

        DrawSeedControl(params.seed);

        ImGui::SliderFloat(Tr(UiText::LabelRadius), &params.radius, 0.05f, 3.0f, "%.3f");

        ImGui::SliderFloat3(Tr(UiText::LabelAnisoScale), &params.anisoScale.x, 0.1f, 2.0f, "%.2f");
        ImGui::SetItemTooltip("%s", Tr(UiText::TipAnisoScale));

        ImGui::Separator();

        ImGui::SliderFloat(Tr(UiText::LabelTargetEdge), &params.targetEdgeLength, 0.005f, 0.2f, "%.4f");
        ImGui::SetItemTooltip("%s", Tr(UiText::TipTargetEdge));

        ImGui::SliderInt(Tr(UiText::LabelBaseSubdivision), &params.baseSubdivision, 2, 7);

        // メッシュが表現できる周波数の上限。ここを超える成分は
        // 頂点変位ではなく Normal マップへ回る
        const float nyquist = RockCore::ComputeMeshNyquistFrequency(params);
        ImGui::Text(Tr(UiText::FormatNyquist), nyquist);
        ImGui::SetItemTooltip("%s", Tr(UiText::TipNyquist));

        ImGui::Separator();
        ImGui::TextUnformatted(Tr(UiText::HeadingNoiseLayers));

        for(size_t i = 0; i < params.noiseLayers.size(); ++i) {
            RockCore::NoiseLayerParams& layer = params.noiseLayers[i];

            char label[64];
            std::snprintf(label, sizeof(label), Tr(UiText::FormatLayerName), i);

            ImGui::PushID(static_cast<int>(i));
            if(ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox(Tr(UiText::LabelEnabled), &layer.enabled);
                ImGui::SliderFloat(Tr(UiText::LabelAmplitude), &layer.amplitude, 0.0f, 0.6f, "%.3f");
                ImGui::SliderFloat(Tr(UiText::LabelFrequency), &layer.fbm.frequency, 0.2f, 12.0f, "%.2f");
                ImGui::SliderInt(Tr(UiText::LabelOctaves), &layer.fbm.octaves, 1, 10);
                ImGui::SliderFloat(Tr(UiText::LabelLacunarity), &layer.fbm.lacunarity, 1.5f, 3.0f, "%.2f");
                ImGui::SliderFloat(Tr(UiText::LabelGain), &layer.fbm.gain, 0.2f, 0.8f, "%.2f");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        if(ImGui::Button(Tr(UiText::ButtonAddLayer))) {
            params.noiseLayers.push_back(RockCore::NoiseLayerParams{});
        }
        ImGui::SameLine();
        if(ImGui::Button(Tr(UiText::ButtonRemoveLayer)) && params.noiseLayers.size() > 1) {
            params.noiseLayers.pop_back();
        }

        ImGui::Separator();
        DrawNote(Tr(UiText::NotePhase3Shape));

        ImGui::End();
    }

    //------------------------------------------------------------------------
    //! マテリアルとベイク設定のパネルを描きます。
    //------------------------------------------------------------------------
    void DrawMaterialPanel(RockCore::RockParams& params) {
        if(!ImGui::Begin(Tr(UiText::WindowMaterial))) {
            ImGui::End();
            return;
        }

        // 色とラフネスは下流のベイク段を無効化しない。
        // ここを動かしても Unwrap と Normal ベイクは走らない
        ImGui::ColorEdit3(Tr(UiText::LabelBaseColor), &params.baseColor.x);
        ImGui::SliderFloat(Tr(UiText::LabelRoughness), &params.roughness, 0.0f, 1.0f, "%.2f");
        DrawNote(Tr(UiText::NoteMetallicFixed));

        ImGui::Separator();

        int sizeIndex = 1;
        for(int i = 0; i < static_cast<int>(std::size(kTextureSizes)); ++i) {
            if(kTextureSizes[i] == params.textureSize) {
                sizeIndex = i;
            }
        }

        // 項目は数字なので翻訳しない
        if(ImGui::Combo(Tr(UiText::LabelTextureSize), &sizeIndex, "512\0" "1024\0" "2048\0")) {
            params.textureSize = kTextureSizes[sizeIndex];
        }
        ImGui::SetItemTooltip("%s", Tr(UiText::TipTextureSize));

        ImGui::SliderInt(Tr(UiText::LabelDilatePasses), &params.dilatePasses, 0, 24);
        ImGui::SetItemTooltip("%s", Tr(UiText::TipDilatePasses));

        ImGui::Separator();
        DrawNote(Tr(UiText::NotePhase45Bake));

        ImGui::End();
    }

    //------------------------------------------------------------------------
    //! 視点とライトのパネルを描きます。
    //------------------------------------------------------------------------
    void DrawViewPanel(PreviewViewSettings& view) {
        if(!ImGui::Begin(Tr(UiText::WindowView))) {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted(Tr(UiText::HeadingCamera));
        ImGui::SliderFloat(Tr(UiText::LabelYaw), &view.cameraYawDeg, -180.0f, 180.0f, "%.0f deg");
        ImGui::SliderFloat(Tr(UiText::LabelPitch), &view.cameraPitchDeg, -85.0f, 85.0f, "%.0f deg");
        ImGui::SliderFloat(Tr(UiText::LabelDistance), &view.cameraDistance, 1.2f, 6.0f, "%.2f x");
        ImGui::SliderFloat(Tr(UiText::LabelFov), &view.fovDeg, 20.0f, 90.0f, "%.0f deg");

        ImGui::Separator();
        ImGui::TextUnformatted(Tr(UiText::HeadingLight));

        // Phase 2 の検証はここを回して凹凸の陰影が追従するかを見る。
        // 追従すれば C++ の符号化とシェーダの ApplyNormalMap が整合している
        ImGui::SliderFloat(Tr(UiText::LabelLightYaw), &view.lightYawDeg, -180.0f, 180.0f, "%.0f deg");
        ImGui::SliderFloat(Tr(UiText::LabelLightPitch), &view.lightPitchDeg, 5.0f, 89.0f, "%.0f deg");
        ImGui::SliderFloat(Tr(UiText::LabelIntensity), &view.lightIntensity, 0.0f, 10.0f, "%.2f");

        DrawNote(Tr(UiText::NoteTbnCheck));

        ImGui::End();
    }

    //------------------------------------------------------------------------
    //! 生成結果のパネルを描きます。
    //------------------------------------------------------------------------
    void DrawStatsPanel(const PipelineSnapshot& snapshot, bool busy, float progress, const std::string& label) {
        if(!ImGui::Begin(Tr(UiText::WindowStats))) {
            ImGui::End();
            return;
        }

        if(busy) {
            ImGui::Text(Tr(UiText::FormatWorking), label.empty() ? "..." : label.c_str());
            ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
        } else {
            ImGui::TextUnformatted(Tr(UiText::StatusIdle));
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
        }

        ImGui::Separator();

        if(!snapshot.valid) {
            ImGui::TextUnformatted(Tr(UiText::StatusNoResult));
            ImGui::End();
            return;
        }

        ImGui::Text(Tr(UiText::FormatVertices), snapshot.mesh.vertexCount);
        ImGui::Text(Tr(UiText::FormatTriangles), snapshot.mesh.triangleCount);
        ImGui::Text(Tr(UiText::FormatSubdivisions), snapshot.mesh.subdivisions);
        ImGui::Text(Tr(UiText::FormatMaxEdge), snapshot.mesh.actualEdgeLength);

        ImGui::Separator();

        const int texelTotal = snapshot.textureSize * snapshot.textureSize;
        const float coverage = (texelTotal > 0) ? (static_cast<float>(snapshot.coveredTexels) / static_cast<float>(texelTotal)) : 0.0f;
        ImGui::Text(Tr(UiText::FormatUvCoverage), coverage * 100.0f);
        ImGui::Text(Tr(UiText::FormatBakedTexels), snapshot.normal.texelsWritten);
        ImGui::Text(Tr(UiText::FormatDegenerateTbn), snapshot.normal.degenerateTexels);

        ImGui::Separator();

        //--------------------------------------------------------------------
        // 段ごとの所要時間と実行回数。
        //
        // 色スライダを動かしたときに Unwrap と Bake の実行回数が増えないことを
        // ここで確かめる。これがダーティ判定が効いている証拠
        //--------------------------------------------------------------------
        if(ImGui::BeginTable("stages", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(Tr(UiText::ColumnStage));
            ImGui::TableSetupColumn(Tr(UiText::ColumnMilliseconds));
            ImGui::TableSetupColumn(Tr(UiText::ColumnRuns));
            ImGui::TableSetupColumn(Tr(UiText::ColumnDirty));
            ImGui::TableHeadersRow();

            for(RockCore::u32 i = 0; i < RockCore::kPipelineStageCount; ++i) {
                const RockCore::PipelineStageStatus& status = snapshot.stages[i];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Tr(static_cast<RockCore::PipelineStage>(i)));
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", status.milliseconds);
                ImGui::TableNextColumn();
                ImGui::Text("%u", status.runCount);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(status.dirty ? Tr(UiText::ValueDirtyYes) : Tr(UiText::ValueDirtyNo));
            }

            ImGui::EndTable();
        }

        ImGui::End();
    }

    //------------------------------------------------------------------------
    //! 設定と保存・読み込みのパネルを描きます。
    //------------------------------------------------------------------------
    bool DrawSettingsPanel(RockCore::RockParams& params,
                           EditorUiState&        ui,
                           EditorSettings&       settings,
                           bool                  japaneseFontAvailable) {
        bool languageChanged = false;

        if(!ImGui::Begin(Tr(UiText::WindowSettings))) {
            ImGui::End();
            return false;
        }

        //--------------------------------------------------------------------
        // 言語。
        //
        // 項目は翻訳しない。言語名は常にその言語自身で出すのが慣例で、
        // 英語表示中に「日本語」と出ている方が探しやすい
        //--------------------------------------------------------------------
        ImGui::TextUnformatted(Tr(UiText::HeadingSettings));

        int languageIndex = static_cast<int>(settings.language);

        // 日本語フォントが1つも読めていない環境で日本語を選ぶと全部 "?" に
        // なってしまうので、そのときは選ばせない
        ImGui::BeginDisabled(!japaneseFontAvailable);
        if(ImGui::Combo(Tr(UiText::LabelLanguage), &languageIndex, "English\0" "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\0")) {
            settings.language = static_cast<Language>(languageIndex);
            languageChanged   = true;
        }
        ImGui::EndDisabled();

        if(!japaneseFontAvailable) {
            DrawNote(Tr(UiText::TipLanguageFontMissing));
        }

        ImGui::Separator();

        //--------------------------------------------------------------------
        // 岩のパラメータの保存と読み込み
        //--------------------------------------------------------------------
        ImGui::TextUnformatted(Tr(UiText::HeadingParamsIo));

        char pathBuffer[260];
        std::snprintf(pathBuffer, sizeof(pathBuffer), "%s", ui.paramsFilePath.c_str());
        if(ImGui::InputText(Tr(UiText::LabelFilePath), pathBuffer, sizeof(pathBuffer))) {
            ui.paramsFilePath = pathBuffer;
        }

        if(ImGui::Button(Tr(UiText::ButtonSave))) {
            std::string error;
            if(RockCore::SaveRockParamsToFile(params, ui.paramsFilePath, &error)) {
                ui.ioStatus = Tr(UiText::PrefixSaved) + ui.paramsFilePath;
            } else {
                ui.ioStatus = Tr(UiText::PrefixSaveFailed) + error;
            }
        }

        ImGui::SameLine();
        if(ImGui::Button(Tr(UiText::ButtonLoad))) {
            std::string error;
            if(RockCore::LoadRockParamsFromFile(params, ui.paramsFilePath, &error)) {
                ui.ioStatus = Tr(UiText::PrefixLoaded) + ui.paramsFilePath;
            } else {
                ui.ioStatus = Tr(UiText::PrefixLoadFailed) + error;
            }
        }

        // 状態の文言は作られた時点の言語のまま残る。次に押したときに
        // 今の言語で書き直されるので、追いかけて訳し直す仕組みは持たない
        if(!ui.ioStatus.empty()) {
            ImGui::TextWrapped("%s", ui.ioStatus.c_str());
        }

        ImGui::Separator();
        ImGui::Checkbox(Tr(UiText::LabelImGuiDemo), &ui.showImGuiDemo);

        ImGui::End();
        return languageChanged;
    }

}    // namespace RockEditor
