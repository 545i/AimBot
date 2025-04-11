#pragma once
#include <string>
#include <vector>
#include <random>
#include <algorithm>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <windows.h>

class SecurityUtils {
public:
    
    static std::vector<unsigned char> encryptData(const std::vector<unsigned char>& data, const std::vector<unsigned char>& key);
    static std::vector<unsigned char> decryptData(const std::vector<unsigned char>& encryptedData, const std::vector<unsigned char>& key);
    static std::vector<unsigned char> generateRandomKey();
    
    
    static std::string generateRandomFunctionName(const std::string& baseName);
    
    
    static void disguiseOpenCVSignatures();
    
    
    static constexpr int getKeySize() { return 32; }
    static constexpr int getBlockSize() { return 16; }

    
    static bool verifyMemoryIntegrity();
    static void protectMemoryRegions();
    static void rotateEncryptionKeys();
    static void encryptMemoryRegions();
    static bool checkMemoryTampering();
    
private:
    static std::mt19937 rng;
    static std::vector<std::vector<unsigned char>> encryptionKeys;
    static std::vector<MEMORY_BASIC_INFORMATION> protectedRegions;
    static std::vector<DWORD> originalChecksums;
    
    
    static DWORD calculateRegionChecksum(const void* addr, size_t size);
    static bool isRegionProtected(const void* addr);
    static void addProtectedRegion(const MEMORY_BASIC_INFORMATION& mbi);
    static void encryptMemoryRegion(void* addr, size_t size, const std::vector<unsigned char>& key);
    static void decryptMemoryRegion(void* addr, size_t size, const std::vector<unsigned char>& key);
}; 