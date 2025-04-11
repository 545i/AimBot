# ColorBot

ColorBot是一個基於計算機視覺的螢幕捕獲和目標檢測系統，主要用於遊戲輔助和自動化操作。

## 主要功能

- 高性能螢幕捕獲
- 即時目標檢測
- 智慧瞄準系統
- 多語言支持
- 安全保護機制

## 技術特點

- 使用OpenCV進行圖像處理
- 基於DXGI的高效螢幕捕獲
- PID控制器實現平滑瞄準
- 多執行緒處理提高性能
- 記憶體映射最佳化數據傳輸

## 系統要求

- Windows 10或更高版本
- DirectX 11或更高版本
- OpenCV 4.x
- CMake 3.10或更高版本

## 構建說明

1. 複製倉庫
```bash
git clone https://github.com/yourusername/ColorBot.git
cd ColorBot
```

2. 配置CMake
```bash
cmake .. -DUSE_DML=ON -DCMAKE_BUILD_TYPE=Release
```

3. 構建項目
```bash
cmake --build . --config Release
```

## 項目結構

```
ColorBot/
├── src/
│   ├── main.cpp              # 主程式入口
│   ├── ScreenCapture.cpp     # 螢幕捕獲實現
│   ├── SecurityProtection.cpp # 安全保護機制
│   ├── SystemUtils.cpp       # 系統工具函數
│   └── ...
├── include/
│   ├── ScreenCapture.h       # 螢幕捕獲介面
│   ├── SecurityUtils.h       # 安全工具介面
│   ├── SystemUtils.h         # 系統工具介面
│   └── ...
└── README.md
```

## 注意事項

- 本項目僅供學習和研究使用
- 請遵守相關法律法規
- 使用前請確保了解相關風險

## 許可證

[AGPL License](LICENSE) 
