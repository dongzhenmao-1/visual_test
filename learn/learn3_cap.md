一些名词声明（代码中看见了自动往那边想谢谢喵）：

SRV：Shader Resource View（着色器资源视图）
纹理 (ID3D11Texture2D) 本质上就是 GPU 内存里的一块像素数据，它本身只是一个数据块，无法直接被着色器（Shader）读取。

SRV 相当于给这块数据开了个只读窗口，定义了着色器如何解读这些数据（比如格式、维度）。只有通过 SRV，着色器 或ImGui 才能把纹理画到屏幕上。

一些新的成员（用的时候讲）：

```cpp
HWND m_targetHwnd = nullptr; // 你要捕获哪个窗口
HWND m_viewerHwnd = nullptr; // 从 m_hwnd 重命名
std::mutex m_mutex; // 互斥锁

winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_winrtDevice{ nullptr }; // 可以当作对 ID3D11Device 的一层封装

winrt::com_ptr<ID3D11Texture2D> m_pendingTexture;
winrt::com_ptr<ID3D11ShaderResourceView> m_currentSRV;

winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr }; // 代表了要捕获的对象
winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr }; // 帧池
winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr }; // 管理器
winrt::event_token m_frameArrivedToken; // 这个后面说
```

初始化 `m_d3dDevice` 与 `m_d3dContext` 再对 `inspectableDevice` 初始化（默认这么写就可以了，也是绑定）：

```cpp
winrt::com_ptr<IInspectable> inspectableDevice;
winrt::check_hresult(
    CreateDirect3D11DeviceFromDXGIDevice(d3dDevice.as<IDXGIDevice>().get(), inspectableDevice.put())
);
m_winrtDevice = inspectableDevice.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
```

两个捕获函数：

```cpp
void StartCapture() {
    auto factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    winrt::check_hresult(
        factory->CreateForWindow(m_targetHwnd, winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(), winrt::put_abi(m_item))
    );

    // 注意了这个函数，她绑定了目标窗口（m_targetHwnd）和我们用来代表其的项（m_item）。
    // 中间不用管。
    // 有人问 m_targetHwnd 上面也没有提到哪来的（哦这是我们要传入的）

    auto size = m_item.Size(); // 设置一下帧池的图像大小

    m_framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
        m_winrtDevice, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size); 
    // 我们恰好来讲一下帧池的含义，这个是由捕获函数捕获的目标窗口的一帧原始图像，存在显存中，不知道的话就这么设置就好了
    
    m_frameArrivedToken = m_framePool.FrameArrived({this, &WindowViewer::OnFrameArrived}); // 相信通过 token 这个词大家大概知道含义了吧，获取 token 以后用。
    
    m_session = m_framePool.CreateCaptureSession(m_item); // 绑定会话（）
    m_session.StartCapture();
}

void StopCapture() {
    if (m_session) m_session.Close();
    if (m_framePool) {
        m_framePool.FrameArrived(m_frameArrivedToken); // 想你平常使用 github 之类的令牌就是这个，你可以用她来干很多东西（或许叫 api 调用），这里就是直接关掉。
        m_framePool.Close();
    }
}
```

开始了就要开始获取帧了：

这里先讲一个东西，我基本所有以 `On` 开头的函数的含义都是当...时，比如下面这个，就代表 `当 FrameArrived 这个事件发生时时应当做的是...`

```cpp
void OnFrameArrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender, 
    winrt::Windows::Foundation::IInspectable const&) {
    auto frame = sender.TryGetNextFrame();
    if (!frame) return; // 

    // 从捕获帧提取 D3D11 纹理
    auto access = frame.Surface().as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    // access 是一个互操作接口对象，她用于在 WinRT 的图形表面和原生 DirectX（DXGI）之间打通桥梁，让你能从 WinRT 类型里取出底层的 COM 纹理指针。
    winrt::com_ptr<ID3D11Texture2D> frameTexture; // 呐就是这个纹理
    winrt::check_hresult(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), frameTexture.put_void()));

    std::lock_guard<std::mutex> lock(m_mutex); // 这个东西下面讲
    m_pendingTexture = frameTexture; // 我得到了~ 待渲染的的下一帧
}

```

相信大家对 `std::mutex` 都不熟悉，简而言之，同一时间，只有一个拿到这个锁的人能使用，比方：

```cpp
fucA {
    std::lock_guard<std::mutex> lock(m_mutex);
    A;
}

fucB {
    std::lock_guard<std::mutex> lock(m_mutex);
    B;
}

```
比方说先执行的是 `fucA`，`std::lock_guard` 会上锁，也就是说，除非等待其析构（在这里指的是 `fucA` 执行完），否则 B 的 `std::lock_guard<std::mutex> lock(m_mutex);` 无法向下执行。

OK 我们进行主进程渲染的东西。

```cpp
void RenderFrame() {
    if (!m_rtv) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    winrt::com_ptr<ID3D11Texture2D> newTexture; { // 注意这里有个括号限定 std::lock_guard<std::mutex> lock(m_mutex); 作用域（什么时候解析）
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pendingTexture) {
            newTexture = m_pendingTexture;
            m_pendingTexture = nullptr;
        }
    }

    if (newTexture) { // 
        m_currentSRV = nullptr; // 释放旧帧
        winrt::check_hresult(
            m_d3dDevice->CreateShaderResourceView(newTexture.get(), nullptr, m_currentSRV.put())
        );
    }

    winrt::com_ptr<ID3D11ShaderResourceView> srv = m_currentSRV;

    ImGui::SetNextWindowPos(ImVec2(0, 0)); // 含义为字面义
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("PreviewWindow", nullptr, 
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | 
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);

    if (srv) { // 如果有
        ImVec2 avail_size = ImGui::GetContentRegionAvail();

        winrt::com_ptr<ID3D11Resource> resource;
        srv->GetResource(resource.put());
        winrt::com_ptr<ID3D11Texture2D> texture = resource.as<ID3D11Texture2D>();
        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);

        float texW = static_cast<float>(desc.Width);
        float texH = static_cast<float>(desc.Height);

        float scale = (std::min)(avail_size.x / texW, avail_size.y / texH);
        ImVec2 drawSize(texW * scale, texH * scale);

        ImVec2 cursorPos((avail_size.x - drawSize.x) * 0.5f, (avail_size.y - drawSize.y) * 0.5f);
        ImGui::SetCursorPos(cursorPos);

        ImGui::Image(reinterpret_cast<ImTextureID>(srv.get()), drawSize);
    } else {
        ImGui::Text("等待捕获图像流入...");
    }

    ImGui::End();

    ImGui::Render();

    ID3D11RenderTargetView* rtvList[] = { m_rtv.get() };
    m_d3dContext->OMSetRenderTargets(1, rtvList, nullptr);

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_d3dContext->ClearRenderTargetView(m_rtv.get(), clearColor);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    m_swapChain->Present(1, 0);
}




```

