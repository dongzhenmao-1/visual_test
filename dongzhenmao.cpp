#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <algorithm>
#include <coroutine> // 防止实验性协程断言阻断

// C++/WinRT 投影头文件
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

// COM 互操作与图形头文件
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <d3d11.h>
#include <dxgi1_2.h>

// Dear ImGui 头文件及后端
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

// 自动链接库
#pragma comment(lib, "windowsapp")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")

// 声明 ImGui 的 Win32 消息处理函数
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ==========================================
// 模块 1: 通用工具箱 (Utils)
// ==========================================
namespace Utils {
    HWND FindWindowByTitle(const std::string& keyword) {
        for (HWND hwnd = GetTopWindow(NULL); hwnd != NULL; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
            if (IsWindowVisible(hwnd)) {
                char title[512];
                if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
                    if (std::string(title).find(keyword) != std::string::npos) {
                        return hwnd;   // 找到第一个匹配的可见窗口，直接返回
                    }
                }
            }
        }
        return nullptr;   // 没有找到
    }
}

// ==========================================
// 模块 2: 实时显示窗口与捕获类 (WindowViewer)
// ==========================================
class WindowViewer {
public:
    WindowViewer(HWND targetHwnd) : m_targetHwnd(targetHwnd) {
        // 1. 注册 Win32 窗口类 (使用 A 版本以支持 UTF-8)
        WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = "CaptureViewerClass";
        RegisterClassExA(&wc);
        
        // 2. 创建用于显示的 Win32 窗口 (使用 A 版本并传入 UTF-8 字符串)
        m_viewerHwnd = CreateWindowExA(0, "CaptureViewerClass", "VS Code 实时预览 (关闭窗口退出)",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768,
            nullptr, nullptr, wc.hInstance, this);
    }

    void Start() {
        std::cout << "初始化 D3D11 设备与渲染资源...\n";
        InitGraphics();

        std::cout << "初始化 ImGui 环境...\n";
        InitImGui();

        std::cout << "启动窗口捕获...\n";
        StartCapture();

        // 显示窗口
        ShowWindow(m_viewerHwnd, SW_SHOW);
        UpdateWindow(m_viewerHwnd);

        // 使用 PeekMessage 的非阻塞主循环，并加入微小休眠
        MSG msg;
        ZeroMemory(&msg, sizeof(msg));
        while (msg.message != WM_QUIT) {
            if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
                continue;
            }

            RenderFrame();
            
            // 限制帧率并释放 CPU 资源，防止抢占导致捕获线程和窗口消息死锁
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        std::cout << "正在清理资源退出...\n";
        StopCapture();
        CleanupImGui();
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return true;

        WindowViewer* viewer = nullptr;
        if (msg == WM_NCCREATE) {
            CREATESTRUCTA* pCreate = reinterpret_cast<CREATESTRUCTA*>(lParam);
            viewer = reinterpret_cast<WindowViewer*>(pCreate->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(viewer));
        } else {
            viewer = reinterpret_cast<WindowViewer*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        if (viewer) {
            switch (msg) {
                case WM_SIZE:
                    viewer->OnResize(LOWORD(lParam), HIWORD(lParam));
                    return 0;
                case WM_DESTROY:
                    PostQuitMessage(0);
                    return 0;
            }
        }

        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }

    void InitGraphics() {
        winrt::com_ptr<ID3D11Device> d3dDevice;
        winrt::com_ptr<ID3D11DeviceContext> d3dContext;
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, 
            d3dDevice.put(), nullptr, d3dContext.put()));
        
        d3dDevice.as<ID3D10Multithread>()->SetMultithreadProtected(TRUE);
        m_d3dDevice = d3dDevice;
        m_d3dContext = d3dContext;

        winrt::com_ptr<IInspectable> inspectableDevice;
        winrt::check_hresult(
            CreateDirect3D11DeviceFromDXGIDevice(d3dDevice.as<IDXGIDevice>().get(), inspectableDevice.put())
        );
        m_winrtDevice = inspectableDevice.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

        winrt::com_ptr<IDXGIAdapter> adapter;
        d3dDevice.as<IDXGIDevice>()->GetAdapter(adapter.put());
        winrt::com_ptr<IDXGIFactory2> dxgiFactory;
        adapter->GetParent(winrt::guid_of<IDXGIFactory2>(), dxgiFactory.put_void());

        RECT rect;
        GetClientRect(m_viewerHwnd, &rect);

        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = std::max<LONG>(1, rect.right - rect.left);
        desc.Height = std::max<LONG>(1, rect.bottom - rect.top);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

        winrt::check_hresult(dxgiFactory->CreateSwapChainForHwnd(d3dDevice.get(), m_viewerHwnd, &desc, nullptr, nullptr, m_swapChain.put()));
        
        UpdateRenderTarget();
    }

    void UpdateRenderTarget() {
        m_rtv = nullptr;
        winrt::com_ptr<ID3D11Texture2D> backBuffer;
        winrt::check_hresult(m_swapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backBuffer.put_void()));
        winrt::check_hresult(m_d3dDevice->CreateRenderTargetView(backBuffer.get(), nullptr, m_rtv.put()));
    }

    void OnResize(int w, int h) {
        if (m_swapChain && w > 0 && h > 0) {
            m_rtv = nullptr;
            m_swapChain->ResizeBuffers(2, w, h, DXGI_FORMAT_UNKNOWN, 0);
            UpdateRenderTarget();
        }
    }

    void InitImGui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        
        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(m_viewerHwnd);
        ImGui_ImplDX11_Init(m_d3dDevice.get(), m_d3dContext.get());
    }

    void CleanupImGui() {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    void StartCapture() {
        auto factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(
            factory->CreateForWindow(m_targetHwnd, winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(), winrt::put_abi(m_item))
        );

        auto size = m_item.Size();

        m_framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            m_winrtDevice, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        
        m_frameArrivedToken = m_framePool.FrameArrived({ this, &WindowViewer::OnFrameArrived });
        
        m_session = m_framePool.CreateCaptureSession(m_item);
        m_session.StartCapture();
    }

    void StopCapture() {
        if (m_session) m_session.Close();
        if (m_framePool) {
            m_framePool.FrameArrived(m_frameArrivedToken);
            m_framePool.Close();
        }
    }

    // 后台捕获回调
    void OnFrameArrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender, 
        winrt::Windows::Foundation::IInspectable const&) {
        auto frame = sender.TryGetNextFrame();
        if (!frame) return;

        // 🎯 获取此帧真正有效的画面尺寸
        auto contentSize = frame.ContentSize();

        // 从捕获帧提取 D3D11 纹理
        auto access = frame.Surface().as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> frameTexture;
        winrt::check_hresult(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), frameTexture.put_void()));

        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingTexture = frameTexture;
        // 🎯 把真实的宽高存下来
        m_pendingWidth = contentSize.Width;
        m_pendingHeight = contentSize.Height;
    }

    // 主线程渲染
    void RenderFrame() {
        if (!m_rtv) return;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        winrt::com_ptr<ID3D11Texture2D> newTexture; {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_pendingTexture) {
                newTexture = m_pendingTexture;
                m_pendingTexture = nullptr;
                // 🎯 提取真实尺寸
            }
        }

        if (newTexture) {
            m_currentSRV = nullptr; 
            winrt::check_hresult(
                m_d3dDevice->CreateShaderResourceView(newTexture.get(), nullptr, m_currentSRV.put())
            );
            // 🎯 更新当前渲染的真实尺寸
            m_currentWidth = m_pendingWidth;
            m_currentHeight = m_pendingHeight;
        }

        winrt::com_ptr<ID3D11ShaderResourceView> srv = m_currentSRV;

        // 消除默认边距
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("PreviewWindow", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | 
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar(2);

        if (srv && m_currentWidth > 0 && m_currentHeight > 0) {
            ImVec2 avail_size = ImGui::GetContentRegionAvail();

            // 解析纹理的实际物理尺寸 (包含右侧/底部的垃圾对齐像素)
            winrt::com_ptr<ID3D11Resource> resource;
            srv->GetResource(resource.put());
            winrt::com_ptr<ID3D11Texture2D> texture = resource.as<ID3D11Texture2D>();
            D3D11_TEXTURE2D_DESC desc;
            texture->GetDesc(&desc);

            float texW = static_cast<float>(desc.Width);
            float texH = static_cast<float>(desc.Height);

            float validW = static_cast<float>(m_currentWidth);
            float validH = static_cast<float>(m_currentHeight);

            // 基于真实有效尺寸进行缩放，防止变形
            float scale = (std::min)(avail_size.x / validW, avail_size.y / validH);
            ImVec2 drawSize(std::round(validW * scale), std::round(validH * scale));

            ImVec2 cursorPos(std::round((avail_size.x - drawSize.x) * 0.5f), 
                             std::round((avail_size.y - drawSize.y) * 0.5f));
            ImGui::SetCursorPos(cursorPos);

            ImVec2 uv0(0.0f, 0.0f); 
            ImVec2 uv1(validW / texW, validH / texH);

            ImGui::Image(reinterpret_cast<ImTextureID>(srv.get()), drawSize, uv0, uv1);
        } else {
            ImGui::SetCursorPos(ImVec2(10, 10));
            ImGui::Text("等待捕获图像流入...");
        }

        ImGui::End();

        // 提交渲染
        ImGui::Render();
        ID3D11RenderTargetView* rtvList[] = { m_rtv.get() };
        m_d3dContext->OMSetRenderTargets(1, rtvList, nullptr);
        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        m_d3dContext->ClearRenderTargetView(m_rtv.get(), clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        m_swapChain->Present(1, 0);
    }

private:
    HWND m_targetHwnd = nullptr;
    HWND m_viewerHwnd = nullptr;
    std::mutex m_mutex;

    // Direct3D 11 资源
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_winrtDevice{ nullptr };
    winrt::com_ptr<IDXGISwapChain1> m_swapChain;
    winrt::com_ptr<ID3D11RenderTargetView> m_rtv;

    // 线程安全传递捕获纹理，仅在主线程操作 SRV
    winrt::com_ptr<ID3D11Texture2D> m_pendingTexture;
    winrt::com_ptr<ID3D11ShaderResourceView> m_currentSRV;

    // 捕获相关资源
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr }; // 代表了要捕获的对象
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr }; // 帧池
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr }; // 管理器
    winrt::event_token m_frameArrivedToken;

    int m_pendingWidth = 0;
    int m_pendingHeight = 0;
    int m_currentWidth = 0;
    int m_currentHeight = 0;
};

// ==========================================
// 模块 3: 主入口
// ==========================================
int main() {
    // 显式初始化为单线程套接字 (STA)，符合 Win32 GUI 程序的运行标准，预防死锁
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    try {
        HWND targetHwnd = Utils::FindWindowByTitle("Visual Studio Code");
        if (!targetHwnd) {
            std::cerr << "未找到可见的 VS Code 窗口。请确保程序正在运行。\n";
            return 1;
        }

        std::cout << "已找到目标窗口句柄: " << targetHwnd << "\n";
        
        // 创建 Viewer 并启动生命周期
        WindowViewer viewer(targetHwnd);
        viewer.Start();

    } catch (winrt::hresult_error const& ex) {
        std::cerr << "WinRT 异常: " << winrt::to_string(ex.message()) << std::endl;
    } catch (std::exception const& ex) {
        std::cerr << "标准异常: " << ex.what() << std::endl;
    }
    
    return 0;
}