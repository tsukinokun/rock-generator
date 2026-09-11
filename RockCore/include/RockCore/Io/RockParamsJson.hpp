//----------------------------------------------------------------------------
//! @file   RockParamsJson.hpp
//! @brief  RockParams の JSON 保存と読み込み
//! @detail 直列化は cereal で行います。cereal のヘッダをここへ持ち込まないのは、
//!         RockParams を使うすべてのファイルが cereal に引きずられないように
//!         するためです（実装は .cpp に閉じています）。
//!
//!         cereal の JSON 入力はフィールド名が見つからないと例外を投げます。
//!         だから RockParams は Phase 3 以降で使うフィールドも最初から
//!         宣言してあります。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Shape/RockParams.hpp>

#include <string>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! パラメータを JSON 文字列へ書き出します。
    //! @param  [in] params 書き出すパラメータ
    //! @return JSON 文字列
    //------------------------------------------------------------------------
    std::string SaveRockParamsToString(const RockParams& params);

    //------------------------------------------------------------------------
    //! JSON 文字列からパラメータを読み込みます。
    //!
    //! 失敗したとき params は書き換わりません（途中まで読んだ状態を
    //! 呼び出し側へ渡さないよう、一時オブジェクトへ読んでから差し替えます）。
    //!
    //! @param  [out] outParams 読み込み先
    //! @param  [in]  json      JSON 文字列
    //! @param  [out] outError  失敗理由。nullptr でもよい
    //! @return 成功したら true
    //------------------------------------------------------------------------
    bool LoadRockParamsFromString(RockParams& outParams, const std::string& json, std::string* outError);

    //------------------------------------------------------------------------
    //! パラメータを JSON ファイルへ保存します。
    //! @param  [in]  params   保存するパラメータ
    //! @param  [in]  filePath 保存先のパス
    //! @param  [out] outError 失敗理由。nullptr でもよい
    //! @return 成功したら true
    //------------------------------------------------------------------------
    bool SaveRockParamsToFile(const RockParams& params, const std::string& filePath, std::string* outError);

    //------------------------------------------------------------------------
    //! JSON ファイルからパラメータを読み込みます。
    //! @param  [out] outParams 読み込み先
    //! @param  [in]  filePath  読み込むパス
    //! @param  [out] outError  失敗理由。nullptr でもよい
    //! @return 成功したら true
    //------------------------------------------------------------------------
    bool LoadRockParamsFromFile(RockParams& outParams, const std::string& filePath, std::string* outError);

}    // namespace RockCore
