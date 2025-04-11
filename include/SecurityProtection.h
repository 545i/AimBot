#pragma once
#include <windows.h>
#include <vector>
#include <string>

class SecurityProtection {
public:
    static bool checkDebugger();
    static bool checkBreakpoints();
    static bool checkMemoryBreakpoints();
    
    static void hideProcess();
    static void protectMemory();
    static void preventInjection();
    static void monitorThreads();
    
    static bool verifyFiles();
    static bool verifyMemory();
    static bool verifyCode();
    static bool verifyResources();
    
    static bool checkVirtualMachine();
    
    static void preventMemoryScanning();
    
    static bool checkAPIHooks();
    
private:
    static bool isDebuggerPresent();
    static bool isRemoteDebuggerPresent();
    static bool isBeingDebugged();
    static bool checkPEB();
    static bool checkHeapFlags();
    static bool checkNtGlobalFlag();
}; 