#include "SecurityProtection.h"
#include <TlHelp32.h>
#include <Psapi.h>
#include <intrin.h>
#include <winternl.h>

typedef NTSTATUS(NTAPI* pNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
);

bool SecurityProtection::checkDebugger() {
    return isDebuggerPresent() || 
           isRemoteDebuggerPresent() || 
           isBeingDebugged() || 
           checkPEB() || 
           checkHeapFlags() || 
           checkNtGlobalFlag();
}

bool SecurityProtection::isDebuggerPresent() {
    return ::IsDebuggerPresent() != FALSE;
}

bool SecurityProtection::isRemoteDebuggerPresent() {
    BOOL isRemoteDebuggerPresent = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &isRemoteDebuggerPresent);
    return isRemoteDebuggerPresent != FALSE;
}

bool SecurityProtection::isBeingDebugged() {
    return (GetCurrentProcessId() != GetCurrentThreadId());
}

bool SecurityProtection::checkPEB() {
    BOOL beingDebugged = FALSE;
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        pNtQueryInformationProcess NtQueryInformationProcess = (pNtQueryInformationProcess)
            GetProcAddress(hNtdll, "NtQueryInformationProcess");
        if (NtQueryInformationProcess) {
            PROCESS_BASIC_INFORMATION pbi;
            NTSTATUS status = NtQueryInformationProcess(
                GetCurrentProcess(),
                ProcessBasicInformation,
                &pbi,
                sizeof(pbi),
                nullptr
            );
            if (NT_SUCCESS(status)) {
                beingDebugged = (pbi.PebBaseAddress->BeingDebugged != 0);
            }
        }
    }
    return beingDebugged;
}

bool SecurityProtection::checkHeapFlags() {
    HANDLE heap = GetProcessHeap();
    if (heap == NULL) return false;
    
    ULONG heapFlags = 0;
    ULONG heapForceFlags = 0;
    if (HeapQueryInformation(heap, HeapCompatibilityInformation, 
                           &heapFlags, sizeof(heapFlags), NULL)) {
        return (heapFlags & HEAP_GROWABLE) == 0;
    }
    return false;
}

bool SecurityProtection::checkNtGlobalFlag() {
    BOOL hasDebuggerFlags = FALSE;
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        pNtQueryInformationProcess NtQueryInformationProcess = (pNtQueryInformationProcess)
            GetProcAddress(hNtdll, "NtQueryInformationProcess");
        if (NtQueryInformationProcess) {
            PROCESS_BASIC_INFORMATION pbi;
            NTSTATUS status = NtQueryInformationProcess(
                GetCurrentProcess(),
                ProcessBasicInformation,
                &pbi,
                sizeof(pbi),
                nullptr
            );
            if (NT_SUCCESS(status)) {
                DWORD ntGlobalFlag = *(DWORD*)((BYTE*)pbi.PebBaseAddress + 0x68);
                hasDebuggerFlags = (ntGlobalFlag & 0x70) != 0;
            }
        }
    }
    return hasDebuggerFlags;
}

void SecurityProtection::hideProcess() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        pNtQueryInformationProcess NtQueryInformationProcess = (pNtQueryInformationProcess)
            GetProcAddress(hNtdll, "NtQueryInformationProcess");
        if (NtQueryInformationProcess) {
            PROCESS_BASIC_INFORMATION pbi;
            NTSTATUS status = NtQueryInformationProcess(
                GetCurrentProcess(),
                ProcessBasicInformation,
                &pbi,
                sizeof(pbi),
                nullptr
            );
            if (NT_SUCCESS(status)) {
                pbi.PebBaseAddress->BeingDebugged = 0;
            }
        }
    }
}

void SecurityProtection::protectMemory() {
    DWORD oldProtect;
    VirtualProtect(GetModuleHandle(NULL), 0x1000, PAGE_EXECUTE_READ | PAGE_GUARD, &oldProtect);
}

void SecurityProtection::preventInjection() {
    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te32;
        te32.dwSize = sizeof(THREADENTRY32);
        
        if (Thread32First(hThreadSnap, &te32)) {
            do {
                if (te32.th32OwnerProcessID == GetCurrentProcessId()) {
                }
            } while (Thread32Next(hThreadSnap, &te32));
        }
        CloseHandle(hThreadSnap);
    }
}

void SecurityProtection::monitorThreads() {
    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te32;
        te32.dwSize = sizeof(THREADENTRY32);
        
        if (Thread32First(hThreadSnap, &te32)) {
            do {
                if (te32.th32OwnerProcessID == GetCurrentProcessId()) {
                    HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, te32.th32ThreadID);
                    if (hThread != NULL) {
                        CONTEXT ctx;
                        ctx.ContextFlags = CONTEXT_FULL;
                        if (GetThreadContext(hThread, &ctx)) {
                        }
                        CloseHandle(hThread);
                    }
                }
            } while (Thread32Next(hThreadSnap, &te32));
        }
        CloseHandle(hThreadSnap);
    }
}

bool SecurityProtection::verifyFiles() {
    const char* criticalFiles[] = {
        "svchost.exe",
        "SecurityUtils.dll",
        "config.ini"
    };
    
    for (const auto& file : criticalFiles) {
        HANDLE hFile = CreateFileA(file, GENERIC_READ, FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        
        BY_HANDLE_FILE_INFORMATION fileInfo;
        if (!GetFileInformationByHandle(hFile, &fileInfo)) {
            CloseHandle(hFile);
            return false;
        }
        
        CloseHandle(hFile);
    }
    return true;
}

bool SecurityProtection::verifyMemory() {
    MEMORY_BASIC_INFORMATION mbi;
    for (LPVOID addr = 0; addr < (LPVOID)0x7FFFFFFF; 
         addr = (LPVOID)((DWORD_PTR)addr + mbi.RegionSize)) {
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) break;
        
        if (mbi.State == MEM_COMMIT && 
            (mbi.Protect & PAGE_EXECUTE_READ || mbi.Protect & PAGE_READONLY)) {
        }
    }
    return true;
}

bool SecurityProtection::verifyCode() {
    HMODULE hModule = GetModuleHandle(NULL);
    if (hModule == NULL) return false;
    
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hModule;
    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hModule + dosHeader->e_lfanew);
    
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);
    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
        if (strcmp((char*)section->Name, ".text") == 0) {
            DWORD checksum = 0;
            for (DWORD j = 0; j < section->SizeOfRawData; j++) {
                checksum += ((BYTE*)hModule + section->VirtualAddress)[j];
            }
        }
        section++;
    }
    return true;
}

bool SecurityProtection::checkVirtualMachine() {
    return isVirtualMachinePresent() || checkVMwareRegistry() || checkVBoxRegistry();
}

bool SecurityProtection::isVirtualMachinePresent() {
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 1);
    return (cpuInfo[2] & (1 << 31)) != 0;
}

bool SecurityProtection::checkVMwareRegistry() {
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, 
        L"SOFTWARE\\VMware, Inc.\\VMware Tools", 
        0, KEY_READ, &hKey);
    
    if (result == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

bool SecurityProtection::checkVBoxRegistry() {
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, 
        L"SOFTWARE\\Oracle\\VirtualBox Guest Additions", 
        0, KEY_READ, &hKey);
    
    if (result == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

void SecurityProtection::preventMemoryScanning() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    
    MEMORY_BASIC_INFORMATION mbi;
    for (LPVOID addr = sysInfo.lpMinimumApplicationAddress; 
         addr < sysInfo.lpMaximumApplicationAddress; 
         addr = (LPVOID)((DWORD_PTR)addr + mbi.RegionSize)) {
        
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) break;
        
        if (mbi.State == MEM_COMMIT && 
            (mbi.Protect & PAGE_EXECUTE_READ || mbi.Protect & PAGE_READONLY)) {
            DWORD oldProtect;
            VirtualProtect(mbi.BaseAddress, mbi.RegionSize, 
                          PAGE_EXECUTE_READ | PAGE_GUARD, &oldProtect);
        }
    }
}
bool SecurityProtection::checkAPIHooks() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return false;

    FARPROC pNtCreateFile = GetProcAddress(hNtdll, "NtCreateFile");
    if (!pNtCreateFile) return false;

    BYTE* pBytes = (BYTE*)pNtCreateFile;
    return (pBytes[0] == 0xE9 || pBytes[0] == 0xFF);
} 