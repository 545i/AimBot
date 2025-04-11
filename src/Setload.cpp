#include "SetLoad.h"
#include "Logger.h"
#include <QTimer>
#include <QButtonGroup>
#include <QAbstractButton>

void SetLoad::saveSettings(
    float* sensitivity,
    float* headOffset,
    bool* autoFire,
    bool* autoAim,
    float* fov,
    bool* mouseLock,
    int triggerKeySelection1,
    int triggerKeySelection2,
    float confidence_threshold,
    int* fireMode,
    QButtonGroup* aimPartBtnGroup,
    bool globeFireLock,
    float* tsensitivity
) {
    std::wstring settingsPath = getAppPath() + L"settings.ini";
    wchar_t buffer[32];
    
    _swprintf_p(buffer, 32, L"%.3f", *sensitivity);
    WritePrivateProfileStringW(L"Settings", L"Sensitivity", buffer, settingsPath.c_str());
    
    _swprintf_p(buffer, 32, L"%.3f", *headOffset);
    WritePrivateProfileStringW(L"Settings", L"HeadOffset", buffer, settingsPath.c_str());
    
    WritePrivateProfileStringW(L"Settings", L"AutoFire", 
        (globeFireLock && *autoFire) ? L"1" : L"0", settingsPath.c_str());
    
    WritePrivateProfileStringW(L"Settings", L"AutoAim", 
        *autoAim ? L"1" : L"0", settingsPath.c_str());
    Logger::logGui("Auto aim setting saved: " + std::string(*autoAim ? "enabled" : "disabled"));
    
    _swprintf_p(buffer, 32, L"%.3f", *fov);
    WritePrivateProfileStringW(L"Settings", L"FOV", buffer, settingsPath.c_str());
    
    _swprintf_p(buffer, 32, L"%d", triggerKeySelection1);
    WritePrivateProfileStringW(L"Settings", L"TriggerKey1", buffer, settingsPath.c_str());
    
    _swprintf_p(buffer, 32, L"%d", triggerKeySelection2);
    WritePrivateProfileStringW(L"Settings", L"TriggerKey2", buffer, settingsPath.c_str());
    
    _swprintf_p(buffer, 32, L"%.3f", confidence_threshold);
    WritePrivateProfileStringW(L"Settings", L"Confidence", buffer, settingsPath.c_str());
    
    _swprintf_p(buffer, 32, L"%d", globeFireLock ? *fireMode : 0);
    WritePrivateProfileStringW(L"Settings", L"FireMode", buffer, settingsPath.c_str());
    
    if (aimPartBtnGroup && aimPartBtnGroup->checkedButton()) {
        _swprintf_p(buffer, 32, L"%d", aimPartBtnGroup->checkedId());
        WritePrivateProfileStringW(L"Settings", L"AimPart", buffer, settingsPath.c_str());
    }

    WritePrivateProfileStringW(L"Settings", L"MouseLock", 
        *mouseLock ? L"1" : L"0", settingsPath.c_str());
    
    _swprintf_p(buffer, 32, L"%.3f", *tsensitivity);
    WritePrivateProfileStringW(L"Settings", L"TSensitivity", buffer, settingsPath.c_str());
    
    Logger::log("成功保存所有參數到 settings.ini");
}

void SetLoad::loadSettings(
    float* sensitivity,
    float* headOffset,
    bool* autoFire,
    bool* autoAim,
    float* fov,
    bool* mouseLock,
    int& triggerKeySelection1,
    int& triggerKeySelection2,
    float& confidence_threshold,
    int* fireMode,
    int* fireDelay,
    QButtonGroup* aimPartBtnGroup,
    bool globeFireLock,
    float* tsensitivity
) {
    std::wstring settingsPath = getAppPath() + L"settings.ini";
    wchar_t buffer[32];
    
    GetPrivateProfileStringW(L"Settings", L"Sensitivity", L"0.65", 
        buffer, 32, settingsPath.c_str());
    *sensitivity = wcstof(buffer, nullptr);
    if (*sensitivity < 0.0f || *sensitivity > 2.0f) *sensitivity = 0.65f;
    
    GetPrivateProfileStringW(L"Settings", L"HeadOffset", L"0", 
        buffer, 32, settingsPath.c_str());
    *headOffset = wcstof(buffer, nullptr);
    if (*headOffset != 0 && *headOffset != 1) *headOffset = 0;
    
    GetPrivateProfileStringW(L"Settings", L"AutoFire", L"1", 
        buffer, 32, settingsPath.c_str());
    *autoFire = globeFireLock ? (wcstol(buffer, nullptr, 10) != 0) : false;
    
    GetPrivateProfileStringW(L"Settings", L"AutoAim", L"1", 
        buffer, 32, settingsPath.c_str());
    *autoAim = (wcstol(buffer, nullptr, 10) != 0);
    
    GetPrivateProfileStringW(L"Settings", L"FOV", L"0.30", 
        buffer, 32, settingsPath.c_str());
    *fov = wcstof(buffer, nullptr);
    if (*fov <= 0.1f || *fov > 1.0f) *fov = 0.30f;
    
    GetPrivateProfileStringW(L"Settings", L"TriggerKey1", L"0", 
        buffer, 32, settingsPath.c_str());
    triggerKeySelection1 = wcstol(buffer, nullptr, 10);
    if (triggerKeySelection1 < 0 || triggerKeySelection1 > 3) {
        triggerKeySelection1 = 0;
    }
    
    GetPrivateProfileStringW(L"Settings", L"TriggerKey2", L"0", 
        buffer, 32, settingsPath.c_str());
    triggerKeySelection2 = wcstol(buffer, nullptr, 10);
    if (triggerKeySelection2 < 0 || triggerKeySelection2 > 4) {
        triggerKeySelection2 = 0;
    }
    
    GetPrivateProfileStringW(L"Settings", L"Confidence", L"0.50", 
        buffer, 32, settingsPath.c_str());
    confidence_threshold = wcstof(buffer, nullptr);
    if (confidence_threshold <= 0.01f || confidence_threshold > 1.0f) {
        confidence_threshold = 0.50f;
    }
    
    GetPrivateProfileStringW(L"Settings", L"FireMode", L"0", 
        buffer, 32, settingsPath.c_str());
    *fireMode = globeFireLock ? wcstol(buffer, nullptr, 10) : 0;
    *fireDelay = (*fireMode == 0) ? 20 : 150;
    
    GetPrivateProfileStringW(L"Settings", L"AimPart", L"0", 
        buffer, 32, settingsPath.c_str());
    int aimPart = wcstol(buffer, nullptr, 10);
    if (aimPart < 0 || aimPart > 2) {
        aimPart = 0;
    }
    
    if (aimPartBtnGroup) {
        QTimer::singleShot(0, [aimPartBtnGroup, aimPart]() {
            QAbstractButton* button = aimPartBtnGroup->button(aimPart);
            if (button) {
                button->setChecked(true);
            }
        });
    }

    GetPrivateProfileStringW(L"Settings", L"MouseLock", L"0", 
        buffer, 32, settingsPath.c_str());
    *mouseLock = (wcstol(buffer, nullptr, 10) != 0);
    
    GetPrivateProfileStringW(L"Settings", L"TSensitivity", L"1.3", 
        buffer, 32, settingsPath.c_str());
    *tsensitivity = wcstof(buffer, nullptr);
    if (*tsensitivity < 1.0f || *tsensitivity > 2.0f) *tsensitivity = 1.3f;

    Logger::logGui("Settings loaded from: " + std::string(settingsPath.begin(), settingsPath.end()));
}

std::wstring SetLoad::getAppPath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring fullPath(path);
    return fullPath.substr(0, fullPath.find_last_of(L"\\/") + 1);
}

SetLoad::Parameters SetLoad::loadAllParameters() {
    Parameters params;
    std::wstring settingsPath = getAppPath() + L"settings.ini";
    
    try {
        params.sensitivity = readFloatValue(L"Settings", L"Sensitivity", 0.65f);
        params.headOffset = readFloatValue(L"Settings", L"HeadOffset", 0.0f);
        params.fov = readFloatValue(L"Settings", L"FOV", 0.30f);
        params.tsensitivity = readFloatValue(L"Settings", L"TSensitivity", 1.3f);
        params.autoFire = readBoolValue(L"Settings", L"AutoFire", false);
        params.autoAim = readBoolValue(L"Settings", L"AutoAim", true);
        params.mouseLock = readBoolValue(L"Settings", L"MouseLock", false);
        params.triggerKeySelection1 = readIntValue(L"Settings", L"TriggerKey1", 0);
        params.triggerKeySelection2 = readIntValue(L"Settings", L"TriggerKey2", 0);
        params.fireMode = readIntValue(L"Settings", L"FireMode", 0);
        params.confidence_threshold = readFloatValue(L"Settings", L"Confidence", 0.50f);
    }
    catch (const std::exception& e) {
        Logger::log("讀取設置時發生錯誤：" + std::string(e.what()));
    }
    
    return params;
}

float SetLoad::readFloatValue(const wchar_t* section, const wchar_t* key, float defaultValue) {
    std::wstring settingsPath = getAppPath() + L"settings.ini";
    wchar_t buffer[32];
    GetPrivateProfileStringW(section, key, std::to_wstring(defaultValue).c_str(), 
        buffer, 32, settingsPath.c_str());
    return wcstof(buffer, nullptr);
}

bool SetLoad::readBoolValue(const wchar_t* section, const wchar_t* key, bool defaultValue) {
    std::wstring settingsPath = getAppPath() + L"settings.ini";
    return GetPrivateProfileIntW(section, key, defaultValue ? 1 : 0, 
        settingsPath.c_str()) != 0;
}

int SetLoad::readIntValue(const wchar_t* section, const wchar_t* key, int defaultValue) {
    std::wstring settingsPath = getAppPath() + L"settings.ini";
    return GetPrivateProfileIntW(section, key, defaultValue, settingsPath.c_str());
}

std::vector<int> SetLoad::getTriggerKeys(int triggerKeySelection1, int triggerKeySelection2) {
    std::vector<int> keys;

    switch (triggerKeySelection1) {
        case 1: 
            keys.push_back(VK_RBUTTON);
            break;
        case 2:
            keys.push_back(VK_XBUTTON1);
            break;
        case 3: 
            keys.push_back(VK_XBUTTON2);
            break;
        default:
            keys.push_back(VK_LBUTTON);
            break;
    }

    if (triggerKeySelection2 > 0) {
        switch (triggerKeySelection2) {
            case 1:
                keys.push_back(VK_LBUTTON);
                break;
            case 2:
                keys.push_back(VK_RBUTTON);
                break;
            case 3: 
                keys.push_back(VK_XBUTTON1);
                break;
            case 4:
                keys.push_back(VK_XBUTTON2);
                break;
        }
    }

    return keys;
}

int SetLoad::getAimPart() {
    return readIntValue(L"Settings", L"AimPart", 0);
}
