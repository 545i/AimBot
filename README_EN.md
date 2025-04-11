# AimBot

AimBot is a computer vision-based screen capture and target detection system, primarily designed for gaming assistance and automation.

## Main Features

- High-performance screen capture
- Real-time target detection
- Intelligent aiming system
- Multi-language support
- Security protection mechanism

## Technical Features

- Image processing using OpenCV
- Efficient screen capture based on DXGI
- Smooth aiming with PID controller
- Multi-threaded processing for improved performance
- Memory mapping optimization for data transfer

## System Requirements

- Windows 10 or later
- DirectX 11 or later
- OpenCV 4.x
- CMake 3.10 or later

## Build Instructions

1. Clone the repository
```bash
git clone https://github.com/545i/AimBot.git
cd AimBot
```

2. Configure CMake
```bash
cmake .. -DUSE_DML=ON -DCMAKE_BUILD_TYPE=Release
```

3. Build the project
```bash
cmake --build . --config Release
```

## Project Structure

```
AimBot/
├── src/
│   ├── main.cpp              # Main program entry
│   ├── ScreenCapture.cpp     # Screen capture implementation
│   ├── SecurityProtection.cpp # Security protection mechanism
│   ├── SystemUtils.cpp       # System utility functions
│   └── ...
├── include/
│   ├── ScreenCapture.h       # Screen capture interface
│   ├── SecurityUtils.h       # Security utility interface
│   ├── SystemUtils.h         # System utility interface
│   └── ...
└── README.md
```

## Important Notes

- This project is for learning and research purposes only
- Please comply with relevant laws and regulations
- Ensure you understand the associated risks before use

## License

[AGPL-3.0 License](LICENSE) 