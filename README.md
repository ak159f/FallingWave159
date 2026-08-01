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

- F or F11 : Toggle Borderless Fullscreen (Alt-Tab friendly!)

- Mouse Hover : Display specific Hz and dB under the cursor


<img width="1278" height="713" alt="image" src="https://github.com/user-attachments/assets/82455b43-390f-455c-b650-e6367cca1a94" />
<img width="1272" height="712" alt="image" src="https://github.com/user-attachments/assets/c731cd33-de79-44cf-bc7f-af29e69e0dde" />

<img width="1392" height="746" alt="image" src="https://github.com/user-attachments/assets/b4ddce08-e619-4f1b-b3fd-b31cb3404e5c" />
<img width="1401" height="753" alt="image" src="https://github.com/user-attachments/assets/3e950e95-7ece-42cb-a953-1530c609bd26" />



### If you want to compile the project yourself, it is incredibly easy:

Ensure you have g++ (MinGW) installed on Windows.

Ensure you have the raylib include/lib folders and miniaudio.h in your project directory.

Double-click the included build.cmd file. (or use your own tools)

The script will automatically link everything statically, embed the 159hz.ico icon, and output a fully portable FallingWave159.exe.

Built with C++, Raylib, and Miniaudio.
