前情提要！这个东西（winrt）文档复杂的很，函数多且杂糅，单个函数不能运行成一个 demo，我尽量采用由简至难的方式一步一步带你们构建，最简单的 demo 也有点长，请耐心 qaq ~

很多参数我们平常根本不会用到，很多框架我们根本不会想着去改变，很多时候你应当想着先去照抄官方框架源代码而不是自己把一层一层全部吃透，这样子费力不讨好，我给出的都是把官方的必要框架扒出来的，为了便于理解我会给出说明，但说明绝对不是底层的实现，而是抽象的概念（这个函数要干什么，她要和谁一起干），想了解那些不常用参数的含义可以直接问 AI（我全部写出来会导致和官方文档一样什么重点都没有，对于想立刻搞出个程序玩玩的小伙伴非常不友好）。

考虑我们想建立一个窗口，不妨写一个类来实现她。

```cpp
struct TestWindow {

};
```

首先我们至少要有以下几个东西才能有一个完整的窗口（具体怎么说看下面）。

```cpp
HWND m_hwnd = nullptr;
winrt::com_ptr<ID3D11Device> m_d3dDevice;
winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
winrt::com_ptr<IDXGISwapChain1> m_swapChain;
winrt::com_ptr<ID3D11RenderTargetView> m_rtv;
```

具体为什么在这里我也不知道，先读下去吧。以后有些东西我将会在使用的时候给出说明，还有就是我的函数内肯能出现下文才写的函数，不在必要时我不会这么做，做了就代表我会给出目的。


先写个构造函数

```cpp
TestWindow() {
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "TestWindowClass";
    RegisterClassExA(&wc);

    m_hwnd = CreateWindowExA(0, "TestWindowClass", "Test show",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, wc.hInstance, this
    );
}

~TestWindow() {
    if (m_hwnd) DestroyWindow(m_hwnd);
}
```

这个构造函数是告诉 windows 我们注册了一个窗口类型，属性在 `wc` 中，由 `RegisterClassExA` 注册。关于 `CreateWindowExA`，这个内容物非常难搞，请先按照我这样填写，一些比较好懂的属性：

```cpp
CreateWindowExA(0, "注册的窗口类名", "这个窗口的标题",
    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 窗口宽度, 窗口长度,
    nullptr, nullptr, wc.hInstance, 
    "额外的附加信息，为了配合下面实现，所以我们默认填入 this，孩子们这里不是字符串"
);
```

我先介绍一个函数：

```cpp
winrt::check_hresult();
```

这个函数的作用是查看传入参数正不正常，所以下文所有形如。

```cpp
winrt::check_hresult(f());
```

可以直接理解为直接往 `f()` 内加了一个 `try` 组件，仅仅起到侦测异常的作用，不做语义改变！

初始化一下图形环境：

```cpp
void InitGraphics() {
    winrt::com_ptr<ID3D11Device> d3dDevice; // 看下面注释
    winrt::com_ptr<ID3D11DeviceContext> d3dContext; 
    winrt::check_hresult(
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            d3dDevice.put(), nullptr, d3dContext.put()
        )
    ); // 默认这么填初始化

    d3dDevice.as<ID3D10Multithread>()->SetMultithreadProtected(TRUE); // 没什么解释，默认要开的
    m_d3dDevice = d3dDevice;
    m_d3dContext = d3dContext;

    // 以上，我们搞出了与 GPU 的基本联通。
    // 关于为什么要创建两个神秘变量再对 m_ 进行赋值，这是为了保证成功状态下一定两个都是好的
    // 而不是可能一个好了，一个不好，留下垃圾值。

    winrt::com_ptr<IDXGIAdapter> adapter; // 你可以直接把她当成 GPU 的身份证（打开你电脑的设备管理器，是不是你的显卡就是显示适配器呀）
    d3dDevice.as<IDXGIDevice>()->GetAdapter(adapter.put()); //

    // 注意到了吗这里我们直接把 ID3D11Device 当成了 IDXGIDevice 使用，这两者基本上是拿来互通的较为简单方法
    // 这里直接把 IDXGIAdapter 绑定到了 ID3D11Device

    winrt::com_ptr<IDXGIFactory2> dxgiFactory; 
    adapter->GetParent(winrt::guid_of<IDXGIFactory2>(), dxgiFactory.put_void());

    // D3D11 的作用是 GPU 资源的创建、渲染管线的控制、绘制命令的提交
    // DXGI 的作用是管理交换链、呈现画面、处理全屏/窗口模式切换
    // 来个形象的比喻 D3D11 是画家，孩子们画家（ID3D11Device）也是要采购画纸和颜料，才能（ID3D11DeviceContext）画画的
    // 而 DXGI 是画廊管理员，用来张贴画（呈现画面）与决定把画放在哪（窗口 or 全屏等）

    // 你不能直接 new 出一个画家，她们也是要妈（工厂）的
    // 这里 IDXGIFactory2 本身不渲染任何东西，它的唯一工作就是制造出交换链对象（IDXGISwapChain1）。

    // DXGI 的一些逻辑链条：Factory (工厂) -> Adapter (适配器) -> Output (输出屏幕)

    // 关于交换链：
    // 想象你在纸上画画，直接画到被观众看到的那张纸上：观众会看到你的画笔在上面涂改、擦除、半成品，体验极差。
    // 正确做法是：准备两张纸，一张摆在观众面前（前台），另一张藏在后面让你画（后台）。等你画完了，瞬间把两张纸交换位置，观众就只看到了完整的成品。
    // 交换链就是这个机制的 GPU 实现。它至少维护两个缓冲区（Back Buffer），绘制一个、呈现一个。
    // 也就是：交换链 = 一组轮换使用的画布 + 一个与屏幕刷新同步的翻页控制器。

    RECT rect;
    GetClientRect(m_hwnd, &rect); // 窗口位置属性

    DXGI_SWAP_CHAIN_DESC1 desc = {}; // 设置交换链描述（desc 是描述的缩写哦）。
    desc.Width = std::max<LONG>(1, rect.right - rect.left);
    desc.Height = std::max<LONG>(1, rect.bottom - rect.top);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;           // **********
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 默认先这么填
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; // 
    desc.SampleDesc.Count = 1;                          // **********
    desc.BufferCount = 2; // 缓冲区个数，也就是一个后台一个前台，这是最小的了。

    winrt::check_hresult(
        dxgiFactory->CreateSwapChainForHwnd(
            m_d3dDevice.get(), m_hwnd, &desc, nullptr, nullptr, m_swapChain.put()
        )
    ); // 默认这么填来搞定交换链

    UpdateRenderTarget(); // 下面讲
}

```

不是哥们怎么这么长，抱歉不过这是必须得过去的一关。

好了，我们来搞下一部分，渲染）：

```cpp
void RenderFrame() {
    if (!m_rtv) return; // 还没画好就不渲染

    ID3D11RenderTargetView* rtvList[] = { m_rtv.get() };
    m_d3dContext->OMSetRenderTargets(1, rtvList, nullptr); // ID3D11 不保证上一帧的状态还是有效的，所以要重设。

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_d3dContext->ClearRenderTargetView(m_rtv.get(), clearColor);

    m_swapChain->Present(1, 0); // 默认呈现（垂直同步）
}
```

别忘了，我们还没有搞定过当前的渲染目标（画纸在哪）：

```cpp
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

```

最后，主进程：

```cpp
void Run() {
    InitGraphics();

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    while (true) {
        RenderFrame(); 
        // std::this_thread::sleep_for(std::chrono::milliseconds(1)); 
        // 事实上垂直同步就够了
    }

}

```

测试一下：

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
    // ...
};

int main() {
    winrt::init_apartment(winrt::apartment_type::single_threaded); // 默认就这么设置就好啦，以后可能有用
    TestWindow app;
    app.Run();   

    return 0;
}
```

不出意外，你就能看见一个~~卡死的~~白色窗口了，为什么是白色，因为卡死了根本渲染不了任何东西啊。

好了我们应该让她先相应我们的消息，我们来更改一下 `Run` 函数：

```cpp
void Run() {
    InitGraphics();

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    while (msg.message != WM_QUIT) { // 听到就退出
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) { // 非阻断式获得 msg，进阶的可以搜索
            TranslateMessage(&msg); // 翻译这条信息（你当然可以不开看看效果）
            DispatchMessageA(&msg); // 让 WndProc 处理这条信息
            continue;
        }

        RenderFrame(); 
        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // 防止占用过高
    }

}

```

欸我们发现这个窗口可以动了，不过为什么关掉窗口程序还是不停止运行啊！

其实，观察我们的构造函数这句话：
```cpp
wc.lpfnWndProc = DefWindowProcA;
```
这句话的含义是给一个默认的处理函数，`winrt` 提供了让我们自己写处理消息函数的接口，直接写就好啦。

```cpp
wc.lpfnWndProc = WndProc;
```

```cpp
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { // 返回值照抄标准库的
    // 这里有必要说明一下
    // msg 是 uint 的，每种 msg 对应的后面两个参数的具体含义都不一样，有兴趣可以查询微软官方的资料
    // 还有 reinterpret_cast 我们平常不怎么用，她的作用是强行将某一段二进制转换成另一种类型。类比 void* 强转。

    TestWindow *self = nullptr; 
    if (msg == WM_NCCREATE) { // 初次创建窗口消息
        CREATESTRUCTA *pCreate = reinterpret_cast<CREATESTRUCTA*>(lParam); 
        self = reinterpret_cast<TestWindow*>(pCreate->lpCreateParams); 
        // 注意了，对于 WM_NCCREATE 消息，lpCreateParams 来自于你创建时给她塞的附加信息，我们默认塞入一个 this 就是为了方便绑定。
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); // 设置窗口的属性，相当于不用你自己写的共享啦。
    } else {
        self = reinterpret_cast<TestWindow*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA)); // Get 属性
    }
        
    if (self) { // 如果已经初始化了。
        if (msg == WM_SIZE) { // 调整窗口大小
            self->OnResize(LOWORD(lParam), HIWORD(lParam)); // 马上实现
            return 0;
        } else if (msg == WM_DESTROY) { // 按下关闭窗口键
            PostQuitMessage(0); // 投递关闭消息
            return 0;
        }
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam); // 调用默认处理函数
}
```

这样就解决了关闭窗口的问题。

调整窗口大小：

```cpp
void OnResize(int w, int h) {
    if (m_swapChain && w > 0 && h > 0) { // 防止崩溃
        ID3D11RenderTargetView* nullViews[] = { nullptr };
        m_d3dContext->OMSetRenderTargets(1, nullViews, nullptr); // 不要画了
        m_rtv = nullptr;
        m_swapChain->ResizeBuffers(2, w, h, DXGI_FORMAT_UNKNOWN, 0); // 重新设置缓冲画布大小
        UpdateRenderTarget(); 
    }
}
```


