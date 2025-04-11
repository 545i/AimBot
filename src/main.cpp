// *打包前要使用cmake .. -DUSE_DML=ON -DCMAKE_BUILD_TYPE=Release    打開DML設定
// *打包指令cmake --build . --config Release  
// *Before packaging, use "cmake .. -DUSE_DML=ON -DCMAKE_BUILD_TYPE=Release" to open the DML setting
// *Packaging command: cmake --build . --config Release

#include <windows.h>
#include <opencv2/opencv.hpp>
#include "KeyboardMouseControl.h"
#include "ControlGUI.h"
#include "ScreenCapture.h"
#include "Logger.h"
#include <CommCtrl.h>
#include <random>
#include <QApplication>
#include <QTimer>
#include <fstream>
#include <processthreadsapi.h>
#include "SystemUtils.h"
#include "Setload.h"
#include "SecurityUtils.h"
#include "SecurityProtection.h"
#pragma comment(lib, "comctl32.lib")
#include "SecurityTimer.h"
#include "VmMouseControl.h"
#include "LanguageManager.h"

#if defined(USE_GUI) && !defined(NO_GUI)
    #include "ControlGUI.h"
#endif



#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


long g_cachedChipID = -1;  
bool globefire = true; // !自動開火功能是否給客戶使用true/false Whether the automatic fire function is used by the customer true/false
bool mouseControlInitialized = false;
bool vmMouseControlInitialized = false;
std::unique_ptr<KeyboardMouseControl> control;
std::unique_ptr<VmMouseControl> vmControl;
std::unique_ptr<ScreenCapture> capture;
int 擷取框 = 300;
float Tsensitivity = 1.3f;
bool g_useGUI = false;
bool crosshairPLUS = false; // !控制準星檢測增強功能 English: Control crosshair detection enhancement function

bool smoothModeEnabled = false;

void SetSystem(std::unique_ptr<KeyboardMouseControl>& control, std::unique_ptr<ScreenCapture>& capture) { //*=============截圖與控制初始化
    try {
        control = std::make_unique<KeyboardMouseControl>();
        mouseControlInitialized = control->initialize();
        
        if (!mouseControlInitialized) {
            Logger::log(LanguageManager::getInstance().getText("MOUSE_CONTROL_INIT_FAILED"));
            vmControl = std::make_unique<VmMouseControl>();
            vmMouseControlInitialized = true;
            Logger::log(LanguageManager::getInstance().getText("VM_MOUSE_INIT_SUCCESS"));
        } else {
            Logger::logMouse(LanguageManager::getInstance().getText("MOUSE_CONTROL_INIT_SUCCESS"));
            std::string deviceInfo = "設備資訊: ";
            
            if (g_cachedChipID == -1) {
                g_cachedChipID = control->getChipID();
            }
            
            if (g_cachedChipID != -1) {
                deviceInfo += "ChipID=" + std::to_string(g_cachedChipID) + " ";
                Logger::log(deviceInfo);
            }
        }
        
        capture = std::make_unique<ScreenCapture>(擷取框, 擷取框, "");
        if (!capture) {
            throw std::runtime_error(LanguageManager::getInstance().getText("CAPTURE_SYSTEM_INIT_FAILED"));
        }
        Logger::log(LanguageManager::getInstance().getText("CAPTURE_SYSTEM_INIT_SUCCESS"));
        
    } catch (const std::exception& e) {
        Logger::log(LanguageManager::getInstance().getText("INIT_FAILED") + std::string(e.what()));
        MessageBoxW(NULL, 
                  std::wstring(LanguageManager::getInstance().getText("CHECK_DEVICE").begin(), 
                              LanguageManager::getInstance().getText("CHECK_DEVICE").end()).c_str(),
                  std::wstring(LanguageManager::getInstance().getText("ERROR").begin(), 
                              LanguageManager::getInstance().getText("ERROR").end()).c_str(),
                  MB_OK | MB_ICONERROR);
        throw; 
    }
}



class PIDController { //*=================================================================================PID控制器
public:
    PIDController(double sensitivity, double Ki, double Kd)
        : sensitivity(sensitivity), Ki(Ki), Kd(Kd), lastErrorX(0), lastErrorY(0), integralX(0), integralY(0) {}
    
    void updatePosition(cv::Point2f& current, const cv::Point2f& target, double dt) {
        float errorX = target.x - current.x;
        float errorY = target.y - current.y;
        
        double pTermX = sensitivity * errorX;
        double pTermY = sensitivity * errorY;
        
        integralX += errorX * dt;
        integralY += errorY * dt;
        double iTermX = Ki * integralX;
        double iTermY = Ki * integralY;
        
        double dTermX = Kd * (errorX - lastErrorX) / dt;
        double dTermY = Kd * (errorY - lastErrorY) / dt;
        
        float moveX = static_cast<float>(pTermX + iTermX + dTermX);
        float moveY = static_cast<float>(pTermY + iTermY + dTermY);
        
        current.x += moveX;
        current.y += moveY;
        
        lastErrorX = errorX;
        lastErrorY = errorY;
    }

    void setSensitivity(double newSensitivity) {
        sensitivity = newSensitivity;
    }

    cv::Point2f stabilize(const cv::Point2f& moveVector) {
        double errorX = moveVector.x;
        double errorY = moveVector.y;
        
        double moveX = sensitivity * errorX + Kd * (errorX - lastErrorX);
        double moveY = sensitivity * errorY + Kd * (errorY - lastErrorY);
        
        lastErrorX = errorX;
        lastErrorY = errorY;
        
        return cv::Point2f(moveX, moveY);
    }

    double getSensitivity() const {
        return sensitivity;
    }

private:
    double sensitivity;
    double Ki, Kd;
    double lastErrorX;
    double lastErrorY;
    double integralX;
    double integralY;

    double applyPID(double input) {
        double error = input;
        integralX += error;
        integralY += error;
        double derivativeX = error - lastErrorX;
        double derivativeY = error - lastErrorY;
        lastErrorX = error;
        lastErrorY = error;
        
        return sensitivity * error + Ki * integralX + Kd * derivativeX + Ki * integralY + Kd * derivativeY;
    }
};


struct MoveData { //*=================================================================================移動數據
    bool valid;
    cv::Point2f lastPosition;
    std::chrono::steady_clock::time_point lastTime;
    float currentSpeed;
    std::chrono::steady_clock::time_point aimStartTime;
    bool isAdjustingSensitivity;
    bool isInTargetBox;
    float trackingDynamics;
};

struct AimData { //*=================================================================================瞄準數據
    cv::Mat& frame;
    cv::Point2f currentPos;
    cv::Point closest;
    std::vector<cv::Rect>& targets;
    float currentFov;
    float maxDistance;
    bool isAiming;
    bool autoAim;
    bool autoFire;
    bool leftMousePressed;
    float sensitivity;
    float currentSensitivity;
    MoveData& preCalculatedMove;
    int fireDelay;

    AimData(
        cv::Mat& frame,
        cv::Point2f currentPos,
        cv::Point closest,
        std::vector<cv::Rect>& targets,
        float currentFov,
        float maxDistance,
        bool isAiming,
        bool autoAim,
        bool autoFire,
        bool leftMousePressed,
        float sensitivity,
        float currentSensitivity, 
        MoveData& preCalculatedMove,
        int fireDelay
    ) : frame(frame),
        currentPos(currentPos),
        closest(closest),
        targets(targets),
        currentFov(currentFov),
        maxDistance(maxDistance),
        isAiming(isAiming),
        autoAim(autoAim),
        autoFire(autoFire),
        leftMousePressed(leftMousePressed),
        sensitivity(sensitivity),
        currentSensitivity(currentSensitivity),
        preCalculatedMove(preCalculatedMove),
        fireDelay(fireDelay)
    {}
};

namespace MouseControl { //*=================================================================================控制器
   static PIDController g_pid(0.5, 0, 0.5); 
   
   static const float SMOOTHING_FACTOR = 0.5f;
   static cv::Point2f lastMove(0, 0);

   static const double NORMAL_KD = 0.5;
   static const double SMOOTH_KD = 0.3;

   inline void Process(AimData& data) {
       if (!mouseControlInitialized && !vmMouseControlInitialized) return;
       
       cv::Point crosshairPos = capture->findCrosshair(data.frame);
       data.currentPos = (crosshairPos.x != -1 && crosshairPos.y != -1) ? 
                        cv::Point2f(crosshairPos.x, crosshairPos.y) :
                        cv::Point2f(data.frame.cols / 2, data.frame.rows / 2);

       bool hasValidTarget = (data.closest.x != -1 && data.closest.y != -1);
       if (!hasValidTarget || data.targets.empty()) {
           data.preCalculatedMove.isInTargetBox = false;
           data.preCalculatedMove.trackingDynamics = 0.0f;
           return;
       }

       cv::Point2f targetPos(data.closest.x, data.closest.y);
       float distance = cv::norm(targetPos - data.currentPos);
       
       bool inBox = false;
       for (const auto& target : data.targets) {
           cv::Rect trackingBox = target;
           int shrinkX = static_cast<int>(target.width * 0.2);
           trackingBox.x += shrinkX;
           trackingBox.width -= (shrinkX * 2);
           
           if (trackingBox.contains(cv::Point(data.frame.cols/2, data.frame.rows/2))) {
               inBox = true;
               break;
           }
       }

       if (data.isAiming && (distance <= data.maxDistance)) {
           data.preCalculatedMove.isInTargetBox = inBox;
           data.preCalculatedMove.trackingDynamics = 
               (inBox || data.preCalculatedMove.isInTargetBox) ? 1.0f : 0.0f;

           if (data.autoAim) {
               cv::Point2f moveVector = targetPos - data.currentPos;
               
               if (g_pid.getSensitivity() != data.currentSensitivity) {
                   g_pid.setSensitivity(data.currentSensitivity);
               }
               
               cv::Point2f stabilizedMove = g_pid.stabilize(moveVector);

               if (smoothModeEnabled) {
                   stabilizedMove.x = stabilizedMove.x * SMOOTHING_FACTOR + lastMove.x * (1 - SMOOTHING_FACTOR);
                   stabilizedMove.y = stabilizedMove.y * SMOOTHING_FACTOR + lastMove.y * (1 - SMOOTHING_FACTOR);
                   lastMove = stabilizedMove;
               }

               const float MIN_MOVE_THRESHOLD = 0;
               if (abs(stabilizedMove.x) > MIN_MOVE_THRESHOLD || 
                   abs(stabilizedMove.y) > MIN_MOVE_THRESHOLD) {
                   if (mouseControlInitialized) {
                       control->moveMouseSmoothly(
                           static_cast<short>(stabilizedMove.x),
                           static_cast<short>(stabilizedMove.y),
                           1, 1
                       );
                   } else if (vmMouseControlInitialized) {
                       vmControl->moveMouseRelative(
                           static_cast<int>(stabilizedMove.x),
                           static_cast<int>(stabilizedMove.y)
                       );
                   }
               }
           }
       } else {
           data.preCalculatedMove.isInTargetBox = false;
           data.preCalculatedMove.trackingDynamics = 0.0f;
       }
   }

   inline void setSmoothMode(bool enabled) {
       smoothModeEnabled = enabled;
       g_pid = PIDController(0.5, 0, enabled ? SMOOTH_KD : NORMAL_KD);
       if (!enabled) {
           lastMove = cv::Point2f(0, 0);
       }
   }
}

namespace AutoFire { //*=================================================================================自動開火
   inline void Fire(AimData& data) {
       static auto lastShotTime = std::chrono::steady_clock::now();
       auto currentTime = std::chrono::steady_clock::now();

       if(!data.isAiming || !data.autoFire || data.leftMousePressed) return;

       cv::Point crosshair = capture->findCrosshair(data.frame);
       cv::Point2f crosshairPos = (crosshair.x != -1 && crosshair.y != -1) ?
           cv::Point2f(crosshair.x, crosshair.y) :
           cv::Point2f(data.frame.cols / 2, data.frame.rows / 2);
           
       if(crosshairPLUS && (crosshair.x == -1 || crosshair.y == -1)) return;

       bool canShoot = std::chrono::duration_cast<std::chrono::milliseconds>(
           currentTime - lastShotTime).count() > data.fireDelay + rand() % 30;

       if (!data.targets.empty() && data.closest.x != -1 && data.closest.y != -1 && canShoot) {
           float distanceToTarget = std::sqrt(
               std::pow(crosshairPos.x - data.closest.x, 2) +
               std::pow(crosshairPos.y - data.closest.y, 2)
           );

           if (distanceToTarget < 20.0f) {
               if (mouseControlInitialized) {
                   control->mouseButtonClick(VK_LBUTTON);
               } else if (vmMouseControlInitialized) {
                   vmControl->mouseButtonClick(0);
               }
               lastShotTime = currentTime;
           }
       }
   }
}


struct PreviewData { //*=================================================================================預覽數據
   cv::Mat& frame;
   bool hasCrosshair;
   float currentFov;
   bool rightMousePressed;
   cv::Point closest;
   std::vector<cv::Rect>& targets;
   MoveData& preCalculatedMove;
   float fps;
   bool crosshairPlus;
   double textScale = 0.5;
   int textThickness = 1;
   int lineHeight = 20;

   PreviewData(cv::Mat& f, bool crosshair, float fov, bool rmp, 
               cv::Point c, std::vector<cv::Rect>& t, MoveData& move, float fp, bool cp = true)
       : frame(f), hasCrosshair(crosshair), currentFov(fov),
         rightMousePressed(rmp), closest(c), targets(t),
         preCalculatedMove(move), fps(fp), crosshairPlus(cp) {}
};


namespace PreviewDisplay { //*=================================================================================預覽顯示
   inline void FovIndicator(PreviewData& data) {
       int fovRadius = static_cast<int>(data.frame.cols * data.currentFov / 2);
       cv::Point center(data.frame.cols / 2, data.frame.rows / 2);
       cv::Scalar crosshairColor(0, 255, 255);
       cv::circle(data.frame, center, fovRadius, crosshairColor, 1);
   }

    
   inline void CrosshairDisplay(PreviewData& data) {
       if (capture->isEnableCrosshairDetection()) {
           cv::Point crosshairPos = capture->findCrosshair(data.frame);
           if (crosshairPos.x != -1 && crosshairPos.y != -1) {
               cv::circle(data.frame, crosshairPos, 5, cv::Scalar(255, 0, 0), 2);

               std::string crosshairText = "Crosshair: (" + 
                   std::to_string(crosshairPos.x) + "," + 
                   std::to_string(crosshairPos.y) + ")";
               cv::putText(data.frame, crosshairText,
                   cv::Point(5, data.frame.rows - data.lineHeight * 3), cv::FONT_HERSHEY_SIMPLEX,
                   data.textScale, cv::Scalar(0, 255, 255), data.textThickness);
           }

           std::string crosshairStatus = LanguageManager::getInstance().getText(
               data.hasCrosshair ? "CROSSHAIR_FOUND" : "CROSSHAIR_NOT_FOUND");
           cv::putText(data.frame, crosshairStatus,
               cv::Point(5, data.frame.rows - data.lineHeight * 2), cv::FONT_HERSHEY_SIMPLEX,
               data.textScale, data.hasCrosshair ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255),
               data.textThickness);
               
           std::string crosshairPlusStatus = LanguageManager::getInstance().getText(
               data.crosshairPlus ? "CROSSHAIR_PLUS_ENABLED" : "CROSSHAIR_PLUS_DISABLED");
           cv::putText(data.frame, crosshairPlusStatus,
               cv::Point(5, data.frame.rows - data.lineHeight), cv::FONT_HERSHEY_SIMPLEX,
               data.textScale, data.crosshairPlus ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255),
               data.textThickness);
       }
   }

   inline void InfoDisplay(PreviewData& data) {
       std::string fovText = "FOV: " + std::to_string(static_cast<int>(data.currentFov * 100)) + "%";
       if (data.rightMousePressed) {
           fovText += LanguageManager::getInstance().getText("FOV_ZOOMED");
       }
       cv::putText(data.frame, fovText, 
           cv::Point(5, data.lineHeight), cv::FONT_HERSHEY_SIMPLEX, 
           data.textScale, cv::Scalar(0, 255, 255), data.textThickness);
       
       for (const auto& target : data.targets) {
           cv::rectangle(data.frame, target, cv::Scalar(0, 255, 0), 1);
       }
       
       std::string fpsText = "FPS: " + std::to_string(static_cast<int>(data.fps));
       cv::putText(data.frame, fpsText, 
           cv::Point(5, data.lineHeight * 2), cv::FONT_HERSHEY_SIMPLEX, 
           data.textScale, cv::Scalar(0, 255, 255), data.textThickness);
   }

   inline void TrackingStatus(PreviewData& data) {
       std::string trackingText = LanguageManager::getInstance().getText(
           data.preCalculatedMove.trackingDynamics == 1.0f ? "TRACKING_LOCKED" : "TRACKING_NONE");
       cv::Scalar trackingColor = (data.preCalculatedMove.trackingDynamics == 1.0f) ? 
                                cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
       
       cv::putText(data.frame, trackingText, 
           cv::Point(5, data.lineHeight * 3), cv::FONT_HERSHEY_SIMPLEX, 
           data.textScale, trackingColor, data.textThickness);
   }

   inline void PredictionVisualize(PreviewData& data) {
       cv::Point2f predicted = capture->getPredictedTarget();
       if (predicted.x > 0 && predicted.y > 0) {
           cv::circle(data.frame, cv::Point(predicted.x, predicted.y), 3, cv::Scalar(0, 255, 255), -1);
           cv::circle(data.frame, cv::Point(predicted.x, predicted.y), 5, cv::Scalar(0, 255, 255), 1);
           
           if (data.closest.x != -1) {
               cv::line(data.frame, data.closest, cv::Point(predicted.x, predicted.y), 
                       cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
           }
       }
   }

   inline void OffsetDisplay(PreviewData& data) {
       if (data.closest.x != -1 && data.closest.y != -1) {
           cv::Point center(data.frame.cols / 2, data.frame.rows / 2);
           float dx = data.closest.x - center.x;
           float dy = data.closest.y - center.y;
           
           std::string offsetText = "Offset X: " + std::to_string(static_cast<int>(dx));
           cv::putText(data.frame, offsetText, 
               cv::Point(5, data.lineHeight * 4), cv::FONT_HERSHEY_SIMPLEX, 
               data.textScale, cv::Scalar(0, 255, 255), data.textThickness);
       }
   }
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance,
                    _In_opt_ HINSTANCE hPrevInstance,
                    _In_ LPWSTR lpCmdLine,
                    _In_ int nShowCmd) {
    SecurityUtils::disguiseOpenCVSignatures();
    SecurityProtection::protectMemory();
    SecurityProtection::preventInjection();
    
    auto encryptionKey = SecurityUtils::generateRandomKey();
    
    std::string randomMainName = SecurityUtils::generateRandomFunctionName("main");
    std::string randomInitName = SecurityUtils::generateRandomFunctionName("init");
    
    srand(static_cast<unsigned>(time(nullptr)));
    
    std::unique_ptr<PlatformInterface> platform = std::make_unique<WindowsPlatform>();
    platform->initializeSafely();

    if (!platform->createMutex("MyApplicationMutex")) {
        platform->showMessage(LanguageManager::getInstance().getText("ALREADY_RUNNING"), 
                            LanguageManager::getInstance().getText("ERROR"));
        return 0;
    }

    Logger::init();
    cv::destroyAllWindows();

    bool isRunning = true;

    SetConsoleCtrlHandler([](DWORD ctrlType) -> BOOL {
        if (ctrlType == CTRL_CLOSE_EVENT || 
            ctrlType == CTRL_SHUTDOWN_EVENT || 
            ctrlType == CTRL_LOGOFF_EVENT) {
            Logger::log(LanguageManager::getInstance().getText("PROGRAM_CLOSED"));
            return TRUE;
        }
        return FALSE;
    }, TRUE);

    SetLoad::Parameters params = SetLoad::loadAllParameters();
    
    bool showPreview = false;
    float sensitivity = params.sensitivity;
    float headOffset = params.headOffset;
    bool autoFire = params.autoFire;
    bool autoAim = params.autoAim;
    float fov = params.fov;
    bool mouseLock = params.mouseLock;
    float Tsensitivity = params.tsensitivity;
    int triggerKeySelection1 = params.triggerKeySelection1;
    int triggerKeySelection2 = params.triggerKeySelection2;

    MoveData preCalculatedMove;
    preCalculatedMove.valid = false;
    
    int argc = 0;
    QApplication app(argc, nullptr);
    
    try {
        Logger::logGui(LanguageManager::getInstance().getText("PROGRAM_START"));
        
        bool previousLoginState = false;
        bool featuresInitialized = false;
        bool isAiming = false;
        float leadDistance = 0.0f;
        
        SetSystem(control, capture);
        
        featuresInitialized = true;
        
        std::unique_ptr<ControlGUI> gui;
        if (g_useGUI) {
            gui = std::make_unique<ControlGUI>(&showPreview, &sensitivity, &headOffset, 
                                             &autoFire, &autoAim, &fov, &mouseLock,
                                             nullptr, globefire, &Tsensitivity);
            
            gui->setScreenCapture(capture.get());
            
            QObject::connect(gui.get(), &ControlGUI::triggerKeysChanged,
                [&triggerKeySelection1, &triggerKeySelection2](int key1, int key2) {
                    triggerKeySelection1 = key1;
                    triggerKeySelection2 = key2;
                });
        }
        
        if (mouseControlInitialized) {
            if (g_useGUI) {
                gui->updateMouseControlInfo(true);
            }
        }

        static bool wasLocked = false;
        
        platform->registerShutdownHandler([]() {
            Logger::log(LanguageManager::getInstance().getText("PROGRAM_CLOSED"));
        });
        
        SecurityTimer securityTimer;
        securityTimer.start();

        while (isRunning) {
            if (g_useGUI) {
                if (!platform->isWindowValid(reinterpret_cast<void*>(gui->winId()))) {
                    Logger::log(LanguageManager::getInstance().getText("GUI_CLOSED"));
                    isRunning = false;
                    break;
                }
                
                bool currentLoginState = gui->isUserLoggedIn();
                if (currentLoginState != previousLoginState) {
                    if (currentLoginState) {
                        Logger::log(LanguageManager::getInstance().getText("USER_LOGGED_IN"));
                        if (featuresInitialized) {
                            gui->loadSettings();
                            
                            showPreview = *gui->getShowPreview();
                            autoFire = *gui->getAutoFire();
                            autoAim = *gui->getAutoAim();
                            sensitivity = sensitivity;
                            fov = fov;

                            if (capture) {
                                capture->setHeadOffset(headOffset);
                                
                                int currentMode = SetLoad::getAimPart();
                                capture->setBoxDisplayMode(currentMode);
                                
                                Logger::log(LanguageManager::getInstance().getText("SETTINGS_UPDATED"));
                            }
                            
                            Logger::log(LanguageManager::getInstance().getText("SETTINGS_LOADED"));
                        }
                    } else {
                        Logger::log(LanguageManager::getInstance().getText("USER_LOGGED_OUT"));
                        showPreview = false;
                        autoFire = false;
                        autoAim = false;
                        isAiming = false;
                        preCalculatedMove.valid = false;
                    }
                    previousLoginState = currentLoginState;
                }
            }
            static int lastMode = -1;
            int currentMode = SetLoad::getAimPart();

            if (lastMode != currentMode) {
                if (capture) {
                    capture->setBoxDisplayMode(currentMode);
                }
                lastMode = currentMode;
            }

            std::vector<cv::Rect> targets;
            cv::Point closest(-1, -1);
            cv::Mat frame;

            try {
                targets = capture->getLatestResults();
                closest = capture->find_closest_target(targets);
                frame = capture->capture_center();
            } catch (const std::exception& e) {
                Logger::log(LanguageManager::getInstance().getText("DETECTION_ERROR") + std::string(e.what()));
                targets.clear();
                closest = cv::Point(-1, -1);
                frame = cv::Mat();
            }

            if (frame.empty()) {
                Logger::logGui(LanguageManager::getInstance().getText("BLANK_FRAME"));
                if (capture->reinitializeCapture()) {
                    Logger::logGui(LanguageManager::getInstance().getText("RELOAD_SUCCESS"));
                } else {
                    Logger::logGui(LanguageManager::getInstance().getText("RELOAD_FAILED"));
                }
                continue;
            }
            
            
            std::vector<int> keys = SetLoad::getTriggerKeys(triggerKeySelection1, triggerKeySelection2);
            bool anyKeyPressed = false;
            bool leftMousePressed = false;
            bool rightMousePressed = false;
            float currentFov = fov;
            

            for (int key : keys) {
                if (key == VK_XBUTTON1 || key == VK_XBUTTON2) {
                    SHORT keyState = GetAsyncKeyState(key);
                    if (keyState & 0x8000) {
                        anyKeyPressed = true;
                    }
                } else {
                    SHORT keyState = GetAsyncKeyState(key);
                    if (keyState & 0x8000) {
                        anyKeyPressed = true;
                        if (key == VK_LBUTTON) {
                            leftMousePressed = true;
                        }
                        if (key == VK_RBUTTON) {
                            rightMousePressed = true;
                        }
                    }
                }
            }
            

            
            float currentSensitivity = sensitivity;
            if (rightMousePressed) {
                currentFov = fov * 2.0f; 
                currentSensitivity = sensitivity * Tsensitivity;
            }
            
            if (anyKeyPressed) {
                if (!isAiming) { 
                    preCalculatedMove.aimStartTime = std::chrono::steady_clock::now();
                    preCalculatedMove.isAdjustingSensitivity = false;
                }
                isAiming = true;
            } else {
                isAiming = false;
                preCalculatedMove.isAdjustingSensitivity = false;
            }

            AimData aimData(frame, cv::Point2f(), closest, targets, currentFov,
                            frame.cols * currentFov / 2, isAiming, autoAim, autoFire,
                            leftMousePressed, sensitivity, currentSensitivity,
                            preCalculatedMove, SetLoad::getFireDelay());


            AutoFire::Fire(aimData); 
            MouseControl::Process(aimData);

            if (showPreview) {
                PreviewData previewData(frame, capture->hasCrosshair, currentFov, rightMousePressed, 
                                    closest, targets, preCalculatedMove, capture->getCurrentFPS(), crosshairPLUS);
                PreviewDisplay::FovIndicator(previewData);
                PreviewDisplay::CrosshairDisplay(previewData);
                PreviewDisplay::InfoDisplay(previewData);
                PreviewDisplay::TrackingStatus(previewData);
                PreviewDisplay::PredictionVisualize(previewData);
                PreviewDisplay::OffsetDisplay(previewData);
                gui->updatePreview(frame);
            }

            cv::waitKey(1);
        }

        if (control) {
            control.reset();
        }
        if (capture) {
            capture.reset();
        }
        
        Logger::log(LanguageManager::getInstance().getText("CLEANUP_COMPLETE"));
    }
    catch (const std::runtime_error& e) {
        if (std::string(e.what()).find("DLL") != std::string::npos) {
            Logger::log(LanguageManager::getInstance().getText("DLL_RESOURCE_ERROR"));
        } else if (std::string(e.what()).find("string") != std::string::npos) {
            Logger::log(LanguageManager::getInstance().getText("STRING_TOO_LONG"));
        } else {
            Logger::log(LanguageManager::getInstance().getText("INIT_FAILED") + std::string(e.what()));
        }
        MessageBoxW(NULL, 
                  std::wstring(LanguageManager::getInstance().getText("CHECK_DEVICE").begin(), 
                              LanguageManager::getInstance().getText("CHECK_DEVICE").end()).c_str(),
                  std::wstring(LanguageManager::getInstance().getText("ERROR").begin(), 
                              LanguageManager::getInstance().getText("ERROR").end()).c_str(),
                  MB_OK | MB_ICONERROR);
        return 1;
    }
    catch (const std::exception& e) {
        Logger::log(LanguageManager::getInstance().getText("INIT_FAILED") + std::string(e.what()));
        MessageBoxW(NULL, 
                  std::wstring(LanguageManager::getInstance().getText("CHECK_DEVICE").begin(), 
                              LanguageManager::getInstance().getText("CHECK_DEVICE").end()).c_str(),
                  std::wstring(LanguageManager::getInstance().getText("ERROR").begin(), 
                              LanguageManager::getInstance().getText("ERROR").end()).c_str(),
                  MB_OK | MB_ICONERROR);
        return 1;
    }

    platform->releaseMutex();
    
    return 0;
}


//PLEASE NO BUG