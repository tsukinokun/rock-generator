@echo off
rem ---------------------------------------------------------------------------
rem ビルド済みの RockEditor を起動する。
rem   run.bat            … Debug を起動
rem   run.bat Release    … Release を起動
rem 追加の引数はそのまま exe へ渡す。
rem ---------------------------------------------------------------------------
setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
shift

set "ROOT=%~dp0"
set "EXE=%ROOT%bin\%CONFIG%\RockEditor.exe"

if not exist "%EXE%" (
    echo run.bat: not built. run "build.bat %CONFIG%" first.
    exit /b 1
)

rem Debug は debugdir がリポジトリルート、Release は exe の隣がカレントになる
rem （tsukino_release_payload() がそう設定している）
if /i "%CONFIG%"=="Release" (
    cd /d "%ROOT%bin\Release"
) else (
    cd /d "%ROOT%"
)

"%EXE%" %1 %2 %3 %4 %5
exit /b %ERRORLEVEL%
