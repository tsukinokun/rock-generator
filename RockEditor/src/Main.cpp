//----------------------------------------------------------------------------
//! @file   Main.cpp
//! @brief  RockEditor のエントリポイント
//! @detail WindowedApp なので WinMain から入ります。中身は App へ丸投げします。
//----------------------------------------------------------------------------
#include <RockEditor/App.hpp>

#include <windows.h>

//----------------------------------------------------------------------------
//! エントリポイントです。
//! @param  [in] hInstance     インスタンスハンドル
//! @param  [in] hPrevInstance 常に nullptr（16bit 時代の遺産）
//! @param  [in] lpCmdLine     コマンドライン
//! @param  [in] nShowCmd      ウィンドウの表示状態
//! @return 終了コード
//----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;

    RockEditor::App app;
    return app.Run();
}
