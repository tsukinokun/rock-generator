//----------------------------------------------------------------------------
//! @file   EditorSettings.hpp
//! @brief  エディタ自身の設定の保存と読み込み
//! @detail RockParams とは別に持ちます。言語は岩のパラメータではないので、
//!         RockParams の JSON へ混ぜると、CombatAndroid へ持って行った岩の
//!         定義に無意味なフィールドが残ってしまいます。
//!
//!         置き場はカレントディレクトリです。run.bat の作り上、Debug では
//!         リポジトリルート、Release では exe の隣になります
//!         （tsukino_release_payload() の debugdir 設定どおり）。
//----------------------------------------------------------------------------
#pragma once
#include <RockEditor/Localization.hpp>

#include <string>

// 名前空間 RockEditor
namespace RockEditor {

    //------------------------------------------------------------------------
    //! @struct EditorSettings
    //! 起動をまたいで覚えておきたいエディタの設定
    //------------------------------------------------------------------------
    struct EditorSettings {
        Language language = Language::English;
    };

    //! 設定ファイルの既定のパス
    inline constexpr const char* kEditorSettingsFileName = "RockEditor.settings.json";

    //------------------------------------------------------------------------
    //! OS の UI 言語から初期値を決めます。
    //!
    //! 設定ファイルがまだ無い初回起動でだけ使います。
    //!
    //! @return 日本語環境なら Language::Japanese、それ以外は English
    //------------------------------------------------------------------------
    Language DetectSystemLanguage();

    //------------------------------------------------------------------------
    //! 設定を読み込みます。
    //!
    //! ファイルが無いのは初回起動なので正常系です。false を返しますが
    //! ログにも出しません（呼び出し側が OS 判定へ落とす）。
    //!
    //! @param  [out] outSettings 読み込み先
    //! @param  [in]  filePath    読み込むパス
    //! @return 読み込めたら true
    //------------------------------------------------------------------------
    bool LoadEditorSettings(EditorSettings& outSettings, const std::string& filePath);

    //------------------------------------------------------------------------
    //! 設定を保存します。
    //! @param  [in] settings 保存する設定
    //! @param  [in] filePath 保存先のパス
    //! @return 保存できたら true
    //------------------------------------------------------------------------
    bool SaveEditorSettings(const EditorSettings& settings, const std::string& filePath);

}    // namespace RockEditor
