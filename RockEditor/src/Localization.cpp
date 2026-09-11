//----------------------------------------------------------------------------
//! @file   Localization.cpp
//! @brief  UI 文字列の英日テーブル
//----------------------------------------------------------------------------
#include <RockEditor/Localization.hpp>

#include <Tsukino/Core/Log.hpp>

#include <cstring>
#include <iterator>
#include <string>

// 名前空間 RockEditor
namespace RockEditor {

    namespace {

        //--------------------------------------------------------------------
        // 英語テーブル。
        //
        // 並び順は UiText の宣言順と厳密に一致させること。
        // `###` より後ろは表示されず ID にだけ使われる（ヘッダの説明を参照）。
        //--------------------------------------------------------------------
        constexpr const char* kEnglish[] = {
            // ウィンドウ
            "Shape###shape",
            "Material / Bake###material",
            "View###view",
            "Stats###stats",
            "Settings / I/O###settings",

            // 形状パネル
            "Seed###seed",
            "Randomize###randomize",
            "Radius (m)###radius",
            "Aniso scale###aniso",
            "(1, 0.25, 1) gives a slab-like rock",
            "Target edge (m)###target_edge",
            "Target mesh edge length. Lower it to add triangles",
            "Base subdivision###base_subdiv",
            "Mesh nyquist frequency: %.1f",
            "Noise above this frequency is not put on the vertices. It is baked into the normal map",
            "Noise layers",
            "Layer %zu###layer",
            "Enabled###enabled",
            "Amplitude###amplitude",
            "Frequency###frequency",
            "Octaves###octaves",
            "Lacunarity###lacunarity",
            "Gain###gain",
            "Add layer###add_layer",
            "Remove layer###remove_layer",

            // マテリアル / ベイクパネル
            "Base color###base_color",
            "Roughness###roughness",
            "Metallic is fixed at 0 (rock)",
            "Texture size###texture_size",
            "The engine shrinks anything over 2048 and rounds to a multiple of 4, so only square sizes are offered",
            "Dilate passes###dilate",
            "Dilation of the coverage mask. Stops chart edges from bleeding",
            "Albedo / MR bake: Phase 4   AO bake: Phase 5",

            // 視点パネル
            "Camera",
            "Yaw###cam_yaw",
            "Pitch###cam_pitch",
            "Distance###cam_distance",
            "FOV###cam_fov",
            "Light",
            "Light yaw###light_yaw",
            "Light pitch###light_pitch",
            "Intensity###light_intensity",
            "If the relief shading follows the light as you rotate it, the TBN is consistent",

            // 統計パネル
            "Working: %s",
            "Idle",
            "No result yet.",
            "Vertices : %u",
            "Triangles: %u",
            "Subdiv   : %d",
            "Max edge : %.4f m",
            "UV coverage   : %.1f %%",
            "Baked texels  : %u",
            "Degenerate TBN: %u",
            "Stage###col_stage",
            "ms###col_ms",
            "Runs###col_runs",
            "Dirty###col_dirty",
            "yes",
            "-",

            // 設定 / 入出力パネル
            "Settings",
            "Language###language",
            "No Japanese font was found on this system",
            "Parameters",
            "File###params_file",
            "Save###params_save",
            "Load###params_load",
            "Saved: ",
            "Save failed: ",
            "Loaded: ",
            "Load failed: ",
            "ImGui demo window###imgui_demo",

            // パイプラインの段名
            "Field",
            "Mesh",
            "Unwrap",
            "Bake: Normal",

            //-- 破断面（Phase 3）
            "Preset###preset",
            "Apply a preset...",
            "Weathered outcrop",
            "Angular rock",
            "Rounded boulder",
            "Slab",

            "Fracture",
            "Cut planes###plane_count",
            "Number of fracture planes. 0 degenerates to a smooth lump",
            "Cut depth###plane_depth",
            "Axis bias###plane_axis",
            "Pulls the plane normals toward the vertical axis. Raise it for slabs",
            "Edge rounding###edge_rounding",
            "Weathering of the ridges. 0 is a fresh sharp break",
            "Crease angle###crease_angle",
            "Adjacent faces steeper than this become a hard edge",

            "Kind###layer_kind",
            "fBm",
            "Worley (cracks)",
            "Ridged",
            "Sharpness###sharpness",
            "Worley: groove width. Ridged: how sharp the ridges are",

            "Cut planes    : %u",
            "Split vertices: %u",

            "UV unwrap",
            "Method###unwrap_method",
            "xatlas evens out texel density across the facets. Octahedral is "
            "instant but loses resolution on faces pointing at the square corners",
            "Octahedral (fast)",
            "xatlas (even density)",
            "Texel density###texels_per_unit",
            "Texels per metre. 0 lets xatlas fit the charts to the texture, "
            "which is almost always what you want for a single rock",
            "Chart padding###uv_padding",
            "Gap between charts in texels. Too small and dilation bleeds one chart into its neighbour",

            "Unwrap        : %s",
            "Charts        : %u",
            "Atlas usage   : %.1f %%",
            "xatlas could not fit a single atlas. Fell back to octahedral projection",
        };

        //--------------------------------------------------------------------
        // 日本語テーブル。
        //
        // fBm / TBN / UV / Normal / Worley / Ridged / ナイキスト のように
        // 定着した語は無理に訳さない。訳すと他のツールや文献と用語がずれて、
        // かえって引けなくなる。
        //--------------------------------------------------------------------
        constexpr const char* kJapanese[] = {
            // ウィンドウ
            "形状###shape",
            "マテリアル / ベイク###material",
            "視点###view",
            "統計###stats",
            "設定 / 入出力###settings",

            // 形状パネル
            "シード###seed",
            "ランダム###randomize",
            "半径 (m)###radius",
            "異方スケール###aniso",
            "(1, 0.25, 1) で板状の岩になる",
            "目標エッジ長 (m)###target_edge",
            "メッシュの目標エッジ長。小さくすると三角形が増える",
            "基本細分割回数###base_subdiv",
            "メッシュのナイキスト周波数: %.1f",
            "この周波数を超えるノイズは頂点に載せず、Normal マップへ焼く",
            "ノイズ層",
            "層 %zu###layer",
            "有効###enabled",
            "振幅###amplitude",
            "周波数###frequency",
            "オクターブ数###octaves",
            "ラクナリティ###lacunarity",
            "ゲイン###gain",
            "層を追加###add_layer",
            "層を削除###remove_layer",

            // マテリアル / ベイクパネル
            "基本色###base_color",
            "ラフネス###roughness",
            "メタリックは 0 固定（岩のため）",
            "テクスチャの一辺###texture_size",
            "エンジンが 2048 超を縮小し4の倍数へ丸めるため、正方形の3択のみ",
            "dilate のパス数###dilate",
            "被覆マスクの膨張。チャート境界の滲み対策",
            "Albedo / MR ベイク: Phase 4   AO ベイク: Phase 5",

            // 視点パネル
            "カメラ",
            "方位角###cam_yaw",
            "仰角###cam_pitch",
            "距離###cam_distance",
            "画角###cam_fov",
            "ライト",
            "ライト方位角###light_yaw",
            "ライト仰角###light_pitch",
            "強度###light_intensity",
            "ライトを回して凹凸の陰影が追従すれば TBN は整合している",

            // 統計パネル
            "処理中: %s",
            "待機中",
            "まだ結果がありません。",
            "頂点数   : %u",
            "三角形数 : %u",
            "細分割   : %d",
            "最長エッジ: %.4f m",
            "UV 被覆率 : %.1f %%",
            "焼いたテクセル: %u",
            "縮退 TBN  : %u",
            "段階###col_stage",
            "ms###col_ms",
            "実行回数###col_runs",
            "要更新###col_dirty",
            "はい",
            "-",

            // 設定 / 入出力パネル
            "設定",
            "言語###language",
            "この環境には日本語フォントが見つかりませんでした",
            "パラメータ",
            "ファイル###params_file",
            "保存###params_save",
            "読み込み###params_load",
            "保存しました: ",
            "保存に失敗しました: ",
            "読み込みました: ",
            "読み込みに失敗しました: ",
            "ImGui デモウィンドウ###imgui_demo",

            // パイプラインの段名
            "場",
            "メッシュ",
            "UV 展開",
            "ベイク: Normal",

            //-- 破断面（Phase 3）
            "プリセット###preset",
            "プリセットを適用...",
            "風化した露岩",
            "角張った岩",
            "丸い転石",
            "板状の岩",

            "破断面",
            "破断面の枚数###plane_count",
            "割れた面の数。0 にすると丸い塊へ退化する",
            "食い込む深さ###plane_depth",
            "軸バイアス###plane_axis",
            "平面の法線を上下へ寄せる。板状の岩で上げる",
            "稜線の丸め###edge_rounding",
            "風化による角の落ち具合。0 で割れたての鋭い角",
            "クリース角###crease_angle",
            "これより折れている隣接面の境目をハードエッジにする",

            "種類###layer_kind",
            "fBm",
            "Worley（割れ目）",
            "Ridged",
            "鋭さ###sharpness",
            "Worley は溝の幅、Ridged は稜線の尖り具合",

            "破断面      : %u",
            "分割した頂点: %u",

            "UV 展開",
            "方式###unwrap_method",
            "xatlas は破断面ごとのテクセル密度の差を無くす。八面体射影は一瞬で"
            "終わるが、正方形の角を向いた面だけ解像度が落ちる",
            "八面体射影（速い）",
            "xatlas（密度が揃う）",
            "テクセル密度###texels_per_unit",
            "1 メートルあたりのテクセル数。0 なら xatlas がテクスチャに収まる"
            "密度を見積もる。岩 1 個を 1 枚へ焼くならほぼ常にこれでよい",
            "チャートの余白###uv_padding",
            "チャート間の隙間（テクセル）。狭すぎると dilate が隣のチャートへ染み出す",

            "展開        : %s",
            "チャート数  : %u",
            "アトラス占有: %.1f %%",
            "xatlas がアトラス 1 枚に収められなかった。八面体射影へ落ちている",
        };

        constexpr size_t kTextCount = static_cast<size_t>(UiText::Count);

        static_assert(std::size(kEnglish) == kTextCount, "kEnglish の項目数が UiText::Count と一致していない");
        static_assert(std::size(kJapanese) == kTextCount, "kJapanese の項目数が UiText::Count と一致していない");

        //! 現在の言語。メインスレッドだけが読み書きする UI の状態
        Language g_language = Language::English;

        //--------------------------------------------------------------------
        //! `###` 以降（= ID 部分）を返します。無ければ空文字列を返します。
        //! @param  [in] text 対象の文字列
        //! @return ID 部分の先頭
        //--------------------------------------------------------------------
        const char* FindIdSuffix(const char* text) {
            const char* found = std::strstr(text, "###");
            return found ? found : "";
        }

        //--------------------------------------------------------------------
        //! 書式指定子の並びを取り出します。
        //!
        //! 統計パネルの文字列は ImGui::Text へ書式として渡るため、訳文の
        //! 指定子が元とずれていると未定義動作になる（%u のつもりの場所へ
        //! %s が来れば即クラッシュ）。ID の食い違いより危険なので検査する。
        //!
        //! フラグ・幅・精度を読み飛ばして変換文字だけを拾う簡易版。
        //! "%%" はリテラルの % なので数えない。
        //!
        //! @param  [in] text 対象の文字列
        //! @return 変換文字を順に並べた文字列（例: "%.1f %%" なら "f"）
        //--------------------------------------------------------------------
        std::string ExtractFormatSpecifiers(const char* text) {
            std::string specifiers;

            for(const char* cursor = text; *cursor != '\0'; ++cursor) {
                if(*cursor != '%') {
                    continue;
                }

                ++cursor;
                if(*cursor == '\0') {
                    break;
                }
                if(*cursor == '%') {
                    // リテラルの %
                    continue;
                }

                // フラグ・幅・精度・長さ修飾子を読み飛ばして変換文字まで進む
                while(*cursor != '\0' && std::strchr("-+ #0123456789.*hlLjzt", *cursor) != nullptr) {
                    ++cursor;
                }
                if(*cursor == '\0') {
                    break;
                }
                specifiers.push_back(*cursor);
            }

            return specifiers;
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! 表示する言語を設定します。
    //------------------------------------------------------------------------
    void SetLanguage(Language language) {
        if(language == Language::Count) {
            return;
        }
        g_language = language;
    }

    //------------------------------------------------------------------------
    //! 現在の言語を返します。
    //------------------------------------------------------------------------
    Language GetLanguage() {
        return g_language;
    }

    //------------------------------------------------------------------------
    //! 現在の言語の文字列を返します。
    //------------------------------------------------------------------------
    const char* Tr(UiText text) {
        const size_t index = static_cast<size_t>(text);
        if(index >= kTextCount) {
            return "";
        }
        return (g_language == Language::Japanese) ? kJapanese[index] : kEnglish[index];
    }

    //------------------------------------------------------------------------
    //! パイプラインの段の表示名を返します。
    //------------------------------------------------------------------------
    const char* Tr(RockCore::PipelineStage stage) {
        switch(stage) {
        case RockCore::PipelineStage::Field:      return Tr(UiText::StageField);
        case RockCore::PipelineStage::Mesh:       return Tr(UiText::StageMesh);
        case RockCore::PipelineStage::Unwrap:     return Tr(UiText::StageUnwrap);
        case RockCore::PipelineStage::BakeNormal: return Tr(UiText::StageBakeNormal);
        default:                                  return "?";
        }
    }

    //------------------------------------------------------------------------
    //! 翻訳テーブルの整合を検査します。
    //------------------------------------------------------------------------
    bool VerifyLocalizationTables() {
        bool ok = true;

        for(size_t i = 0; i < kTextCount; ++i) {
            if(!kEnglish[i] || !kJapanese[i]) {
                Tsukino::Core::Log::Error("Localization: entry " + std::to_string(i) + " is null.");
                ok = false;
                continue;
            }

            // ここが本命。ID 部分が食い違うと、言語を切り替えた瞬間だけ
            // ドック配置とウィジェットの状態が失われる
            const char* englishId  = FindIdSuffix(kEnglish[i]);
            const char* japaneseId = FindIdSuffix(kJapanese[i]);

            if(std::strcmp(englishId, japaneseId) != 0) {
                Tsukino::Core::Log::Error("Localization: the ### id differs between languages at entry " +
                                          std::to_string(i) + " ('" + englishId + "' vs '" + japaneseId +
                                          "'). Switching the language would break the dock layout.");
                ok = false;
            }

            const std::string englishSpecifiers  = ExtractFormatSpecifiers(kEnglish[i]);
            const std::string japaneseSpecifiers = ExtractFormatSpecifiers(kJapanese[i]);

            if(englishSpecifiers != japaneseSpecifiers) {
                Tsukino::Core::Log::Error("Localization: the format specifiers differ between languages at entry " +
                                          std::to_string(i) + " ('" + englishSpecifiers + "' vs '" + japaneseSpecifiers +
                                          "'). Passing the translated string to ImGui::Text would be undefined behaviour.");
                ok = false;
            }
        }

        return ok;
    }

}    // namespace RockEditor
