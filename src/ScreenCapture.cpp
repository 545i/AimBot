#include "ScreenCapture.h"
#include "Logger.h"
#include <QFile>
#include <QByteArray>
#include <QIODevice>
#include <omp.h>

MemoryMapping::MemoryMapping(size_t reqSize) 
    : fileHandle(NULL), mappingHandle(NULL), mappedView(NULL), size(reqSize) {
    char tempPath[MAX_PATH];
    char tempFile[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    GetTempFileNameA(tempPath, "mm", 0, tempFile);
    
    fileHandle = CreateFileA(tempFile,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        NULL);

    if (fileHandle != INVALID_HANDLE_VALUE) {
        mappingHandle = CreateFileMappingA(fileHandle,
            NULL,
            PAGE_READWRITE,
            0, static_cast<DWORD>(size),
            NULL);

        if (mappingHandle) {
            mappedView = MapViewOfFile(mappingHandle,
                FILE_MAP_ALL_ACCESS,
                0, 0,
                size);
        }
    }
}

MemoryMapping::~MemoryMapping() {
    if (mappedView) UnmapViewOfFile(mappedView);
    if (mappingHandle) CloseHandle(mappingHandle);
    if (fileHandle != INVALID_HANDLE_VALUE) CloseHandle(fileHandle);
}

void* MemoryMapping::getData() { 
    return mappedView; 
}

ScreenCapture::ScreenCapture(int sizeX, int sizeY, const std::string& model_path)
    : capture_size_X(sizeX)
    , capture_size_Y(sizeY)
    , env(ORT_LOGGING_LEVEL_WARNING, "ScreenCapture")
    , head_offset(0.1f)
    , buffer(std::make_unique<BYTE[]>(sizeX * sizeY * 4))
    , processed_image(cv::Mat(input_height, input_width, CV_8UC3))
    , input_tensor_shape({1, 3, input_height, input_width})
    , input_tensor(xt::zeros<float>(input_tensor_shape))
    , last_fps_update(std::chrono::steady_clock::now())
    , current_fps(0.0f)
    , FPS_SAMPLE_SIZE(120)
    , model_path(model_path)
    , boxDisplayMode(0)
    , confidence_threshold(0.5f)
    , running(false)
    , targetWindow(nullptr)
    , useWindowCapture(false)
    , current_texture_index(0)
    , enableCrosshairDetection(false)
{
    try {
        size_t frameBufferSize = sizeX * sizeY * 4;
        frameMapping = std::make_unique<MemoryMapping>(frameBufferSize);
        resultMapping = std::make_unique<MemoryMapping>(sizeof(cv::Rect) * 100);
        
        initDXGI();
        
        initOnnxRuntime(model_path);
        
        front_buffer = cv::Mat(capture_size_Y, capture_size_X, CV_8UC4);
        back_buffer = cv::Mat(capture_size_Y, capture_size_X, CV_8UC4);
        
        frame_times.push(last_fps_update);
        previous_boxes.resize(SMOOTH_FRAMES);
        
        initKalmanFilters();
        
        Logger::logDetection("ScreenCapture 初始化成功");
    }
    catch (const std::exception& e) {
        Logger::logDetection("ScreenCapture 初始化失敗: " + std::string(e.what()));
        throw;
    }
}

ScreenCapture::~ScreenCapture() {
    running = false;
    frame_ready.notify_all();
    results_ready.notify_all();
    
    releaseResources();
}

void ScreenCapture::releaseResources() {
    if (duplication) duplication.Reset();
    if (d3d_context) d3d_context.Reset();
    if (d3d_device) d3d_device.Reset();
    staging_textures.clear();
    
    front_buffer.release();
    back_buffer.release();
    processed_image.release();
    
    std::queue<cv::Mat>().swap(frame_queue);
    std::queue<std::vector<cv::Rect>>().swap(results_queue);
    std::queue<std::chrono::steady_clock::time_point>().swap(frame_times);
    
    frameMapping.reset();
    resultMapping.reset();
}

void ScreenCapture::initDXGI() {
    UINT createDeviceFlags = 0;
    #ifdef _DEBUG
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    Microsoft::WRL::ComPtr<IDXGIFactory1> dxgiFactory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&dxgiFactory);
    if (FAILED(hr)) {
        throw std::runtime_error("無法創建 DXGI Factory");
    }

    std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> adapters;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> preferredAdapter;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

    for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        
        char videoCardDescription[128];
        size_t converted;
        wcstombs_s(&converted, videoCardDescription, desc.Description, 128);
        Logger::logDetection("找到顯示適配器 " + std::to_string(i) + ": " + std::string(videoCardDescription));
        Logger::logDetection("顯存大小: " + std::to_string(desc.DedicatedVideoMemory / 1024 / 1024) + " MB");
        
        if (wcsstr(desc.Description, L"Intel") != nullptr) {
            preferredAdapter = adapter;
            Logger::logDetection("選擇 Intel 顯示適配器作為首選");
        }
        
        adapters.push_back(adapter);
        adapter = nullptr;
    }

    if (preferredAdapter) {
        adapters.insert(adapters.begin(), preferredAdapter);
    }

    bool deviceCreated = false;
    for (auto& currentAdapter : adapters) {
        DXGI_ADAPTER_DESC1 desc;
        currentAdapter->GetDesc1(&desc);
        
        hr = D3D11CreateDevice(
            currentAdapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            createDeviceFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &d3d_device,
            nullptr,
            &d3d_context
        );

        if (SUCCEEDED(hr)) {
            char videoCardDescription[128];
            size_t converted;
            wcstombs_s(&converted, videoCardDescription, desc.Description, 128);
            Logger::logDetection("成功創建設備於適配器: " + std::string(videoCardDescription));
            
            Microsoft::WRL::ComPtr<IDXGIDevice1> dxgi_device;
            Microsoft::WRL::ComPtr<IDXGIAdapter> dxgi_adapter;
            Microsoft::WRL::ComPtr<IDXGIOutput> dxgi_output;
            Microsoft::WRL::ComPtr<IDXGIOutput1> dxgi_output1;
            
            if (SUCCEEDED(d3d_device.As(&dxgi_device)) &&
                SUCCEEDED(dxgi_device->GetAdapter(&dxgi_adapter)) &&
                SUCCEEDED(dxgi_adapter->EnumOutputs(0, &dxgi_output)) &&
                SUCCEEDED(dxgi_output.As(&dxgi_output1))) {
                
                hr = dxgi_output1->DuplicateOutput(d3d_device.Get(), &duplication);
                if (SUCCEEDED(hr)) {
                    Logger::logDetection("成功創建桌面複製器");
                    deviceCreated = true;
                    break;
                }
            }
            
            d3d_device = nullptr;
            d3d_context = nullptr;
            Logger::logDetection("此適配器不支持桌面複製，嘗試下一個...");
        }
    }

    if (!deviceCreated) {
        Logger::logDetection("所有硬件適配器都失敗，嘗試使用 WARP...");
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            createDeviceFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &d3d_device,
            nullptr,
            &d3d_context
        );

        if (SUCCEEDED(hr)) {
            Logger::logDetection("成功使用 WARP 創建設備");
            deviceCreated = true;
        }
    }

    if (!deviceCreated) {
        throw std::runtime_error("無法創建支持桌面複製的 D3D11 設備");
    }

    Microsoft::WRL::ComPtr<IDXGIDevice1> dxgi_device;
    Microsoft::WRL::ComPtr<IDXGIAdapter> dxgi_adapter;
    
    if (SUCCEEDED(d3d_device.As(&dxgi_device))) {
        dxgi_device->SetMaximumFrameLatency(1);
    }

    Microsoft::WRL::ComPtr<IDXGIOutput> dxgi_output;
    if (SUCCEEDED(dxgi_device->GetAdapter(&dxgi_adapter)) &&
        SUCCEEDED(dxgi_adapter->EnumOutputs(0, &dxgi_output))) {
        
        DXGI_OUTPUT_DESC desc;
        dxgi_output->GetDesc(&desc);
        screen_width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
        screen_height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
        
        Logger::logDetection("螢幕分辨率: " + std::to_string(screen_width) + "x" + std::to_string(screen_height));
    }

    initStagingTextures();
}

void ScreenCapture::initStagingTextures() {
    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = capture_size_X;
    texture_desc.Height = capture_size_Y;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D11_USAGE_STAGING;
    texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    texture_desc.MiscFlags = 0;
    texture_desc.BindFlags = 0;

    staging_textures.reserve(TEXTURE_COUNT);
    
    for (int i = 0; i < TEXTURE_COUNT; ++i) {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        HRESULT hr = d3d_device->CreateTexture2D(&texture_desc, nullptr, &texture);
        if (SUCCEEDED(hr)) {
            staging_textures.push_back(texture);
        } else {
            throw std::runtime_error("無法創建暫存紋理");
        }
    }
}

void ScreenCapture::handleDXGIError(HRESULT hr) {
    std::string error_message;
    
    typedef LONG NTSTATUS;
    typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    #define STATUS_SUCCESS (0x00000000)

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    std::string windows_version = "未知 Windows 版本";

    if (ntdll) {
        auto RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(ntdll, "RtlGetVersion");
        if (RtlGetVersion) {
            RTL_OSVERSIONINFOW osvi = { 0 };
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            if (RtlGetVersion(&osvi) == STATUS_SUCCESS) {
                windows_version = "Windows ";
                if (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 22000) {
                    windows_version += "11";
                } else if (osvi.dwMajorVersion == 10) {
                    windows_version += "10";
                } else if (osvi.dwMajorVersion == 6) {
                    switch(osvi.dwMinorVersion) {
                        case 3: windows_version += "8.1"; break;
                        case 2: windows_version += "8"; break;
                        case 1: windows_version += "7"; break;
                        case 0: windows_version += "Vista"; break;
                    }
                }
                windows_version += " (Build " + std::to_string(osvi.dwBuildNumber) + ")";
            }
        }
    }
    
    switch (hr) {
        case DXGI_ERROR_UNSUPPORTED:
            error_message = "統不支持此功能 (DXGI_ERROR_UNSUPPORTED)\n"
                          "錯誤代碼: 0x887A0004\n"
                          "操作系統: " + windows_version + "\n"
                          "可能原因:\n"
                          "1. 顯示驅動程序不支持桌面複製\n"
                          "2. 系統不支持 DXGI 1.1 或更高版本\n"
                          "3. 需要更新顯示驅動程序\n"
                          "建議操作:\n"
                          "1. 確保系統為 Windows 7 或更高版本\n"
                          "2. 更新顯示卡驅動程序到最新版本\n"
                          "3. 檢查是否啟用了遠程桌面或其他虛擬化環境";
            break;
            
        case E_ACCESSDENIED:
            error_message = "拒絕存取 (E_ACCESSDENIED)\n"
                          "錯誤代碼: 0x80070005\n"
                          "操作系統: " + windows_version + "\n"
                          "可能原因:\n"
                          "1. 程序沒有管理員權限\n"
                          "2. 系統安全策略限制\n"
                          "建議操作:\n"
                          "1. 以管理員身份運行程序\n"
                          "2. 檢查統安全設置";
            break;
            
        case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:
            error_message = "資源不足 (DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)\n"
                          "錯誤代碼: 0x887A0022\n"
                          "操作系統: " + windows_version + "\n"
                          "可能原因:\n"
                          "1. 其他程序正在使用桌面複製\n"
                          "2. 系統資源不足\n"
                          "建議操作:\n"
                          "1. 關閉其他可能使用桌面複製的程序\n"
                          "2. 重新啟動系統";
            break;
            
        default:
            error_message = "DXGI 錯誤\n"
                          "錯誤代碼: 0x" + 
                          std::string("00000000" + std::to_string(hr)).substr(std::to_string(hr).length()) + "\n"
                          "操作系統: " + windows_version + "\n"
                          "系統錯誤碼: " + std::to_string(GetLastError()) + "\n"
                          "建議操作:\n"
                          "1. 檢查顯示驅動程序\n"
                          "2. 重新啟動系統\n"
                          "3. 更新 Windows";
            break;
    }

    Logger::logDetection(error_message);
    throw std::runtime_error(error_message);
}

cv::Mat ScreenCapture::capture_center() {
    updateFPSStats();

    DXGI_OUTDUPL_FRAME_INFO frame_info;
    Microsoft::WRL::ComPtr<IDXGIResource> desktop_resource;
    HRESULT hr = duplication->AcquireNextFrame(16, &frame_info, &desktop_resource);
    
    if (FAILED(hr)) {
        handleAcquireFrameError(hr);
        return front_buffer;
    }

    struct FrameGuard {
        IDXGIOutputDuplication* dupl;
        FrameGuard(IDXGIOutputDuplication* d) : dupl(d) {}
        ~FrameGuard() { dupl->ReleaseFrame(); }
    } frame_guard(duplication.Get());

    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktop_texture;
    hr = desktop_resource.As(&desktop_texture);
    if (FAILED(hr)) {
        Logger::logDetection("獲取桌面紋理失敗");
        return front_buffer;
    }

    current_texture_index = (current_texture_index + 1) % staging_textures.size();
    auto& current_staging = staging_textures[current_texture_index];

    D3D11_BOX sourceRegion = calculateCaptureRegion();

    d3d_context->CopySubresourceRegion(
        current_staging.Get(), 0,
        0, 0, 0,
        desktop_texture.Get(), 0,
        &sourceRegion
    );

    if (!copyTextureDataToBuffer(current_staging)) {
        return front_buffer;
    }

    std::swap(front_buffer, back_buffer);
    
    current_frame = front_buffer.clone();
    return front_buffer;
}


void ScreenCapture::setExecutionProvider(const std::string& provider) {
    if (provider == current_provider) {
        return;
    }

    try {
        Ort::SessionOptions session_options;
        bool provider_set = false;

        if (provider == "DML" && dml_available) {
            #ifdef USE_DML
            OrtStatus* status = OrtSessionOptionsAppendExecutionProvider_DML(session_options, 0);
            if (status == nullptr) {
                current_provider = "DML";
                provider_set = true;
                Logger::logDetection("切換到 DirectML 加速");
            } else {
                Ort::GetApi().ReleaseStatus(status);
                throw std::runtime_error("無法切換到 DirectML");
            }
            #endif
        }
        else if (provider == "CUDA" && cuda_available) {
            OrtCUDAProviderOptions cuda_options{};
            cuda_options.device_id = 0;
            session_options.AppendExecutionProvider_CUDA(cuda_options);
            current_provider = "CUDA";
            provider_set = true;
            Logger::logDetection("切換到 CUDA 加速");
        }

        if (!provider_set) {
            current_provider = "CPU";
            Logger::logDetection("切換到 CPU 運行");
        }

        std::wstring widestr = std::wstring(model_path.begin(), model_path.end());
        session = std::make_unique<Ort::Session>(env, widestr.c_str(), session_options);

        Ort::AllocatorWithDefaultOptions allocator;
        input_name = session->GetInputNameAllocated(0, allocator).get();
        output_name = session->GetOutputNameAllocated(0, allocator).get();
        
        input_node_names.clear();
        output_node_names.clear();
        input_node_names.push_back(input_name.c_str());
        output_node_names.push_back(output_name.c_str());

    } catch (const std::exception& e) {
        Logger::logDetection("切換執行提供程序錯誤: " + std::string(e.what()));
        throw;
    }
}

void ScreenCapture::updateFPSStats() {
    auto current_time = std::chrono::steady_clock::now();
    frame_times.push(current_time);
    
    while (frame_times.size() > FPS_SAMPLE_SIZE) {
        frame_times.pop();
    }
    
    auto time_since_last_update = std::chrono::duration_cast<std::chrono::milliseconds>(
        current_time - last_fps_update).count();
        
    if (frame_times.size() >= 2 && time_since_last_update >= 1000) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            frame_times.back() - frame_times.front()).count();
            
        if (duration > 0) {
            current_fps = (frame_times.size() - 1) * 1000.0f / duration;
        }
        
        last_fps_update = current_time;
    }
}

void ScreenCapture::handleAcquireFrameError(HRESULT hr) {
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } else {
        try {
            reinitializeCapture();
        } catch (const std::exception& e) {
            Logger::logDetection("重新初始化失敗: " + std::string(e.what()));
        }
    }
}

D3D11_BOX ScreenCapture::calculateCaptureRegion() {
    int center_x, center_y;
    
    if (useWindowCapture && updateWindowRect()) {
        center_x = (windowRect.right + windowRect.left) / 2 - capture_size_X / 2;
        center_y = (windowRect.bottom + windowRect.top) / 2 - capture_size_Y / 2;
    } else {
        center_x = screen_width / 2 - capture_size_X / 2;
        center_y = screen_height / 2 - capture_size_Y / 2;
    }

    center_x = std::clamp(center_x, 0, screen_width - capture_size_X);
    center_y = std::clamp(center_y, 0, screen_height - capture_size_Y);

    return D3D11_BOX{
        static_cast<UINT>(center_x),
        static_cast<UINT>(center_y),
        0,
        static_cast<UINT>(center_x + capture_size_X),
        static_cast<UINT>(center_y + capture_size_Y),
        1
    };
}

bool ScreenCapture::copyTextureDataToBuffer(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = d3d_context->Map(texture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    
    if (SUCCEEDED(hr)) {
        const int bytes_per_pixel = 4;
        const int row_bytes = capture_size_X * bytes_per_pixel;
        
        std::lock_guard<std::mutex> lock(bufferMutex);
        
        void* mappedBuffer = frameMapping->getData();
        if (mappedBuffer) {
            #pragma omp parallel for
            for (int y = 0; y < capture_size_Y; ++y) {
                const BYTE* src = static_cast<const BYTE*>(mapped.pData) + y * mapped.RowPitch;
                BYTE* dst = static_cast<BYTE*>(mappedBuffer) + y * row_bytes;
                
                #if defined(__AVX2__)
                    int x = 0;
                    for (; x <= row_bytes - 64; x += 64) {
                        __m256i data1 = _mm256_loadu_si256((__m256i*)(src + x));
                        __m256i data2 = _mm256_loadu_si256((__m256i*)(src + x + 32));
                        _mm256_storeu_si256((__m256i*)(dst + x), data1);
                        _mm256_storeu_si256((__m256i*)(dst + x + 32), data2);
                    }
                    for (; x < row_bytes; ++x) {
                        dst[x] = src[x];
                    }
                #else
                    memcpy(dst, src, row_bytes);
                #endif
            }
            
            memcpy(front_buffer.data, mappedBuffer, capture_size_Y * row_bytes);
        }

        d3d_context->Unmap(texture.Get(), 0);
        return true;
    }
    
    Logger::logDetection("映射紋理失敗");
    return false;
}

bool ScreenCapture::reinitializeCapture() {
    std::lock_guard<std::mutex> lock(frame_mutex);
    
    try {
        releaseResources();
        
        initDXGI();
        
        front_buffer = cv::Mat(capture_size_Y, capture_size_X, CV_8UC4);
        back_buffer = cv::Mat(capture_size_Y, capture_size_X, CV_8UC4);
        
        Logger::logGui("截圖系統重新初始化成功");
        return true;
    }
    catch (const std::exception& e) {
        Logger::logGui("截圖系統重新初始化失敗: " + std::string(e.what()));
        return false;
    }
}

bool ScreenCapture::updateWindowRect() {
    if (!targetWindow || !IsWindow(targetWindow)) {
        useWindowCapture = false;
        return false;
    }
    
    RECT client_rect;
    if (!GetClientRect(targetWindow, &client_rect)) {
        useWindowCapture = false;
        return false;
    }
    
    POINT pt = {client_rect.left, client_rect.top};
    if (!ClientToScreen(targetWindow, &pt)) {
        useWindowCapture = false;
        return false;
    }
    
    windowRect.left = pt.x;
    windowRect.top = pt.y;
    
    pt.x = client_rect.right;
    pt.y = client_rect.bottom;
    if (!ClientToScreen(targetWindow, &pt)) {
        useWindowCapture = false;
        return false;
    }
    
    windowRect.right = pt.x;
    windowRect.bottom = pt.y;
    
    return true;
}



xt::xarray<float> ScreenCapture::preprocess(const cv::Mat& image) { //!圖片預處理
    static const float normalize_factor = 1.0f / 255.0f;
    
    cv::resize(image, processed_image, cv::Size(input_width, input_height), 0, 0, cv::INTER_LINEAR_EXACT);
    
    #pragma omp parallel for collapse(2)
    for (int i = 0; i < input_height; ++i) {
        for (int j = 0; j < input_width; ++j) {
            const auto* pixel = processed_image.ptr<uint8_t>(i, j);
            input_tensor(0, 0, i, j) = pixel[2] * normalize_factor;  // R
            input_tensor(0, 1, i, j) = pixel[1] * normalize_factor;  // G
            input_tensor(0, 2, i, j) = pixel[0] * normalize_factor;  // B
        }
    }
    
    return input_tensor;
}

Ort::Value ScreenCapture::inference(const xt::xarray<float>& input_tensor) { //!模型推理
    try {
        static Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);
        static std::vector<int64_t> input_shape = {1, 3, input_height, input_width};
        
        Ort::Value input_ort = Ort::Value::CreateTensor<float>(
            memory_info,
            const_cast<float*>(input_tensor.data()),
            input_tensor.size(),
            input_shape.data(),
            input_shape.size()
        );
        
        auto output_tensors = session->Run(
            Ort::RunOptions{nullptr},
            input_node_names.data(),
            &input_ort,
            1,
            output_node_names.data(),
            1
        );
        
        return std::move(output_tensors[0]);
    }
    catch (const Ort::Exception& e) {
        Logger::logDetection("ONNX 推理錯誤: " + std::string(e.what()));
        throw;
    }
}

std::vector<DetectionBox> ScreenCapture::postprocess( //!目標位置計算物動
    const Ort::Value& output_tensor,
    cv::Size image_size,
    float conf_threshold)
{
    try {
        const float* output_data = output_tensor.GetTensorData<float>();
        auto output_shape = output_tensor.GetTensorTypeAndShapeInfo().GetShape();
        const int num_boxes = static_cast<int>(output_shape[2]);
        
        std::vector<DetectionBox> detections;
        detections.reserve(num_boxes);
        
        #pragma omp parallel
        {
            std::vector<DetectionBox> local_detections;
            const int thread_count = std::max(1, omp_get_max_threads());
            local_detections.reserve(num_boxes / thread_count);
            
            #pragma omp for nowait
            for (int i = 0; i < num_boxes; i++) {
                float head_conf = output_data[4 * num_boxes + i];
                float body_conf = output_data[5 * num_boxes + i];
                
                bool should_process = false;
                float confidence_to_use = 0.0f;
                
                switch (boxDisplayMode) {
                    case 0:
                        should_process = (head_conf > conf_threshold || body_conf > conf_threshold);
                        confidence_to_use = std::max(head_conf, body_conf);
                        break;
                    case 1:
                    case 2:
                        should_process = (head_conf > conf_threshold);
                        confidence_to_use = head_conf;
                        break;
                }
                
                if (should_process) {
                    float x = output_data[0 * num_boxes + i];
                    float y = output_data[1 * num_boxes + i];
                    float w = output_data[2 * num_boxes + i];
                    float h = output_data[3 * num_boxes + i];
                    
                    float scale_x = static_cast<float>(image_size.width) / input_width;
                    float scale_y = static_cast<float>(image_size.height) / input_height;
                    
                    int x1 = std::round((x - w/2) * scale_x);
                    int y1 = std::round((y - h/2) * scale_y);
                    int x2 = std::round((x + w/2) * scale_x);
                    int y2 = std::round((y + h/2) * scale_y);
                    
                    local_detections.emplace_back(x1, y1, x2, y2, confidence_to_use);
                }
            }
            #pragma omp critical
            {
                detections.insert(detections.end(), 
                                local_detections.begin(), 
                                local_detections.end());
            }
        }
        
        return detections;
    }
    catch (const std::exception& e) {
        Logger::logDetection("後處理錯誤: " + std::string(e.what()));
        throw;
    }
}

void ScreenCapture::initOnnxRuntime(const std::string& model_path) {
    try {
        char tempPath[MAX_PATH];
        char tempFile[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        GetTempFileNameA(tempPath, "sys", 0, tempFile);
        std::string datPath = std::string(tempFile) + ".dat";
        
        QFile modelFile(":/3w.onnx");
        if (!modelFile.open(QIODevice::ReadOnly)) {
            throw std::runtime_error("無法從資源中讀取模型文件");
        }
        
        QFile datFile(QString::fromStdString(datPath));
        if (datFile.open(QIODevice::WriteOnly)) {
            datFile.write(modelFile.readAll());
            datFile.close();
            modelFile.close();
        } else {
            modelFile.close();
            throw std::runtime_error("無法創建臨時文件");
        }

        // 使用 MemoryMapping 讀取 .dat 文件
        std::ifstream input(datPath, std::ios::binary);
        if (!input) {
            DeleteFileA(datPath.c_str());
            throw std::runtime_error("無法讀取臨時文件");
        }

        // 讀取數據到內存
        input.seekg(0, std::ios::end);
        size_t size = input.tellg();
        input.seekg(0, std::ios::beg);
        
        MemoryMapping memMap(size);
        if (memMap.getData()) {
            input.read(static_cast<char*>(memMap.getData()), size);
            model_data = QByteArray(static_cast<const char*>(memMap.getData()), size);
        }
        input.close();

        // 刪除臨時文件
        DeleteFileA(datPath.c_str());

        // 創建會話選項
        Ort::SessionOptions session_options;
        bool provider_initialized = false;

        // 檢查 DirectML 可用性
        #ifdef USE_DML
        try {
            OrtStatus* status = OrtSessionOptionsAppendExecutionProvider_DML(session_options, 0);
            if (status == nullptr) {
                current_provider = "DML";
                provider_initialized = true;
                dml_available = true;
                Logger::logDetection("已啟用 DirectML 加速");
            } else {
                Ort::GetApi().ReleaseStatus(status);
                throw Ort::Exception("DirectML 初始化失敗", ORT_FAIL);
            }
        } catch (...) {
            dml_available = false;
            Logger::logDetection("DirectML 不可用，將使用 CPU");
        }
        #endif

        // 如果 DirectML 不可用，檢查 CUDA
        if (!provider_initialized) {
            try {
                OrtCUDAProviderOptions cuda_options{};
                cuda_options.device_id = 0;
                session_options.AppendExecutionProvider_CUDA(cuda_options);
                current_provider = "CUDA";
                cuda_available = true;
                provider_initialized = true;
                Logger::logDetection("已啟用 CUDA 加速");
            } catch (...) {
                cuda_available = false;
                Logger::logDetection("CUDA 不可用");
            }
        }

        // 如果都不可用，使用 CPU
        if (!provider_initialized) {
            current_provider = "CPU";
            Logger::logDetection("使用 CPU 執行");
        }

        // 使用內存中的模型數據創建會話
        session = std::make_unique<Ort::Session>(
            env, 
            reinterpret_cast<const void*>(model_data.constData()),
            static_cast<size_t>(model_data.size()),
            session_options);

        // 獲取輸入輸出資訊
        Ort::AllocatorWithDefaultOptions allocator;
        input_name = session->GetInputNameAllocated(0, allocator).get();
        output_name = session->GetOutputNameAllocated(0, allocator).get();
        
        input_node_names.push_back(input_name.c_str());
        output_node_names.push_back(output_name.c_str());

        // 輸出模型資訊
        auto input_shape = session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        Logger::logDetection("模型輸入形狀: ");
        for (auto dim : input_shape) {
            Logger::logDetection(std::to_string(dim) + " ");
        }
        Logger::logDetection("\n");

        Logger::logDetection("模型載入成功\n");
    }
    catch (const Ort::Exception& e) {
        Logger::logDetection("ONNX Runtime 初始化失敗: " + std::string(e.what()));
        throw;
    }
}


void ScreenCapture::setHeadOffset(float offset) {
    head_offset = std::clamp(offset, 0.0f, 1.0f);
    Logger::logDetection("瞄準點偏移: " + std::to_string(head_offset));
}

void ScreenCapture::setConfidenceThreshold(float threshold) {
    confidence_threshold = std::clamp(threshold, 0.0f, 1.0f);
    Logger::logDetection("置信度閾值: " + std::to_string(confidence_threshold));
}

void ScreenCapture::setBoxDisplayMode(int mode) {
    boxDisplayMode = std::clamp(mode, 0, 2);
    if (boxDisplayMode == 2) {
        head_offset = 0.11f;
    }
    Logger::logDetection("框顯示模式: " + std::to_string(boxDisplayMode));
}

void ScreenCapture::setTargetWindow(HWND hwnd) {
    std::lock_guard<std::mutex> lock(frame_mutex);
    targetWindow = hwnd;
    useWindowCapture = (hwnd != nullptr);
    updateWindowRect();
}

bool ScreenCapture::checkCUDAStatus() const {
    return cuda_available;
}

float ScreenCapture::getCurrentFPS() const {
    return current_fps;
}

std::vector<cv::Rect> ScreenCapture::getLatestResults() {
    cv::Mat frame = capture_center();
    return detect_targets(frame);
}







//*""==============================================================================================================================================""









std::vector<cv::Rect> ScreenCapture::detect_targets(const cv::Mat& frame) {
    if (frame.empty()) return {};
    
    try {
        auto detections = detect(frame);
        std::vector<cv::Rect> filtered_boxes;
        filtered_boxes.reserve(detections.size());
        
        // 更新現有軌跡
        updateTracks(detections);
        
        // 獲取穩定後的目標框
        for (const auto& track : target_tracks) {
            if (track.active) {
                cv::Rect predicted_box = getPredictedBox(track);
                filtered_boxes.push_back(predicted_box);
            }
        }
        
        return non_max_suppression(filtered_boxes, 0.3f);
    }
    catch (...) {
        return {};
    }
}

cv::Point ScreenCapture::find_closest_target(const std::vector<cv::Rect>& targets) {
    current_targets = targets;
    
    if (targets.empty()) {
        return cv::Point(-1, -1);
    }
    
    cv::Point closest = cv::Point(-1, -1);
    float min_distance = std::numeric_limits<float>::max();
    cv::Point screen_center(capture_size_X / 2, capture_size_Y / 2);
    
    for (const auto& target : targets) {
        cv::Point targetCenter = calculateTargetCenter(target);
        float distance = cv::norm(targetCenter - cv::Point(screen_center));
        
        if (distance < min_distance) {
            min_distance = distance;
            closest = targetCenter;
        }
    }
    
    return closest;
}


cv::Point ScreenCapture::calculateTargetCenter(const cv::Rect& target) {
    switch (boxDisplayMode) {
        case 2:
            return cv::Point(
                target.x + target.width/2,
                target.y + static_cast<int>(target.height * 0.11)
            );
            
        case 1:
        case 0:
        default:
            return cv::Point(
                target.x + target.width/2,
                target.y + target.height/2 - static_cast<int>(target.height * 0.25)
            );
    }
}

std::vector<DetectionBox> ScreenCapture::detect(const cv::Mat& frame) {
    try {
        auto input_tensor = preprocess(frame);
        auto output_tensor = inference(input_tensor);
        return postprocess(output_tensor, frame.size(), confidence_threshold);
    }
    catch (const std::exception& e) {
        Logger::logDetection("檢測錯誤: " + std::string(e.what()));
        return {};
    }
}


float ScreenCapture::calculate_iou(const cv::Rect& box1, const cv::Rect& box2) {
    int x1 = std::max(box1.x, box2.x);
    int y1 = std::max(box1.y, box2.y);
    int x2 = std::min(box1.x + box1.width, box2.x + box2.width);
    int y2 = std::min(box1.y + box1.height, box2.y + box2.height);

    if (x2 <= x1 || y2 <= y1) return 0.0f;

    float intersection_area = static_cast<float>((x2 - x1) * (y2 - y1));
    float box1_area = static_cast<float>(box1.width * box1.height);
    float box2_area = static_cast<float>(box2.width * box2.height);
    float union_area = box1_area + box2_area - intersection_area;

    return intersection_area / union_area;
}

std::vector<cv::Rect> ScreenCapture::non_max_suppression(
    const std::vector<cv::Rect>& boxes, 
    float iou_threshold)
{
    if (boxes.empty()) return {};

    std::vector<std::pair<cv::Rect, float>> boxes_with_scores;
    boxes_with_scores.reserve(boxes.size());
    
    for (const auto& box : boxes) {
        float area = box.width * box.height;
        float center_x = box.x + box.width * 0.5f;
        float center_y = box.y + box.height * 0.5f;
        
        float dx = center_x - capture_size_X * 0.5f;
        float dy = center_y - capture_size_Y * 0.5f;
        float distance_score = std::sqrt(dx*dx + dy*dy);
        
        float score = area / (1.0f + distance_score);
        
        if (area > 100 && area < (capture_size_X * capture_size_Y * 0.5f)) {
            boxes_with_scores.emplace_back(box, score);
        }
    }


    std::sort(boxes_with_scores.begin(), boxes_with_scores.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<cv::Rect> selected_boxes;
    selected_boxes.reserve(boxes_with_scores.size());

    while (!boxes_with_scores.empty()) {
        const auto& current = boxes_with_scores[0];
        selected_boxes.push_back(current.first);
        
        boxes_with_scores.erase(boxes_with_scores.begin());
        
        boxes_with_scores.erase(
            std::remove_if(boxes_with_scores.begin(), boxes_with_scores.end(),
                [&](const auto& box) {
                    return calculate_iou(current.first, box.first) > iou_threshold;
                }),
            boxes_with_scores.end()
        );
    }

    return selected_boxes;
}

cv::Point ScreenCapture::findCrosshair(const cv::Mat& frame) {
    // 如果禁用了準心檢測,直接返回無效點
    if (!enableCrosshairDetection) {
        hasCrosshair = false;
        return cv::Point(-1, -1);
    }

    cv::Mat hsv;
    cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
    
    cv::Scalar lower_cyan(85, 200, 200);
    cv::Scalar upper_cyan(95, 255, 255);
    
    cv::Mat mask;
    
    if (crosshair_locked && last_crosshair_pos.x != -1) {
        cv::Rect roi(
            std::max(0, last_crosshair_pos.x - SEARCH_RADIUS),
            std::max(0, last_crosshair_pos.y - SEARCH_RADIUS),
            std::min(frame.cols, last_crosshair_pos.x + SEARCH_RADIUS * 2) - std::max(0, last_crosshair_pos.x - SEARCH_RADIUS),
            std::min(frame.rows, last_crosshair_pos.y + SEARCH_RADIUS * 2) - std::max(0, last_crosshair_pos.y - SEARCH_RADIUS)
        );
        
        cv::Mat roi_hsv = hsv(roi);
        cv::Mat roi_mask;
        cv::inRange(roi_hsv, lower_cyan, upper_cyan, roi_mask);
        
        mask = cv::Mat::zeros(frame.size(), CV_8UC1);
        roi_mask.copyTo(mask(roi));
    } else {
        cv::inRange(hsv, lower_cyan, upper_cyan, mask);
    }
    
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    cv::Point best_center(-1, -1);
    double min_distance = std::numeric_limits<double>::max();
    
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        
        if (area < 10 || area > 500) continue;
        
        cv::Moments m = cv::moments(contour);
        if (m.m00 == 0) continue;
        
        cv::Point center(m.m10 / m.m00, m.m01 / m.m00);
        
        if (crosshair_locked && last_crosshair_pos.x != -1) {
            double distance = cv::norm(center - last_crosshair_pos);
            if (distance < min_distance) {
                min_distance = distance;
                best_center = center;
            }
        } else {
            best_center = center;
            break;
        }
    }

    if (best_center.x != -1) {
        if (!crosshair_locked) {
            crosshair_locked = true;
            last_crosshair_pos = best_center;
            hasCrosshair = true;
        } else {
            double distance = cv::norm(best_center - last_crosshair_pos);
            if (distance < LOCK_THRESHOLD) {
                last_crosshair_pos = best_center;
                hasCrosshair = true;
            } else {
                best_center = last_crosshair_pos;
            }
        }
    } else {
        crosshair_locked = false;
        hasCrosshair = false;
        last_crosshair_pos = cv::Point(-1, -1);
    }
    
    return best_center;
}

bool ScreenCapture::copyDataToMapping(void* dest, const void* src, size_t size) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    if (!dest || !src) return false;
    memcpy(dest, src, size);
    return true;
}

bool ScreenCapture::copyDataFromMapping(void* dest, const void* src, size_t size) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    if (!dest || !src) return false;
    memcpy(dest, src, size);
    return true;
}

// 修改 updateTracks 函數，調整匹配策略和重置邏輯
void ScreenCapture::updateTracks(const std::vector<DetectionBox>& detections) {
    // 如果沒有檢測到目標,直接重置所有軌跡
    if (detections.empty()) {
        for (auto& track : target_tracks) {
            track.active = false;
        }
        return;
    }
    
    const float MAX_DISTANCE = 150.0f;
    std::vector<bool> matched_detections(detections.size(), false);
    
    // 預測並更新現有活躍軌跡
    for (auto& track : target_tracks) {
        if (!track.active) continue;
        
        track.kalman.predict();
        float min_dist = MAX_DISTANCE;
        int best_match = -1;
        
        // 尋找最近的檢測框
        for (int i = 0; i < detections.size(); i++) {
            if (matched_detections[i]) continue;
            
            const auto& det = detections[i];
            cv::Point2f det_center(det.x1 + (det.x2 - det.x1)/2.0f,
                                 det.y1 + (det.y2 - det.y1)/2.0f);
                                 
            cv::Mat prediction = track.kalman.statePre;
            float dist = std::sqrt(std::pow(det_center.x - prediction.at<float>(0), 2) +
                         std::pow(det_center.y - prediction.at<float>(1), 2));
                                 
            if (dist < min_dist) {
                min_dist = dist;
                best_match = i;
            }
        }
        
        // 如果找不到匹配,直接停用該軌跡
        if (best_match < 0) {
            track.active = false;
            continue;
        }
        
        // 更新找到匹配的軌跡
        const auto& det = detections[best_match];
        cv::Point2f det_center(det.x1 + (det.x2 - det.x1)/2.0f,
                             det.y1 + (det.y2 - det.y1)/2.0f);
                             
        cv::Mat measurement = (cv::Mat_<float>(2,1) << det_center.x, det_center.y);
        track.kalman.correct(measurement);
        track.last_box = cv::Rect(det.x1, det.y1, det.x2 - det.x1, det.y2 - det.y1);
        matched_detections[best_match] = true;
    }
    
    // 為未匹配的檢測框創建新軌跡
    for (int i = 0; i < detections.size(); i++) {
        if (matched_detections[i]) continue;
        
        const auto& det = detections[i];
        cv::Point2f det_center(det.x1 + (det.x2 - det.x1)/2.0f,
                             det.y1 + (det.y2 - det.y1)/2.0f);
        
        // 尋找非活躍軌跡進行重用
        bool reused = false;
        for (auto& track : target_tracks) {
            if (!track.active) {
                // 重置並重用軌跡
                track.kalman.statePost = (cv::Mat_<float>(4,1) << 
                    det_center.x, det_center.y, 0, 0);
                track.kalman.errorCovPost = cv::Mat::eye(4, 4, CV_32F);
                track.last_box = cv::Rect(det.x1, det.y1, det.x2 - det.x1, det.y2 - det.y1);
                track.active = true;
                reused = true;
                break;
            }
        }
        
        // 如果沒有可重用的軌跡,創建新的
        if (!reused && target_tracks.size() < MAX_TARGETS) {
            Track new_track;
            new_track.kalman = kalman_filters[target_tracks.size()];
            new_track.kalman.statePost = (cv::Mat_<float>(4,1) << 
                det_center.x, det_center.y, 0, 0);
            new_track.last_box = cv::Rect(det.x1, det.y1, det.x2 - det.x1, det.y2 - det.y1);
            new_track.active = true;
            target_tracks.push_back(new_track);
        }
    }
}

// 修改 initKalmanFilters 函數，調整卡爾曼濾波器參數
void ScreenCapture::initKalmanFilters() {
    kalman_filters.clear();
    for (int i = 0; i < MAX_TARGETS; i++) {
        auto kf = cv::KalmanFilter(4, 2, 0);
        
        kf.transitionMatrix = (cv::Mat_<float>(4, 4) << 
            1, 0, 1, 0,
            0, 1, 0, 1,
            0, 0, 1, 0,
            0, 0, 0, 1);
            
        kf.measurementMatrix = (cv::Mat_<float>(2, 4) <<
            1, 0, 0, 0,
            0, 1, 0, 0);
            
        // 增加過程噪聲，使系統更容易接受新的觀測
        kf.processNoiseCov = cv::Mat::eye(4, 4, CV_32F) * 1e-2;
        // 減少測量噪聲，提高對新觀測的信任度
        kf.measurementNoiseCov = cv::Mat::eye(2, 2, CV_32F) * 1e-2;
        kf.errorCovPost = cv::Mat::eye(4, 4, CV_32F);
        
        kalman_filters.push_back(kf);
    }
    
    target_tracks.clear();
}

// 添加獲取預測框函數
cv::Rect ScreenCapture::getPredictedBox(const Track& track) {
    cv::Mat state = track.kalman.statePost;
    cv::Point2f center(state.at<float>(0), state.at<float>(1));
    
    // 使用上一個檢測框的大小
    return cv::Rect(
        center.x - track.last_box.width/2,
        center.y - track.last_box.height/2,
        track.last_box.width,
        track.last_box.height
    );
}