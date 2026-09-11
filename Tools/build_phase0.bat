@echo off
rem ---------------------------------------------------------------
rem Phase 0 の使い捨てハーネスをビルドする。
rem
rem premake もエンジンのビルドも通さず、cl.exe で直接叩く。
rem Phase 0 で必要なのは Assimp だけで、エンジン一式をビルドすると
rem 検証の回転が遅くなるため。
rem
rem Assimp は CombatAndoroid が使っているのと同じ NuGet パッケージを
rem そのまま参照する（バージョンが食い違うと実測の意味が無くなる）。
rem ---------------------------------------------------------------
setlocal

set ROOT=%~dp0..
set ASSIMP=%USERPROFILE%\.nuget\packages\assimpcpp\5.0.1.6\build\native
set OUT=%ROOT%in\Phase0

if not exist "%ASSIMP%\include" (
    echo [!] Assimp NuGet package not found at %ASSIMP%
    echo     Build CombatAndoroid once to restore it.
    exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [!] failed to set up the MSVC environment
    exit /b 1
)

cl /nologo /std:c++20 /EHsc /MD /W3 /utf-8 ^
   /I "%ASSIMP%\include" ^
   "%ROOT%\RockCli\src\Phase0RoundTrip.cpp" ^
   /Fe:"%OUT%\phase0.exe" /Fo:"%OUT%\\" ^
   /link "%ASSIMP%\lib\Release\assimp-vc142-mt.lib"
if errorlevel 1 exit /b 1

rem Assimp は DLL なので exe の隣へ置く
copy /y "%ASSIMP%\bin\Release\assimp-vc142-mt.dll" "%OUT%\" >nul

echo build ok: %OUT%\phase0.exe
endlocal
