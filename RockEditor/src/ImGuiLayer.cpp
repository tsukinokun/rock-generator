//----------------------------------------------------------------------------
//! @file   ImGuiLayer.cpp
//! @brief  ImGui の初期化と Overlay への積み込みの実装
//----------------------------------------------------------------------------
#include <RockEditor/ImGuiLayer.hpp>

#include <Tsukino/Core/Log.hpp>
#include <Tsukino/Renderer/DrawCommand.hpp>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>

#include <climits>

//! imgui_impl_win32.cpp が定義する。ヘッダには宣言が無いのでここで引く
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 名前空間 RockEditor
namespace RockEditor {

    namespace {

        //--------------------------------------------------------------------
        //! 日本語が出るフォントを読み込みます。
        //!
        //! ImGui の既定フォント（ProggyClean）は ASCII しか持たないため、
        //! 説明を日本語で書くと全部 "?" になる。Windows に必ず入っている
        //! フォントから、見つかった最初の1つを日本語の字形範囲付きで読む。
        //!
        //! 見つからなくても致命的ではない（既定フォントのままになる）ので
        //! 失敗を報告しない。
        //--------------------------------------------------------------------
        void LoadJapaneseFont() {
            // Meiryo UI → Meiryo → 游ゴシック の順で探す。
            // どれか1つは実質すべての Windows に入っている
            const char* candidates[] = {
                "C:/Windows/Fonts/meiryo.ttc",
                "C:/Windows/Fonts/YuGothM.ttc",
                "C:/Windows/Fonts/msgothic.ttc",
            };

            ImGuiIO& io = ImGui::GetIO();
            for(const char* path : candidates) {
                if(io.Fonts->AddFontFromFileTTF(path, 17.0f, nullptr, io.Fonts->GetGlyphRangesJapanese())) {
                    return;
                }
            }
        }

    }    // namespace

    //------------------------------------------------------------------------
    //! 後片付けします。
    //------------------------------------------------------------------------
    ImGuiLayer::~ImGuiLayer() {
        if(!m_initialized) {
            return;
        }

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_initialized = false;
    }

    //------------------------------------------------------------------------
    //! ImGui を初期化し、ウィンドウのメッセージを繋ぎます。
    //------------------------------------------------------------------------
    bool ImGuiLayer::Initialize(Tsukino::Core::Window& window, Tsukino::Renderer::Renderer& renderer) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        //--------------------------------------------------------------------
        // 補い 1/3: カーソル形状を ImGui に触らせない。
        //
        // Window のメッセージフィルタは WM_SETCURSOR を転送しないため、
        // ImGui がカーソルを変えようとしても OS 側が即座に元へ戻してしまう。
        // 中途半端にちらつくより、変えない方が素直
        //--------------------------------------------------------------------
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        // .ini をリポジトリへ吐かせない。レイアウトはコードで敷く
        io.IniFilename = nullptr;

        LoadJapaneseFont();

        ImGui::StyleColorsDark();

        if(!ImGui_ImplWin32_Init(window.GetHWND())) {
            Tsukino::Core::Log::Error("Failed to initialize the ImGui Win32 backend.");
            return false;
        }

        if(!ImGui_ImplDX11_Init(renderer.GetDevice(), renderer.GetContext())) {
            Tsukino::Core::Log::Error("Failed to initialize the ImGui DX11 backend.");
            ImGui_ImplWin32_Shutdown();
            return false;
        }

        //--------------------------------------------------------------------
        // 入力の転送。
        //
        // 転送されるのは WM_KEYFIRST..WM_KEYLAST（WM_CHAR を含む）と
        // WM_MOUSEFIRST..WM_MOUSELAST（ホイールを含む）だけ
        //--------------------------------------------------------------------
        window.SetMessageCallback([hwnd = window.GetHWND()](UINT msg, WPARAM wParam, LPARAM lParam) {
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
        });

        //--------------------------------------------------------------------
        // 補い 2/3: フォーカスを失ったときのキー残留。
        //
        // WM_KILLFOCUS は転送されないので、Alt+Tab で離れると押していたキーが
        // 押されたままになる。Window 側の専用コールバックで落とす
        //--------------------------------------------------------------------
        window.SetFocusLostCallback([]() { ImGui::GetIO().ClearInputKeys(); });

        m_window      = &window;
        m_initialized = true;
        return true;
    }

    //------------------------------------------------------------------------
    //! フレームを開始します。
    //------------------------------------------------------------------------
    void ImGuiLayer::BeginFrame() {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();

        //--------------------------------------------------------------------
        // 補い 3/3: ウィンドウ外へ出たマウスの hover 残留。
        //
        // WM_MOUSELEAVE (0x02A3) は WM_MOUSELAST (0x020E) より上なので
        // メッセージフィルタを通らない。カーソルがクライアント領域の外に
        // あるかを毎フレーム自分で見て、外なら座標を無効値にする
        //--------------------------------------------------------------------
        if(m_window) {
            POINT cursor{};
            if(GetCursorPos(&cursor) && ScreenToClient(m_window->GetHWND(), &cursor)) {
                const bool outside = cursor.x < 0 || cursor.y < 0 || cursor.x >= m_window->GetWidth() ||
                                     cursor.y >= m_window->GetHeight();
                if(outside) {
                    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                }
            }
        }

        ImGui::NewFrame();
    }

    //------------------------------------------------------------------------
    //! 画面全体を覆うドックスペースを敷きます。
    //------------------------------------------------------------------------
    void ImGuiLayer::BeginDockSpace() {
        // パネルだけをドックし、3Dプレビューは背景として素通しにしたいので
        // ドックスペース自身は描画しない（PassthruCentralNode）
        const ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode;
        const ImGuiID            dockId    = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockFlags);

        if(m_layoutBuilt) {
            return;
        }
        m_layoutBuilt = true;

        //--------------------------------------------------------------------
        // 既定のレイアウトを組む。
        //
        // io.IniFilename を nullptr にしてあるので、ImGui は前回の配置を
        // 覚えていない。何もしないと全パネルが同じ位置に重なって出るため、
        // 起動のたびにここで同じ配置を作る（毎回同じ画面になる方が、
        // ツールとしては .ini が散らかるより扱いやすい）
        //--------------------------------------------------------------------
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, dockFlags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->Size);

        ImGuiID center = dockId;
        ImGuiID left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
        ImGuiID right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);

        ImGuiID leftBottom  = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.35f, nullptr, &left);
        ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.60f, nullptr, &right);
        ImGuiID rightIo     = ImGui::DockBuilderSplitNode(rightBottom, ImGuiDir_Down, 0.40f, nullptr, &rightBottom);

        ImGui::DockBuilderDockWindow("Shape", left);
        ImGui::DockBuilderDockWindow("Material / Bake", leftBottom);
        ImGui::DockBuilderDockWindow("View", right);
        ImGui::DockBuilderDockWindow("Stats", rightBottom);
        ImGui::DockBuilderDockWindow("Parameters I/O", rightIo);

        ImGui::DockBuilderFinish(dockId);
    }

    //------------------------------------------------------------------------
    //! 描画コマンドを Overlay パスへ積みます。
    //------------------------------------------------------------------------
    void ImGuiLayer::SubmitDrawCommand(Tsukino::Renderer::Renderer& renderer) {
        ImGui::Render();

        Tsukino::Renderer::DrawCommand command{};
        command.pass = Tsukino::Renderer::RenderPass::Overlay;

        // Overlay は sortOrder の昇順に実行される。UI は最後に重ねる
        command.sortOrder = INT_MAX;

        // customDraw があると ExecuteDrawCommand はパイプラインもマテリアルも
        // 要求しない。RTV とビューポートは呼び出し元が Tonemap 後の
        // バックバッファを張った状態で入ってくるので、ここで触ってはいけない
        command.customDraw = [](ID3D11DeviceContext*) { ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); };

        renderer.PushDrawCommand(command);
    }

}    // namespace RockEditor
