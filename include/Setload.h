#pragma once
#include <windows.h>
#include <string>
#include <QButtonGroup>
#include <vector>

class SetLoad {
public:
    
    struct Parameters {
        float sensitivity = 0.65f;
        float headOffset = 0.0f;
        float fov = 0.30f;
        float tsensitivity = 1.3f;
        bool autoFire = false;
        bool autoAim = true;
        bool mouseLock = false;
        int triggerKeySelection1 = 0;
        int triggerKeySelection2 = 0;
        int fireMode = 0;
        float confidence_threshold = 0.50f;
    };

    
    static Parameters loadAllParameters();

    
    static void saveSettings(
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
    );

    static void loadSettings(
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
    );

    static std::vector<int> getTriggerKeys(int selection1, int selection2);

    static int getAimPart();

    static int getFireMode() {
        return readIntValue(L"Settings", L"FireMode", 0);
    }
    static void setFireMode(int mode) {
        std::wstring settingsPath = getAppPath() + L"settings.ini";
        wchar_t buffer[32];
        _swprintf_p(buffer, 32, L"%d", mode);
        WritePrivateProfileStringW(L"Settings", L"FireMode", buffer, settingsPath.c_str());
    }

    static int getFireDelay() {
        int fireMode = getFireMode();
        return (fireMode == 0) ? 20 : 150; 
    }

private:
    static std::wstring getAppPath();
    static float readFloatValue(const wchar_t* section, const wchar_t* key, float defaultValue);
    static bool readBoolValue(const wchar_t* section, const wchar_t* key, bool defaultValue);
    static int readIntValue(const wchar_t* section, const wchar_t* key, int defaultValue);
};