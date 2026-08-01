// 1. Prevent Windows API collisions
#define Rectangle WinRectangle
#define CloseWindow WinCloseWindow
#define ShowCursor WinShowCursor

// 2. Audio Backend
#define MINIAUDIO_IMPLEMENTATION
#define MA_API static
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include "miniaudio.h"

// 3. Clear macros before Raylib
#undef Rectangle
#undef CloseWindow
#undef ShowCursor
#undef PlaySound
#undef DrawText
#undef DrawTextEx
#undef LoadImage

// 4. Raylib & Standard Libs
#include "raylib.h"
#include <iostream>
#include <vector>
#include <complex>
#include <cmath>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <fstream> 
#include <string>  
#include <cstdlib> 

namespace Config {
    const int WindowWidth = 1820;
    const int WindowHeight = 980;
    
    // Audio Processing - DOUBLED ENGINE SPECS
    const int SampleRate = 176400; 
    const int MaxFFTSize = 32768; // Used to reserve maximum memory for Mode 3 & 4
    
    // Visuals 
    const int DotCount = 900; 
    const float MinFreq = 20.0f;
    const float MaxFreq = 20000.0f;
    const float MinDB = -130.0f; 
    const float MaxDB = 0.0f; 
}

Color GetWaterfallColor(float val) {
    val = std::clamp(val, 0.0f, 1.0f);
    
    if (val < 0.25f) {
        float t = val / 0.25f;
        return (Color){ (unsigned char)(20 + t*60), (unsigned char)(20 + t*20), (unsigned char)(60 + t*70), 255 };
    } else if (val < 0.50f) {
        float t = (val - 0.25f) / 0.25f;
        return (Color){ (unsigned char)(80 + t*120), (unsigned char)(40 + t*40), (unsigned char)(130 + t*30), 255 };
    } else if (val < 0.75f) {
        float t = (val - 0.50f) / 0.25f;
        return (Color){ (unsigned char)(200 + t*55), (unsigned char)(80 + t*100), (unsigned char)(160 - t*60), 255 };
    } else {
        float t = (val - 0.75f) / 0.25f;
        return (Color){ 255, (unsigned char)(180 + t*75), (unsigned char)(100 + t*120), 255 };
    }
}

class MathEngine {
public:
    static void ComputeInPlaceFFT(std::vector<std::complex<float>>& buffer) {
        int n = buffer.size(); // Dynamically adapts to 16384 or 32768
        for (int i = 1, j = 0; i < n; i++) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(buffer[i], buffer[j]);
        }
        for (int len = 2; len <= n; len <<= 1) {
            float angle = -2.0f * PI / len;
            std::complex<float> wlen(std::cos(angle), std::sin(angle));
            for (int i = 0; i < n; i += len) {
                std::complex<float> w(1.0f, 0.0f);
                for (int j = 0; j < len / 2; j++) {
                    std::complex<float> u = buffer[i + j];
                    std::complex<float> v = buffer[i + j + len / 2] * w;
                    buffer[i + j] = u + v;
                    buffer[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
    }
};

class AudioCapture {
private:
    ma_device device;
    std::mutex mtx;
    std::vector<float> circularBuffer;
    int writeHead;

    static void Callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        if (!pInput) return;
        AudioCapture* instance = (AudioCapture*)pDevice->pUserData;
        const float* fInput = (const float*)pInput;
        int channels = pDevice->capture.channels;

        std::lock_guard<std::mutex> lock(instance->mtx);
        for (ma_uint32 i = 0; i < frameCount; ++i) {
            float mono = 0.0f;
            for (int c = 0; c < channels; ++c) {
                mono += fInput[i * channels + c];
            }
            mono /= channels;
            instance->circularBuffer[instance->writeHead] = mono;
            instance->writeHead = (instance->writeHead + 1) % Config::MaxFFTSize;
        }
    }

public:
    AudioCapture() : circularBuffer(Config::MaxFFTSize, 0.0f), writeHead(0) {}

    bool Start() {
        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_loopback);
        deviceConfig.capture.pDeviceID = NULL;
        deviceConfig.capture.format = ma_format_f32;
        deviceConfig.capture.channels = 2;
        deviceConfig.sampleRate = Config::SampleRate;
        deviceConfig.dataCallback = Callback;
        deviceConfig.pUserData = this;
        
        deviceConfig.periodSizeInMilliseconds = 5; 

        if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) return false;
        return ma_device_start(&device) == MA_SUCCESS;
    }

    void Stop() { ma_device_uninit(&device); }

    void GetLatestFrames(std::vector<float>& outBuffer, int framesToRead) {
        std::lock_guard<std::mutex> lock(mtx);
        // Calculate the exact offset to grab only the needed amount of history
        int offset = (writeHead - framesToRead + Config::MaxFFTSize) % Config::MaxFFTSize;
        for (int i = 0; i < framesToRead; ++i) {
            outBuffer[i] = circularBuffer[(offset + i) % Config::MaxFFTSize];
        }
    }
};

class FrequencyExtractor {
private:
    std::vector<float> audioFrames;
    std::vector<std::complex<float>> fftBuffer;
    std::vector<float> rawMagnitudes;

public:
    FrequencyExtractor() {
        // Pre-allocate the maximum possible memory sizes
        audioFrames.reserve(Config::MaxFFTSize);
        fftBuffer.reserve(Config::MaxFFTSize);
        rawMagnitudes.reserve(Config::MaxFFTSize / 2);
    }

    void ProcessNewAudio(AudioCapture& audio, int currentMode) {
        // 1. Determine sizes based on the 4 new modes
        int currentFFTSize = (currentMode >= 2) ? 32768 : 16384;
        int currentCaptureSize = (currentMode == 1) ? 4096 : ((currentMode == 3) ? 8192 : currentFFTSize);
        bool isHighSpeed = (currentMode == 1 || currentMode == 3);

        // Resize the active vectors (Extremely fast, no reallocation because we reserved memory)
        audioFrames.resize(currentFFTSize);
        fftBuffer.resize(currentFFTSize);
        rawMagnitudes.resize(currentFFTSize / 2);

        audio.GetLatestFrames(audioFrames, currentFFTSize);
        
        if (!isHighSpeed) { 
            // Normal Mode (Asymmetric Window)
            int peakIndex = currentFFTSize - (currentFFTSize / 8); 
            
            for (int i = 0; i < currentFFTSize; ++i) {
                float window = 0.0f;
                if (i < peakIndex) {
                    window = 0.5f * (1.0f - std::cos(PI * i / peakIndex));
                } else {
                    window = 0.5f * (1.0f + std::cos(PI * (i - peakIndex) / (currentFFTSize - peakIndex)));
                }
                fftBuffer[i] = std::complex<float>(audioFrames[i] * window, 0.0f);
            }
        } else { 
            // High Speed Mode (Zero-Padding)
            int paddingOffset = currentFFTSize - currentCaptureSize;
            for (int i = 0; i < currentFFTSize; ++i) {
                if (i >= paddingOffset) {
                    float t = (float)(i - paddingOffset) / (currentCaptureSize - 1.0f);
                    float window = 0.5f * (1.0f - std::cos(2.0f * PI * t));
                    fftBuffer[i] = std::complex<float>(audioFrames[i] * window, 0.0f);
                } else {
                    fftBuffer[i] = std::complex<float>(0.0f, 0.0f); 
                }
            }
        }
        
        MathEngine::ComputeInPlaceFFT(fftBuffer);
        
        float activeSamples = isHighSpeed ? (float)currentCaptureSize : (float)currentFFTSize;
        
        for (int i = 0; i < currentFFTSize / 2; ++i) {
            float re = fftBuffer[i].real();
            float im = fftBuffer[i].imag();
            rawMagnitudes[i] = (std::sqrt(re * re + im * im) / (activeSamples * 0.5f)) * 2.0f;
        }
    }

    void ExtractXY(std::vector<float>& outHeights, std::vector<float>& outFreqs, int currentMode) {
        int currentFFTSize = (currentMode >= 2) ? 32768 : 16384;
        
        float nyquist = Config::SampleRate / 2.0f;
        float logMin = std::log10(Config::MinFreq);
        float logMax = std::log10(Config::MaxFreq);

        for (int i = 0; i < Config::DotCount; ++i) {
            float t1 = i / (float)Config::DotCount;
            float t2 = (i + 1) / (float)Config::DotCount;
            
            float freqStart = std::pow(10.0f, logMin + t1 * (logMax - logMin));
            float freqEnd = std::pow(10.0f, logMin + t2 * (logMax - logMin));
            outFreqs[i] = (freqStart + freqEnd) * 0.5f;

            float exactStartBin = (freqStart / nyquist) * (currentFFTSize / 2);
            float exactEndBin = (freqEnd / nyquist) * (currentFFTSize / 2);
            
            float peakMag = 0.0f;
            int bStart = std::max(0, (int)exactStartBin);
            int bEnd = std::min((currentFFTSize / 2) - 1, (int)exactEndBin);

            if (bStart >= bEnd) {
                int centerBin = ((freqStart + freqEnd) * 0.5f / nyquist) * (currentFFTSize / 2);
                peakMag = rawMagnitudes[std::max(0, centerBin)];
            } else {
                for (int b = bStart; b <= bEnd; ++b) {
                    if (rawMagnitudes[b] > peakMag) peakMag = rawMagnitudes[b];
                }
            }

            float db = Config::MinDB;
            if (peakMag > 1e-12f) {
                db = 20.0f * std::log10(peakMag);
            }

            float mappedY = (db - Config::MinDB) / (Config::MaxDB - Config::MinDB);
            outHeights[i] = std::clamp(mappedY, 0.0f, 1.0f);
        }
    }
};

class Renderer {
private:
    Image waterfallImg;
    Texture2D waterfallTex;
    int waterfallHeight;

public:
    Renderer() {
        waterfallHeight = 400; 
        waterfallImg = GenImageColor(Config::DotCount, waterfallHeight, BLACK);
        waterfallTex = LoadTextureFromImage(waterfallImg);
    }

    ~Renderer() {
        UnloadTexture(waterfallTex);
        UnloadImage(waterfallImg);
    }

    void UpdateAndDraw(const std::vector<float>& yHeights, const std::vector<float>& xFreqs, int currentMode, int waterfallSpeed, bool isPaused, bool showUI) {
        int w = GetScreenWidth();
        int h = GetScreenHeight();
        
        float scaleX = (float)w / Config::WindowWidth;
        float scaleY = (float)h / Config::WindowHeight;
        float avgScale = (scaleX + scaleY) * 0.5f;

        int dbFontSize = std::max(10, (int)(20.0f * scaleY));  
        int freqFontSize = std::max(8, (int)(20.0f * scaleX)); 
        
        int uiFontSize = std::max(12, (int)(22.0f * avgScale)); 
        int crosshairFontSize = std::max(14, (int)(28.0f * avgScale));
        
        int leftMargin = MeasureText("-130", dbFontSize) + 15;  
        int topMargin = uiFontSize + 10;   
        int textGap = freqFontSize + 15;   
        
        float graphH = h * 0.35f;
        float waterH = h - (topMargin + graphH + textGap);

        if (!isPaused) {
            int shiftRows = waterfallSpeed;
            if (shiftRows > waterfallHeight) shiftRows = waterfallHeight;
            
            Color* pixels = (Color*)waterfallImg.data;
            std::memmove(pixels + (Config::DotCount * shiftRows), 
                         pixels, 
                         sizeof(Color) * Config::DotCount * (waterfallHeight - shiftRows));

            for (int r = 0; r < shiftRows; ++r) {
                for (int i = 0; i < Config::DotCount; ++i) {
                    pixels[r * Config::DotCount + i] = GetWaterfallColor(yHeights[i]);
                }
            }
            UpdateTexture(waterfallTex, waterfallImg.data);
        }

        BeginDrawing();
        ClearBackground((Color){12, 12, 12, 255}); 

        Rectangle src = { 0.0f, 0.0f, (float)Config::DotCount, (float)waterfallHeight };
        Rectangle dest = { (float)leftMargin, topMargin + graphH + textGap, (float)(w - leftMargin), waterH };
        DrawTexturePro(waterfallTex, src, dest, (Vector2){0, 0}, 0.0f, WHITE);

        DrawRectangle(0, 0, leftMargin, h, (Color){8, 8, 8, 255}); 
        DrawRectangle(leftMargin, topMargin + graphH, w - leftMargin, textGap, (Color){8, 8, 8, 255}); 

        Color gridColor = (Color){45, 45, 45, 255};
        Color textColor = (Color){150, 150, 150, 255};

        int dbSteps[] = {0, -25, -50, -75, -100, -125};
        for(int db : dbSteps) {
            float normalizedY = (db - Config::MinDB) / (Config::MaxDB - Config::MinDB);
            float y = topMargin + (graphH - (normalizedY * graphH));
            
            DrawLine(leftMargin, y, w, y, gridColor);
            
            const char* text = TextFormat("%d", db);
            int textW = MeasureText(text, dbFontSize); 
            DrawText(text, leftMargin - textW - 5, y - (dbFontSize/2), dbFontSize, textColor);
        }

        for(int db = 0; db >= -130; db -= 5) { 
            float normalizedY = (db - Config::MinDB) / (Config::MaxDB - Config::MinDB);
            float y = (topMargin + graphH + textGap) + (1.0f - normalizedY) * waterH;
            
            const char* text = TextFormat("%d", db);
            int textW = MeasureText(text, dbFontSize);
            float intensity = std::clamp(normalizedY, 0.0f, 1.0f);
            Color dbColor = GetWaterfallColor(intensity);
            DrawText(text, leftMargin - textW - 5, y - (dbFontSize/2), dbFontSize, dbColor);
        }

        float logMin = std::log10(Config::MinFreq);
        float logMax = std::log10(Config::MaxFreq);
        int freqs[] = {40, 60, 100, 200, 400, 600, 1000, 2000, 4000, 6000, 10000, 20000};
        
        for(int f : freqs) {
            float t = (std::log10((float)f) - logMin) / (logMax - logMin);
            float x = leftMargin + t * (w - leftMargin);
            
            DrawLine(x, topMargin, x, topMargin + graphH, gridColor);
            DrawLine(x, topMargin + graphH + textGap, x, h, (Color){255, 255, 255, 15});
            
            const char* fText = TextFormat("%d", f);
            int textW = MeasureText(fText, freqFontSize); 
            
            float textX = x - (textW / 2.0f);
            if (textX + textW > w) textX = w - textW - 5;
            if (textX < leftMargin + 5) textX = leftMargin + 5; 
            
            DrawText(fText, textX, topMargin + graphH + 3, freqFontSize, textColor);
        }

        float barWidth = (float)(w - leftMargin) / Config::DotCount;
        for (int i = 0; i < Config::DotCount; ++i) {
            float x = leftMargin + ((float)i / Config::DotCount) * (w - leftMargin);
            float height = yHeights[i] * graphH;
            float y = topMargin + (graphH - height);
            
            Color barColor = GetWaterfallColor(yHeights[i]);
            DrawRectangle(x, y, std::max(1.0f, barWidth - 1.0f), std::max(1.0f, height), barColor);
        }

        if (showUI) {
            const char* modeText = "";
            Color modeColor = WHITE;
            if (currentMode == 0) { modeText = "MODE [1]: NormalMode 16384"; modeColor = (Color){200, 200, 200, 200}; }
            else if (currentMode == 1) { modeText = "MODE [2]: HighSpeedMode 4096"; modeColor = (Color){0, 255, 200, 200}; }
            else if (currentMode == 2) { modeText = "MODE [3]: NormalMode 32768"; modeColor = (Color){255, 150, 255, 200}; }
            else if (currentMode == 3) { modeText = "MODE [4]: HighSpeedMode 8192"; modeColor = (Color){255, 255, 100, 200}; }

            DrawText(modeText, leftMargin + 10, topMargin, uiFontSize, modeColor);
            DrawText(TextFormat("SPEED: %d (UP/DOWN)", waterfallSpeed), leftMargin + 10, topMargin + uiFontSize + 5, uiFontSize, (Color){255, 200, 0, 200});
            DrawText("HIDE UI: TAB | FREEZE: SPACE", leftMargin + 10, topMargin + (uiFontSize * 2) + 10, uiFontSize - 2, (Color){100, 150, 255, 180});
            
            // Draw the sleek legend at the exact bottom center
            const char* legendText = "[1] NormalMode 16384   [2] HighSpeedMode 4096   [3] NormalMode 32768   [4] HighSpeedMode 8192";
            int legendFontSize = std::max(11, (int)(16.0f * avgScale));
            int legW = MeasureText(legendText, legendFontSize);
            DrawText(legendText, leftMargin + ((w - leftMargin) - legW) / 2, h - legendFontSize - 10, legendFontSize, (Color){200, 200, 200, 150});
        }

        if (isPaused) {
            const char* pauseText = "PAUSED";
            int pWidth = MeasureText(pauseText, uiFontSize * 2);
            DrawText(pauseText, w - pWidth - 20, topMargin, uiFontSize * 2, (Color){255, 50, 50, 200});
        }

        float mx = GetMouseX();
        float my = GetMouseY();
        if (mx >= leftMargin && mx < w && my >= 0 && my < h) {
            Color crossColor = (Color){0, 255, 200, 200}; 
            DrawLine(mx, 0, mx, h, crossColor);
            
            float hoverFreq = Config::MinFreq * std::pow(10.0f, ((mx - leftMargin) / (w - leftMargin)) * (logMax - logMin));
            DrawText(TextFormat("%.0f Hz", hoverFreq), w - MeasureText("20000 Hz", crosshairFontSize) - 20, 20 + topMargin, crosshairFontSize, crossColor);
            
            if (my >= topMargin && my < topMargin + graphH) {
                DrawLine(leftMargin, my, w, my, crossColor);
                float hoverDB = Config::MinDB + (1.0f - ((my - topMargin) / graphH)) * (Config::MaxDB - Config::MinDB);
                DrawText(TextFormat("%.0f dB", hoverDB), w - MeasureText("20000 Hz", crosshairFontSize) - 20, 20 + topMargin + crosshairFontSize + 5, crosshairFontSize, crossColor);
            }
        }

        EndDrawing();
    }
};

int main() {
    std::string configPath = "fw159_config.txt"; 
    if (const char* userProfile = std::getenv("USERPROFILE")) {
        configPath = std::string(userProfile) + "\\Documents\\fw159_config.txt";
    }

    int winW = Config::WindowWidth;
    int winH = Config::WindowHeight;
    int winX = -9999, winY = -9999;
    int currentMode = 0; 
    int waterfallSpeed = 2; 
    bool showUI = true;

    std::ifstream inFile(configPath);
    if (inFile.is_open()) {
        inFile >> winW >> winH >> winX >> winY >> currentMode >> waterfallSpeed >> showUI;
        inFile.close();
    }

    if (winW < 400) winW = Config::WindowWidth;
    if (winH < 300) winH = Config::WindowHeight;
    if (waterfallSpeed < 1 || waterfallSpeed > 20) waterfallSpeed = 2;
    if (currentMode < 0 || currentMode > 3) currentMode = 0; // Updated failsafe to 3

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(winW, winH, "FallingWave 159");

    if (winX != -9999 && winY != -9999) {
        SetWindowPosition(winX, winY);
    }
    
    HWND hwnd = (HWND)GetWindowHandle();
    HICON hIcon = LoadIcon(GetModuleHandle(NULL), "IDI_ICON1");
    if (hIcon) {
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }

    SetTargetFPS(60);

    AudioCapture audio;
    if (!audio.Start()) {
        std::cerr << "Audio capture failed!" << std::endl;
        return -1;
    }

    FrequencyExtractor extractor;
    Renderer renderer;

    std::vector<float> activeHeights(Config::DotCount, 0.0f);
    std::vector<float> activeFreqs(Config::DotCount, 0.0f);

    bool isFullscreen = false;
    bool isPaused = false;
    
    int windowedW = winW;
    int windowedH = winH;
    int windowedX = winX;
    int windowedY = winY;

    while (!WindowShouldClose()) {
        
        if (IsKeyPressed(KEY_F11) || IsKeyPressed(KEY_F)) {
            int monitor = GetCurrentMonitor();
            if (!isFullscreen) {
                windowedW = GetScreenWidth();
                windowedH = GetScreenHeight();
                Vector2 pos = GetWindowPosition();
                windowedX = (int)pos.x;
                windowedY = (int)pos.y;

                SetWindowState(FLAG_WINDOW_UNDECORATED);
                SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
                Vector2 monPos = GetMonitorPosition(monitor);
                SetWindowPosition((int)monPos.x, (int)monPos.y);
                isFullscreen = true;
            } else {
                ClearWindowState(FLAG_WINDOW_UNDECORATED);
                SetWindowSize(windowedW, windowedH);
                SetWindowPosition(windowedX, windowedY);
                isFullscreen = false;
            }
        }
        
        if (IsKeyPressed(KEY_SPACE)) isPaused = !isPaused;
        if (IsKeyPressed(KEY_TAB)) showUI = !showUI;
        if (IsKeyPressed(KEY_ONE)) currentMode = 0;
        if (IsKeyPressed(KEY_TWO)) currentMode = 1;
        if (IsKeyPressed(KEY_THREE)) currentMode = 2; // New Mode 3
        if (IsKeyPressed(KEY_FOUR)) currentMode = 3;  // New Mode 4
        
        if (IsKeyPressed(KEY_UP)) waterfallSpeed++;
        if (IsKeyPressed(KEY_DOWN)) waterfallSpeed--;
        
        if (waterfallSpeed < 1) waterfallSpeed = 1;
        if (waterfallSpeed > 20) waterfallSpeed = 20;

        if (!isPaused) {
            extractor.ProcessNewAudio(audio, currentMode);
            // Pass currentMode to ExtractXY so it knows exactly what FFT map to use
            extractor.ExtractXY(activeHeights, activeFreqs, currentMode);
        }
        
        renderer.UpdateAndDraw(activeHeights, activeFreqs, currentMode, waterfallSpeed, isPaused, showUI);
    }

    std::ofstream outFile(configPath);
    if (outFile.is_open()) {
        if (isFullscreen) {
            outFile << windowedW << " " << windowedH << " " << windowedX << " " << windowedY << " ";
        } else {
            Vector2 pos = GetWindowPosition();
            outFile << GetScreenWidth() << " " << GetScreenHeight() << " " << (int)pos.x << " " << (int)pos.y << " ";
        }
        outFile << currentMode << " " << waterfallSpeed << " " << showUI;
        outFile.close();
    }

    audio.Stop();
    CloseWindow();
    return 0;
}
