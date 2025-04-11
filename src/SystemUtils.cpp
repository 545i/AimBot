#include "SystemUtils.h"
#include "Logger.h"
#include <opencv2/opencv.hpp>

void safeDestroyWindow(const std::string& windowName) {
    try {
        if (cv::getWindowProperty(windowName, cv::WND_PROP_VISIBLE) >= 0) {
            cv::destroyWindow(windowName);
            cv::waitKey(1);
        }
    }
    catch (const cv::Exception& e) {
        Logger::log("警告: 關閉視窗時發生異常: " + std::string(e.what()));
    }
}

std::string generateRandomPID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    std::stringstream ss;
    ss << "PID_" << dis(gen);
    return ss.str();
}

std::string ProcessCamouflage::generateLegitName() {
    const std::vector<std::string> legitNames = {
        "RuntimeBroker",
        "svchost",
        "dllhost",
        "csrss",
        "WmiPrvSE"
    };
    return legitNames[rand() % legitNames.size()] + 
           "_" + std::to_string(GetTickCount64() % 1000);
}

void ProcessCamouflage::applyCamouflage() {
    try {
        std::string legitName = generateLegitName();
        SetConsoleTitleA(legitName.c_str());
        
        HWND hwnd = GetConsoleWindow();
        if (hwnd) {
            ShowWindow(hwnd, SW_HIDE);
        }

        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    } catch (...) {
    }
}

void ProcessCamouflage::adjustThreadAttributes() {
    SetThreadDescription(
        GetCurrentThread(),
        L"Windows.UI.Core.CoreWindow"
    );
}

void WindowsPlatform::adjustProcessAttributes() {
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
}

bool WindowsPlatform::createMutex(const std::string& name) {
    std::wstring wideName = L"Global\\" + std::wstring(name.begin(), name.end());
    mutexHandle = CreateMutexW(NULL, TRUE, wideName.c_str());
    return (GetLastError() != ERROR_ALREADY_EXISTS);
}

void WindowsPlatform::releaseMutex() {
    if (mutexHandle) {
        ReleaseMutex(mutexHandle);
        CloseHandle(mutexHandle);
        mutexHandle = nullptr;
    }
}

bool WindowsPlatform::isWindowValid(void* windowHandle) {
    if (!windowHandle) return false;
    return IsWindow(reinterpret_cast<HWND>(windowHandle));
}

bool WindowsPlatform::registerShutdownHandler(std::function<void()> handler) {
    return SetConsoleCtrlHandler([](DWORD ctrlType) -> BOOL {
        if (ctrlType == CTRL_CLOSE_EVENT || 
            ctrlType == CTRL_SHUTDOWN_EVENT || 
            ctrlType == CTRL_LOGOFF_EVENT) {
            return TRUE;
        }
        return FALSE;
    }, TRUE);
}

void WindowsPlatform::showMessage(const std::string& message, const std::string& title, bool isError) {
    int type = isError ? MB_ICONERROR : MB_ICONINFORMATION;
    std::wstring wMessage(message.begin(), message.end());
    std::wstring wTitle(title.begin(), title.end());
    MessageBoxW(NULL, wMessage.c_str(), wTitle.c_str(), MB_OK | type);
}

void WindowsPlatform::initializeSafely() {
    try {
        camouflage.applyCamouflage();
        camouflage.adjustThreadAttributes();
        adjustProcessAttributes();
    } catch (...) {
    }
} 