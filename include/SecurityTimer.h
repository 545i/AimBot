#pragma once
#include <QTimer>
#include <QMessageBox>
#include <QCoreApplication>
#include "SecurityUtils.h"

class SecurityTimer : public QTimer {
    Q_OBJECT
public:
    SecurityTimer(QObject* parent = nullptr);
    
private slots:
    void onTimeout();
}; 