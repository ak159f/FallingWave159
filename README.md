# FallingWave159

An ultra-fast, zero-latency audio spectrum and waterfall analyzer built for real-time transient analysis and deep frequency resolution.
Built in C++ with Raylib and Miniaudio, this tool captures desktop audio directly via WASAPI loopback, rendering a mathematically accurate 900-band logarithmic spectrum from 20 Hz to 20,000 Hz.

# Key Features

- **Zero-Latency Capture: :** Uses WASAPI loopback to analyze whatever you are currently listening to on your PC.


- **Dynamic UI Scaling: :** Resize the window freely; fonts and margins automatically calculate precise spatial ratios to maintain a beautiful layout.

- **Interactive Studio Crosshair: :** Hover anywhere on the graph to instantly see the exact Frequency (Hz) and Volume (dB) of that exact pixel.

- **Persistent Memory: :** Automatically saves your window position, size, UI state, and speed to your Documents folder. It opens exactly how you left it.

- **Portable: :** Compiles into a single, standalone .exe file with an embedded icon. No external DLLs or image files required!

# Controls

- Numbers : Swap Engine Modes

- UP / DOWN : Increase or decrease the Waterfall scrolling speed

- SPACE : Freeze/Pause the audio capture for static analysis

- TAB : Settings menu

- F or F11 : Toggle Borderless mode

- Mouse Hover : Display specific Hz and dB under the cursor

<img width="1919" height="1041" alt="image" src="https://github.com/user-attachments/assets/32c24934-a5ac-44b4-ab17-de3b9ad58b87" />

<img width="1919" height="1039" alt="image" src="https://github.com/user-attachments/assets/fbabd7b8-d12c-4613-beb4-13bc2b383745" />

<img width="1919" height="1053" alt="image" src="https://github.com/user-attachments/assets/16606a6a-72ac-4780-affd-84f1215b7b2d" />

<img width="1919" height="1079" alt="image" src="https://github.com/user-attachments/assets/ecaffb9f-7efe-48fd-9ca2-c119ef692396" />


### 159 hz 0db

Zero-padding 65536

<img width="1919" height="1052" alt="image" src="https://github.com/user-attachments/assets/f4f052c7-90a0-4de7-9f7d-031863e74424" />

Long-window 65536

<img width="1919" height="1055" alt="image" src="https://github.com/user-attachments/assets/760f1732-f49a-478c-bb0f-591e2a0302e2" />



### If you want to compile the project yourself, follow these requirements

- Ensure you have g++ (MinGW) installed on Windows.

- Ensure you have the raylib include/lib folders and miniaudio.h in your project directory.

- Double-click the included build.cmd file. (or use your own tools)

The script will automatically link everything statically, embed the 159hz.ico icon, and output a fully portable FallingWave159.exe.

Built with C++, Raylib, and Miniaudio.

Note : Because build errors usually depend on your specific environment, troubleshooting can vary widely. However, I have provided a guide below for the most common compiler setup issue.

### If you are building from source, it is assumed you are comfortable configuring your Windows environment.

If you need to install MinGW, follow these steps:

- Go to https://winlibs.com/

- Choose the latest UCRT 64-bit version.

<img width="1120" height="780" alt="image" src="https://github.com/user-attachments/assets/be7e16f0-8ca4-4b2c-b146-129e3b3742e6" />

### how to install 

After extracting the downloaded file, move the mingw64 folder directly into your C:\ drive.

<img width="1365" height="685" alt="image" src="https://github.com/user-attachments/assets/021589b1-e605-45be-84d4-2a18b3622c9b" />

Next, you need to add it to your system path. Open Environment Variables in Windows:

<img width="763" height="425" alt="image" src="https://github.com/user-attachments/assets/29ff9094-3fcb-4f5e-a9d4-ea0bf0c1ad85" />

<img width="416" height="480" alt="image" src="https://github.com/user-attachments/assets/fd13f816-4d13-41d8-8ae9-03cb1912c9a5" />

Add this exact path to your Path variable: C:\mingw64\bin

<img width="618" height="582" alt="image" src="https://github.com/user-attachments/assets/de1eb22b-faba-4724-8f7d-3633a8c1b677" />

<img width="516" height="496" alt="image" src="https://github.com/user-attachments/assets/9d1e5bbb-1cc0-4e20-9e75-c033150590a4" />

C:\mingw64\bin

Hit OK to save your changes. You should now be able to run build.cmd successfully to compile the project.
