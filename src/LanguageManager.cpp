#include "LanguageManager.h"
#include "Logger.h"
#include <map>
#include <stdexcept>

LanguageManager::LanguageManager() {
    try {
        LANGID langID = GetUserDefaultUILanguage();
        m_isEnglish = (PRIMARYLANGID(langID) == LANG_ENGLISH);
        m_currentLanguage = m_isEnglish ? "en" : "zh";
        Logger::log("Language Manager initialized: " + m_currentLanguage);
    } catch (const std::exception& e) {
        Logger::log("Failed to initialize Language Manager: " + std::string(e.what()));
        m_isEnglish = false;
        m_currentLanguage = "zh";
    }
}

bool LanguageManager::isEnglishSystem() const {
    return m_isEnglish;
}

std::string LanguageManager::getText(const std::string& key) const {
    try {
        static const std::map<std::string, std::map<std::string, std::string>> texts = {
            {"INIT_FAILED", {
                {"zh", "系統初始化失敗: "},
                {"en", "System initialization failed: "}
            }},
            {"CHECK_DEVICE", {
                {"zh", "請檢查設備連接"},
                {"en", "Please check device connection"}
            }},
            {"ERROR", {
                {"zh", "錯誤"},
                {"en", "Error"}
            }},
            {"ALREADY_RUNNING", {
                {"zh", "程序已在運行中!"},
                {"en", "Application is already running!"}
            }},
            {"PROGRAM_CLOSED", {
                {"zh", "程序因系統事件關閉"},
                {"en", "Program closed due to system event"}
            }},
            {"GUI_CLOSED", {
                {"zh", "GUI視窗已關閉，正在關閉..."},
                {"en", "GUI window closed, shutting down..."}
            }},
            {"USER_LOGGED_IN", {
                {"zh", "用戶已登入，正在載入設定..."},
                {"en", "User logged in, loading settings..."}
            }},
            {"SETTINGS_LOADED", {
                {"zh", "設定已載入，功能已開啟"},
                {"en", "Settings loaded, features enabled"}
            }},
            {"USER_LOGGED_OUT", {
                {"zh", "用戶已登出，正在禁用功能"},
                {"en", "User logged out, disabling features"}
            }},
            {"DETECTION_ERROR", {
                {"zh", "檢測或圖錯誤: "},
                {"en", "Detection or image error: "}
            }},
            {"BLANK_FRAME", {
                {"zh", "檢測到空白幀，嘗試重載截圖系統"},
                {"en", "Blank frame detected, attempting to reload capture system"}
            }},
            {"RELOAD_SUCCESS", {
                {"zh", "自動重載成功"},
                {"en", "Auto reload successful"}
            }},
            {"RELOAD_FAILED", {
                {"zh", "自動重載失敗"},
                {"en", "Auto reload failed"}
            }},
            {"CLEANUP_COMPLETE", {
                {"zh", "程序清理完成"},
                {"en", "Program cleanup complete"}
            }},
            {"PROGRAM_START", {
                {"zh", "程序起動"},
                {"en", "Program started"}
            }},
            {"CAPTURE_POINTER_SET", {
                {"zh", "已將 capture 指針設置到 GUI"},
                {"en", "Capture pointer set to GUI"}
            }},
            {"MOUSE_CONTROL_INIT_SUCCESS", {
                {"zh", "控制器初始化成功"},
                {"en", "Mouse control initialized successfully"}
            }},
            {"MOUSE_CONTROL_INIT_FAILED", {
                {"zh", "初始化控制器失敗，嘗試使用Vm_Mouse"},
                {"en", "Mouse control initialization failed, trying Vm_Mouse"}
            }},
            {"VM_MOUSE_INIT_SUCCESS", {
                {"zh", "Vm_Mouse 初始化成功"},
                {"en", "Vm_Mouse initialized successfully"}
            }},
            {"CAPTURE_SYSTEM_INIT_SUCCESS", {
                {"zh", "成功初始化截圖系統"},
                {"en", "Capture system initialized successfully"}
            }},
            {"CAPTURE_SYSTEM_INIT_FAILED", {
                {"zh", "無法創建截圖系統"},
                {"en", "Failed to create capture system"}
            }},
            {"CROSSHAIR_FOUND", {
                {"zh", "Crosshair: Found"},
                {"en", "Crosshair: Found"}
            }},
            {"CROSSHAIR_NOT_FOUND", {
                {"zh", "Crosshair: Not Found"},
                {"en", "Crosshair: Not Found"}
            }},
            {"CROSSHAIR_PLUS_ENABLED", {
                {"zh", "CrosshairPlus: Enabled"},
                {"en", "CrosshairPlus: Enabled"}
            }},
            {"CROSSHAIR_PLUS_DISABLED", {
                {"zh", "CrosshairPlus: Disabled"},
                {"en", "CrosshairPlus: Disabled"}
            }},
            {"TRACKING_LOCKED", {
                {"zh", "Tracking: Locked"},
                {"en", "Tracking: Locked"}
            }},
            {"TRACKING_NONE", {
                {"zh", "Tracking: None"},
                {"en", "Tracking: None"}
            }},
            {"FOV_ZOOMED", {
                {"zh", " (Zoomed)"},
                {"en", " (Zoomed)"}
            }},
            {"DLL_RESOURCE_ERROR", {
                {"zh", "無法打開DLL資源"},
                {"en", "Failed to open DLL resource"}
            }},
            {"STRING_TOO_LONG", {
                {"zh", "字符串過長"},
                {"en", "String too long"}
            }},
            {"SETTINGS_UPDATED", {
                {"zh", "已更新所有截圖系統設定"},
                {"en", "All capture system settings updated"}
            }},
            {"SETTINGS_LOADED", {
                {"zh", "設定已載入，功能已開啟"},
                {"en", "Settings loaded, features enabled"}
            }}
        };

        auto it = texts.find(key);
        if (it != texts.end()) {
            auto langIt = it->second.find(m_currentLanguage);
            if (langIt != it->second.end()) {
                return langIt->second;
            }
        }
        Logger::log("Warning: Text key not found: " + key);
        return key;
    } catch (const std::exception& e) {
        Logger::log("Error in getText: " + std::string(e.what()));
        return key;
    }
} 