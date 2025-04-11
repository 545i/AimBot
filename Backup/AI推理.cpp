#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include <windows.h>
#include <dwmapi.h>
#include <chrono>
#include <thread>
#include <queue>
#include <mutex>
#include <iostream>
#include <vector>
#include <memory>
#include <numeric>
#include <iomanip>
#include <xtensor/xarray.hpp>
#include <xtensor/xview.hpp>
#include <xtensor/xadapt.hpp>
#include <xtensor/xio.hpp>

// 检测框结构
struct DetectionBox {
    int x1, y1, x2, y2;
    float confidence;

    DetectionBox(int _x1, int _y1, int _x2, int _y2, float conf)
        : x1(_x1), y1(_y1), x2(_x2), y2(_y2), confidence(conf) {}
};

// ONNX目标检测器类
class ObjectDetector {
private:
    Ort::Env env;
    std::unique_ptr<Ort::Session> session;
    const int input_width = 640;
    const int input_height = 640;
    std::vector<const char*> input_node_names;
    std::vector<const char*> output_node_names;
    std::string input_name;
    std::string output_name;
    
    // 预分配的内存缓冲区
    xt::xarray<float> input_tensor_values;
    cv::Mat resized_array;
    const float confidence_threshold = 0.25f;  // 調整閾值

    xt::xarray<float> preprocess(const cv::Mat& image) {
        if (image.empty()) {
            throw std::runtime_error("Empty image in preprocess");
        }

        try {
            float scale_x = static_cast<float>(input_width) / image.cols;
            float scale_y = static_cast<float>(input_height) / image.rows;
            float scale = std::min(scale_x, scale_y);

            int new_width = static_cast<int>(image.cols * scale);
            int new_height = static_cast<int>(image.rows * scale);

            cv::Mat resized = cv::Mat::zeros(input_height, input_width, CV_8UC3);
            int dx = (input_width - new_width) / 2;
            int dy = (input_height - new_height) / 2;

            cv::Mat scaled;
            cv::resize(image, scaled, cv::Size(new_width, new_height));
            scaled.copyTo(resized(cv::Rect(dx, dy, new_width, new_height)));
            cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);
            
            xt::xarray<float> input_tensor = xt::zeros<float>({1, 3, input_height, input_width});
            cv::Mat float_mat;
            resized.convertTo(float_mat, CV_32F, 1.0/255.0);

            for (int c = 0; c < 3; c++) {
                for (int h = 0; h < input_height; h++) {
                    for (int w = 0; w < input_width; w++) {
                        input_tensor(0, c, h, w) = float_mat.at<cv::Vec3f>(h, w)[c];
                    }
                }
            }
            
            return input_tensor;
        }
        catch (const std::exception& e) {
            throw;
        }
    }

    Ort::Value inference(const xt::xarray<float>& input_tensor) {
        try {
            std::vector<int64_t> input_shape = {1, 3, input_height, input_width};
            auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

            Ort::Value input_ort = Ort::Value::CreateTensor<float>(
                memory_info,
                const_cast<float*>(input_tensor.data()),
                input_tensor.size(),
                input_shape.data(),
                input_shape.size()
            );

            auto output_tensors = session->Run(
                Ort::RunOptions{nullptr},
                input_node_names.data(),
                &input_ort,
                1,
                output_node_names.data(),
                1
            );

            if (output_tensors.size() == 0) {
                throw std::runtime_error("No output tensors");
            }

            return std::move(output_tensors[0]);
        }
        catch (const std::exception& e) {
            throw;
        }
    }

public:
    ObjectDetector(const std::string& model_path) 
        : env(ORT_LOGGING_LEVEL_WARNING, "object-detection"),
          input_tensor_values(xt::zeros<float>({1, 3, input_height, input_width}))
    {
        try {
            Ort::SessionOptions session_options;
            
            std::cout << "Checking CUDA availability...\n";
            OrtCUDAProviderOptions cuda_options{};
            cuda_options.device_id = 0;
            cuda_options.arena_extend_strategy = 0;
            cuda_options.cudnn_conv_algo_search = static_cast<OrtCudnnConvAlgoSearch>(1);
            cuda_options.do_copy_in_default_stream = 1;

            try {
                session_options.AppendExecutionProvider_CUDA(cuda_options);
                std::cout << "CUDA provider added successfully\n";
            }
            catch (const Ort::Exception& e) {
                std::cout << "CUDA unavailable, falling back to CPU: " << e.what() << "\n";
            }

            session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            session_options.SetIntraOpNumThreads(1);
            
            std::wstring widestr = std::wstring(model_path.begin(), model_path.end());
            session = std::make_unique<Ort::Session>(env, widestr.c_str(), session_options);
            
            Ort::AllocatorWithDefaultOptions allocator;
            input_name = session->GetInputNameAllocated(0, allocator).get();
            output_name = session->GetOutputNameAllocated(0, allocator).get();
            
            input_node_names.push_back(input_name.c_str());
            output_node_names.push_back(output_name.c_str());

            auto input_shape = session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
            std::cout << "Input shape: ";
            for (auto dim : input_shape) std::cout << dim << " ";
            std::cout << "\n";

            std::cout << "Model loaded successfully\n";
        }
        catch (const Ort::Exception& e) {
            std::cerr << "ONNX Runtime error: " << e.what() << std::endl;
            throw;
        }
    }

    std::vector<DetectionBox> detect(const cv::Mat& frame) {
        try {
            auto input_tensor = preprocess(frame);
            auto output_tensor = inference(input_tensor);
            return postprocess(output_tensor, frame.size(), confidence_threshold);
        }
        catch (const std::exception& e) {
            std::cerr << "Detection error: " << e.what() << std::endl;
            return {};
        }
    }

    std::vector<DetectionBox> postprocess(const Ort::Value& output_tensor,
                                    cv::Size image_size,
                                    float conf_threshold)
    {
        try {
            const float* output_data = output_tensor.GetTensorData<float>();
            auto output_shape = output_tensor.GetTensorTypeAndShapeInfo().GetShape();
            
            const int num_boxes = static_cast<int>(output_shape[2]);
            std::vector<DetectionBox> detections;
            
            for (int i = 0; i < num_boxes; i++) {
                float confidence = output_data[4 * num_boxes + i];
                
                if (confidence > conf_threshold) {
                    float x = output_data[0 * num_boxes + i];
                    float y = output_data[1 * num_boxes + i];
                    float w = output_data[2 * num_boxes + i];
                    float h = output_data[3 * num_boxes + i];
                    
                    float x1 = x - w/2;
                    float y1 = y - h/2;
                    float x2 = x + w/2;
                    float y2 = y + h/2;
                    
                    float scale_x = static_cast<float>(image_size.width) / input_width;
                    float scale_y = static_cast<float>(image_size.height) / input_height;
                    
                    x1 *= scale_x;
                    y1 *= scale_y;
                    x2 *= scale_x;
                    y2 *= scale_y;
                    
                    detections.emplace_back(
                        static_cast<int>(std::round(x1)),
                        static_cast<int>(std::round(y1)),
                        static_cast<int>(std::round(x2)),
                        static_cast<int>(std::round(y2)),
                        confidence
                    );
                }
            }
            
            return detections;
        }
        catch (const std::exception& e) {
            throw;
        }
    }
};

// 屏幕捕获类
class ScreenCapture {
private:
    HWND desktop_hwnd;
    HDC desktop_dc;
    HDC capture_dc;
    HBITMAP bitmap;
    int width, height;
    BITMAPINFOHEADER bi;
    std::vector<BYTE> buffer;
    int screen_x, screen_y;

public:
    ScreenCapture(int w, int h) : width(w), height(h) {
        desktop_hwnd = GetDesktopWindow();
        desktop_dc = GetDC(desktop_hwnd);
        capture_dc = CreateCompatibleDC(desktop_dc);
        bitmap = CreateCompatibleBitmap(desktop_dc, width, height);
        SelectObject(capture_dc, bitmap);

        screen_x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        screen_y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = width;
        bi.biHeight = -height;  // Top-down
        bi.biPlanes = 1;
        bi.biBitCount = 24;
        bi.biCompression = BI_RGB;
        bi.biSizeImage = 0;

        buffer.resize(width * height * 3);
    }

    cv::Mat capture() {
        BitBlt(capture_dc, 0, 0, width, height, 
               desktop_dc, screen_x, screen_y, SRCCOPY);

        GetDIBits(capture_dc, bitmap, 0, height, buffer.data(),
                  (BITMAPINFO*)&bi, DIB_RGB_COLORS);

        return cv::Mat(height, width, CV_8UC3, buffer.data()).clone();
    }

    ~ScreenCapture() {
        DeleteObject(bitmap);
        DeleteDC(capture_dc);
        ReleaseDC(desktop_hwnd, desktop_dc);
    }
};

// 视觉化类
class Visualizer {
private:
    std::vector<cv::Scalar> colors;
    const double font_scale = 0.5;
    const int thickness = 2;

public:
    Visualizer() {
        cv::RNG rng(12345);
        for (int i = 0; i < 10; i++) {
            colors.push_back(cv::Scalar(
                rng.uniform(0, 255),
                rng.uniform(0, 255),
                rng.uniform(0, 255)
            ));
        }
    }

    cv::Mat draw_detections(const cv::Mat& frame, 
                          const std::vector<DetectionBox>& detections) 
    {
        cv::Mat display_frame = frame.clone();
        
        
        for (size_t i = 0; i < detections.size(); i++) {
            const auto& det = detections[i];
            cv::Scalar color = colors[i % colors.size()];
            
            
            // 画框 (继续)
            cv::rectangle(display_frame, 
                         cv::Point(det.x1, det.y1), 
                         cv::Point(det.x2, det.y2), 
                         color, thickness);
            
            // 显示置信度
            std::string label = cv::format("%.2f", det.confidence);
            int baseline;
            cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                                font_scale, thickness, &baseline);
            
            cv::rectangle(display_frame,
                         cv::Point(det.x1, det.y1 - text_size.height - 5),
                         cv::Point(det.x1 + text_size.width, det.y1),
                         color, -1);
                         
            cv::putText(display_frame, label,
                       cv::Point(det.x1, det.y1 - 5),
                       cv::FONT_HERSHEY_SIMPLEX, font_scale,
                       cv::Scalar(255, 255, 255), thickness);
        }
        
        return display_frame;
    }
};

int main() {
    try {
        if (AllocConsole()) {
            FILE* fp;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONIN$", "r", stdin);
            freopen_s(&fp, "CONERR$", "w", stderr);
        }

        std::cout << "Initializing object detection system...\n";
        
        const int capture_width = 500;
        const int capture_height = 500;
        
        std::unique_ptr<ScreenCapture> screen_cap;
        std::unique_ptr<ObjectDetector> detector;
        std::unique_ptr<Visualizer> visualizer;
        
        try {
            screen_cap = std::make_unique<ScreenCapture>(capture_width, capture_height);
            detector = std::make_unique<ObjectDetector>("3w.onnx");
            visualizer = std::make_unique<Visualizer>();
        }
        catch (const std::exception& e) {
            std::cerr << "Initialization error: " << e.what() << std::endl;
            return 1;
        }

        std::cout << "Starting detection loop...\n";
        std::deque<double> fps_queue;
        auto last_fps_print = std::chrono::steady_clock::now();
        bool running = true;
        
        while (running) {
            auto loop_start = std::chrono::steady_clock::now();
            
            cv::Mat frame = screen_cap->capture();
            if (frame.empty()) {
                std::cerr << "Failed to capture screen\n";
                continue;
            }
            
            // 执行检测
            auto detections = detector->detect(frame);
            
            cv::Mat display_frame = visualizer->draw_detections(frame, detections);
            cv::imshow("Object Detection", display_frame);
            
            int key = cv::waitKey(1);
            if (key == 'q' || key == 27) {  // 'q' or ESC
                running = false;
                break;
            }
            
            // FPS计算和控制
            auto loop_end = std::chrono::steady_clock::now();
            double loop_time = std::chrono::duration<double>(loop_end - loop_start).count();
            double fps = 1.0 / loop_time;
            
            fps_queue.push_back(fps);
            if (fps_queue.size() > 30) fps_queue.pop_front();
            
            auto current_time = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(current_time - last_fps_print).count() >= 5.0) {
                double avg_fps = std::accumulate(fps_queue.begin(), fps_queue.end(), 0.0) / fps_queue.size();
                std::cout << "Average FPS: " << std::fixed << std::setprecision(2) 
                         << avg_fps << std::endl;
                last_fps_print = current_time;
            }
            
            // 帧率控制
            const double target_fps = 40.0;
            double remaining = (1.0/target_fps) - loop_time;
            if (remaining > 0) {
                std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
            }
        }
        
        cv::destroyAllWindows();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}