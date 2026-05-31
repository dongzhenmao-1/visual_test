接下来我们尝试接入 ImGui，这是一个图形库，安装好后，我们先写两个基础函数（这里没有的问为什么，基本只有这一种写法）。

```cpp
void InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO(); // 与外界交互的钥匙
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // 启用键盘交互

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(m_hwnd);                               // 绑定
    ImGui_ImplDX11_Init(m_d3dDevice.get(), m_d3dContext.get()); // 绑定
}

void CleanupImGui() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(); // 字面义
}
```

开启了 ImGui 后，有些信息可能会被 ImGui 处理，所以在 `WndProc` 最前面加上：
```cpp
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;        
    // ...
}
```

将初始化和清理添加到 `Run` 中：

```cpp
void Run() {
    InitGraphics();
    InitImGui();

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            continue;
        }

        RenderFrame(); 
    }    

    CleanupImGui();
}
```

在 `RenderFrame` 中进行一个小测试，标星号的代表你们写代码可以直接这么写，只用替换掉中间的。

```cpp
void RenderFrame() {
    if (!m_rtv) return;

    ImGui_ImplDX11_NewFrame();    // *
    ImGui_ImplWin32_NewFrame();   // *
    ImGui::NewFrame();            // *

    ImGui::Begin("Hello ImGui Window");
    ImGui::Text("Hello, ImGui!");
    ImGui::Text("This is a single window example.");
    ImGui::End();

    ImGui::Render();              // * 绘入缓存

    ID3D11RenderTargetView* rtvList[] = { m_rtv.get() };
    m_d3dContext->OMSetRenderTargets(1, rtvList, nullptr);

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_d3dContext->ClearRenderTargetView(m_rtv.get(), clearColor); 
    // 到这里有人会疑惑了为什么要清屏与为什么不用 ImGui
    // 因为 ImGui 的宗旨只是话 UI 控件而不是整个窗口

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); // * 将缓存绘入屏幕

    m_swapChain->Present(1, 0);
}
```

总览：

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <algorithm>
#include <coroutine> 

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#pragma comment(lib, "windowsapp")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct TestWindow {
    HWND m_hwnd = nullptr;
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
    winrt::com_ptr<IDXGISwapChain1> m_swapChain;
    winrt::com_ptr<ID3D11RenderTargetView> m_rtv;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;        

        TestWindow *self = nullptr;
        if (msg == WM_NCCREATE) {
            CREATESTRUCTA *pCreate = reinterpret_cast<CREATESTRUCTA*>(lParam);
            self = reinterpret_cast<TestWindow*>(pCreate->lpCreateParams);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<TestWindow*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        }
        
        if (self) {
            if (msg == WM_SIZE) {
                self->OnResize(LOWORD(lParam), HIWORD(lParam));
                return 0;
            } else if (msg == WM_DESTROY) {
                PostQuitMessage(0);
                return 0;
            }
        }

        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }

    void OnResize(int w, int h) {
        if (m_swapChain && w > 0 && h > 0) {
            ID3D11RenderTargetView* nullViews[] = { nullptr };
            m_d3dContext->OMSetRenderTargets(1, nullViews, nullptr);
            m_rtv = nullptr;
            m_swapChain->ResizeBuffers(2, w, h, DXGI_FORMAT_UNKNOWN, 0);
            UpdateRenderTarget();
        }
    }

    void UpdateRenderTarget() {
        m_rtv = nullptr;
        winrt::com_ptr<ID3D11Texture2D> backBuffer;
        winrt::check_hresult(
            m_swapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backBuffer.put_void())
        );
        winrt::check_hresult(
            m_d3dDevice->CreateRenderTargetView(backBuffer.get(), nullptr, m_rtv.put())
        );
    }

    TestWindow() {
        WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "TestWindowClass";
        RegisterClassExA(&wc);

        m_hwnd = CreateWindowExA(0, "TestWindowClass", "Test 喵",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
            nullptr, nullptr, wc.hInstance, this
        );
    }
    
    ~TestWindow() {
        if (m_hwnd) DestroyWindow(m_hwnd);
    }

    void InitGraphics() {
        winrt::com_ptr<ID3D11Device> d3dDevice;
        winrt::com_ptr<ID3D11DeviceContext> d3dContext;
        winrt::check_hresult(
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                d3dDevice.put(), nullptr, d3dContext.put()
            )
        );

        d3dDevice.as<ID3D10Multithread>()->SetMultithreadProtected(TRUE);
        m_d3dDevice = d3dDevice;
        m_d3dContext = d3dContext;

        winrt::com_ptr<IDXGIAdapter> adapter;
        d3dDevice.as<IDXGIDevice>()->GetAdapter(adapter.put());
        winrt::com_ptr<IDXGIFactory2> dxgiFactory;
        adapter->GetParent(winrt::guid_of<IDXGIFactory2>(), dxgiFactory.put_void());

        RECT rect;
        GetClientRect(m_hwnd, &rect);

        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = std::max<LONG>(1, rect.right - rect.left);
        desc.Height = std::max<LONG>(1, rect.bottom - rect.top);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.SampleDesc.Count = 1;
        desc.BufferCount = 2;

        winrt::check_hresult(
            dxgiFactory->CreateSwapChainForHwnd(
                m_d3dDevice.get(), m_hwnd, &desc, nullptr, nullptr, m_swapChain.put()
            )
        );

        UpdateRenderTarget();
    }

    void InitImGui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(m_hwnd);
        ImGui_ImplDX11_Init(m_d3dDevice.get(), m_d3dContext.get());
    }

    void CleanupImGui() {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();        
    }

    void RenderFrame() {
        if (!m_rtv) return;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Hello ImGui Window");
        ImGui::Text("Hello, ImGui!");
        ImGui::Text("This is a single window example.");
        ImGui::End();

        ImGui::Render();

        ID3D11RenderTargetView* rtvList[] = { m_rtv.get() };
        m_d3dContext->OMSetRenderTargets(1, rtvList, nullptr);

        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        m_d3dContext->ClearRenderTargetView(m_rtv.get(), clearColor);

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        m_swapChain->Present(1, 0);
    }

    void Run() {
        InitGraphics();
        InitImGui();

        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);

        MSG msg;
        ZeroMemory(&msg, sizeof(msg));
        while (msg.message != WM_QUIT) {
            if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
                continue;
            }

            RenderFrame(); 
        }

        CleanupImGui();
    }
};

int main() {
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    try {
        TestWindow app;
        app.Run();
    } catch (const std::exception& ex) {
        std::cerr << "异常: " << ex.what() << std::endl;
    }

    return 0;
}
```