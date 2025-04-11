#pragma once
#include <string>
#include <windows.h>

class LanguageManager {
public:
    static LanguageManager& getInstance() {
        static LanguageManager instance;
        return instance;
    }

    std::string getText(const std::string& key) const;
    bool isEnglishSystem() const;

private:
    LanguageManager();
    ~LanguageManager() = default;
    LanguageManager(const LanguageManager&) = delete;
    LanguageManager& operator=(const LanguageManager&) = delete;

    bool m_isEnglish;
    std::string m_currentLanguage;
}; 