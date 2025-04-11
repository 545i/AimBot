#pragma once
#include <string>
#include <chrono>
#include <iomanip>
#include <QtCore/QString>
#include <fstream>
#include <thread>
#include <opencv2/opencv.hpp>

class Logger {
private:
    static bool initialized;
    static bool showTerminal;
    static bool logToFile;
    static FILE* terminalWindow;
    static std::ofstream logFile;
    static QString securityKey;
    static std::thread commandThread;
    static bool showPreview;

    static QString generateSecurityHash();
    static bool validateSecurityHash(const QString& hash);
    static void processCommand(const std::string& command);

public:
    static void init();
    static void log(const std::string& message);
    static void close();
    static void setShowTerminal(bool show);
    static bool isShowTerminal() { return showTerminal; }
    static void logGui(const std::string& message);
    static void logMouse(const std::string& message);
    static void logDetection(const std::string& message);
    static void setLogToFile(bool enable);
    static void info(const std::string& message);
    static void error(const std::string& message);
    static void setShowPreview(bool show);
    static bool isShowPreview() { return showPreview; }
};