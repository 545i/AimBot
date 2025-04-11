#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include <memory>
#include <algorithm>
#include <wrl/client.h>
#include "KeyboardMouseControl.h"
#include <CommCtrl.h>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <shlobj.h>
#include "resource.h"
#pragma comment(lib, "comctl32.lib")

using Microsoft::WRL::ComPtr;

// 在文件開頭添加調試開關
#define DEBUG_MODE false

// 修改後的 Logger 類（保持不變）
class Logger {
private:
    static std::ofstream logFile;
    static bool initialized;

public:
    static void init() {
        if (DEBUG_MODE && !initialized) {
            logFile.open("debug_log.txt", std::ios::out | std::ios::app);
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            logFile << "\n=== New Session Started at " 
                   << std::ctime(&time)
                   << " ===\n";
            initialized = true;
        }
    }

    static void log(const std::string& message) {
        if (DEBUG_MODE && logFile.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::string timeStr = std::ctime(&time);
            timeStr = timeStr.substr(11, 8); // 只取時間部分 HH:MM:SS
            logFile << timeStr << " - " << message << std::endl;
            logFile.flush();
        }
    }

    static void close() {
        if (logFile.is_open()) {
            logFile.close();
        }
    }
};

// 初始化靜態成員
std::ofstream Logger::logFile;
bool Logger::initialized = false;

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
    ScreenCapture(int sizeX = 300, int sizeY = 300) 
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
        
        // 擷取螢幕像
        auto screenData = captureScreen();
        if (screenData.empty()) {
            return cv::Mat();
        }
        
        // 轉為OpenCV格式
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
        
        // 調整面篩選件
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
        
        // 修改超時時加重試邏輯
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

        // 根據面積從大到小排
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

// GUI 控制類
class ControlGUI {
private:
    static const int ID_CLOSE_ = 1001;
    static const int ID_TRIGGER_COMBO = 4;  // 下拉選單的ID
    static const int ID_AUTO_FIRE = 5;  // 新的控件ID
    static const int ID_AUTO_AIM = 6;
    static const int ID_SETTINGS = 1002;  // 新增設定按鈕ID
    bool isDragging = false;
    POINT dragStart;
    int triggerKeySelection;  // 移除默認值 = 0
    
    HWND hwndMain;
    HWND hwndPreview;    // 預覽複選框
    HWND hwndHeadOffset; // 頭部偏移滑塊
    HWND hwndSensitivity;// 靈敏滑塊
    HWND hwndHeadOffsetText;  // 新增：頭部偏移數值顯示
    HWND hwndSensitivityText; // 新增：靈敏度數值顯示
    HWND hwndClose;  // 添加關閉按鈕的句柄
    HWND hwndTriggerCombo;                  // 下拉選單控件
    int* triggerKey;                        // 觸發按鍵設置
    HWND hwndAutoFire;    // 自動開火複選框
    bool* autoFire;       // 自動開火狀態指針
    HWND hwndAutoAim;
    bool* autoAim;

    // 觸發按鍵的枚舉
    enum TriggerKeys {
        TRIGGER_LBUTTON = VK_LBUTTON,      // 左鍵
        TRIGGER_RBUTTON = VK_RBUTTON,      // 右鍵
        TRIGGER_XBUTTON1 = VK_XBUTTON1,    // 前側鍵
        TRIGGER_XBUTTON2 = VK_XBUTTON2     // 後側鍵
    };
    
    bool* showPreview;
    float* sensitivity;
    float* headOffset;
    ScreenCapture* capture;
    std::wstring iniPath; // 保存 INI 文件路徑

    // 添加FOV相關控件
    HWND hwndFOV;         // FOV滑塊
    HWND hwndFOVText;     // FOV數值顯示
    float* fov;           // FOV值指針

    HWND hwndSettings;  // 新增設定按鈕句柄
    HICON hSettingsIcon;  // 新增設定圖標句柄

    // 添加新的成員變數
    bool isSettingsExpanded = false;
    int normalWidth = 300;
    int expandedWidth = 600;
    
    // 添加設定頁面的控件句柄
    HWND hwndSettingsPanel;

    // 添加預覽視窗相關成員
    bool isPreviewExpanded = false;
    HWND hwndPreviewPanel;
    int previewPanelWidth = 300;  // 預覽面板寬度

    // 新增：獲取應用程式目錄
    std::wstring getAppPath() {
        wchar_t path[MAX_PATH];
        GetModuleFileName(NULL, path, MAX_PATH);
        std::wstring fullPath(path);
        return fullPath.substr(0, fullPath.find_last_of(L"\\/") + 1);
    }

    // 修改：保存設定到 INI 文件
    void saveSettings() {
        std::wstring settingsPath = getAppPath() + L"settings.ini";
        // 保存靈敏度和頭部偏移 - 使用更精確的格式
        wchar_t buffer[32];
        swprintf(buffer, 32, L"%.3f", *sensitivity);
        WritePrivateProfileString(L"Settings", L"Sensitivity", buffer, settingsPath.c_str());
        
        swprintf(buffer, 32, L"%.3f", *headOffset);
        WritePrivateProfileString(L"Settings", L"HeadOffset", buffer, settingsPath.c_str());
        
        // 保存觸發按鍵設置
        int currentSelection = SendMessage(hwndTriggerCombo, CB_GETCURSEL, 0, 0);
        swprintf(buffer, 32, L"%d", currentSelection);
        WritePrivateProfileString(L"Settings", L"TriggerKey", buffer, settingsPath.c_str());
        
        // 保存自動開火設置
        WritePrivateProfileString(L"Settings", L"AutoFire", 
            *autoFire ? L"1" : L"0", settingsPath.c_str());
        
        // 保存自動瞄準設置
        WritePrivateProfileString(L"Settings", L"AutoAim", 
            *autoAim ? L"1" : L"0", settingsPath.c_str());
        
        // 保存FOV設置
        swprintf(buffer, 32, L"%.3f", *fov);
        WritePrivateProfileString(L"Settings", L"FOV", buffer, settingsPath.c_str());
        
        Logger::log("Settings saved to: " + std::string(settingsPath.begin(), settingsPath.end()));
    }

    // 修改：從 INI 文件讀取設定
    void loadSettings() {
        std::wstring settingsPath = getAppPath() + L"settings.ini";
        wchar_t buffer[32];
        
        // 讀取 ShowPreview
        GetPrivateProfileString(L"Settings", L"ShowPreview", L"0", 
            buffer, 32, settingsPath.c_str());
        *showPreview = (wcstol(buffer, nullptr, 10) != 0);
        
        // 讀取 Sensitivity
        GetPrivateProfileString(L"Settings", L"Sensitivity", L"0.5", 
            buffer, 32, settingsPath.c_str());
        *sensitivity = std::wcstof(buffer, nullptr);
        
        // 讀取 HeadOffset
        GetPrivateProfileString(L"Settings", L"HeadOffset", L"0.1", 
            buffer, 32, settingsPath.c_str());
        *headOffset = std::wcstof(buffer, nullptr);
        
        // 讀取觸發按鍵設置
        GetPrivateProfileString(L"Settings", L"TriggerKey", L"0", 
            buffer, 32, settingsPath.c_str());
        triggerKeySelection = wcstol(buffer, nullptr, 10);
        
        // 確保值在有效範圍內
        if (triggerKeySelection < 0 || triggerKeySelection > 3) {
            triggerKeySelection = 0;  // 如果無效則設為默認值（左鍵）
        }
        
        // 讀取自動開火設置
        GetPrivateProfileString(L"Settings", L"AutoFire", L"1", 
            buffer, 32, settingsPath.c_str());
        *autoFire = (wcstol(buffer, nullptr, 10) != 0);
        
        // 讀取自動瞄準設置
        GetPrivateProfileString(L"Settings", L"AutoAim", L"1", 
            buffer, 32, settingsPath.c_str());
        *autoAim = (wcstol(buffer, nullptr, 10) != 0);
        
        // 讀取FOV設置
        GetPrivateProfileString(L"Settings", L"FOV", L"0.5", 
            buffer, 32, settingsPath.c_str());
        *fov = std::wcstof(buffer, nullptr);
        
        // 更新FOV滑塊
        if (hwndFOV) {
            SendMessage(hwndFOV, TBM_SETPOS, TRUE, (LPARAM)(*fov * 100));
            updateSliderText(hwndFOVText, *fov);
        }
        
        // 更新自動開火複選框
        if (hwndAutoFire) {
            SendMessage(hwndAutoFire, BM_SETCHECK, 
                *autoFire ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        
        // 更新自動瞄準複選框
        if (hwndAutoAim) {
            SendMessage(hwndAutoAim, BM_SETCHECK, 
                *autoAim ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        
        // 更新界面控件的值
        if (hwndHeadOffset) {
            SendMessage(hwndHeadOffset, TBM_SETPOS, TRUE, (LPARAM)(*headOffset * 100));
            updateSliderText(hwndHeadOffsetText, *headOffset);
        }
        
        if (hwndSensitivity) {
            SendMessage(hwndSensitivity, TBM_SETPOS, TRUE, (LPARAM)(*sensitivity * 100));
            updateSliderText(hwndSensitivityText, *sensitivity);
        }
        
        if (hwndPreview) {
            SendMessage(hwndPreview, BM_SETCHECK, *showPreview ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        
        // 更新 ScreenCapture 的頭部偏移值
        if (capture) {
            capture->setHeadOffset(*headOffset);
        }
        
        // 更新下拉選單的選擇
        if (hwndTriggerCombo) {
            SendMessage(hwndTriggerCombo, CB_SETCURSEL, triggerKeySelection, 0);
        }
        
        Logger::log("Settings loaded from: " + std::string(settingsPath.begin(), settingsPath.end()));
    }

    // 添加 hDarkBrush 作為類成員
    HBRUSH hDarkBrush;

    // 預覽相關
    HWND hwndPreviewImage;     // 圖像顯示控件
    HBITMAP hPreviewBitmap;    // 當前顯示的位圖
    static const int WM_UPDATE_PREVIEW = WM_USER + 1;  // 自定義消息
    
    // 將 OpenCV Mat 轉換為 HBITMAP
    HBITMAP MatToHBitmap(const cv::Mat& frame) {
        // 確保圖像格式正確
        cv::Mat rgbFrame;
        if (frame.channels() == 4) {
            cv::cvtColor(frame, rgbFrame, cv::COLOR_BGRA2BGR);
        } else {
            frame.copyTo(rgbFrame);
        }

        // 創建 BITMAPINFO 結構
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = rgbFrame.cols;
        bmi.bmiHeader.biHeight = -rgbFrame.rows;  // 負值表示自上而下
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        // 創建 DIB section
        void* pBits;
        HDC hdc = GetDC(NULL);
        HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
        
        if (hBitmap) {
            // 複製圖像數據
            memcpy(pBits, rgbFrame.data, rgbFrame.total() * rgbFrame.channels());
        }
        
        ReleaseDC(NULL, hdc);
        return hBitmap;
    }

    // 預覽圖像窗口過程
    static LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        ControlGUI* gui = (ControlGUI*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        
        switch (msg) {
            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                
                if (gui && gui->hPreviewBitmap) {
                    // 獲取窗口尺寸
                    RECT rect;
                    GetClientRect(hwnd, &rect);
                    
                    // 創建內存 DC
                    HDC hdcMem = CreateCompatibleDC(hdc);
                    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, gui->hPreviewBitmap);
                    
                    // 獲取位圖信息
                    BITMAP bm;
                    GetObject(gui->hPreviewBitmap, sizeof(bm), &bm);
                    
                    // 計算縮放比例
                    float scaleX = (float)(rect.right - rect.left) / bm.bmWidth;
                    float scaleY = (float)(rect.bottom - rect.top) / bm.bmHeight;
                    float scale = std::min(scaleX, scaleY);  // 修改這行，添加 std:: 前缀
                    
                    // 計算目標尺寸和位置
                    int targetWidth = (int)(bm.bmWidth * scale);
                    int targetHeight = (int)(bm.bmHeight * scale);
                    int x = (rect.right - targetWidth) / 2;
                    int y = (rect.bottom - targetHeight) / 2;
                    
                    // 使用高質量縮放
                    SetStretchBltMode(hdc, HALFTONE);
                    SetBrushOrgEx(hdc, 0, 0, NULL);
                    
                    // 繪製圖像
                    StretchBlt(hdc, x, y, targetWidth, targetHeight,
                              hdcMem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                    
                    // 清理
                    SelectObject(hdcMem, hOldBitmap);
                    DeleteDC(hdcMem);
                }
                
                EndPaint(hwnd, &ps);
                return 0;
            }
            
            case WM_ERASEBKGND:
            {
                HDC hdc = (HDC)wParam;
                RECT rect;
                GetClientRect(hwnd, &rect);
                FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
                return TRUE;
            }
        }
        
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    // 註冊預覽窗口類
    void registerPreviewClass() {
        WNDCLASSEX wc = {0};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = PreviewWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"PreviewClass";
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassEx(&wc);
    }

public:
    ControlGUI(bool* showPreview, float* sensitivity, float* headOffset, 
               bool* autoFire, bool* autoAim, float* fov, ScreenCapture* capture) 
        : showPreview(showPreview), sensitivity(sensitivity), 
          headOffset(headOffset), autoFire(autoFire), autoAim(autoAim),
          fov(fov), capture(capture), hPreviewBitmap(NULL) {
        
        // 在構造函數中初始化深色畫刷
        hDarkBrush = CreateSolidBrush(RGB(18, 18, 18));
        
        // 在創建控件之前先讀取設定
        loadSettings();
        
        // 初始化 Common Controls
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_WIN95_CLASSES;
        InitCommonControlsEx(&icex);
        
        // 註冊窗口類
        WNDCLASSEX wc = {0};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"ControlGUI";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassEx(&wc);
        
        // 創建自定義標題欄的窗口
        hwndMain = CreateWindowEx(
            WS_EX_LAYERED,
            L"ControlGUI", 
            L"NBRC",
            WS_POPUP | WS_VISIBLE, // 使 WS_POPUP 替代 WS_OVERLAPPEDWINDOW
            CW_USEDEFAULT, CW_USEDEFAULT, 300, 700,
            NULL, NULL, GetModuleHandle(NULL), this
        );

        // 設置窗口透明度
        SetLayeredWindowAttributes(hwndMain, 0, 255, LWA_ALPHA);
        
        // 創建控件
        createControls();
        
        // 顯示窗口
        ShowWindow(hwndMain, SW_SHOW);
        UpdateWindow(hwndMain);
        
        // 初始化控件時使用讀取的設定值
        SendMessage(hwndHeadOffset, TBM_SETPOS, TRUE, (LPARAM)(*headOffset * 100));
        SendMessage(hwndSensitivity, TBM_SETPOS, TRUE, (LPARAM)(*sensitivity * 100));
        SendMessage(hwndPreview, BM_SETCHECK, *showPreview ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    // 添加析構函數來清理資源
    ~ControlGUI() {
        if (hSettingsIcon) {
            DestroyIcon(hSettingsIcon);
        }
        if (hDarkBrush) {
            DeleteObject(hDarkBrush);
        }
        if (hPreviewBitmap) {
            DeleteObject(hPreviewBitmap);
        }
    }

    void update() {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    
    int getCurrentTriggerKey() {
        switch (SendMessage(hwndTriggerCombo, CB_GETCURSEL, 0, 0)) {
            case 1:  // 右鍵
                return VK_RBUTTON;
            case 2:  // 前側鍵
                return VK_XBUTTON1;
            case 3:  // 後側鍵
                return VK_XBUTTON2;
            default: // 默認左鍵
                return VK_LBUTTON;
        }
    }
    
    // 添加公共方法來訪問和設置預覽窗口的狀態
    void setPreviewCheckState(bool checked) {
        if (hwndPreview) {
            SendMessage(hwndPreview, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    }
    
    // 移動這個方法到 public 部分
    void updatePreview(const cv::Mat& frame) {
        if (!frame.empty() && isPreviewExpanded) {
            // 刪除舊的位圖
            if (hPreviewBitmap) {
                DeleteObject(hPreviewBitmap);
            }
            
            // 創建新的位圖
            hPreviewBitmap = MatToHBitmap(frame);
            
            // 強制重繪預覽窗口
            InvalidateRect(hwndPreviewImage, NULL, TRUE);
            UpdateWindow(hwndPreviewImage);
        }
    }

private:
    void createControls() {
        // 載入圖標
        HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
        
        // 創建圖標控件
        HWND hwndIcon = CreateWindow(
            L"STATIC", NULL,
            WS_VISIBLE | WS_CHILD | SS_ICON,
            10, 5, 16, 16,  // 圖標大小
            hwndMain, NULL, GetModuleHandle(NULL), NULL
        );
        SendMessage(hwndIcon, STM_SETICON, (WPARAM)hIcon, 0);

        // 創建標題文字 - 調整位置和字體大小
        HWND hwndTitle = CreateWindow(
            L"STATIC", L"NBRC",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            45, 5, 200, 45,  // 增加高度到 25 以適應更大的字體
            hwndMain, NULL, GetModuleHandle(NULL), NULL
        );

        // 創建大號字體用於標題
        HFONT hTitleFont = CreateFont(
            40,                    // 字高度設為 20
            0,                     // 字體寬度（自動）
            0,                     // 文字傾斜角度
            0,                     // 字符傾斜角度
            FW_BOLD,              // 粗體
            FALSE,                 // 不使用斜體
            FALSE,                 // 不使用下劃線
            FALSE,                 // 不使用刪除線
            DEFAULT_CHARSET,       // 使用默認字符集
            OUT_DEFAULT_PRECIS,    // 默認輸出精度
            CLIP_DEFAULT_PRECIS,   // 默認裁剪精度
            CLEARTYPE_QUALITY,     // 使用 ClearType 提高清晰度
            DEFAULT_PITCH | FF_DONTCARE,
            L"Arial"              // 使用 Arial 字體
        );

        // 將字體應用到標題
        SendMessage(hwndTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);

        // 修改載入圖標的方式
        hSettingsIcon = (HICON)LoadImage(
            GetModuleHandle(NULL),
            MAKEINTRESOURCE(IDI_SETTINGS),
            IMAGE_ICON,
            16,  // 指定圖標寬度
            16,  // 指定圖標高度
            LR_DEFAULTCOLOR | LR_SHARED
        );

        if (!hSettingsIcon) {
            // 加入錯誤檢查和日誌記錄
            DWORD error = GetLastError();
            Logger::log("Failed to load settings icon. Error code: " + std::to_string(error));
        }

        // 修改設定按鈕的創建
        hwndSettings = CreateWindow(
            L"BUTTON", L"",
            WS_VISIBLE | WS_CHILD | BS_ICON | BS_FLAT,
            235, 5, 20, 20,
            hwndMain, (HMENU)ID_SETTINGS, GetModuleHandle(NULL), NULL
        );

        // 直接發送設置圖標的消息
        if (hSettingsIcon) {
            SendMessage(hwndSettings, BM_SETIMAGE, IMAGE_ICON, (LPARAM)hSettingsIcon);
        }

        // 創建設定面板（初始時隱藏）
        hwndSettingsPanel = CreateWindow(
            L"STATIC", NULL,
            WS_CHILD | SS_NOTIFY, // 初始時不可見
            300, 0, 300, 700, // 位置在主視窗右側
            hwndMain, NULL, GetModuleHandle(NULL), NULL
        );

        // 在設定面板上創建控件
        createSettingsControls();

        // 修改關閉按鈕的位置
        hwndClose = CreateWindow(
            L"BUTTON", L"X",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
            260, 5, 20, 20,  // X位置改為260，緊接著設定按鈕
            hwndMain, (HMENU)ID_CLOSE_, GetModuleHandle(NULL), NULL
        );

        // 設置按鈕的字體
        HFONT hFont = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        SendMessage(hwndClose, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        // 修改預覽複選框的創建和初始狀態設置
        hwndPreview = CreateWindow(
            L"BUTTON", L"顯示預覽",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            10, 50, 100, 30,
            hwndMain, (HMENU)1, GetModuleHandle(NULL), NULL
        );
        // 設置初始狀態
        SendMessage(hwndPreview, BM_SETCHECK, *showPreview ? BST_CHECKED : BST_UNCHECKED, 0);

        // 頭部偏移標籤 - 調整位置
        HWND hwndLabel = CreateWindow(
            L"STATIC", L"頭部偏移:",
            WS_VISIBLE | WS_CHILD | SS_LEFTNOWORDWRAP,
            10, 140, 70, 20,  // Y位置從50改為80
            hwndMain, NULL, GetModuleHandle(NULL), NULL
        );
        SetWindowLongPtr(hwndLabel, GWLP_USERDATA, (LONG_PTR)hDarkBrush);

        // 頭部偏移數值顯示 - 調整位置
        hwndHeadOffsetText = CreateWindow(
            L"STATIC", L"0.0",
            WS_VISIBLE | WS_CHILD | SS_LEFTNOWORDWRAP,
            240, 140, 40, 20,  // Y位置從50改為80
            hwndMain, NULL, GetModuleHandle(NULL), NULL
        );
        SetWindowLongPtr(hwndHeadOffsetText, GWLP_USERDATA, (LONG_PTR)hDarkBrush);

        // 頭部偏移滑塊 - 調整位置
        hwndHeadOffset = CreateWindow(
            TRACKBAR_CLASS, NULL,
            WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_NOTICKS,
            80, 140, 150, 30,  // Y位置從50改為80
            hwndMain, (HMENU)2, GetModuleHandle(NULL), NULL
        );
        SendMessage(hwndHeadOffset, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessage(hwndHeadOffset, TBM_SETPOS, TRUE, (LPARAM)(*headOffset * 100));

        // 靈敏度標籤 - 調整位置
        CreateWindow(
            L"STATIC", L"靈敏度:",
            WS_VISIBLE | WS_CHILD,
            10, 170, 70, 20,  // Y位置從90改為120
            hwndMain, NULL, GetModuleHandle(NULL), NULL
        );

        // 靈敏度滑塊 - 調整位置
        hwndSensitivity = CreateWindow(
            TRACKBAR_CLASS, NULL,
            WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_NOTICKS,
            80, 170, 150, 30,  // Y位置從90改為120
            hwndMain, (HMENU)3, GetModuleHandle(NULL), NULL
        );
        SendMessage(hwndSensitivity, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessage(hwndSensitivity, TBM_SETPOS, TRUE, (LPARAM)(*sensitivity * 100));

        // 靈敏度數值顯示 - 調整位置
        hwndSensitivityText = CreateWindow(
            L"STATIC", L"0.0",
            WS_VISIBLE | WS_CHILD,
            240, 170, 40, 20,  // Y位置從90改為120
            hwndMain, NULL, GetModuleHandle(NULL), NULL
        );

        // 初始化顯示值
        updateSliderText(hwndHeadOffsetText, *headOffset);
        updateSliderText(hwndSensitivityText, *sensitivity);

        // 為所有子控件設置透明背景
        EnumChildWindows(hwndMain, [](HWND hwnd, LPARAM lParam) -> BOOL {
            SetWindowLong(hwnd, GWL_EXSTYLE, 
                GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
            return TRUE;
        }, 0);

        // 創建觸發按鍵標籤
        CreateWindow(
            L"STATIC", L"觸發按鍵:",
            WS_VISIBLE | WS_CHILD | SS_LEFTNOWORDWRAP,
            10, 90, 70, 20,
            hwndMain, NULL, GetModuleHandle(NULL), NULL
        );

        // 創建下拉選
        hwndTriggerCombo = CreateWindow(
            L"COMBOBOX", NULL,
            WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            80, 90, 150, 100,
            hwndMain, (HMENU)ID_TRIGGER_COMBO, GetModuleHandle(NULL), NULL
        );

        // 添加選項
        SendMessage(hwndTriggerCombo, CB_ADDSTRING, 0, (LPARAM)L"滑鼠左鍵");
        SendMessage(hwndTriggerCombo, CB_ADDSTRING, 0, (LPARAM)L"滑鼠右鍵");
        SendMessage(hwndTriggerCombo, CB_ADDSTRING, 0, (LPARAM)L"滑鼠前側鍵");
        SendMessage(hwndTriggerCombo, CB_ADDSTRING, 0, (LPARAM)L"滑鼠後側鍵");

        // 設置保存的選項
        SendMessage(hwndTriggerCombo, CB_SETCURSEL, triggerKeySelection, 0);

        // 修自動開火複選框的創建和初始狀態設置
        hwndAutoFire = CreateWindow(
            L"BUTTON", L"自動開火",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            10, 200, 100, 30,
            hwndMain, (HMENU)ID_AUTO_FIRE, GetModuleHandle(NULL), NULL
        );
        // 設置初始狀態
        SendMessage(hwndAutoFire, BM_SETCHECK, *autoFire ? BST_CHECKED : BST_UNCHECKED, 0);

        // 修改自動瞄準複選框的創建和初始狀態設置
        hwndAutoAim = CreateWindow(
            L"BUTTON", L"自動瞄準",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            10, 230, 100, 30,
            hwndMain, (HMENU)ID_AUTO_AIM, GetModuleHandle(NULL), NULL
        );
        // 設置初始狀態
        SendMessage(hwndAutoAim, BM_SETCHECK, *autoAim ? BST_CHECKED : BST_UNCHECKED, 0);

        // 創建FOV標籤
        CreateWindow(
            L"STATIC", L"瞄準FOV:",
            WS_VISIBLE | WS_CHILD | SS_LEFTNOWORDWRAP,
            10, 260, 70, 20,
            hwndMain, NULL, GetModuleHandle(NULL), NULL
        );

        // FOV滑塊
        hwndFOV = CreateWindow(
            TRACKBAR_CLASS, NULL,
            WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_NOTICKS,
            80, 260, 150, 30,
            hwndMain, (HMENU)7, GetModuleHandle(NULL), NULL
        );
        SendMessage(hwndFOV, TBM_SETRANGE, TRUE, MAKELONG(10, 100));  // 10-100的範圍
        SendMessage(hwndFOV, TBM_SETPOS, TRUE, (LPARAM)(*fov * 100));

        // FOV數值顯示
        hwndFOVText = CreateWindow(
            L"STATIC", L"0.0",
            WS_VISIBLE | WS_CHILD | SS_LEFTNOWORDWRAP,
            240, 260, 40, 20,
            hwndMain, NULL, GetModuleHandle(NULL), NULL
        );
        updateSliderText(hwndFOVText, *fov);

        // 為所有控件設置背景色
        EnumChildWindows(hwndMain, [](HWND hwnd, LPARAM lParam) -> BOOL {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lParam);
            return TRUE;
        }, (LPARAM)hDarkBrush);

        // 創建預覽面板（初始時隱藏）
        hwndPreviewPanel = CreateWindow(
            L"STATIC", NULL,
            WS_CHILD | SS_NOTIFY,
            300, 0, previewPanelWidth, 700,
            hwndMain, NULL, GetModuleHandle(NULL), NULL
        );

        // 註冊預覽窗口類
        registerPreviewClass();

        // 創建預覽圖像控件
        hwndPreviewImage = CreateWindow(
            L"PreviewClass", NULL,
            WS_CHILD | WS_VISIBLE,
            0, 0, previewPanelWidth, 700,
            hwndPreviewPanel, NULL, GetModuleHandle(NULL), this
        );
        
        SetWindowLongPtr(hwndPreviewImage, GWLP_USERDATA, (LONG_PTR)this);
    }
    
    // 新增：更新滑塊數值顯示的輔助函數
    void updateSliderText(HWND hwndText, float value) {
        wchar_t buffer[10];
        swprintf(buffer, 10, L"%.2f", value);
        SetWindowText(hwndText, buffer);
    }
    
    // 新增：創建設定��板的控件
    void createSettingsControls() {
        // 設定面板標題
        HWND hwndSettingsTitle = CreateWindow(
            L"STATIC", L"進階設定",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            10, 10, 280, 30,
            hwndSettingsPanel, NULL, GetModuleHandle(NULL), NULL
        );

        // 使用與主視窗相同的標題字體
        HFONT hTitleFont = CreateFont(
            30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial"
        );
        SendMessage(hwndSettingsTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
    }

    // 新增：切換設定面板的顯示狀態
    void toggleSettingsPanel() {
        isSettingsExpanded = !isSettingsExpanded;
        
        // 獲取當前窗口位置
        RECT rect;
        GetWindowRect(hwndMain, &rect);
        
        // 計算新的寬度
        int newWidth = isSettingsExpanded ? expandedWidth : normalWidth;
        
        // 動畫效果展開/收縮窗口
        const int steps = 10;
        int currentWidth = rect.right - rect.left;
        int widthDelta = (newWidth - currentWidth) / steps;
        
        for (int i = 0; i < steps; i++) {
            currentWidth += widthDelta;
            SetWindowPos(hwndMain, NULL, rect.left, rect.top,
                currentWidth, rect.bottom - rect.top,
                SWP_NOZORDER | SWP_NOMOVE);
            Sleep(10); // 短暫延遲製造動畫效果
        }
        
        // 確保最終尺寸精確
        SetWindowPos(hwndMain, NULL, rect.left, rect.top,
            newWidth, rect.bottom - rect.top,
            SWP_NOZORDER | SWP_NOMOVE);
        
        // 顯示或隱藏設定面板
        ShowWindow(hwndSettingsPanel, isSettingsExpanded ? SW_SHOW : SW_HIDE);
        
        // 重繪窗口
        InvalidateRect(hwndMain, NULL, TRUE);
    }

    // 新增：切換預覽面板的顯示狀態
    void togglePreviewPanel() {
        isPreviewExpanded = !isPreviewExpanded;
        
        // 獲取當前窗口位置
        RECT rect;
        GetWindowRect(hwndMain, &rect);
        
        // 計算新的寬度（考慮設定面板是否已展開）
        int baseWidth = isSettingsExpanded ? expandedWidth : normalWidth;
        int newWidth = baseWidth + (isPreviewExpanded ? previewPanelWidth : 0);
        
        // 動畫效果展開/收縮窗口
        const int steps = 10;
        int currentWidth = rect.right - rect.left;
        int widthDelta = (newWidth - currentWidth) / steps;
        
        for (int i = 0; i < steps; i++) {
            currentWidth += widthDelta;
            SetWindowPos(hwndMain, NULL, rect.left, rect.top,
                currentWidth, rect.bottom - rect.top,
                SWP_NOZORDER | SWP_NOMOVE);
            Sleep(10);
        }
        
        // 確保最終尺寸精確
        SetWindowPos(hwndMain, NULL, rect.left, rect.top,
            newWidth, rect.bottom - rect.top,
            SWP_NOZORDER | SWP_NOMOVE);
        
        // 顯示或隱藏預覽面板
        ShowWindow(hwndPreviewPanel, isPreviewExpanded ? SW_SHOW : SW_HIDE);
        
        // 更新預覽狀態
        *showPreview = isPreviewExpanded;
        
        // 重繪窗口
        InvalidateRect(hwndMain, NULL, TRUE);
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        ControlGUI* gui = nullptr;
        
        if (uMsg == WM_CREATE) {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            gui = (ControlGUI*)pCreate->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)gui);
        } else {
            gui = (ControlGUI*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }
        
        if (gui) {
            switch (uMsg) {
                case WM_COMMAND:
                    // 修改關閉按鈕的處理
                    if (LOWORD(wParam) == ID_CLOSE_) {
                        Logger::log("Close button clicked");
                        // 保存設置
                        gui->saveSettings();
                        // 確保關閉所有OpenCV窗口
                        cv::destroyAllWindows();
                        // 記錄程序結束
                        Logger::log("Program terminated by close button");
                        Logger::close();
                        // 強制結束程序
                        ExitProcess(0);  // 直接使用 ExitProcess
                        return 0;
                    }
                    if (LOWORD(wParam) == 1) { // 預覽複選框
                        bool newPreviewState = (SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
                        if (*gui->showPreview != newPreviewState) {
                            // 使用新的切換方法
                            if (newPreviewState != gui->isPreviewExpanded) {
                                gui->togglePreviewPanel();
                            }
                            gui->saveSettings();
                        }
                    }
                    if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == ID_TRIGGER_COMBO) {
                        // 保存設置
                        gui->saveSettings();
                    }
                    if (LOWORD(wParam) == ID_AUTO_FIRE) {
                        *gui->autoFire = (SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
                        gui->saveSettings();
                    }
                    if (LOWORD(wParam) == ID_AUTO_AIM) {
                        *gui->autoAim = (SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
                        gui->saveSettings();
                    }
                    if (LOWORD(wParam) == ID_SETTINGS) {
                        gui->toggleSettingsPanel();
                        return 0;
                    }
                    break;
                    
                case WM_HSCROLL: // 滑塊消息
                    if ((HWND)lParam == gui->hwndHeadOffset) {
                        int pos = SendMessage(gui->hwndHeadOffset, TBM_GETPOS, 0, 0);
                        *gui->headOffset = pos / 100.0f;
                        gui->capture->setHeadOffset(*gui->headOffset);
                        gui->updateSliderText(gui->hwndHeadOffsetText, *gui->headOffset);
                        gui->saveSettings();
                    }
                    else if ((HWND)lParam == gui->hwndSensitivity) {
                        int pos = SendMessage(gui->hwndSensitivity, TBM_GETPOS, 0, 0);
                        *gui->sensitivity = pos / 100.0f;
                        gui->updateSliderText(gui->hwndSensitivityText, *gui->sensitivity);
                        gui->saveSettings();
                    }
                    else if ((HWND)lParam == gui->hwndFOV) {
                        int pos = SendMessage(gui->hwndFOV, TBM_GETPOS, 0, 0);
                        *gui->fov = pos / 100.0f;
                        gui->updateSliderText(gui->hwndFOVText, *gui->fov);
                        gui->saveSettings();
                    }
                    break;
                    
                case WM_CLOSE:
                    // 保存設置
                    gui->saveSettings();
                    // 確保關閉所有OpenCV窗口
                    cv::destroyAllWindows();
                    // 記錄程序結束
                    Logger::log("Program terminated by window close");
                    Logger::close();
                    // 強制��束程序
                    ExitProcess(0);
                    return 0;
                    
                case WM_CTLCOLORSTATIC:
                {
                    HDC hdcStatic = (HDC)wParam;
                    HWND hwndStatic = (HWND)lParam;
                    
                    // 設置所���靜態控件的文字顏色為白色
                    SetTextColor(hdcStatic, RGB(255, 255, 255));
                    
                    // 如果是設定面板或其子控件
                    if (hwndStatic == gui->hwndSettingsPanel || 
                        GetParent(hwndStatic) == gui->hwndSettingsPanel) {
                        SetBkColor(hdcStatic, RGB(28, 28, 28));
                        static HBRUSH hSettingsBrush = CreateSolidBrush(RGB(28, 28, 28));
                        return (LRESULT)hSettingsBrush;
                    }
                    
                    // 主視窗的控件
                    SetBkColor(hdcStatic, RGB(18, 18, 18));
                    return (LRESULT)gui->hDarkBrush;
                }

                // 添加主窗口背景色設置
                case WM_ERASEBKGND:
                {
                    HDC hdc = (HDC)wParam;
                    RECT rect;
                    GetClientRect(hwnd, &rect);
                    SetBkColor(hdc, RGB(18, 18, 18));
                    ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rect, NULL, 0, NULL);
                    return TRUE;
                }

                case WM_CTLCOLORBTN:
                {
                    // 設置按鈕的背景色
                    HDC hdcBtn = (HDC)wParam;
                    SetBkColor(hdcBtn, RGB(18, 18, 18));
                    return (LRESULT)gui->hDarkBrush;
                }

                case WM_LBUTTONDOWN:
                {
                    // 開始拖動視窗
                    gui->isDragging = true;
                    SetCapture(hwnd);
                    GetCursorPos(&gui->dragStart);
                    return 0;
                }

                case WM_MOUSEMOVE:
                {
                    if (gui->isDragging) {
                        POINT currentPos;
                        GetCursorPos(&currentPos);
                        
                        // 計算移動距離
                        int deltaX = currentPos.x - gui->dragStart.x;
                        int deltaY = currentPos.y - gui->dragStart.y;
                        
                        // 獲取當前視窗位置
                        RECT windowRect;
                        GetWindowRect(hwnd, &windowRect);
                        
                        // 移動視窗
                        SetWindowPos(hwnd, NULL,
                            windowRect.left + deltaX,
                            windowRect.top + deltaY,
                            0, 0,
                            SWP_NOSIZE | SWP_NOZORDER);
                        
                        // 更新拖動起始點
                        gui->dragStart = currentPos;
                    }
                    return 0;
                }

                case WM_LBUTTONUP:
                {
                    // 結束拖動
                    if (gui->isDragging) {
                        gui->isDragging = false;
                        ReleaseCapture();
                    }
                    return 0;
                }

                case WM_NCHITTEST:
                {
                    // 讓整個視窗區域都可以拖動
                    LRESULT hit = DefWindowProc(hwnd, uMsg, wParam, lParam);
                    if (hit == HTCLIENT)
                        return HTCAPTION;
                    return hit;
                }

                case WM_DRAWITEM:
                {
                    LPDRAWITEMSTRUCT lpDIS = (LPDRAWITEMSTRUCT)lParam;
                    if (lpDIS->CtlID == ID_SETTINGS && lpDIS->itemAction & ODA_DRAWENTIRE) {
                        if (gui->hSettingsIcon) {
                            DrawIconEx(lpDIS->hDC,
                                lpDIS->rcItem.left,
                                lpDIS->rcItem.top,
                                gui->hSettingsIcon,
                                20, 20,  // 圖標大
                                0, NULL, DI_NORMAL);
                            return TRUE;
                        }
                    }
                    break;
                }
            }
        }
        
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
};

// 添加一個輔助函數來安全地關閉視窗
void safeDestroyWindow(const std::string& windowName) {
    try {
        // 檢查視窗是否存在
        if (cv::getWindowProperty(windowName, cv::WND_PROP_VISIBLE) >= 0) {
            cv::destroyWindow(windowName);
            cv::waitKey(1);  // 給OpenCV一個處理視窗關閉的機會
        }
    }
    catch (const cv::Exception& e) {
        // 忽略任何OpenCV異常，因為我們只是在嘗試關閉視窗
        Logger::log("Warning: Exception while closing window: " + std::string(e.what()));
    }
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance,
                    _In_opt_ HINSTANCE hPrevInstance,
                    _In_ LPWSTR lpCmdLine,
                    _In_ int nShowCmd) {
    Logger::init();
    
    // 確保關閉所有可能存在的OpenCV視窗
    cv::destroyAllWindows();
    
    try {
        // 記錄程序啟動
        Logger::log("Program started");
        
        KeyboardMouseControl control;
        if (!control.initialize()) {
            Logger::log("Failed to initialize keyboard/mouse control");
            MessageBoxW(NULL, L"無法初始化鍵鼠控制器", L"錯誤", MB_OK | MB_ICONERROR);
            return -1;
        }
        Logger::log("Keyboard/mouse control initialized successfully");

        // 關閉非必要的 OpenCV 日誌輸出
        cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);
        enableUTF8Console();
        std::wcout << L"初始化截圖統..." << std::endl;
        ScreenCapture capture;
        
        // 控制參數
        bool showPreview = false;
        float sensitivity = 0.5f;
        float headOffset = 0.1f;
        bool isAiming = false;
        bool autoFire = true;
        bool autoAim = true;  // 新增自動瞄準控制變量
        float fov = 0.5f;  // 默認FOV值
        
        // 創建 GUI
        ControlGUI gui(&showPreview, &sensitivity, &headOffset, &autoFire, &autoAim, &fov, &capture);
        
        while (true) {
            // 更新 GUI
            gui.update();
            
            cv::Mat frame = capture.capture_center();
            if (frame.empty()) {
                std::wcout << L"無法取畫面，重試中..." << std::endl;
                Sleep(500);
                continue;
            }
            
            // 檢查選定的按鍵狀態
            int currentTriggerKey = gui.getCurrentTriggerKey();
            if (control.getKeyState(currentTriggerKey)) {
                isAiming = true;
            } else {
                isAiming = false;
            }
            
            auto targets = capture.detect_targets(frame);
            auto closest = capture.find_closest_target(targets);
            
            // 繪製所有檢測到的目標框和其他視覺元素
            for (const auto& target : targets) {
                cv::rectangle(frame, target, cv::Scalar(0, 255, 0), 2);
            }
            
            // 在目標很近時才自動開槍
            if (closest.x != -1 && closest.y != -1) {
                // 計算目標點與屏幕中心的偏移
                int centerX = frame.cols / 2;
                int centerY = frame.rows / 2;
                int offsetX = closest.x - centerX;
                int offsetY = closest.y - centerY;
                
                // 計算目標是否在FOV範圍內
                float distance = sqrt(offsetX * offsetX + offsetY * offsetY);
                float maxDistance = frame.cols * fov;  // 使用FOV值計算最大距離
                
                // 只有當目標在FOV範圍內時才進行移動和開火
                if (distance <= maxDistance) {
                    // 修改移動邏輯，添加非線性移動和抖動
                    float moveX = offsetX * sensitivity;
                    float moveY = offsetY * sensitivity;
                    
                    if ((abs(moveX) > 0.5 || abs(moveY) > 0.5) && isAiming && autoAim) {
                        // 添加非線性因子，使移動更加平滑
                        float nonLinearX = moveX * (1.0f + abs(moveX) / frame.cols);
                        float nonLinearY = moveY * (1.0f + abs(moveY) / frame.rows);
                        
                        // 生成隨機抖動值 (-0.5 到 0.5)
                        float jitterX = (static_cast<float>(rand()) / RAND_MAX - 0.5f);
                        float jitterY = (static_cast<float>(rand()) / RAND_MAX - 0.5f);
                        
                        // 應用抖動
                        nonLinearX += jitterX;
                        nonLinearY += jitterY;
                        
                        // 修改這裡：增加步驟數和調整延遲時間
                        control.moveMouseSmoothly(
                            static_cast<short>(nonLinearX),
                            static_cast<short>(nonLinearY),
                            1,    // 增加步驟數到4
                            1   // 減少延遲到0.5毫秒
                        );
                    }
                    
                    // 自動開火邏輯
                    if (isAiming && autoFire) {
                        static auto lastFireTime = std::chrono::steady_clock::now();
                        auto currentTime = std::chrono::steady_clock::now();
                        
                        // 檢查準心是否在任何目標框內
                        bool targetInSight = false;
                        cv::Point crosshair(frame.cols/2, frame.rows/2);
                        
                        for (const auto& target : targets) {
                            if (target.contains(crosshair)) {
                                targetInSight = true;
                                break;
                            }
                        }
                        
                        // 如果準心在目標框內且滿足冷卻時間，則開火
                        if (targetInSight && 
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                currentTime - lastFireTime).count() > 1) {
                            
                            control.mouseButtonClick(VK_LBUTTON);
                            lastFireTime = currentTime;
                            
                            if (DEBUG_MODE) {
                                Logger::log("Auto fire triggered - Target in crosshair");
                            }
                        }
                    }
                }
            }
            
            // 在顯示預覽之前，繪製FOV指示器
            if (showPreview) {
                // 計算FOV圓圈的半徑
                int fovRadius = static_cast<int>(frame.cols * fov / 2);
                cv::Point center(frame.cols / 2, frame.rows / 2);
                
                // 繪製十字準心
                cv::Scalar crosshairColor(0, 255, 255);
                cv::line(frame, 
                    cv::Point(center.x - 10, center.y), 
                    cv::Point(center.x + 10, center.y), 
                    crosshairColor, 1);
                cv::line(frame, 
                    cv::Point(center.x, center.y - 10), 
                    cv::Point(center.x, center.y + 10), 
                    crosshairColor, 1);
                
                // 繪製FOV圓圈
                cv::Scalar fovColor(0, 255, 255);
                cv::circle(frame, center, fovRadius, fovColor, 1);
                
                // 可選：添加FOV數值顯示
                std::string fovText = "FOV: " + std::to_string(static_cast<int>(fov * 100)) + "%";
                cv::putText(frame, fovText, 
                    cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 
                    1.0, cv::Scalar(0, 255, 255), 2);
                
                // 只更新內嵌預覽面板，不顯示獨立視窗
                gui.updatePreview(frame);
            }
            
            // 移除這部分代碼，因為我們不再需要獨立的OpenCV視窗
            // if (showPreview) {
            //     cv::imshow("Preview", frame);
            //     cv::waitKey(1);
            // }
            
            // 修改ES退出的處理
            if (cv::waitKey(1) == 27) {
                safeDestroyWindow("Capture");  // 使用新的安全關閉函數
                Logger::log("Program terminated by ESC key");
                Logger::close();
                ExitProcess(0);
            }
        }
    }
    catch (const cv::Exception& e) {
        Logger::log("OpenCV error: " + std::string(e.what()));
        MessageBoxA(NULL, e.what(), "OpenCV 錯誤", MB_OK | MB_ICONERROR);
        return -1;
    }
    catch (const std::exception& e) {
        Logger::log("Program error: " + std::string(e.what()));
        MessageBoxA(NULL, e.what(), "程式錯誤", MB_OK | MB_ICONERROR);
        return -1;
    }

    // 程序結束時關閉日誌
    Logger::log("Program terminated normally");
    Logger::close();
    cv::destroyAllWindows();
    return 0;
}