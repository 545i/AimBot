#pragma once
#include <windows.h>
#include <string>
#include <stdexcept>
#include <iostream>
#include <cstdint>
#include <QFile>
#include <QDir>
#include <QString>
#include "Logger.h"

class KeyboardMouseControl {
private:
    typedef int (*OpenDeviceFunc)();
    typedef void (*CloseFunc)();
    typedef int (*MoveToFunc)(unsigned short x, unsigned short y);
    typedef int (*MoveRFunc)(short dx, short dy);
    typedef int (*MoveRDFunc)(short dx, short dy, unsigned char delay, unsigned char delta);
    typedef void (*KeyDownNameFunc)(const char* keyname);
    typedef void (*KeyUpNameFunc)(const char* keyname);
    typedef void (*KeyPressNameFunc)(const char* keyname, int min, int max);
    typedef int (*DllGetKeyStateFunc)(int vk);
    typedef int (*MouseButtonDownFunc)(unsigned char index);
    typedef int (*MouseButtonUpFunc)(unsigned char index);
    typedef int (*MouseButtonClickFunc)(unsigned char index, int min, int max);
    typedef void (*RebootFunc)();
    typedef void (*KeyAllupFunc)();
    typedef void (*SetEncryptFunc)(unsigned char* data);
    typedef long (*GetChipIDFunc)();
    typedef int (*GetVersionFunc)();
    typedef int (*GetModelFunc)();
    typedef void (*SetWaitResponFunc)(int wait);
    typedef int (*IsOpenFunc)();
    typedef int (*OpenDeviceByIDFunc)(int id1, int id2);
    typedef void (*CombineMoveRFunc)(unsigned char interval);

    HMODULE hDLL;
    OpenDeviceFunc OpenDevice;
    CloseFunc Close;
    MoveToFunc MoveTo;
    MoveRFunc MoveR;
    MoveRDFunc MoveRD;
    KeyDownNameFunc KeyDownName;
    KeyUpNameFunc KeyUpName;
    KeyPressNameFunc KeyPressName;
    DllGetKeyStateFunc DllGetKeyState;
    MouseButtonDownFunc MouseButtonDown;
    MouseButtonUpFunc MouseButtonUp;
    MouseButtonClickFunc MouseButtonClick;
    RebootFunc Reboot;
    KeyAllupFunc KeyAllup;
    SetEncryptFunc SetEncrypt;
    GetChipIDFunc GetChipID;
    GetVersionFunc GetVersion;
    GetModelFunc GetModel;
    SetWaitResponFunc SetWaitRespon;
    IsOpenFunc IsOpen;
    OpenDeviceByIDFunc OpenDeviceByID;
    CombineMoveRFunc CombineMoveR;

    std::string extractDllToTemp() {
        try {
            QString tempPath = QDir::tempPath() + "/ddll64.dll";
            QFile::remove(tempPath); 
            
            QFile resourceFile(":/dll/DLL/ddll64.dll");
            if (!resourceFile.exists()) {
                Logger::log("DLL resource file does not exist in resources");
                throw std::runtime_error("DLL resource not found");
            }
            
            if (!resourceFile.open(QIODevice::ReadOnly)) {
                Logger::log("Failed to open DLL resource: " + resourceFile.errorString().toStdString());
                throw std::runtime_error("Failed to open DLL resource");
            }
            
            QFile tempFile(tempPath);
            if (!tempFile.open(QIODevice::WriteOnly)) {
                Logger::log("Failed to create temp DLL: " + tempFile.errorString().toStdString());
                throw std::runtime_error("Failed to create temp DLL");
            }
            
            QByteArray data = resourceFile.readAll();
            if (data.isEmpty()) {
                Logger::log("DLL resource is empty");
                throw std::runtime_error("DLL resource is empty");
            }
            
            if (tempFile.write(data) != data.size()) {
                Logger::log("Failed to write DLL data: " + tempFile.errorString().toStdString());
                throw std::runtime_error("Failed to write DLL data");
            }
            
            tempFile.close();
            resourceFile.close();
            
            return tempPath.toStdString();
        } catch (const std::exception& e) {
            Logger::log("Error in extractDllToTemp: " + std::string(e.what()));
            throw;
        }
    }

    bool loadFunctions() {
        OpenDevice = reinterpret_cast<OpenDeviceFunc>(GetProcAddress(hDLL, "OpenDevice"));
        Close = reinterpret_cast<CloseFunc>(GetProcAddress(hDLL, "Close"));
        MoveTo = reinterpret_cast<MoveToFunc>(GetProcAddress(hDLL, "MoveTo"));
        MoveR = reinterpret_cast<MoveRFunc>(GetProcAddress(hDLL, "MoveR"));
        MoveRD = reinterpret_cast<MoveRDFunc>(GetProcAddress(hDLL, "MoveRD"));
        KeyDownName = reinterpret_cast<KeyDownNameFunc>(GetProcAddress(hDLL, "KeyDownName"));
        KeyUpName = reinterpret_cast<KeyUpNameFunc>(GetProcAddress(hDLL, "KeyUpName")); 
        KeyPressName = reinterpret_cast<KeyPressNameFunc>(GetProcAddress(hDLL, "KeyPressName"));
        DllGetKeyState = reinterpret_cast<DllGetKeyStateFunc>(GetProcAddress(hDLL, "DllGetKeyState"));
        MouseButtonDown = reinterpret_cast<MouseButtonDownFunc>(GetProcAddress(hDLL, "MouseButtonDown"));
        MouseButtonUp = reinterpret_cast<MouseButtonUpFunc>(GetProcAddress(hDLL, "MouseButtonUp"));
        MouseButtonClick = reinterpret_cast<MouseButtonClickFunc>(GetProcAddress(hDLL, "MouseButtonClick"));
        Reboot = reinterpret_cast<RebootFunc>(GetProcAddress(hDLL, "Reboot"));
        KeyAllup = reinterpret_cast<KeyAllupFunc>(GetProcAddress(hDLL, "KeyAllup"));
        SetEncrypt = reinterpret_cast<SetEncryptFunc>(GetProcAddress(hDLL, "SetEncrypt"));
        GetChipID = reinterpret_cast<GetChipIDFunc>(GetProcAddress(hDLL, "GetChipID"));
        GetVersion = reinterpret_cast<GetVersionFunc>(GetProcAddress(hDLL, "GetVersion"));
        GetModel = reinterpret_cast<GetModelFunc>(GetProcAddress(hDLL, "GetModel"));
        SetWaitRespon = reinterpret_cast<SetWaitResponFunc>(GetProcAddress(hDLL, "SetWaitRespon"));
        IsOpen = reinterpret_cast<IsOpenFunc>(GetProcAddress(hDLL, "IsOpen"));
        OpenDeviceByID = reinterpret_cast<OpenDeviceByIDFunc>(GetProcAddress(hDLL, "OpenDeviceByID"));
        CombineMoveR = reinterpret_cast<CombineMoveRFunc>(GetProcAddress(hDLL, "CombineMoveR"));

        return OpenDevice && Close && MoveTo && MoveR && DllGetKeyState && CombineMoveR;
    }

public:
    KeyboardMouseControl() : hDLL(nullptr) {
        std::string dllPath = extractDllToTemp();
        
        hDLL = LoadLibraryA(dllPath.c_str());
        if (!hDLL) {
            DWORD error = GetLastError();
            std::string error_message = "Failed to load ddll64.dll. Error code: " + std::to_string(error);
            throw std::runtime_error(error_message);
        }

        if (!loadFunctions()) {
            std::string error_message = "Failed to load required functions from ddll64.dll";
            FreeLibrary(hDLL);
            throw std::runtime_error(error_message);
        }
    }

    ~KeyboardMouseControl() {
        if (hDLL) {
            Close();
            FreeLibrary(hDLL);
        }
    }

    bool initialize(int id1 = 0, int id2 = 0) {
        if (!IsOpen()) {
            return OpenDeviceByID(id1, id2) == 1;
        }
        return true;
    }

    void shutdown() {
        if (IsOpen()) {
            Close();
        }
    }

    bool isReady() const {
        return IsOpen() == 1;
    }

    bool moveMouseTo(unsigned short x, unsigned short y) {
        return MoveTo(x, y) == 1;
    }

    bool moveMouseRelative(short dx, short dy) {
        return MoveR(dx, dy) == 1;
    }

    bool moveMouseSmoothly(short dx, short dy, unsigned char delay = 2, unsigned char delta = 5) {
        return MoveRD(dx, dy, delay, delta) == 1;
    }

    bool mouseButtonDown(unsigned char button) {
        return MouseButtonDown(button) == 1;
    }

    bool mouseButtonUp(unsigned char button) {
        return MouseButtonUp(button) == 1;
    }

    bool mouseButtonClick(unsigned char button, int minDelay = 0, int maxDelay = 0) {
        return MouseButtonClick(button, minDelay, maxDelay) == 1;
    }

    void keyDown(const std::string& keyName) {
        KeyDownName(keyName.c_str());
    }

    void keyUp(const std::string& keyName) {
        KeyUpName(keyName.c_str());
    }

    void keyPress(const std::string& keyName, int minDelay = 0, int maxDelay = 0) {
        KeyPressName(keyName.c_str(), minDelay, maxDelay);
    }

    int getKeyState(int virtualKeyCode) {
        return DllGetKeyState(virtualKeyCode);
    }

    long getChipID() {
        if (!IsOpen() || !GetChipID) {
            return -1;
        }
        try {
            return GetChipID();
        } catch (...) {
            return -1;
        }
    }

    int getVersion() {
        if (!IsOpen() || !GetVersion) {
            return -1;
        }
        try {
            return GetVersion();
        } catch (...) {
            return -1;
        }
    }

    int getModel() {
        if (!IsOpen() || !GetModel) {
            return -1;
        }
        try {
            return GetModel();
        } catch (...) {
            return -1;
        }
    }

    void reboot() {
        Reboot();
    }

    void releaseAllKeys() {
        KeyAllup();
    }

    void setEncryption(unsigned char* data) {
        SetEncrypt(data);
    }

    void setWaitResponse(bool wait) {
        SetWaitRespon(wait ? 1 : 0);
    }

    bool Lock_KeyBoard(unsigned char option) {
        if (!hDLL) return false;
        
        typedef int (*Lock_KeyBoardFunc)(unsigned char);
        Lock_KeyBoardFunc lockKeyBoard = reinterpret_cast<Lock_KeyBoardFunc>(
            GetProcAddress(hDLL, "Lock_KeyBoard")
        );
        
        if (!lockKeyBoard) return false;
        
        return lockKeyBoard(option) == 1;
    }

    bool KeyUpVirtualCode(uint8_t keycode) {
        if (!hDLL) return false;
        
        typedef int (*KeyUpVirtualCodeFunc)(uint8_t);
        KeyUpVirtualCodeFunc keyUpVirtualCode = reinterpret_cast<KeyUpVirtualCodeFunc>(
            GetProcAddress(hDLL, "KeyUpVirtualCode")
        );
        
        if (!keyUpVirtualCode) return false;
        
        return keyUpVirtualCode(keycode) == 1;
    }

    bool Lock_Mouse(unsigned char option) {
        if (!hDLL) return false;
        
        typedef int (*Lock_MouseFunc)(unsigned char);
        Lock_MouseFunc lockMouse = reinterpret_cast<Lock_MouseFunc>(
            GetProcAddress(hDLL, "Lock_Mouse")
        );
        
        if (!lockMouse) return false;
        
        return lockMouse(option) == 1;
    }

    bool setCombineMoveR(unsigned char interval) {
        if (!hDLL || !CombineMoveR) return false;
        try {
            CombineMoveR(interval);
            return true;
        } catch (...) {
            return false;
        }
    }
};