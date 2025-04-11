#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include <memory>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <string>
#include <wrl/client.h>
#include <onnxruntime_cxx_api.h>
#include <xtensor/xarray.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <directml.h>
#include "dml_provider_factory.h"
#include <QFile>
#include <QByteArray>
#include <QIODevice>

struct DetectionBox {
    int x1, y1, x2, y2;
    float confidence;
    
    DetectionBox(int _x1, int _y1, int _x2, int _y2, float _conf) :
        x1(_x1), y1(_y1), x2(_x2), y2(_y2), confidence(_conf) {}
};


class MemoryMapping {
public:
    MemoryMapping(size_t reqSize);
    ~MemoryMapping();
    void* getData();
    
private:
    HANDLE fileHandle;
    HANDLE mappingHandle;
    void* mappedView;
    size_t size;
    
    
    MemoryMapping(const MemoryMapping&) = delete;
    MemoryMapping& operator=(const MemoryMapping&) = delete;
};

class ScreenCapture {
public:
    ScreenCapture(int sizeX, int sizeY, const std::string& model_path);
    ~ScreenCapture();

    
    cv::Mat capture_center();
    std::vector<cv::Rect> detect_targets(const cv::Mat& frame);
    cv::Point find_closest_target(const std::vector<cv::Rect>& targets);

    
    void setHeadOffset(float offset);
    void setConfidenceThreshold(float threshold);
    void setBoxDisplayMode(int mode);
    void setTargetWindow(HWND hwnd);
    void setExecutionProvider(const std::string& provider);

    
    bool checkCUDAStatus() const;
    float getCurrentFPS() const;
    int getBoxDisplayMode() const { return boxDisplayMode; }
    
    
    std::vector<cv::Rect> getLatestResults();
    bool updateWindowRect();

    bool reinitializeCapture();

    cv::Point2f getPredictedTarget() const { return prev_target; }

    cv::Point findCrosshair(const cv::Mat& frame);
    bool hasCrosshair = false;  

    
    void setEnableCrosshairDetection(bool enable) {
        enableCrosshairDetection = enable;
    }
    
    bool isEnableCrosshairDetection() const {
        return enableCrosshairDetection;
    }

private:
    
    static constexpr int input_width = 640;
    static constexpr int input_height = 640;
    static constexpr int TEXTURE_COUNT = 3;  
    static constexpr int SMOOTH_FRAMES = 3;
    static constexpr size_t MAX_QUEUE_SIZE = 2;
    
    
    static constexpr int MAX_TARGETS = 10;
    static constexpr int MAX_LOST_FRAMES = 30;
    
    struct Track {
        cv::KalmanFilter kalman;
        cv::Rect last_box;
        bool active;
        int frames_since_update;
    };
    
    std::vector<cv::KalmanFilter> kalman_filters;
    std::vector<Track> target_tracks;
    
    
    void initDXGI();
    void initOnnxRuntime(const std::string& model_path);
    
    
    std::vector<DetectionBox> detect(const cv::Mat& frame);
    xt::xarray<float> preprocess(const cv::Mat& image);
    Ort::Value inference(const xt::xarray<float>& input_tensor);
    std::vector<DetectionBox> postprocess(const Ort::Value& output_tensor, 
                                        cv::Size image_size, 
                                        float conf_threshold);
    
    
    float calculate_iou(const cv::Rect& box1, const cv::Rect& box2);
    std::vector<cv::Rect> non_max_suppression(const std::vector<cv::Rect>& boxes, 
                                             float iou_threshold);
    cv::Rect smooth_detection(const cv::Rect& current_box);

    
    void releaseResources();
    
    
    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> staging_textures;
    
    
    cv::Mat front_buffer;
    cv::Mat back_buffer;
    cv::Mat processed_image;
    std::unique_ptr<BYTE[]> buffer;
    
    
    Ort::Env env;
    std::unique_ptr<Ort::Session> session;
    std::vector<const char*> input_node_names;
    std::vector<const char*> output_node_names;
    std::string input_name;
    std::string output_name;
    std::string model_path;
    std::string current_provider;
    
    
    bool cuda_available;
    bool dml_available;
    float confidence_threshold;
    float head_offset;
    int boxDisplayMode;
    int current_texture_index;
    
    
    std::chrono::steady_clock::time_point last_fps_update;
    std::queue<std::chrono::steady_clock::time_point> frame_times;
    float current_fps;
    const size_t FPS_SAMPLE_SIZE;
    
    
    HWND targetWindow;
    bool useWindowCapture;
    RECT windowRect;
    int screen_width;
    int screen_height;
    const int capture_size_X;
    const int capture_size_Y;
    
    
    std::array<std::size_t, 4> input_tensor_shape;
    xt::xarray<float> input_tensor;
    std::deque<cv::Rect> previous_boxes;
    
    
    std::mutex frame_mutex;
    std::mutex results_mutex;
    std::condition_variable frame_ready;
    std::condition_variable results_ready;
    
    
    std::queue<cv::Mat> frame_queue;
    std::queue<std::vector<cv::Rect>> results_queue;
    bool running;

    
    void handleDXGIError(HRESULT hr);
    void initStagingTextures();
    void updateFPSStats();
    void handleAcquireFrameError(HRESULT hr);
    D3D11_BOX calculateCaptureRegion();
    bool copyTextureDataToBuffer(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture);
    cv::Point calculateTargetCenter(const cv::Rect& target);

    QByteArray model_data;


    
    cv::Mat prev_gray;
    cv::Mat current_frame;  
    std::vector<cv::Point2f> prev_points;
    std::vector<cv::Point2f> curr_points;
    std::vector<uchar> status;
    std::vector<float> err;
    cv::Point2f prev_target;
    bool is_tracking;
    const int MAX_CORNERS = 50;
    std::vector<cv::Rect> current_targets; 
    
    
    cv::Point2f predictTargetMotion(const cv::Point& current_target);
    void initOpticalFlow(const cv::Mat& frame, const cv::Rect& target_box);
    void updateOpticalFlow(const cv::Mat& frame);

    
    std::unique_ptr<MemoryMapping> frameMapping;
    std::unique_ptr<MemoryMapping> resultMapping;
    
    
    bool copyDataToMapping(void* dest, const void* src, size_t size);
    bool copyDataFromMapping(void* dest, const void* src, size_t size);
    
    
    struct MappedBuffer {
        void* data;
        size_t size;
    };
    std::vector<MappedBuffer> mappedBuffers;
    
    
    static constexpr size_t BUFFER_POOL_SIZE = 3;
    std::mutex bufferMutex;
    std::queue<size_t> availableBuffers;
    
    
    SECURITY_ATTRIBUTES securityAttributes;
    SECURITY_DESCRIPTOR securityDescriptor;

    
    cv::Point last_crosshair_pos{-1, -1};
    bool crosshair_locked = false;
    const int LOCK_THRESHOLD = 30;
    const int SEARCH_RADIUS = 40;

    
    bool enableCrosshairDetection;

    
    void initKalmanFilters();
    void updateTracks(const std::vector<DetectionBox>& detections);
    cv::Rect getPredictedBox(const Track& track);
};