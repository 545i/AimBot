#include "../include/SecurityUtils.h"
#include "SecurityProtection.h"
#include <memory>
#include <cstring>
#include <algorithm>
#include <random>
#include <chrono>
#include <windows.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <intrin.h>

std::mt19937 SecurityUtils::rng(static_cast<unsigned int>(
    std::chrono::system_clock::now().time_since_epoch().count()));
std::vector<std::vector<unsigned char>> SecurityUtils::encryptionKeys;
std::vector<MEMORY_BASIC_INFORMATION> SecurityUtils::protectedRegions;
std::vector<DWORD> SecurityUtils::originalChecksums;

void SecurityUtils::disguiseOpenCVSignatures() {
}

std::vector<unsigned char> SecurityUtils::generateRandomKey() {
    std::vector<unsigned char> key(getKeySize());
    RAND_bytes(key.data(), static_cast<int>(getKeySize()));
    return key;
}

std::string SecurityUtils::generateRandomFunctionName(const std::string& baseName) {
    const std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, chars.size() - 1);
    
    std::string suffix;
    for (int i = 0; i < 8; ++i) {
        suffix += chars[dis(gen)];
    }
    return baseName + "_" + suffix;
}

std::vector<unsigned char> SecurityUtils::encryptData(const std::vector<unsigned char>& data, const std::vector<unsigned char>& key) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    std::vector<unsigned char> iv(getBlockSize());
    RAND_bytes(iv.data(), static_cast<int>(getBlockSize()));
    
    std::vector<unsigned char> encryptedData(data.size() + getBlockSize());
    int len = 0;
    int encryptedLen = 0;
    
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key.data(), iv.data());
    EVP_EncryptUpdate(ctx, encryptedData.data(), &len, data.data(), static_cast<int>(data.size()));
    encryptedLen = len;
    EVP_EncryptFinal_ex(ctx, encryptedData.data() + len, &len);
    encryptedLen += len;
    
    encryptedData.resize(encryptedLen);
    encryptedData.insert(encryptedData.begin(), iv.begin(), iv.end());
    
    EVP_CIPHER_CTX_free(ctx);
    return encryptedData;
}

std::vector<unsigned char> SecurityUtils::decryptData(const std::vector<unsigned char>& encryptedData, const std::vector<unsigned char>& key) {
    if (encryptedData.size() <= getBlockSize()) {
        return std::vector<unsigned char>();
    }
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    std::vector<unsigned char> iv(encryptedData.begin(), encryptedData.begin() + getBlockSize());
    std::vector<unsigned char> ciphertext(encryptedData.begin() + getBlockSize(), encryptedData.end());
    
    std::vector<unsigned char> decryptedData(ciphertext.size());
    int len = 0;
    int decryptedLen = 0;
    
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key.data(), iv.data());
    EVP_DecryptUpdate(ctx, decryptedData.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size()));
    decryptedLen = len;
    EVP_DecryptFinal_ex(ctx, decryptedData.data() + len, &len);
    decryptedLen += len;
    
    decryptedData.resize(decryptedLen);
    EVP_CIPHER_CTX_free(ctx);
    return decryptedData;
}

DWORD SecurityUtils::calculateRegionChecksum(const void* addr, size_t size) {
    DWORD checksum = 0;
    const BYTE* data = static_cast<const BYTE*>(addr);
    for (size_t i = 0; i < size; ++i) {
        checksum = (checksum << 1) | (checksum >> 31);
        checksum += data[i];
    }
    return checksum;
}

bool SecurityUtils::isRegionProtected(const void* addr) {
    for (const auto& region : protectedRegions) {
        if (addr >= region.BaseAddress && 
            addr < static_cast<const BYTE*>(region.BaseAddress) + region.RegionSize) {
            return true;
        }
    }
    return false;
}

void SecurityUtils::addProtectedRegion(const MEMORY_BASIC_INFORMATION& mbi) {
    protectedRegions.push_back(mbi);
    originalChecksums.push_back(calculateRegionChecksum(mbi.BaseAddress, mbi.RegionSize));
}

void SecurityUtils::encryptMemoryRegion(void* addr, size_t size, const std::vector<unsigned char>& key) {
    DWORD oldProtect;
    if (VirtualProtect(addr, size, PAGE_READWRITE, &oldProtect)) {
        BYTE* data = static_cast<BYTE*>(addr);
        for (size_t i = 0; i < size; ++i) {
            data[i] ^= key[i % key.size()];
        }
        VirtualProtect(addr, size, oldProtect, &oldProtect);
    }
}

void SecurityUtils::decryptMemoryRegion(void* addr, size_t size, const std::vector<unsigned char>& key) {
    encryptMemoryRegion(addr, size, key);
}

bool SecurityUtils::verifyMemoryIntegrity() {
    for (size_t i = 0; i < protectedRegions.size(); ++i) {
        const auto& region = protectedRegions[i];
        DWORD currentChecksum = calculateRegionChecksum(region.BaseAddress, region.RegionSize);
        if (currentChecksum != originalChecksums[i]) {
            return false;
        }
    }
    return true;
}

void SecurityUtils::protectMemoryRegions() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    
    MEMORY_BASIC_INFORMATION mbi;
    for (LPVOID addr = sysInfo.lpMinimumApplicationAddress; 
         addr < sysInfo.lpMaximumApplicationAddress; 
         addr = static_cast<LPVOID>(static_cast<char*>(addr) + mbi.RegionSize)) {
        
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) break;
        
        if (mbi.State == MEM_COMMIT && 
            (mbi.Protect & PAGE_EXECUTE_READ || mbi.Protect & PAGE_READONLY)) {
            addProtectedRegion(mbi);
            
            DWORD oldProtect;
            VirtualProtect(mbi.BaseAddress, mbi.RegionSize, 
                          PAGE_EXECUTE_READ | PAGE_GUARD, &oldProtect);
        }
    }
}

void SecurityUtils::rotateEncryptionKeys() {
    std::vector<unsigned char> newKey(getKeySize());
    RAND_bytes(newKey.data(), static_cast<int>(getKeySize()));
    encryptionKeys.push_back(newKey);
    
    if (encryptionKeys.size() > 3) {
        encryptionKeys.erase(encryptionKeys.begin());
    }
    
    for (const auto& region : protectedRegions) {
        encryptMemoryRegion(region.BaseAddress, region.RegionSize, newKey);
    }
}

void SecurityUtils::encryptMemoryRegions() {
    if (encryptionKeys.empty()) {
        std::vector<unsigned char> newKey(getKeySize());
        RAND_bytes(newKey.data(), static_cast<int>(getKeySize()));
        encryptionKeys.push_back(newKey);
    }
    
    for (const auto& region : protectedRegions) {
        encryptMemoryRegion(region.BaseAddress, region.RegionSize, encryptionKeys.back());
    }
}

bool SecurityUtils::checkMemoryTampering() {
    if (!verifyMemoryIntegrity()) {
        return false;
    }
    
    for (const auto& region : protectedRegions) {
        DWORD oldProtect;
        if (VirtualProtect(region.BaseAddress, region.RegionSize, PAGE_READWRITE, &oldProtect)) {
            BYTE* data = static_cast<BYTE*>(region.BaseAddress);
            for (size_t i = 0; i < region.RegionSize; ++i) {
                if (data[i] != (data[i] ^ encryptionKeys.back()[i % encryptionKeys.back().size()])) {
                    VirtualProtect(region.BaseAddress, region.RegionSize, oldProtect, &oldProtect);
                    return false;
                }
            }
            VirtualProtect(region.BaseAddress, region.RegionSize, oldProtect, &oldProtect);
        }
    }
    
    return true;
}