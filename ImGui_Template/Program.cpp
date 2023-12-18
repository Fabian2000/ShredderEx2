// Dear ImGui: standalone example application for DirectX 12

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// Important: to compile on 32-bit systems, the DirectX12 backend requires code to be compiled with '#define ImTextureID ImU64'.
// This is because we need ImTextureID to carry a 64-bit value and by default ImTextureID is defined as void*.
// This define is set in the example .vcxproj file and need to be replicated in your app or by adding it to your imconfig.h file.

#include "imgui/imgui.h"
#include "imgui_backends/imgui_impl_win32.h"
#include "imgui_backends/imgui_impl_dx12.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <tchar.h>

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif
#include <dwmapi.h>
#include "imgui/imgui_internal.h"
#include <future>
#include "FileManagement.h"

#define ImGuiColor(r, g, b, a)  ImVec4(r / 255.f, g / 255.f, b / 255.f, a / 255.f)
#define ImGuiColor(r, g, b)  ImVec4(r / 255.f, g / 255.f, b / 255.f, 1.f)

#define ImGuiWString(wstr) ([](const std::wstring& _wstr) -> std::string { \
    if (_wstr.empty()) return std::string(); \
    int bufferSize = WideCharToMultiByte(CP_UTF8, 0, _wstr.c_str(), -1, nullptr, 0, nullptr, nullptr); \
    std::string str(bufferSize, 0); \
    WideCharToMultiByte(CP_UTF8, 0, _wstr.c_str(), -1, &str[0], bufferSize, nullptr, nullptr); \
    return str; \
})(wstr).c_str()

struct FrameContext
{
    ID3D12CommandAllocator* CommandAllocator;
    UINT64                  FenceValue;
};

// Data
static int const                    NUM_FRAMES_IN_FLIGHT = 3;
static FrameContext                 g_frameContext[NUM_FRAMES_IN_FLIGHT] = {};
static UINT                         g_frameIndex = 0;

static int const                    NUM_BACK_BUFFERS = 3;
static ID3D12Device* g_pd3dDevice = nullptr;
static ID3D12DescriptorHeap* g_pd3dRtvDescHeap = nullptr;
static ID3D12DescriptorHeap* g_pd3dSrvDescHeap = nullptr;
static ID3D12CommandQueue* g_pd3dCommandQueue = nullptr;
static ID3D12GraphicsCommandList* g_pd3dCommandList = nullptr;
static ID3D12Fence* g_fence = nullptr;
static HANDLE                       g_fenceEvent = nullptr;
static UINT64                       g_fenceLastSignaledValue = 0;
static IDXGISwapChain3* g_pSwapChain = nullptr;
static HANDLE                       g_hSwapChainWaitableObject = nullptr;
static ID3D12Resource* g_mainRenderTargetResource[NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE  g_mainRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};

LPWSTR* argv;

// Forward declarations of helper functions
wstring ImGuiTruncateTextMiddle(const std::wstring& text, float maxWidth);
void ImGuiPushDisableItem(bool toggle);
void ImGuiPopDisableItem(bool toggle);
void ImGuiMarqueeProgressBar(float speed, ImVec2 size);
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void WaitForLastSubmittedFrame();
FrameContext* WaitForNextFrameResources();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main code
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    try {
        int argc;
        argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        // Create application window
        //ImGui_ImplWin32_EnableDpiAwareness();
        WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
        ::RegisterClassExW(&wc);
        HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"ShredderEx2", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

        // Initialize Direct3D
        if (!CreateDeviceD3D(hwnd))
        {
            CleanupDeviceD3D();
            ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return 1;
        }

        // Window size and controls
        ImVec2 windowFullSize = ImVec2(550, 260);
        SetWindowPos(hwnd, NULL, 200, 200, windowFullSize.x, windowFullSize.y, 0);
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        style &= ~WS_MAXIMIZEBOX;
        style &= ~WS_THICKFRAME;
        SetWindowLongPtr(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        DWORD titlebarColor = RGB(45, 45, 45);
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &titlebarColor, sizeof(titlebarColor));
        HMENU hMenu = GetSystemMenu(hwnd, FALSE);
        RemoveMenu(hMenu, SC_CLOSE, MF_BYCOMMAND);

        LONG extendedStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        SetWindowLong(hwnd, GWL_EXSTYLE, extendedStyle | WS_EX_DLGMODALFRAME);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        // Show the window
        ::ShowWindow(hwnd, SW_SHOWDEFAULT);
        ::UpdateWindow(hwnd);

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsLight();

        // Setup Platform/Renderer backends
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX12_Init(g_pd3dDevice, NUM_FRAMES_IN_FLIGHT,
            DXGI_FORMAT_R8G8B8A8_UNORM, g_pd3dSrvDescHeap,
            g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
            g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

        // Load Fonts
        // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
        // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
        // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
        // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
        // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
        // - Read 'docs/FONTS.md' for more instructions and details.
        // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
        //io.Fonts->AddFontDefault();
        //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
        //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
        //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
        //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
        ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\Arial.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
        IM_ASSERT(font != nullptr);

        ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

        atomic<bool> cancelFutureTasks(false);
        FileManagement fileManagement;
        vector<vector<wstring>> filesAndFolders;
        int totalCount = 0;
        future<void> findFilesAndFolders = async(launch::async, [&] {
            // Get all paths and subpaths
            for (int i = 1; i < argc; i++) {
                if (fileManagement.IsFile(argv[i])) {
                    vector<wstring> simpleFile = { argv[i] };
                    filesAndFolders.push_back(simpleFile);
                }
                else {
                    filesAndFolders.push_back(fileManagement.GetAllNeededPaths(argv[i], &cancelFutureTasks));
                }
            }

            // Count inner vectors -> Useful for the progressbar -> totalCount = 100 %
            for (const auto& innerVec : filesAndFolders) {
                totalCount += innerVec.size();
            }
        });

        // Main loop
        bool done = false;
        float marqueeFileSearchSpeed = 1.f;
        bool rememberCheckbox = false;
        bool enableStartBtn = false;
        bool alreadyEnabledOnes = false;
        future<void> deleteFilesAndFolders;
        wstring closeBtnText = L"Cancel";
        while (!done) {
            if (findFilesAndFolders.wait_for(chrono::seconds(0)) == future_status::ready && !alreadyEnabledOnes) {
                marqueeFileSearchSpeed = 0.f;
                enableStartBtn = true;
                alreadyEnabledOnes = true;
            }

            bool isFindFilesAndFoldersReady = !findFilesAndFolders.valid() ||
                (findFilesAndFolders.wait_for(chrono::seconds(0)) == future_status::ready);
            bool isDeleteFilesAndFoldersReady = !deleteFilesAndFolders.valid() ||
                (deleteFilesAndFolders.wait_for(chrono::seconds(0)) == future_status::ready);

            if (isFindFilesAndFoldersReady && isDeleteFilesAndFoldersReady) {
                closeBtnText = L"Close";
            }
            else {
                closeBtnText = L"Cancel";
            }

            // Poll and handle messages (inputs, window resize, etc.)
            // See the WndProc() function below for our to dispatch events to the Win32 backend.
            MSG msg;
            while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
            {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                if (msg.message == WM_QUIT)
                    done = true;
            }
            if (done)
                break;

            // Start the Dear ImGui frame
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowBorderSize = 0.0f;
            style.WindowPadding = ImVec2(15, 15);
            style.FrameRounding = 5.f;

            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImVec2 windowSize = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowSize(windowSize);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGuiColor(GetRValue(titlebarColor), GetGValue(titlebarColor), GetBValue(titlebarColor)));
                ImGui::Begin("###NormalWindow", (bool*)true, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration);
                ImVec4 actionColor = ImGuiColor(240, 150, 40);
                ImVec4 normalColor = ImGuiColor(220, 110, 25);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, actionColor);
                ImGui::PushStyleColor(ImGuiCol_Button, normalColor);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, normalColor);
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, actionColor);
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, actionColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, actionColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, actionColor);
                ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGuiColor(255, 255, 255));
                    ImGuiMarqueeProgressBar(marqueeFileSearchSpeed, ImVec2(windowSize.x - style.WindowPadding.x * 2, 33));
                    ImGui::SetNextItemWidth(windowSize.x - style.WindowPadding.x * 2);
                    ImGui::Text(ImGuiWString(ImGuiTruncateTextMiddle(fileManagement.LatestScanFile, windowSize.x - style.WindowPadding.x * 2)));
                    ImGui::Dummy(ImVec2(0, style.WindowPadding.y));
                    ImGui::ProgressBar(.5f, ImVec2(windowSize.x - style.WindowPadding.x * 3 - 75, 33));
                    ImGui::SameLine(0, style.WindowPadding.x);
                    ImGuiPushDisableItem(!enableStartBtn);
                        if (ImGui::Button("Start", ImVec2(75, 33))) {
                            enableStartBtn = false;

                            //deleteFilesAndFolders = std::async(std::launch::async, [&] {
                                
                            //});
                        }
                    ImGuiPopDisableItem(!enableStartBtn);
                    ImGui::Text(ImGuiWString(ImGuiTruncateTextMiddle(fileManagement.LatestDeleteFile, windowSize.x - style.WindowPadding.x * 2)));
                    ImGui::Dummy(ImVec2(0, style.WindowPadding.y));
                    if (ImGui::Button(ImGuiWString(closeBtnText), ImVec2(75, 33)))
                    {
                        done = true;
                        cancelFutureTasks = true;
                    }
                    ImGuiPushDisableItem(true);
                        float btnSize = ImGui::GetItemRectSize().y;
                        ImGui::SameLine(0, windowSize.x - (75 * 3 + style.WindowPadding.x * 5 + (/*Checkbox*/style.FramePadding.y * 2 + style.ItemInnerSpacing.x + ImGui::CalcTextSize("Remember Choice").x + 3)));
                        float currentPosY = ImGui::GetCursorPosY();
                        ImGui::SetCursorPosY(currentPosY + btnSize / 2 - ImGui::GetFrameHeight() / 2);
                        ImGui::Checkbox("Remember Choice", &rememberCheckbox);
                        ImGui::SetCursorPosY(currentPosY);
                        ImGui::SameLine(0, style.WindowPadding.x);
                        ImGui::Button("Skip", ImVec2(75, 33));
                        ImGui::SameLine(0, style.WindowPadding.x);
                        ImGui::Button("Kill", ImVec2(75, 33));
                    ImGuiPopDisableItem(true);
                ImGui::PopStyleColor(8);
                ImGui::End();
            ImGui::PopStyleColor();

            // Rendering
            ImGui::Render();

            FrameContext* frameCtx = WaitForNextFrameResources();
            UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
            frameCtx->CommandAllocator->Reset();

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIdx];
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);
            g_pd3dCommandList->ResourceBarrier(1, &barrier);

            // Render Dear ImGui graphics
            const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
            g_pd3dCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
            g_pd3dCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
            g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            g_pd3dCommandList->ResourceBarrier(1, &barrier);
            g_pd3dCommandList->Close();

            g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);

            g_pSwapChain->Present(1, 0); // Present with vsync
            //g_pSwapChain->Present(0, 0); // Present without vsync

            UINT64 fenceValue = g_fenceLastSignaledValue + 1;
            g_pd3dCommandQueue->Signal(g_fence, fenceValue);
            g_fenceLastSignaledValue = fenceValue;
            frameCtx->FenceValue = fenceValue;
        }

        WaitForLastSubmittedFrame();
        findFilesAndFolders.get();

        // Cleanup
        LocalFree(argv);
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        CleanupDeviceD3D();
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    }
    catch (const exception) {}

    return 0;
}

// Helper functions

wstring ImGuiTruncateTextMiddle(const wstring& text, float maxWidth) {
    float padding = 10.0f;
    maxWidth -= padding;

    ImVec2 fullTextSize = ImGui::CalcTextSize(ImGuiWString(text));
    if (fullTextSize.x <= maxWidth) {
        return text;
    }

    wstring ellipsis = L"...";
    ImVec2 ellipsisSize = ImGui::CalcTextSize(ImGuiWString(ellipsis));
    float availableWidth = maxWidth - ellipsisSize.x;

    int leftCharCount = 0, rightCharCount = 0;
    float widthLeft = 0.0f, widthRight = 0.0f;

    while (leftCharCount + rightCharCount < text.length()) {
        if (widthLeft <= widthRight) {
            if (leftCharCount < text.length()) {
                widthLeft = ImGui::CalcTextSize(ImGuiWString(text.substr(0, leftCharCount + 1))).x;
                if (widthLeft + widthRight <= availableWidth) {
                    leftCharCount++;
                }
                else {
                    break;
                }
            }
        }
        else {
            if (rightCharCount < text.length()) {
                widthRight = ImGui::CalcTextSize(ImGuiWString(text.substr(text.length() - rightCharCount - 1))).x;
                if (widthLeft + widthRight <= availableWidth) {
                    rightCharCount++;
                }
                else {
                    break;
                }
            }
        }
    }

    return text.substr(0, leftCharCount) + ellipsis + text.substr(text.length() - rightCharCount);
}

void ImGuiPushDisableItem(bool toggle)
{
    if (toggle)
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }
}

void ImGuiPopDisableItem(bool toggle)
{
    if (toggle)
    {
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();
    }
}

void ImGuiMarqueeProgressBar(float speed, ImVec2 size)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    static float progress = 0.0f; // Progress of the animation
    static bool goingRight = true; // Direction of the movement

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID("__marquee__");
    const ImVec2 pos = window->DC.CursorPos;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 inner_size = size;
    inner_size.x -= (window->WindowPadding.x * 2);

    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(bb, window->WindowPadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return;

    // Render the background
    draw_list->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), g.Style.FrameRounding);

    // Update the progress
    if (goingRight) {
        progress += ImGui::GetIO().DeltaTime * speed;
        if (progress >= 1.0f) {
            progress = 1.0f;
            goingRight = false;
        }
    }
    else {
        progress -= ImGui::GetIO().DeltaTime * speed;
        if (progress <= 0.0f) {
            progress = 0.0f;
            goingRight = true;
        }
    }

    float bar_start = bb.Min.x + progress * (bb.GetWidth() - inner_size.x / 4);
    float bar_end = bar_start + inner_size.x / 4; // The width of the animated bar

    // Render the moving bar
    draw_list->AddRectFilled(ImVec2(bar_start, bb.Min.y), ImVec2(bar_end, bb.Max.y), ImGui::GetColorU32(ImGuiCol_PlotHistogram), g.Style.FrameRounding);
}


bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC1 sd;
    {
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = NUM_BACK_BUFFERS;
        sd.Width = 0;
        sd.Height = 0;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.Stereo = FALSE;
    }

    // [DEBUG] Enable debug interface
#ifdef DX12_ENABLE_DEBUG_LAYER
    ID3D12Debug* pdx12Debug = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
        pdx12Debug->EnableDebugLayer();
#endif

    // Create device
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&g_pd3dDevice)) != S_OK)
        return false;

    // [DEBUG] Setup debug interface to break on any warnings/errors
#ifdef DX12_ENABLE_DEBUG_LAYER
    if (pdx12Debug != nullptr)
    {
        ID3D12InfoQueue* pInfoQueue = nullptr;
        g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
        pInfoQueue->Release();
        pdx12Debug->Release();
    }
#endif

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = NUM_BACK_BUFFERS;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap)) != S_OK)
            return false;

        SIZE_T rtvDescriptorSize = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        {
            g_mainRenderTargetDescriptor[i] = rtvHandle;
            rtvHandle.ptr += rtvDescriptorSize;
        }
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
            return false;
    }

    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_pd3dCommandQueue)) != S_OK)
            return false;
    }

    for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator)) != S_OK)
            return false;

    if (g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList)) != S_OK ||
        g_pd3dCommandList->Close() != S_OK)
        return false;

    if (g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)) != S_OK)
        return false;

    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (g_fenceEvent == nullptr)
        return false;

    {
        IDXGIFactory4* dxgiFactory = nullptr;
        IDXGISwapChain1* swapChain1 = nullptr;
        if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK)
            return false;
        if (dxgiFactory->CreateSwapChainForHwnd(g_pd3dCommandQueue, hWnd, &sd, nullptr, nullptr, &swapChain1) != S_OK)
            return false;
        if (swapChain1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain)) != S_OK)
            return false;
        swapChain1->Release();
        dxgiFactory->Release();
        g_pSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
        g_hSwapChainWaitableObject = g_pSwapChain->GetFrameLatencyWaitableObject();
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->SetFullscreenState(false, nullptr); g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_hSwapChainWaitableObject != nullptr) { CloseHandle(g_hSwapChainWaitableObject); }
    for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        if (g_frameContext[i].CommandAllocator) { g_frameContext[i].CommandAllocator->Release(); g_frameContext[i].CommandAllocator = nullptr; }
    if (g_pd3dCommandQueue) { g_pd3dCommandQueue->Release(); g_pd3dCommandQueue = nullptr; }
    if (g_pd3dCommandList) { g_pd3dCommandList->Release(); g_pd3dCommandList = nullptr; }
    if (g_pd3dRtvDescHeap) { g_pd3dRtvDescHeap->Release(); g_pd3dRtvDescHeap = nullptr; }
    if (g_pd3dSrvDescHeap) { g_pd3dSrvDescHeap->Release(); g_pd3dSrvDescHeap = nullptr; }
    if (g_fence) { g_fence->Release(); g_fence = nullptr; }
    if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1* pDebug = nullptr;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
    {
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
        pDebug->Release();
    }
#endif
}

void CreateRenderTarget()
{
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        ID3D12Resource* pBackBuffer = nullptr;
        g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, g_mainRenderTargetDescriptor[i]);
        g_mainRenderTargetResource[i] = pBackBuffer;
    }
}

void CleanupRenderTarget()
{
    WaitForLastSubmittedFrame();

    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        if (g_mainRenderTargetResource[i]) { g_mainRenderTargetResource[i]->Release(); g_mainRenderTargetResource[i] = nullptr; }
}

void WaitForLastSubmittedFrame()
{
    FrameContext* frameCtx = &g_frameContext[g_frameIndex % NUM_FRAMES_IN_FLIGHT];

    UINT64 fenceValue = frameCtx->FenceValue;
    if (fenceValue == 0)
        return; // No fence was signaled

    frameCtx->FenceValue = 0;
    if (g_fence->GetCompletedValue() >= fenceValue)
        return;

    g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
    WaitForSingleObject(g_fenceEvent, INFINITE);
}

FrameContext* WaitForNextFrameResources()
{
    UINT nextFrameIndex = g_frameIndex + 1;
    g_frameIndex = nextFrameIndex;

    HANDLE waitableObjects[] = { g_hSwapChainWaitableObject, nullptr };
    DWORD numWaitableObjects = 1;

    FrameContext* frameCtx = &g_frameContext[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
    UINT64 fenceValue = frameCtx->FenceValue;
    if (fenceValue != 0) // means no fence was signaled
    {
        frameCtx->FenceValue = 0;
        g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
        waitableObjects[1] = g_fenceEvent;
        numWaitableObjects = 2;
    }

    WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

    return frameCtx;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            WaitForLastSubmittedFrame();
            CleanupRenderTarget();
            HRESULT result = g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
            assert(SUCCEEDED(result) && "Failed to resize swapchain.");
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_CLOSE:
        LocalFree(argv);
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
