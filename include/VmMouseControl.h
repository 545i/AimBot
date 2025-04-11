#pragma once
#include <windows.h>
#include <string>
#include <stdexcept>
#include <QFile>
#include <QDir>
#include "Logger.h"

class VmMouseControl {
private:
    typedef void (*click_Left_downFunc)();
    typedef void (*click_Left_upFunc)();
    typedef void (*click_Right_downFunc)();
    typedef void (*click_Right_upFunc)();
    typedef void (*move_AbsFunc)(int x, int y);
    typedef void (*move_RFunc)(int dx, int dy);

    HMODULE hDLL;
    click_Left_downFunc click_Left_down;
    click_Left_upFunc click_Left_up;
    click_Right_downFunc click_Right_down;
    click_Right_upFunc click_Right_up;
    move_AbsFunc move_Abs;
    move_RFunc move_R;

    std::string extractDllToTemp() {
        try {
            QString tempPath = QDir::tempPath() + "/Vm_Mouse.dll";
            QFile::remove(tempPath); 
            
            QFile resourceFile(":/dll/DLL/Vm_Mouse.dll");
            if (!resourceFile.exists()) {
                Logger::log("Vm_Mouse.dll resource file does not exist in resources");
                throw std::runtime_error("Vm_Mouse.dll resource not found");
            }
            
            if (!resourceFile.open(QIODevice::ReadOnly)) {
                Logger::log("Failed to open Vm_Mouse.dll resource: " + resourceFile.errorString().toStdString());
                throw std::runtime_error("Failed to open Vm_Mouse.dll resource");
            }
            
            QFile tempFile(tempPath);
            if (!tempFile.open(QIODevice::WriteOnly)) {
                Logger::log("Failed to create temp Vm_Mouse.dll: " + tempFile.errorString().toStdString());
                throw std::runtime_error("Failed to create temporary Vm_Mouse.dll file");
            }
            
            QByteArray data = resourceFile.readAll();
            if (data.isEmpty()) {
                Logger::log("Vm_Mouse.dll resource is empty");
                throw std::runtime_error("Vm_Mouse.dll resource is empty");
            }
            
            if (tempFile.write(data) != data.size()) {
                Logger::log("Failed to write Vm_Mouse.dll data: " + tempFile.errorString().toStdString());
                throw std::runtime_error("Failed to write Vm_Mouse.dll data");
            }
            
            tempFile.close();
            resourceFile.close();
            
            return tempPath.toStdString();
        } catch (const std::exception& e) {
            Logger::log("Error in extractDllToTemp (Vm_Mouse): " + std::string(e.what()));
            throw;
        }
    }

    bool loadFunctions() {
        click_Left_down = reinterpret_cast<click_Left_downFunc>(GetProcAddress(hDLL, "click_Left_down"));
        click_Left_up = reinterpret_cast<click_Left_upFunc>(GetProcAddress(hDLL, "click_Left_up"));
        click_Right_down = reinterpret_cast<click_Right_downFunc>(GetProcAddress(hDLL, "click_Right_down"));
        click_Right_up = reinterpret_cast<click_Right_upFunc>(GetProcAddress(hDLL, "click_Right_up"));
        move_Abs = reinterpret_cast<move_AbsFunc>(GetProcAddress(hDLL, "move_Abs"));
        move_R = reinterpret_cast<move_RFunc>(GetProcAddress(hDLL, "move_R"));

        return click_Left_down && click_Left_up && click_Right_down && 
               click_Right_up && move_Abs && move_R;
    }

public:
    VmMouseControl() : hDLL(nullptr) {
        std::string dllPath = extractDllToTemp();
        
        hDLL = LoadLibraryA(dllPath.c_str());
        if (!hDLL) {
            DWORD error = GetLastError();
            std::string error_message = "Failed to load Vm_Mouse.dll. Error code: " + std::to_string(error);
            throw std::runtime_error(error_message);
        }

        if (!loadFunctions()) {
            std::string error_message = "Failed to load required functions from Vm_Mouse.dll";
            FreeLibrary(hDLL);
            throw std::runtime_error(error_message);
        }
    }

    ~VmMouseControl() {
        if (hDLL) {
            FreeLibrary(hDLL);
        }
    }

    bool moveMouseTo(int x, int y) {
        try {
            move_Abs(x, y);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool moveMouseRelative(int dx, int dy) {
        try {
            move_R(dx, dy);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool mouseButtonDown(unsigned char button) {
        try {
            if (button == 0) {
                click_Left_down();
            } else if (button == 1) {
                click_Right_down();
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    bool mouseButtonUp(unsigned char button) {
        try {
            if (button == 0) {
                click_Left_up();
            } else if (button == 1) {
                click_Right_up();
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    bool mouseButtonClick(unsigned char button) {
        try {
            if (button == 0) {
                click_Left_down();
                Sleep(10);
                click_Left_up();
            } else if (button == 1) {
                click_Right_down();
                Sleep(10);
                click_Right_up();
            }
            return true;
        } catch (...) {
            return false;
        }
    }
}; 