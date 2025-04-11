#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include <memory>
#include <algorithm>
#include <wrl/client.h>
#include "KeyboardMouseControl.h"

using Microsoft::WRL::ComPtr;

void enableUTF8Console() {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    
    GetConsoleMode(hErr, &dwMode);
    dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hErr, dwMode);
    
    setlocale(LC_ALL, ".UTF8");
    std::locale::global(std::locale(".UTF8"));
}

class ScreenCapture {
private:
    int capture_size_X;
    int capture_size_Y;
    cv::Vec3b lower_color;
    cv::Vec3b upper_color;
    float head_offset;
    
    // DXCam相關成員
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<IDXGIOutputDuplication> deskDupl;
    
public:
    ScreenCapture(int sizeX = 320, int sizeY = 320) 
        : capture_size_X(sizeX), capture_size_Y(sizeY), head_offset(0.1f) {
        // 設置HSV顏色範圍，與Python代碼相同
        lower_color = cv::Vec3b(140, 120, 180);
        upper_color = cv::Vec3b(160, 200, 255);
        
        initDXGI();
    }
    
    void setHeadOffset(float offset) {
        head_offset = std::clamp(offset, 0.0f, 1.0f);
    }
    
    cv::Mat capture_center() {
        // 獲取螢幕中心區域
        RECT screenRect;
        GetClientRect(GetDesktopWindow(), &screenRect);
        int screenWidth = screenRect.right - screenRect.left;
        int screenHeight = screenRect.bottom - screenRect.top;
        
        // 計算中心區域
        int centerX = screenWidth / 2;
        int centerY = screenHeight / 2;
        
        // 計算擷取區域
        int left = std::max(0, centerX - capture_size_X / 2);
        int top = std::max(0, centerY - capture_size_Y / 2);
        
        // 擷取螢幕圖像
        auto screenData = captureScreen();
        if (screenData.empty()) {
            return cv::Mat();
        }
        
        // 轉換為OpenCV格式
        cv::Mat frame(capture_size_Y, capture_size_X, CV_8UC4, screenData.data());
        
        // 轉換色彩空間
        cv::Mat hsv;
        cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
        
        // 創建遮罩
        cv::Mat mask;
        cv::inRange(hsv, lower_color, upper_color, mask);
        
        // 應遮罩
        cv::Mat result;
        cv::bitwise_and(frame, frame, result, mask);
        
        return result;
    }
    
    std::vector<cv::Rect> detect_targets(const cv::Mat& frame) {
        std::vector<cv::Rect> targets;
        
        // 轉換到HSV
        cv::Mat hsv;
        cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
        
        // 創建遮罩
        cv::Mat mask;
        cv::inRange(hsv, lower_color, upper_color, mask);
        
        // 增加膨脹處理的強度
        cv::Mat dilated;
        cv::dilate(mask, dilated, 
            cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)), // 增加核心大小
            cv::Point(-1,-1), 
            4); // 增加迭代次數
        
        // 尋找輪廓
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(dilated, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE); // 改用EXTERNAL
        
        // 調整面積篩選條件
        for (const auto& contour : contours) {
            double area = cv::contourArea(contour);
            if (area >= 200 && area <= 15000) { // 放寬面積範圍
                targets.push_back(cv::boundingRect(contour));
            }
        }
        
        // 提高重疊閾值
        return non_max_suppression(targets, 0.6f); // 增加重疊閾值
    }
    
    cv::Point find_closest_target(const std::vector<cv::Rect>& targets) {
        if (targets.empty()) {
            return cv::Point(-1, -1);
        }
        
        cv::Point screen_center(capture_size_X / 2, capture_size_Y / 2);
        double min_distance = std::numeric_limits<double>::max();
        cv::Point closest_point(-1, -1);
        
        for (const auto& target : targets) {
            cv::Point target_center(
                target.x + target.width / 2,
                target.y + target.height * head_offset
            );
            
            double distance = cv::norm(target_center - screen_center);
            if (distance < min_distance) {
                min_distance = distance;
                closest_point = target_center;
            }
        }
        
        return closest_point;
    }
    
private:
    bool initDXGI() {
        // 初始化DirectX設備和DXGI
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, nullptr, 0, D3D11_SDK_VERSION,
            &d3dDevice, nullptr, &d3dContext);
            
        if (FAILED(hr)) {
            return false;
        }

        // 獲取DXGI設備
        ComPtr<IDXGIDevice> dxgiDevice;
        hr = d3dDevice.As(&dxgiDevice);
        if (FAILED(hr)) return false;

        // 獲取DXGI適配器
        ComPtr<IDXGIAdapter> dxgiAdapter;
        hr = dxgiDevice->GetAdapter(&dxgiAdapter);
        if (FAILED(hr)) return false;

        // 獲取主要輸出
        ComPtr<IDXGIOutput> dxgiOutput;
        hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
        if (FAILED(hr)) return false;

        // 獲取DXGI輸出1
        ComPtr<IDXGIOutput1> dxgiOutput1;
        hr = dxgiOutput.As(&dxgiOutput1);
        if (FAILED(hr)) return false;

        // 創建桌面複製器
        hr = dxgiOutput1->DuplicateOutput(d3dDevice.Get(), &deskDupl);
        if (FAILED(hr)) return false;

        return true;
    }
    
    std::vector<uint8_t> captureScreen() {
        if (!deskDupl) {
            std::cerr << "Desktop duplication is null" << std::endl;
            return std::vector<uint8_t>();
        }

        ComPtr<IDXGIResource> desktopResource;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        
        // 修改超時時間並添加重試邏輯
        const int maxRetries = 3;
        const UINT timeout = 500;  // 增加到500毫秒
        
        for (int retry = 0; retry < maxRetries; retry++) {
            HRESULT hr = deskDupl->AcquireNextFrame(timeout, &frameInfo, &desktopResource);
            if (SUCCEEDED(hr)) {
                break;
            }
            
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                if (retry < maxRetries - 1) {
                    Sleep(100);  // 等待100毫秒後重試
                    continue;
                }
                std::cerr << "Frame capture timeout after " << maxRetries << " retries" << std::endl;
            } else {
                std::cerr << "Failed to acquire frame: " << std::hex << hr << std::endl;
            }
            return std::vector<uint8_t>();
        }

        // 獲取桌面紋理
        ComPtr<ID3D11Texture2D> desktopTexture;
        HRESULT hr = desktopResource.As(&desktopTexture);
        if (FAILED(hr)) {
            deskDupl->ReleaseFrame();
            return std::vector<uint8_t>();
        }

        // 創建可以CPU讀取的紋理
        D3D11_TEXTURE2D_DESC textureDesc;
        desktopTexture->GetDesc(&textureDesc);
        textureDesc.Usage = D3D11_USAGE_STAGING;
        textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        textureDesc.BindFlags = 0;
        textureDesc.MiscFlags = 0;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.SampleDesc.Count = 1;

        ComPtr<ID3D11Texture2D> stagingTexture;
        hr = d3dDevice->CreateTexture2D(&textureDesc, nullptr, &stagingTexture);
        if (FAILED(hr)) {
            deskDupl->ReleaseFrame();
            return std::vector<uint8_t>();
        }

        // 複製資源
        d3dContext->CopyResource(stagingTexture.Get(), desktopTexture.Get());

        // 映射資源
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
        if (FAILED(hr)) {
            deskDupl->ReleaseFrame();
            return std::vector<uint8_t>();
        }

        // 計算擷取區域
        RECT screenRect;
        GetClientRect(GetDesktopWindow(), &screenRect);
        int screenWidth = screenRect.right - screenRect.left;
        int screenHeight = screenRect.bottom - screenRect.top;
        int centerX = screenWidth / 2;
        int centerY = screenHeight / 2;
        int left = std::max(0, centerX - capture_size_X / 2);
        int top = std::max(0, centerY - capture_size_Y / 2);

        // 複製指定區域的資料
        std::vector<uint8_t> buffer(capture_size_X * capture_size_Y * 4);
        uint8_t* src = static_cast<uint8_t*>(mappedResource.pData);
        uint8_t* dst = buffer.data();

        for (int y = 0; y < capture_size_Y; ++y) {
            memcpy(
                dst + y * capture_size_X * 4,
                src + (top + y) * mappedResource.RowPitch + left * 4,
                capture_size_X * 4
            );
        }

        // 解除映射和釋放資源
        d3dContext->Unmap(stagingTexture.Get(), 0);
        deskDupl->ReleaseFrame();

        return buffer;
    }
    
    std::vector<cv::Rect> non_max_suppression(const std::vector<cv::Rect>& boxes, float overlap_threshold = 0.6f) {
        std::vector<cv::Rect> result;
        if (boxes.empty()) {
            return result;
        }

        // 計算每個框的面積並創建索引列表
        std::vector<std::pair<float, int>> areas;
        for (int i = 0; i < boxes.size(); i++) {
            float area = boxes[i].width * boxes[i].height;
            areas.push_back(std::make_pair(area, i));
        }

        // 根據面積從大到小排序
        std::sort(areas.begin(), areas.end(), 
            [](const std::pair<float, int>& a, const std::pair<float, int>& b) {
                return a.first > b.first;
            });

        std::vector<bool> pick(boxes.size(), true);

        // 執行NMS
        for (int i = 0; i < boxes.size(); i++) {
            if (!pick[areas[i].second]) continue;

            const auto& rect1 = boxes[areas[i].second];
            
            for (int j = i + 1; j < boxes.size(); j++) {
                if (!pick[areas[j].second]) continue;

                const auto& rect2 = boxes[areas[j].second];
                
                // 檢查重疊區域
                int x1 = std::max(rect1.x, rect2.x);
                int y1 = std::max(rect1.y, rect2.y);
                int x2 = std::min(rect1.x + rect1.width, rect2.x + rect2.width);
                int y2 = std::min(rect1.y + rect1.height, rect2.y + rect2.height);
                
                if (x2 > x1 && y2 > y1) {
                    float intersection = float((x2 - x1) * (y2 - y1));
                    float area1 = rect1.width * rect1.height;
                    float area2 = rect2.width * rect2.height;
                    
                    // 計算小框的面積占比
                    float ratio = intersection / area2;  // area2 一定是較小的框（因為已排序）
                    
                    // 如果重疊區域占小框面積的比例超過50%，則移除小框
                    if (ratio > 0.5) {
                        pick[areas[j].second] = false;
                    }
                }
            }
        }

        // 收集保留的框
        for (int i = 0; i < boxes.size(); i++) {
            if (pick[i]) {
                result.push_back(boxes[i]);
            }
        }

        return result;
    }
};

// Windows 入口點
int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nShowCmd
) {
    // 初始化鍵鼠控制
    KeyboardMouseControl control;
    if (!control.initialize()) {
        MessageBoxW(NULL, L"無法初始化鍵鼠控制器", L"錯誤", MB_OK | MB_ICONERROR);
        return -1;
    }
    
    // 關閉非必要的 OpenCV 日誌輸出
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);
    try {
        enableUTF8Console();
        std::wcout << L"初始化截圖系統..." << std::endl;
        ScreenCapture capture;
        
        // 移動靈敏度參數
        const float sensitivity = 0.9f;  // 可以調整這個值來改變移動速度
        const int min_move = 0;         // 最小移動距離
        bool isAiming = false;          // 瞄準狀態
        
        // 新增控制變量
        bool showPreview = true;  // 預覽畫面開關
        float headOffset = 0.15f;  // 頭部偏移比例
        
        while (true) {
            cv::Mat frame = capture.capture_center();
            if (frame.empty()) {
                std::wcout << L"無法擷取畫面，重試中..." << std::endl;
                Sleep(500);
                continue;
            }
            
            // 檢查左鍵狀態（按住左鍵時啟動瞄準）
            if (control.getKeyState(VK_LBUTTON)) {
                isAiming = true;
            } else {
                isAiming = false;
            }
            
            // 檢查右鍵狀態（按住右鍵時暫停）
            if (control.getKeyState(VK_RBUTTON)) {
                Sleep(100);
                continue;
            }
            
            auto targets = capture.detect_targets(frame);
            auto closest = capture.find_closest_target(targets);
            
            // 繪製所有檢測到的目標框
            for (const auto& target : targets) {
                cv::rectangle(frame, target, cv::Scalar(0, 255, 0), 2);
            }
            
            // 如果有最近的目標且正在瞄準，控制滑鼠
            if (closest.x != -1 && closest.y != -1 && isAiming) {
                cv::drawMarker(frame, closest, cv::Scalar(0, 0, 255), 
                              cv::MARKER_CROSS, 20, 2);
                
                // 計算目標點與屏幕中心的偏移
                int centerX = frame.cols / 2;
                int centerY = frame.rows / 2;
                int offsetX = closest.x - centerX;
                int offsetY = closest.y - centerY;
                
                // 計算移動距離（加入靈敏度調整）
                float moveX = offsetX * sensitivity;
                float moveY = offsetY * sensitivity;
                
                // 只有當偏移量大於最小移動距離時才移動
                if (abs(moveX) > min_move || abs(moveY) > min_move) {
                    // 使用平滑移動
                    control.moveMouseSmoothly(
                        static_cast<short>(moveX),
                        static_cast<short>(moveY),
                        1,  // delay: 移動延遲（越大越慢）
                        1   // delta: 每次移動的步長（越大越不平滑）
                    );
                }
            }
            
            // 按F1切換預覽畫面
            if (GetAsyncKeyState(VK_F1) & 1) {
                showPreview = !showPreview;
            }
            
            // 按PageUp/PageDown調整頭部偏移
            if (GetAsyncKeyState(VK_PRIOR) & 1) {  // PageUp
                headOffset = std::min(headOffset + 0.05f, 1.0f);
                capture.setHeadOffset(headOffset);
                std::wcout << L"頭部偏移: " << headOffset << std::endl;
            }
            if (GetAsyncKeyState(VK_NEXT) & 1) {  // PageDown
                headOffset = std::max(headOffset - 0.05f, 0.0f);
                capture.setHeadOffset(headOffset);
                std::wcout << L"頭部偏移: " << headOffset << std::endl;
            }
            
            // 只在開啟預覽時顯示畫面
            if (showPreview) {
                cv::imshow("Capture", frame);
            } else if (cv::getWindowProperty("Capture", cv::WND_PROP_VISIBLE) >= 0) {
                cv::destroyWindow("Capture");
            }
            
            // 按ESC退出
            if (cv::waitKey(1) == 27) {
                break;
            }
        }
    }
    catch (const cv::Exception& e) {
        std::wcerr << L"OpenCV 錯誤: " << e.what() << std::endl;
        return -1;
    }
    catch (const std::exception& e) {
        std::wcerr << L"程式錯誤: " << e.what() << std::endl;
        return -1;
    }
    
    return 0;
}