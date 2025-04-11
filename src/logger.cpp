#include "Logger.h"
#include <iostream>
#include "Setload.h"
#ifdef _WIN32
#include <windows.h>
#define WHITE    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE
#define YELLOW   FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define GREEN    FOREGROUND_GREEN | FOREGROUND_INTENSITY
#endif
#define _CRT_SECURE_NO_WARNINGS

bool Logger::initialized = false;
bool Logger::showTerminal = false;
bool Logger::logToFile = false;
FILE* Logger::terminalWindow = nullptr;
std::ofstream Logger::logFile;
std::thread Logger::commandThread;

void Logger::init() {
    if (!initialized) {
        extern bool g_useGUI;
        if (!g_useGUI) {
            showTerminal = true;
        }

        #ifdef _WIN32
        if (showTerminal) {
            AllocConsole();
            terminalWindow = nullptr;
            
            SetConsoleCP(CP_UTF8);
            SetConsoleOutputCP(CP_UTF8);
            
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
            HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
            
            DWORD dwMode = 0;
            GetConsoleMode(hOut, &dwMode);
            dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
            
            GetConsoleMode(hErr, &dwMode);
            dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hErr, dwMode);
            
            _wfreopen_s(&terminalWindow, L"CONOUT$", L"w", stdout);
            _wfreopen_s(&terminalWindow, L"CONIN$", L"r", stdin);
            
            setlocale(LC_ALL, ".UTF8");
            std::locale::global(std::locale(".UTF8"));
        }
        #endif
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        char timeBuffer[26];
        ctime_s(timeBuffer, sizeof(timeBuffer), &time);
        std::string timeStr = timeBuffer;
        timeStr = timeStr.substr(11, 8);
        
        std::cout << "\n=== 新的會話開始於 " 
                  << timeStr
                  << " ===\n";
        
        if (logToFile) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::string filename = "logs/log_" + std::to_string(time) + ".txt";
            logFile.open(filename, std::ios::app);
            
            if (logFile.is_open()) {
                logFile << "\n=== 新的會話開始於 " 
                       << timeStr
                       << " ===\n";
            }
        }
        
        if (showTerminal) {
            commandThread = std::thread([&]() {
                std::string command;
                while (initialized) {
                    std::getline(std::cin, command);
                    if (!command.empty()) {
                        processCommand(command);
                    }
                }
            });
            commandThread.detach();
        }
        
        initialized = true;
    }
}

void Logger::log(const std::string& message) {
    if (!showTerminal && !logToFile) {
        return;
    }

    if (initialized) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        char timeBuffer[26];
        ctime_s(timeBuffer, sizeof(timeBuffer), &time);
        std::string timeStr = timeBuffer;
        timeStr = timeStr.substr(11, 8);
        
        if (showTerminal) {
            std::cout << timeStr << " - " << message << std::endl;
        }
        
        if (logToFile && logFile.is_open()) {
            logFile << timeStr << " - " << message << std::endl;
            logFile.flush();
        }
    }
}

void Logger::close() {
    if (initialized) {
        #ifdef _WIN32
        if (terminalWindow) {
            fclose(terminalWindow);
            FreeConsole();
        }
        #endif
        
        if (logFile.is_open()) {
            logFile.close();
        }
        
        initialized = false;
        if (commandThread.joinable()) {
            commandThread.join();
        }
    }
}

void Logger::setShowTerminal(bool show) {
    showTerminal = show;
    
    if (!show && initialized) {
        #ifdef _WIN32
        if (terminalWindow) {
            fclose(terminalWindow);
            FreeConsole();
            terminalWindow = nullptr;
        }
        #endif
    } else if (show && initialized) {
        #ifdef _WIN32
        AllocConsole();
        _wfreopen_s(&terminalWindow, L"CONOUT$", L"w", stdout);
        
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
        
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        GetConsoleMode(hOut, &dwMode);
        dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
        #endif
    }
}

void Logger::logGui(const std::string& message) {
    if (initialized && showTerminal) {
        #ifdef _WIN32
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), WHITE);
        #endif
        log(message);
    }
}

void Logger::logMouse(const std::string& message) {
    if (initialized && showTerminal) {
        #ifdef _WIN32
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), YELLOW);
        #endif
        log(message);
    }
}

void Logger::logDetection(const std::string& message) {
    if (initialized && showTerminal) {
        #ifdef _WIN32
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), GREEN);
        #endif
        log(message);
    }
}

void Logger::info(const std::string& message) {
    if (initialized && showTerminal) {
        #ifdef _WIN32
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), WHITE);
        #endif
        log(message);
    }
}

void Logger::error(const std::string& message) {
    if (initialized && showTerminal) {
        #ifdef _WIN32
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), YELLOW);
        #endif
        log(message);
    }
}

void Logger::setLogToFile(bool enable) {
    logToFile = enable;
    
    if (!enable && logFile.is_open()) {
        logFile.close();
    } else if (enable && initialized && !logFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::string filename = "logs/log_" + std::to_string(time) + ".txt";
        logFile.open(filename, std::ios::app);
    }
}

void Logger::processCommand(const std::string& command) {
    if (command == "/clear") {
        #ifdef _WIN32
        if (showTerminal) {
            system("cls");
            
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            char timeBuffer[26];
            ctime_s(timeBuffer, sizeof(timeBuffer), &time);
            std::string timeStr = timeBuffer;
            timeStr = timeStr.substr(11, 8);
            
            std::cout << "\n=== 新的會話開始於 " 
                     << timeStr
                     << " ===\n";
        }
        #else
        system("clear");
        #endif
    }
    else if (command == "/help") {
        std::cout << "\n=== 可用指令列表 ===\n"
                  << "/help   - 顯示此幫助信息\n"
                  << "/clear  - 清空終端機輸出\n"
                  << "/status - 顯示日誌系統狀態\n"
                  << "/log    - 切換文件日誌記錄\n"
                  << "==================\n";
    }
    else if (command == "/status") {
        std::string triggerKey1Text;
        switch (SetLoad::loadAllParameters().triggerKeySelection1) {
            case 0: triggerKey1Text = "滑鼠左鍵"; break;
            case 1: triggerKey1Text = "滑鼠右鍵"; break;
            case 2: triggerKey1Text = "滑鼠後側鍵"; break;
            case 3: triggerKey1Text = "滑鼠前側鍵"; break;
            default: triggerKey1Text = "未知"; break;
        }

        std::string triggerKey2Text;
        switch (SetLoad::loadAllParameters().triggerKeySelection2) {
            case 0: triggerKey2Text = "無"; break;
            case 1: triggerKey2Text = "滑鼠左鍵"; break;
            case 2: triggerKey2Text = "滑鼠右鍵"; break;
            case 3: triggerKey2Text = "滑鼠後側鍵"; break;
            case 4: triggerKey2Text = "滑鼠前側鍵"; break;
            default: triggerKey2Text = "未知"; break;
        }

        std::string fireModeText = (SetLoad::loadAllParameters().fireMode == 0) ? "連發" : "單發";

        std::cout << "\n=== 系統狀態 ===\n"
                  << "終端機輸出: " << (showTerminal ? "開啟" : "關閉") << "\n"
                  << "文件記錄: " << (logToFile ? "開啟" : "關閉") << "\n"
                  << "初始化狀態: " << (initialized ? "已完成" : "未完成") << "\n"
                  << "靈敏度: " << SetLoad::loadAllParameters().sensitivity << "\n"
                  << "頭部偏移: " << SetLoad::loadAllParameters().headOffset << "\n"
                  << "FOV: " << SetLoad::loadAllParameters().fov << "\n"
                  << "TSensitivity: " << SetLoad::loadAllParameters().tsensitivity << "\n"
                  << "自動開火: " << (SetLoad::loadAllParameters().autoFire ? "開啟" : "關閉") << "\n"
                  << "自動瞄準: " << (SetLoad::loadAllParameters().autoAim ? "開啟" : "關閉") << "\n"
                  << "鼠標鎖定: " << (SetLoad::loadAllParameters().mouseLock ? "開啟" : "關閉") << "\n"
                  << "觸發鍵選擇1: " << triggerKey1Text << "\n"
                  << "觸發鍵選擇2: " << triggerKey2Text << "\n"
                  << "火力模式: " << fireModeText << "\n"
                  << "置信度閾值: " << SetLoad::loadAllParameters().confidence_threshold << "\n"
                  << "===============\n";
    }
    else if (command == "/log") {
        logToFile = !logToFile;
        setLogToFile(logToFile);
        std::cout << "文件日誌記錄已" << (logToFile ? "開啟" : "關閉") << "\n";
    }
    else {
        std::cout << "未知指令。輸入 /help 查看可用指令列表。\n";
    }
}