import numpy as np
import cv2
import mss
import onnxruntime as ort
import time
from typing import List, Tuple, Optional, Dict
import json
from pathlib import Path
from multiprocessing import shared_memory
import traceback
from dataclasses import dataclass
import threading
from queue import Queue
import os
from collections import deque

@dataclass
class DetectionBox:
    x1: int
    y1: int 
    x2: int
    y2: int
    confidence: float

    def to_dict(self) -> dict:
        # 確保所有值都是Python原生類型
        return {
            'x1': int(self.x1),  # 顯式轉換為Python int
            'y1': int(self.y1),
            'x2': int(self.x2),
            'y2': int(self.y2),
            'confidence': float(self.confidence)  # 顯式轉換為Python float
        }

class ObjectDetector:
    def __init__(self, model_path: str):
        self.model_path = model_path
        self.input_width = 640
        self.input_height = 640
        self.initialize_model()
        # 預分配內存給中間結果
        self.resized_array = np.zeros((self.input_height, self.input_width, 3), dtype=np.uint8)
        self.float_array = np.zeros((1, 3, self.input_height, self.input_width), dtype=np.float32)
        
    def initialize_model(self) -> None:
        if not Path(self.model_path).exists():
            raise FileNotFoundError(f"Model file not found: {self.model_path}")
        
        providers = ort.get_available_providers()
        cuda_available = 'CUDAExecutionProvider' in providers
        print(f"CUDA Available: {cuda_available}")
        
        sess_options = ort.SessionOptions()
        if cuda_available:
            sess_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
            sess_options.intra_op_num_threads = 1
            sess_options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
            sess_options.enable_profiling = False
            
            providers = [
                ('CUDAExecutionProvider', {
                    'device_id': 0,
                    'cudnn_conv_algo_search': 'EXHAUSTIVE',
                    'do_copy_in_default_stream': True,
                    'arena_extend_strategy': 'kNextPowerOfTwo',
                }),
                'CPUExecutionProvider'
            ]
        else:
            sess_options.intra_op_num_threads = os.cpu_count()
            sess_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
            sess_options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
            sess_options.enable_profiling = False
            providers = ['CPUExecutionProvider']
        
        try:
            self.session = ort.InferenceSession(
                self.model_path,
                sess_options=sess_options,
                providers=providers
            )
            
            # 輸出模型信息
            print("\n=== 模型信息 ===")
            print(f"模型路徑: {self.model_path}")
            
            # 輸出所有輸入節點信息
            print("\n輸入節點:")
            for i, input_node in enumerate(self.session.get_inputs()):
                print(f"\n輸入節點 {i + 1}:")
                print(f"  名稱: {input_node.name}")
                print(f"  形狀: {input_node.shape}")
                print(f"  類型: {input_node.type}")
            
            # 輸出所有輸出節點信息
            print("\n輸出節點:")
            for i, output_node in enumerate(self.session.get_outputs()):
                print(f"\n輸出節點 {i + 1}:")
                print(f"  名稱: {output_node.name}")
                print(f"  形狀: {output_node.shape}")
                print(f"  類型: {output_node.type}")
            
            # 獲取第一個輸入輸出節點的名稱
            self.input_name = self.session.get_inputs()[0].name
            self.output_name = self.session.get_outputs()[0].name
            
        except Exception as e:
            print(f"Error initializing model: {e}")
            raise

    def preprocess(self, image: np.ndarray) -> np.ndarray:
        try:
            # 使用預分配的數組進行resize
            cv2.resize(image, (self.input_width, self.input_height), 
                      dst=self.resized_array, interpolation=cv2.INTER_LINEAR)
            
            # 優化的色彩空間轉換和正規化
            cv2.cvtColor(self.resized_array, cv2.COLOR_BGR2RGB, dst=self.resized_array)
            np.multiply(self.resized_array, 1/255.0, out=self.float_array[0].transpose(1,2,0))
            
            return self.float_array
            
        except Exception as e:
            print(f"Error in preprocessing: {e}")
            raise

    def detect(self, image: np.ndarray, conf_threshold: float = 0.5) -> List[DetectionBox]:
        try:
            input_tensor = self.preprocess(image)
            outputs = self.session.run([self.output_name], {self.input_name: input_tensor})
            return self.postprocess(outputs[0], image.shape[:2], conf_threshold)
            
        except Exception as e:
            print(f"Error in detection: {e}")
            return []

    def postprocess(self, output: np.ndarray, image_shape: Tuple[int, int],
                   conf_threshold: float = 0.5) -> List[DetectionBox]:
        image_height, image_width = image_shape
        scale_x = image_width / self.input_width
        scale_y = image_height / self.input_height
        
        output = output.transpose((0, 2, 1))
        scores = output[0, :, 4]
        
        # 使用NumPy的向量化操作
        mask = scores > conf_threshold
        
        if not mask.any():
            return []
        
        filtered_output = output[0, mask]
        scores = scores[mask]
        
        x_centers = filtered_output[:, 0]
        y_centers = filtered_output[:, 1]
        widths = filtered_output[:, 2]
        heights = filtered_output[:, 3]
        
        # 一次性計算所有坐標
        x1s = ((x_centers - widths/2) * scale_x).astype(np.int32)
        y1s = ((y_centers - heights/2) * scale_y).astype(np.int32)
        x2s = ((x_centers + widths/2) * scale_x).astype(np.int32)
        y2s = ((y_centers + heights/2) * scale_y).astype(np.int32)
        
        # 使用向量化的clip操作
        np.clip(x1s, 0, image_width - 1, out=x1s)
        np.clip(y1s, 0, image_height - 1, out=y1s)
        np.clip(x2s, 0, image_width - 1, out=x2s)
        np.clip(y2s, 0, image_height - 1, out=y2s)
        
        valid_mask = (x2s > x1s) & (y2s > y1s)
        x1s = x1s[valid_mask]
        y1s = y1s[valid_mask]
        x2s = x2s[valid_mask]
        y2s = y2s[valid_mask]
        scores = scores[valid_mask]
        
        # 轉換為 DetectionBox 列表
        return [
            DetectionBox(int(x1), int(y1), int(x2), int(y2), float(conf))
            for x1, y1, x2, y2, conf in zip(x1s, y1s, x2s, y2s, scores)
        ]

class ThreadSafeScreenCapture:
    def __init__(self, monitor: Dict[str, int]):
        self.monitor = monitor
        self._thread_local = threading.local()
        self.lock = threading.Lock()
        # 預分配緩衝區
        self.frame_buffer = np.zeros((monitor['height'], monitor['width'], 4), dtype=np.uint8)
        self.bgr_buffer = np.zeros((monitor['height'], monitor['width'], 3), dtype=np.uint8)
        
    def _ensure_mss_instance(self):
        if not hasattr(self._thread_local, 'sct'):
            self._thread_local.sct = mss.mss()
    
    def capture(self) -> np.ndarray:
        try:
            self._ensure_mss_instance()
            with self.lock:
                screenshot = self._thread_local.sct.grab(self.monitor)
                # 直接使用預分配的緩衝區
                np.copyto(self.frame_buffer, np.array(screenshot, dtype=np.uint8))
                cv2.cvtColor(self.frame_buffer, cv2.COLOR_BGRA2BGR, dst=self.bgr_buffer)
                return self.bgr_buffer
        except Exception as e:
            print(f"Screen capture error: {e}")
            traceback.print_exc()
            return self.bgr_buffer  # 返回上一幀而不是空幀

    def close(self):
        if hasattr(self._thread_local, 'sct'):
            self._thread_local.sct.close()

class DetectionWorker:
    def __init__(self, model_path: str, screen_cap: ThreadSafeScreenCapture):
        self.detector = ObjectDetector(model_path)
        self.screen_cap = screen_cap
        self.running = True
        self.fps_queue = deque(maxlen=30)
        self.last_fps_print = time.time()
        self.visualizer = Visualizer()
        
    def run(self):
        print("Starting detection worker...")
        error_count = 0
        max_errors = 3
        
        while self.running and error_count < max_errors:
            try:
                loop_start = time.perf_counter()
                
                frame = self.screen_cap.capture()
                if frame is None or frame.size == 0:
                    raise ValueError("Invalid frame captured")
                
                detections = self.detector.detect(frame)
                
                # 添加視覺化
                display_frame = self.visualizer.draw_detections(frame, detections)
                cv2.imshow('Object Detection', display_frame)
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break
                
                loop_time = time.perf_counter() - loop_start
                fps = 1 / loop_time if loop_time > 0 else 0
                self.fps_queue.append(fps)
                current_fps = float(sum(self.fps_queue) / len(self.fps_queue))
                
                current_time = time.time()
                if current_time - self.last_fps_print >= 5:
                    print(f"Current FPS: {current_fps:.2f}")
                    self.last_fps_print = current_time
                
                # 動態幀率控制
                target_fps = 40  # 目標FPS
                if current_fps < target_fps:
                    time.sleep(0.001)  # 最小延遲
                else:
                    remaining = 1/target_fps - (time.perf_counter() - loop_start)
                    if remaining > 0:
                        time.sleep(remaining)
                
                error_count = 0
                
            except Exception as e:
                error_count += 1
                print(f"Error in detection loop ({error_count}/{max_errors}): {e}")
                traceback.print_exc()
                time.sleep(1)
        
        cv2.destroyAllWindows()
        
        if error_count >= max_errors:
            print("Too many errors occurred. Stopping detection worker.")
    
    def stop(self):
        self.running = False
        cv2.destroyAllWindows()

class Visualizer:
    def __init__(self):
        self.colors = np.random.randint(0, 255, size=(10, 3), dtype=np.uint8)
        self.font = cv2.FONT_HERSHEY_SIMPLEX
        self.font_scale = 0.5
        self.thickness = 2

    def draw_detections(self, frame: np.ndarray, detections: List[DetectionBox]) -> np.ndarray:
        display_frame = frame.copy()
        for i, det in enumerate(detections):
            color = tuple(map(int, self.colors[i % len(self.colors)]))
            
            # 畫框
            cv2.rectangle(display_frame, 
                         (det.x1, det.y1), 
                         (det.x2, det.y2), 
                         color, 
                         self.thickness)
            
            # 顯示置信度
            label = f"{det.confidence:.2f}"
            (label_w, label_h), _ = cv2.getTextSize(label, 
                                                   self.font, 
                                                   self.font_scale, 
                                                   self.thickness)
            cv2.rectangle(display_frame, 
                         (det.x1, det.y1 - label_h - 5),
                         (det.x1 + label_w, det.y1), 
                         color, 
                         -1)
            cv2.putText(display_frame, 
                       label,
                       (det.x1, det.y1 - 5), 
                       self.font, 
                       self.font_scale, 
                       (255, 255, 255), 
                       self.thickness)
        
        return display_frame

def main():
    try:
        print("Initializing object detection system...")
        screen_cap = None
        
        try:
            # 設置屏幕捕獲區域
            with mss.mss() as sct:
                screen_width = sct.monitors[1]['width']
                screen_height = sct.monitors[1]['height']
            
            monitor = {
                'top': (screen_height // 2) - 150,
                'left': (screen_width // 2) - 150,
                'width': 300,
                'height': 300
            }
            
            print("Initializing screen capture...")
            screen_cap = ThreadSafeScreenCapture(monitor)
            
            print("Loading model...")
            model_path = "3w.onnx"
            worker = DetectionWorker(model_path, screen_cap)
            
            print("Starting detection thread...")
            detection_thread = threading.Thread(target=worker.run)
            detection_thread.start()
            
            try:
                detection_thread.join()
            except KeyboardInterrupt:
                print("\nStopping detection...")
                worker.stop()
                detection_thread.join(timeout=5.0)
                if detection_thread.is_alive():
                    print("Warning: Detection thread did not stop gracefully")
                
        except Exception as e:
            print(f"Error during execution: {e}")
            traceback.print_exc()
            
    except Exception as e:
        print(f"Fatal error: {e}")
        traceback.print_exc()
        
    finally:
        print("Cleaning up resources...")
        if screen_cap:
            screen_cap.close()

if __name__ == "__main__":
    main()