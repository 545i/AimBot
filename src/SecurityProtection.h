#pragma once

#include <windows.h>

class SecurityProtection {
public:
    
    static bool checkDebugger();
    static bool isDebuggerPresent();
    static bool isRemoteDebuggerPresent();
    static bool isBeingDebugged();
    static bool checkPEB();
    static bool checkHeapFlags();
    static bool checkNtGlobalFlag();
    static bool checkVirtualMachine();
    static bool checkAPIHooks();
    static void preventMemoryScanning();

    
    static void hideProcess();
    static void protectMemory();
    static void preventInjection();
    static void monitorThreads();

    
    static bool verifyFiles();
    static bool verifyMemory();
    static bool verifyCode();
    static bool verifyResources();

private:
    static bool isVirtualMachinePresent();
    static bool checkVMwareRegistry();
    static bool checkVBoxRegistry();
}; 