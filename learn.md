前情提要！这个东西（winrt）文档复杂的很，函数多且杂糅，单个函数不能运行成一个 demo，我尽量采用由简至难的方式一步一步带你们构建，最简单的 demo 也有点长，请耐心 qaq ~

考虑我们想建立一个窗口，不妨写一个类来实现她。

```cpp
struct TestWindow {

};
```

首先我们至少要有以下几个东西才能有一个完整的窗口。

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

初始化一下图形环境

```cpp
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
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    winrt::check_hresult(
        dxgiFactory->CreateSwapChainForHwnd(
            m_d3dDevice.get(), m_hwnd, &desc, nullptr, nullptr, m_swapChain.put()
        )
    );

    UpdateRenderTarget();
}

```

不是哥们怎么这么长，抱歉不过这是必须得过去的一关

。