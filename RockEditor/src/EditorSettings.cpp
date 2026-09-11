//----------------------------------------------------------------------------
//! @file   EditorSettings.cpp
//! @brief  エディタ設定の保存と読み込みの実装
//----------------------------------------------------------------------------
#include <RockEditor/EditorSettings.hpp>

#include <Tsukino/Core/Log.hpp>

#include <cereal/archives/json.hpp>

#include <windows.h>

#include <fstream>
#include <sstream>

// 名前空間 RockEditor
namespace RockEditor {

    //------------------------------------------------------------------------
    //! EditorSettings を直列化します。
    //!
    //! RockParamsJson.cpp と同じ流儀で、cereal のヘッダはこの .cpp に閉じて
    //! あります（EditorSettings.hpp を include するだけで cereal を
    //! 引きずり込まないようにするため）。
    //!
    //! @tparam Archive           アーカイブの型
    //! @param  [in,out] ar       アーカイブ
    //! @param  [in,out] settings 対象の設定
    //------------------------------------------------------------------------
    template <class Archive>
    void serialize(Archive& ar, EditorSettings& settings) {
        ar(cereal::make_nvp("language", settings.language));
    }

    //------------------------------------------------------------------------
    //! OS の UI 言語から初期値を決めます。
    //------------------------------------------------------------------------
    Language DetectSystemLanguage() {
        const LANGID langId = GetUserDefaultUILanguage();
        return (PRIMARYLANGID(langId) == LANG_JAPANESE) ? Language::Japanese : Language::English;
    }

    //------------------------------------------------------------------------
    //! 設定を読み込みます。
    //------------------------------------------------------------------------
    bool LoadEditorSettings(EditorSettings& outSettings, const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if(!file) {
            // 初回起動。呼び出し側が OS 判定へ落とすので、ここでは黙る
            return false;
        }

        try {
            // 失敗したときに呼び出し側を半端な状態にしないよう、一時物へ読む
            EditorSettings loaded{};

            cereal::JSONInputArchive archive(file);
            archive(cereal::make_nvp("editorSettings", loaded));

            // 手で壊された値で落ちないようにする
            if(loaded.language != Language::English && loaded.language != Language::Japanese) {
                loaded.language = Language::English;
            }

            outSettings = loaded;
            return true;
        } catch(const std::exception& error) {
            Tsukino::Core::Log::Warn(std::string("Failed to read the editor settings, falling back to the system language: ") +
                                     error.what());
            return false;
        }
    }

    //------------------------------------------------------------------------
    //! 設定を保存します。
    //------------------------------------------------------------------------
    bool SaveEditorSettings(const EditorSettings& settings, const std::string& filePath) {
        std::ostringstream stream;
        {
            // JSONOutputArchive は破棄時に閉じ括弧を書くため、
            // stream.str() より先にスコープを閉じる必要がある
            cereal::JSONOutputArchive archive(stream);
            archive(cereal::make_nvp("editorSettings", settings));
        }

        std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
        if(!file) {
            Tsukino::Core::Log::Warn("Failed to open the editor settings file for writing: " + filePath);
            return false;
        }

        const std::string json = stream.str();
        file.write(json.data(), static_cast<std::streamsize>(json.size()));

        if(!file) {
            Tsukino::Core::Log::Warn("Failed to write the editor settings file: " + filePath);
            return false;
        }
        return true;
    }

}    // namespace RockEditor
