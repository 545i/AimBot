#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <random>
#include <sstream>


void safeDestroyWindow(const std::string& windowName);


std::string generateRandomPID();


class PlatformInterface {
public:
    virtual ~PlatformInterface() = default;
    virtual bool createMutex(const std::string& name) = 0;
    virtual void releaseMutex() = 0;
    virtual bool registerShutdownHandler(std::function<void()> handler) = 0;
    virtual bool isWindowValid(void* windowHandle) = 0;
    virtual void showMessage(const std::string& message, const std::string& title, bool isError = false) = 0;
    virtual void initializeSafely() = 0;
};


class ProcessCamouflage {
private:
    std::string generateLegitName();

public:
    void applyCamouflage();
    void adjustThreadAttributes();
};


class WindowsPlatform : public PlatformInterface {
private:
    HANDLE mutexHandle = nullptr;
    ProcessCamouflage camouflage;
    void adjustProcessAttributes();

public:
    bool createMutex(const std::string& name) override;
    void releaseMutex() override;
    bool isWindowValid(void* windowHandle) override;
    bool registerShutdownHandler(std::function<void()> handler) override;
    void showMessage(const std::string& message, const std::string& title, bool isError = false) override;
    void initializeSafely() override;
}; 