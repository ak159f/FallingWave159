# FallingWave159

An ultra-fast, zero-latency audio spectrum and waterfall analyzer built for real-time transient analysis and deep frequency resolution.
Built in C++ with Raylib and Miniaudio, this tool captures desktop audio directly via WASAPI loopback, rendering a mathematically accurate 900-band logarithmic spectrum from 20 Hz to 20,000 Hz.

# Key Features

- **Zero-Latency Capture: :** Uses WASAPI loopback to analyze whatever you are currently listening to on your PC.

- **Hot-Swappable Math Modes: :** Instantly switch between high-resolution mastering modes and ultra-fast transient modes (perfect for breakcore and drum & bass).

- **Dynamic UI Scaling: :** Resize the window freely; fonts and margins automatically calculate precise spatial ratios to maintain a beautiful layout.

- **Interactive Studio Crosshair: :** Hover anywhere on the graph to instantly see the exact Frequency (Hz) and Volume (dB) of that exact pixel.

- **Persistent Memory: :** Automatically saves your window position, size, UI state, and speed to your Documents folder. It opens exactly how you left it.

- **Portable: :** Compiles into a single, standalone .exe file with an embedded icon. No external DLLs or image files required!

# Controls

- Numbers : Swap between the 4 Engine Modes

- UP / DOWN : Increase or decrease the Waterfall scrolling speed

- SPACE : Freeze/Pause the audio capture for static analysis

- TAB : Toggle UI text visibility (Crosshair remains active)

- F or F11 : Toggle Borderless Fullscreen

- Mouse Hover : Display specific Hz and dB under the cursor


<img width="1151" height="604" alt="image" src="https://github.com/user-attachments/assets/57c7bef1-d908-47de-99a0-00f52ded340d" />
<img width="1154" height="601" alt="image" src="https://github.com/user-attachments/assets/261f8b57-7f2c-4903-961d-08cd9fb7f02c" />

### 159 hz 0db
[1] NormalMode 16384

<img width="1148" height="610" alt="image" src="https://github.com/user-attachments/assets/977e2871-998a-4e08-9230-45d7aeaacbaa" />
[2] HighSpeedMode 4096

<img width="1141" height="601" alt="image" src="https://github.com/user-attachments/assets/c7c9a5b0-0ff0-49a3-bec9-57f886785abb" />
[3] NormalMode 32768

<img width="1148" height="602" alt="image" src="https://github.com/user-attachments/assets/289516c0-80e7-40b8-ac0f-e9ef0b9d8436" />
[4] HighSpeedMode 8192
<img width="1144" height="600" alt="image" src="https://github.com/user-attachments/assets/665c1ea3-9c86-4ebb-a241-f6af28ea3f47" />



### If you want to compile the project yourself

Ensure you have g++ (MinGW) installed on Windows.

Ensure you have the raylib include/lib folders and miniaudio.h in your project directory.

Double-click the included build.cmd file. (or use your own tools)

The script will automatically link everything statically, embed the 159hz.ico icon, and output a fully portable FallingWave159.exe.

Built with C++, Raylib, and Miniaudio.

### winlib guide
https://winlibs.com/
- chose lastest UCRT 64 bit
### what to download

<img width="1120" height="780" alt="image" src="https://github.com/user-attachments/assets/be7e16f0-8ca4-4b2c-b146-129e3b3742e6" />

### how to install 

follow this order

<img width="1365" height="685" alt="image" src="https://github.com/user-attachments/assets/021589b1-e605-45be-84d4-2a18b3622c9b" />

open Environment Variables

<img width="763" height="425" alt="image" src="https://github.com/user-attachments/assets/29ff9094-3fcb-4f5e-a9d4-ea0bf0c1ad85" />

<img width="416" height="480" alt="image" src="https://github.com/user-attachments/assets/fd13f816-4d13-41d8-8ae9-03cb1912c9a5" />

<img width="618" height="582" alt="image" src="https://github.com/user-attachments/assets/de1eb22b-faba-4724-8f7d-3633a8c1b677" />

<img width="516" height="496" alt="image" src="https://github.com/user-attachments/assets/9d1e5bbb-1cc0-4e20-9e75-c033150590a4" />

C:\mingw64\bin

then hit ok and you should able to use build.cmd now
