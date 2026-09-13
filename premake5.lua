-- RockGenerator — 岩プロシージャル生成エディタのビルド定義。
--
-- TsukinoEngine のヘルパーを先に読み込む（workspace 宣言より前）。
-- include / link / 配布物の定義はエンジン側の1箇所にあり、ここへは書き写さない。
-- architecture・configurations・/utf-8・NDEBUG・JPH_DEBUG_RENDERER はすべて
-- tsukino_workspace_defaults() が持っているので、ここで重複して書かないこと。
--
-- 再生成: open.bat（= External\TsukinoEngine\vendor\premake5.exe vs2022）
include "External/TsukinoEngine/Tools/premake/tsukino.lua"

-- vendored 3rd party。project をまたいで使うのでここで一度だけ定義する
local IMGUI_DIR  = "External/imgui"
local XATLAS_DIR = "External/xatlas/source/xatlas"

-- cereal はヘッダオンリーで、エンジンの submodule として既に存在する。
-- tsukino_link() の includedirs にも入っているが、tsukino_link() はエンジン8モジュール
-- 全部を links に入れてしまうため RockCore では呼べない。RockParams の JSON 直列化に
-- しか使わないので、includedir 1本だけ借りる形にしてある
local CEREAL_DIR = "External/TsukinoEngine/External/cereal/include"

-- Assimp は tsukino_link() の nuget（AssimpCpp:5.0.1.6）で手に入る。
-- 復元先は親（このリポジトリ）の .build/packages
local ASSIMP_INCLUDE_DIR = ".build/packages/AssimpCpp.5.0.1.6/build/native/include"

workspace "RockGenerator"
    startproject "RockEditor"
    location ".build"

    -- アーキテクチャ・構成・警告まわりはエンジンと共通の設定を使う
    tsukino_workspace_defaults()

include "External/TsukinoEngine"

--------------------------------------------------------------
-- RockCore — 岩の生成そのもの。
--
-- DX11 / COM / ImGui / Assimp / Windows / TsukinoEngine に一切依存しない。
-- 依存は標準ライブラリ + xatlas + cereal（ヘッダオンリー）だけ。
-- グローバル状態を持たず、RockParams と seed から決定論的に結果が決まる。
-- この純粋性を守ると CLI とテストが自動で成立する。
--------------------------------------------------------------
project "RockCore"
    location ".build/RockCore"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/%{cfg.buildcfg}")
    objdir ("bin-int/%{cfg.buildcfg}")

    files {
        "RockCore/src/**.cpp",
        "RockCore/include/**.hpp",
        -- xatlas は単一 .cpp なので独立 project は作らず直接ビルドする
        XATLAS_DIR .. "/xatlas.cpp",
    }

    includedirs {
        "RockCore/include",
        XATLAS_DIR,
        CEREAL_DIR,
    }

    filter "action:vs*"
        buildoptions { "/permissive-" }
    filter {}

    -- 3rd party のソースを自分の警告設定で測らない
    filter "files:External/**.cpp"
        warnings "Off"
    filter {}

--------------------------------------------------------------
-- RockExport — 成果物の書き出し。
--
-- 自前 GlbWriter / FbxExporter(Assimp) / PngWriter。
-- 中身の実装は Phase 6。今は骨だけ。
--------------------------------------------------------------
project "RockExport"
    location ".build/RockExport"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/%{cfg.buildcfg}")
    objdir ("bin-int/%{cfg.buildcfg}")

    files {
        "RockExport/src/**.cpp",
        "RockExport/include/**.hpp",
    }

    includedirs {
        "RockCore/include",
        "RockExport/include",
        CEREAL_DIR,
        ASSIMP_INCLUDE_DIR,
    }

    links { "RockCore" }

    filter "action:vs*"
        buildoptions { "/permissive-" }
    filter {}

--------------------------------------------------------------
-- RockEditor — GUI 本体。
--
-- EngineIntegration / Scene / ECS は使わず、Window + Renderer を直接
-- ブートストラップする。ImGui はここだけに閉じる。
--------------------------------------------------------------
project "RockEditor"
    location ".build/RockEditor"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/%{cfg.buildcfg}")
    objdir ("bin-int/%{cfg.buildcfg}")

    files {
        "RockEditor/src/**.cpp",
        "RockEditor/include/**.hpp",

        -- ImGui。docking ブランチの submodule 内のものだけを使い、
        -- 別バージョンの backends を混ぜないこと
        IMGUI_DIR .. "/imgui.cpp",
        IMGUI_DIR .. "/imgui_draw.cpp",
        IMGUI_DIR .. "/imgui_tables.cpp",
        IMGUI_DIR .. "/imgui_widgets.cpp",
        IMGUI_DIR .. "/imgui_demo.cpp",
        IMGUI_DIR .. "/backends/imgui_impl_win32.cpp",
        IMGUI_DIR .. "/backends/imgui_impl_dx11.cpp",
    }

    includedirs {
        "RockCore/include",
        "RockExport/include",
        "RockEditor/include",
        CEREAL_DIR,
        IMGUI_DIR,
        IMGUI_DIR .. "/backends",
    }

    links { "RockCore", "RockExport" }

    -- エンジンの include・lib・NuGet
    tsukino_link()

    -- 実行時の基準ディレクトリと、エンジンが持ち込む Release 配布物
    -- （組み込み Assets / Tools / エンジン側のライセンス条文）
    tsukino_release_payload()

    filter "action:vs*"
        buildoptions { "/permissive-" }
    filter {}

    filter "files:External/**.cpp"
        warnings "Off"
    filter {}

    --------------------------------------------------------------
    -- このツール自身のライセンス条文を Release 配布物へ置く。
    -- ImGui と xatlas は exe にコードが取り込まれるため、exe を配った時点で
    -- MIT のバイナリ再配布に当たる。
    --
    -- ファイル名を素の LICENSE / THIRD_PARTY_NOTICES.md にしないのは、
    -- tsukino_release_payload() がその名前でエンジン側の条文を既に置いており、
    -- 後から走るこちらの COPYFILE が上書きしてしまうため
    --------------------------------------------------------------
    filter "configurations:Release"
        postbuildcommands {
            "{COPYFILE} %{wks.location}/../LICENSE %{cfg.targetdir}/LICENSE.RockGenerator.txt",
            "{COPYFILE} %{wks.location}/../THIRD_PARTY_NOTICES.md %{cfg.targetdir}/THIRD_PARTY_NOTICES.RockGenerator.md",
        }
    filter {}

--------------------------------------------------------------
-- RockCli — バッチ生成（Phase 7）。
--
-- RockCore + RockExport だけを使う。DX11 もウィンドウも要らないことが
-- コアの純粋性の検証になっている。
--------------------------------------------------------------
project "RockCli"
    location ".build/RockCli"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/%{cfg.buildcfg}")
    objdir ("bin-int/%{cfg.buildcfg}")

    files {
        "RockCli/src/**.cpp",
    }

    -- Phase 0 のラウンドトリップ検証ハーネスは main() を持つ使い捨てで、
    -- premake 経路ではビルドしない（Tools/build_phase0.bat から cl.exe 直叩き）
    removefiles { "RockCli/src/Phase0RoundTrip.cpp" }

    includedirs {
        "RockCore/include",
        "RockExport/include",
        CEREAL_DIR,
        ASSIMP_INCLUDE_DIR,
    }

    links { "RockCore", "RockExport" }

    -- RockExport が Assimp のシンボルを未解決のまま持っているので、
    -- 最終的にリンクする exe 側（ここ）で実体を持ってくる必要がある。
    -- RockExport 自身は StaticLib で最終リンクをしないため nuget は要らない
    -- （ヘッダだけ ASSIMP_INCLUDE_DIR 経由で見えていれば足りる）。
    -- RockEditor は tsukino_link() が同じパッケージを内部で引くので二重にはならない
    nuget {
        "AssimpCpp:5.0.1.6",
    }

    filter "action:vs*"
        buildoptions { "/permissive-" }
    filter {}
