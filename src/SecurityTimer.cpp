#include "SecurityTimer.h"

SecurityTimer::SecurityTimer(QObject* parent) : QTimer(parent) {
    setInterval(30000);
    connect(this, &QTimer::timeout, this, &SecurityTimer::onTimeout);
}

void SecurityTimer::onTimeout() {
    if (!SecurityUtils::checkMemoryTampering()) {
        QMessageBox::critical(nullptr, "安全警告", "检测到内存篡改，程序将退出");
        QCoreApplication::exit(1);
    }
    
    static int keyRotationCounter = 0;
    if (++keyRotationCounter >= 10) {
        SecurityUtils::rotateEncryptionKeys();
        keyRotationCounter = 0;
    }
}