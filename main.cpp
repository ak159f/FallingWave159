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

namespace Config {
    const int WindowWidth = 1820;
    const int WindowHeight = 980;
    
    // Audio Processing
    const int SampleRate = 88200;
    const int FFTSize = 16384; 
    const int CaptureSize = 4096; 
    
    // Visuals 
    const int DotCount = 900; 
    const float MinFreq = 20.0f;
    const float MaxFreq = 20000.0f;
    
    // Exact dB scale from the screenshot (-125 to 0)
    const float MinDB = -125.0f; 
    const float MaxDB = 0.0f; 
}

// Exact replication of the Purple -> Pink -> Peach -> Yellow gradient from the image
Color GetWaterfallColor(float val) {
    val = std::clamp(val, 0.0f, 1.0f);
    
    if (val < 0.25f) {
        // Deep Blue/Purple to Violet
        float t = val / 0.25f;
        return (Color){ (unsigned char)(20 + t*60), (unsigned char)(20 + t*20), (unsigned char)(60 + t*70), 255 };
    } else if (val < 0.50f) {
        // Violet to Pink/Magenta
        float t = (val - 0.25f) / 0.25f;
        return (Color){ (unsigned char)(80 + t*120), (unsigned char)(40 + t*40), (unsigned char)(130 + t*30), 255 };
    } else if (val < 0.75f) {
        // Pink to Peach/Light Orange
        float t = (val - 0.50f) / 0.25f;
        return (Color){ (unsigned char)(200 + t*55), (unsigned char)(80 + t*100), (unsigned char)(160 - t*60), 255 };
    } else {
        // Peach to Bright Yellow/White
        float t = (val - 0.75f) / 0.25f;
        return (Color){ 255, (unsigned char)(180 + t*75), (unsigned char)(100 + t*120), 255 };
    }
}

class MathEngine {
public:
    static void ComputeInPlaceFFT(std::vector<std::complex<float>>& buffer) {
        int n = buffer.size();
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
            instance->writeHead = (instance->writeHead + 1) % Config::FFTSize;
        }
    }

public:
    AudioCapture() : circularBuffer(Config::FFTSize, 0.0f), writeHead(0) {}

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

    void GetLatestFrames(std::vector<float>& outBuffer) {
        std::lock_guard<std::mutex> lock(mtx);
        for (int i = 0; i < Config::FFTSize; ++i) {
            outBuffer[i] = circularBuffer[(writeHead + i) % Config::FFTSize];
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
        audioFrames.resize(Config::FFTSize);
        fftBuffer.resize(Config::FFTSize);
        rawMagnitudes.resize(Config::FFTSize / 2);
    }

    void ProcessNewAudio(AudioCapture& audio, int currentMode) {
        audio.GetLatestFrames(audioFrames);
        
        if (currentMode == 0) { // High Resolution (Asymmetric Window)
            int peakIndex = Config::FFTSize - 2048; 
            
            for (int i = 0; i < Config::FFTSize; ++i) {
                float window = 0.0f;
                if (i < peakIndex) {
                    // Smooth rise for the older history data (Fast natural decay)
                    window = 0.5f * (1.0f - std::cos(PI * i / peakIndex));
                } else {
                    // Fast taper for the newest data (Instant visual attack)
                    window = 0.5f * (1.0f + std::cos(PI * (i - peakIndex) / (Config::FFTSize - peakIndex)));
                }
                
                fftBuffer[i] = std::complex<float>(audioFrames[i] * window, 0.0f);
            }
        } else { // High Speed (Zero-Padding)
            int paddingOffset = Config::FFTSize - Config::CaptureSize;
            for (int i = 0; i < Config::FFTSize; ++i) {
                if (i >= paddingOffset) {
                    // Apply a Hann window only to the short active capture burst
                    float t = (float)(i - paddingOffset) / (Config::CaptureSize - 1.0f);
                    float window = 0.5f * (1.0f - std::cos(2.0f * PI * t));
                    fftBuffer[i] = std::complex<float>(audioFrames[i] * window, 0.0f);
                } else {
                    // Zero pad the history
                    fftBuffer[i] = std::complex<float>(0.0f, 0.0f); 
                }
            }
        }
        
        MathEngine::ComputeInPlaceFFT(fftBuffer);
        
        for (int i = 0; i < Config::FFTSize / 2; ++i) {
            // Restore normalization for the full high-resolution buffer
            float re = fftBuffer[i].real();
            float im = fftBuffer[i].imag();
            rawMagnitudes[i] = std::sqrt(re * re + im * im) / (Config::FFTSize * 0.5f);
        }
    }

    void ExtractXY(std::vector<float>& outHeights, std::vector<float>& outFreqs) {
        float nyquist = Config::SampleRate / 2.0f;
        float logMin = std::log10(Config::MinFreq);
        float logMax = std::log10(Config::MaxFreq);

        for (int i = 0; i < Config::DotCount; ++i) {
            float t1 = i / (float)Config::DotCount;
            float t2 = (i + 1) / (float)Config::DotCount;
            
            float freqStart = std::pow(10.0f, logMin + t1 * (logMax - logMin));
            float freqEnd = std::pow(10.0f, logMin + t2 * (logMax - logMin));
            outFreqs[i] = (freqStart + freqEnd) * 0.5f;

            float exactStartBin = (freqStart / nyquist) * (Config::FFTSize / 2);
            float exactEndBin = (freqEnd / nyquist) * (Config::FFTSize / 2);
            
            float peakMag = 0.0f;
            int bStart = std::max(0, (int)exactStartBin);
            int bEnd = std::min((Config::FFTSize / 2) - 1, (int)exactEndBin);

            if (bStart >= bEnd) {
                // Pixel is narrower than a single FFT bin.
                // We use raw max bin for blocky solid bars (Looks great with duplicating frequencies!)
                int centerBin = ((freqStart + freqEnd) * 0.5f / nyquist) * (Config::FFTSize / 2);
                peakMag = rawMagnitudes[std::max(0, centerBin)];
            } else {
                // Max peak in the range for high frequencies
                for (int b = bStart; b <= bEnd; ++b) {
                    if (rawMagnitudes[b] > peakMag) peakMag = rawMagnitudes[b];
                }
            }

            float db = Config::MinDB;
            if (peakMag > 1e-12f) {
                db = 20.0f * std::log10(peakMag); // Proper Decibel formula
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
        waterfallHeight = 400; // Increased resolution buffer for the waterfall
        waterfallImg = GenImageColor(Config::DotCount, waterfallHeight, BLACK);
        waterfallTex = LoadTextureFromImage(waterfallImg);
    }

    ~Renderer() {
        UnloadTexture(waterfallTex);
        UnloadImage(waterfallImg);
    }

    void UpdateAndDraw(const std::vector<float>& yHeights, const std::vector<float>& xFreqs, int currentMode, int waterfallSpeed) {
        int w = GetScreenWidth();
        int h = GetScreenHeight();
        
        // --- NEW UI LAYOUT CONSTANTS ---
        int leftMargin = 65;  // Dedicated space for dB text
        int textGap = 25;     // Dedicated space for Hz text
        
        float graphH = h * 0.35f;
        float waterH = h - graphH - textGap;

        // 1. Shift Waterfall DOWNWARDS with variable speed
        int shiftRows = waterfallSpeed;
        if (shiftRows > waterfallHeight) shiftRows = waterfallHeight;
        
        Color* pixels = (Color*)waterfallImg.data;
        std::memmove(pixels + (Config::DotCount * shiftRows), 
                     pixels, 
                     sizeof(Color) * Config::DotCount * (waterfallHeight - shiftRows));

        // Write new line exactly at Row 0
        for (int r = 0; r < shiftRows; ++r) {
            for (int i = 0; i < Config::DotCount; ++i) {
                pixels[r * Config::DotCount + i] = GetWaterfallColor(yHeights[i]);
            }
        }
        UpdateTexture(waterfallTex, waterfallImg.data);

        BeginDrawing();
        ClearBackground((Color){12, 12, 12, 255}); // Dark gray studio background

        // 2. Draw Waterfall in the bottom section, shifted by margins
        Rectangle src = { 0.0f, 0.0f, (float)Config::DotCount, (float)waterfallHeight };
        Rectangle dest = { (float)leftMargin, graphH + textGap, (float)(w - leftMargin), waterH };
        DrawTexturePro(waterfallTex, src, dest, (Vector2){0, 0}, 0.0f, WHITE);

        // --- DRAW DEDICATED UI PANELS ---
        DrawRectangle(0, 0, leftMargin, h, (Color){8, 8, 8, 255}); // Left Column
        DrawRectangle(leftMargin, graphH, w - leftMargin, textGap, (Color){8, 8, 8, 255}); // Middle Row

        // 3. Draw Grid Lines
        Color gridColor = (Color){45, 45, 45, 255};
        Color textColor = (Color){150, 150, 150, 255};

        // Horizontal Grid (dB Levels) matching the image exactly
        int dbSteps[] = {0, -25, -50, -75, -100, -125};
        for(int db : dbSteps) {
            float normalizedY = (db - Config::MinDB) / (Config::MaxDB - Config::MinDB);
            float y = graphH - (normalizedY * graphH);
            
            DrawLine(leftMargin, y, w, y, gridColor);
            
            const char* text = TextFormat("%d", db);
            int textW = MeasureText(text, 20); 
            DrawText(text, leftMargin - textW - 5, y - 10, 20, textColor);
        }

        for(int db = 0; db >= -125; db -= 10) { 
            float normalizedY = (db - Config::MinDB) / (Config::MaxDB - Config::MinDB);
            float y = (graphH + textGap) + (1.0f - normalizedY) * waterH;
            
            const char* text = TextFormat("%d", db);
            int textW = MeasureText(text, 18);
            DrawText(text, leftMargin - textW - 5, y - 9, 18, (Color){200, 200, 200, 150});
        }

        // Vertical Grid (Frequencies)
        float logMin = std::log10(Config::MinFreq);
        float logMax = std::log10(Config::MaxFreq);
        int freqs[] = {40, 60, 100, 200, 400, 600, 1000, 2000, 4000, 6000, 10000, 20000};
        
        for(int f : freqs) {
            float t = (std::log10((float)f) - logMin) / (logMax - logMin);
            float x = leftMargin + t * (w - leftMargin);
            
            DrawLine(x, 0, x, graphH, gridColor);
            DrawLine(x, graphH + textGap, x, h, (Color){255, 255, 255, 15});
            
            const char* fText = TextFormat("%d", f);
            int textW = MeasureText(fText, 20); 
            
            float textX = x - (textW / 2.0f);
            
            // SAFETY FIX: Prevent cutoff on the right edge
            if (textX + textW > w) {
                textX = w - textW - 5;
            }
            
            DrawText(fText, textX, graphH + 3, 20, textColor);
        }

        // 4. Draw Solid Dynamic Bars
        float barWidth = (float)(w - leftMargin) / Config::DotCount;
        for (int i = 0; i < Config::DotCount; ++i) {
            float x = leftMargin + ((float)i / Config::DotCount) * (w - leftMargin);
            float height = yHeights[i] * graphH;
            float y = graphH - height;
            
            Color barColor = GetWaterfallColor(yHeights[i]);
            DrawRectangle(x, y, std::max(1.0f, barWidth - 1.0f), std::max(1.0f, height), barColor);
        }

        // 5. Studio Crosshair Logic
        float mx = GetMouseX();
        float my = GetMouseY();
        if (mx >= leftMargin && mx < w && my >= 0 && my < h) {
            Color crossColor = (Color){0, 255, 200, 200}; // Vibrant Cyan
            
            DrawLine(mx, 0, mx, h, crossColor);
            
            float hoverFreq = Config::MinFreq * std::pow(10.0f, ((mx - leftMargin) / (w - leftMargin)) * (logMax - logMin));
            DrawText(TextFormat("%.0f Hz", hoverFreq), w - 200, 20, 30, crossColor);
            
            if (my < graphH) {
                DrawLine(leftMargin, my, w, my, crossColor);
                float hoverDB = Config::MinDB + (1.0f - (my / graphH)) * (Config::MaxDB - Config::MinDB);
                DrawText(TextFormat("%.0f dB", hoverDB), w - 200, 60, 30, crossColor);
            }
        }

        // Draw current mode and speed UI
        if (currentMode == 0) {
            DrawText("MODE: HI-RES (SPACE to swap)", leftMargin + 10, 15, 20, (Color){200, 200, 200, 200});
        } else {
            DrawText("MODE: HI-SPEED (SPACE to swap)", leftMargin + 10, 15, 20, (Color){0, 255, 200, 200}); 
        }
        
        DrawText(TextFormat("SPEED: %d (UP/DOWN to change)", waterfallSpeed), leftMargin + 10, 40, 20, (Color){255, 200, 0, 200});

        EndDrawing();
    }
};

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    
    // UPDATE: Set the official name of the app
    InitWindow(Config::WindowWidth, Config::WindowHeight, "FallingWave 159");
    
    // UPDATE: Load your custom 159Hz icon file
    Image icon = LoadImage("159hz.ico");
    if (icon.data != NULL) {
        SetWindowIcon(icon);
        UnloadImage(icon); // We can safely unload it from CPU memory once the OS takes it
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

    int currentMode = 0; 
    int waterfallSpeed = 2; 

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_SPACE)) {
            currentMode = (currentMode == 0) ? 1 : 0;
        }
        
        if (IsKeyPressed(KEY_UP)) waterfallSpeed++;
        if (IsKeyPressed(KEY_DOWN)) waterfallSpeed--;
        
        if (waterfallSpeed < 1) waterfallSpeed = 1;
        if (waterfallSpeed > 20) waterfallSpeed = 20;

        extractor.ProcessNewAudio(audio, currentMode);
        extractor.ExtractXY(activeHeights, activeFreqs);
        renderer.UpdateAndDraw(activeHeights, activeFreqs, currentMode, waterfallSpeed);
    }

    audio.Stop();
    CloseWindow();
    return 0;
}