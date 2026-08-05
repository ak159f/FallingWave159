// 1. The "Protection Shield" MUST go first!
#define Rectangle WinRectangle
#define CloseWindow WinCloseWindow
#define ShowCursor WinShowCursor
#define DrawText WinDrawText
#define DrawTextEx WinDrawTextEx
#define PlaySound WinPlaySound
#define LoadImage WinLoadImage

// 2. Audio Backend First (This internally loads windows.h behind our backs)
#define MINIAUDIO_IMPLEMENTATION
#define MA_API static
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include "miniaudio.h"

// 3. Load explicit Windows API for our transparency features
#include <windows.h>

// 4. Drop the shield so Raylib can use these names safely
#undef Rectangle
#undef CloseWindow
#undef ShowCursor
#undef DrawText
#undef DrawTextEx
#undef PlaySound
#undef LoadImage

// 5. Raylib
#include "raylib.h"

// 6. Standard Libs
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
    
    // Audio Processing
    const int SampleRate = 176400; 
    const int MaxFFTSize = 65536; 
    
    // Visuals 
    const int DotCount = 900; 
    const float MinFreq = 20.0f;
    const float MaxFreq = 20000.0f;
    const float MinDB = -130.0f; 
    const float MaxDB = 0.0f; 
}

struct AppState {
    bool isZeroPadded = false;
    int fftSizeIndex = 3; 
    int visMode = 1;      
    bool cursorOn = true;
    int activePreset = 0;
    float opacity = 1.0f;
    bool overlayMode = false;
};
AppState globalState;
AppState presets[9]; 
const int FFT_SIZES[] = {2048, 4096, 8192, 16384, 32768, 65536};
const char* VIS_NAMES[] = {"Full Wave", "1:3 Waterfall", "1:2 Waterfall", "Full Waterfall"};
const char* FFT_NAMES[] = {"2048", "4096", "8192", "16384", "32768", "65536"};

Color GetWaterfallColor(float val) {
    val = std::clamp(val, 0.0f, 1.0f);
    if (val < 0.25f) {
        float t = val / 0.25f;
        return Color{ (unsigned char)(20 + t*60), (unsigned char)(20 + t*20), (unsigned char)(60 + t*70), 255 };
    } else if (val < 0.50f) {
        float t = (val - 0.25f) / 0.25f;
        return Color{ (unsigned char)(80 + t*120), (unsigned char)(40 + t*40), (unsigned char)(130 + t*30), 255 };
    } else if (val < 0.75f) {
        float t = (val - 0.50f) / 0.25f;
        return Color{ (unsigned char)(200 + t*55), (unsigned char)(80 + t*100), (unsigned char)(160 - t*60), 255 };
    } else {
        float t = (val - 0.75f) / 0.25f;
        return Color{ 255, (unsigned char)(180 + t*75), (unsigned char)(100 + t*120), 255 };
    }
}

Color PRESET_COLORS[] = {
    Color{255, 50, 50, 255}, Color{255, 150, 0, 255}, Color{255, 220, 0, 255},
    Color{50, 255, 50, 255}, Color{0, 255, 200, 255}, Color{50, 150, 255, 255},
    Color{180, 50, 255, 255}, Color{255, 50, 255, 255}, Color{255, 100, 150, 255}
};

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
        audioFrames.reserve(Config::MaxFFTSize);
        fftBuffer.reserve(Config::MaxFFTSize);
        rawMagnitudes.reserve(Config::MaxFFTSize / 2);
    }

    void ProcessNewAudio(AudioCapture& audio) {
        int currentFFTSize = FFT_SIZES[globalState.fftSizeIndex];
        bool isHighSpeed = globalState.isZeroPadded;
        
        int currentCaptureSize = std::max(1024, currentFFTSize / 4);

        audioFrames.resize(currentFFTSize);
        fftBuffer.resize(currentFFTSize);
        rawMagnitudes.resize(currentFFTSize / 2);

        audio.GetLatestFrames(audioFrames, currentFFTSize);
        
        if (!isHighSpeed) { 
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

    void ExtractXY(std::vector<float>& outHeights, std::vector<float>& outFreqs) {
        int currentFFTSize = FFT_SIZES[globalState.fftSizeIndex];
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
    int activeDropdown = -1; 
    bool isDraggingOpacity = false; 

    bool DrawCustomButton(Rectangle bounds, const char* text, Color borderColor, Color textColor, int fontSize, bool ignoreDropdown = false) {
        bool clicked = false;
        Vector2 mouse = GetMousePosition();
        bool hover = CheckCollisionPointRec(mouse, bounds) && (activeDropdown == -1 || ignoreDropdown); 
        
        if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) clicked = true;
        
        DrawRectangleLinesEx(bounds, 2.0f, hover ? WHITE : borderColor);
        int tw = MeasureText(text, fontSize);
        DrawText(text, bounds.x + bounds.width/2 - tw/2, bounds.y + bounds.height/2 - fontSize/2, fontSize, hover ? WHITE : textColor);
        return clicked;
    }

    bool DrawCustomSlider(Rectangle bounds, const char* label, float& value, float min, float max, Color color) {
        bool valueChanged = false;
        Vector2 mouse = GetMousePosition();
        bool hover = CheckCollisionPointRec(mouse, bounds) && activeDropdown == -1;
        
        if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) isDraggingOpacity = true;
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) isDraggingOpacity = false;
        
        if (isDraggingOpacity) {
            float t = (mouse.x - bounds.x) / bounds.width;
            value = min + t * (max - min);
            value = std::clamp(value, min, max);
            valueChanged = true;
        }
        
        DrawRectangleRec(bounds, Color{40, 40, 40, 255});
        float t = (value - min) / (max - min);
        DrawRectangleRec(Rectangle{bounds.x, bounds.y, bounds.width * t, bounds.height}, color);
        DrawRectangleLinesEx(bounds, 1.0f, hover ? WHITE : Color{100, 100, 100, 255});
        
        DrawText(TextFormat("%s: %.0f%%", label, value * 100.0f), bounds.x + bounds.width + 10, bounds.y + bounds.height/2 - 7, 14, WHITE);
        
        return valueChanged;
    }

public:
    Renderer() {
        waterfallHeight = 600; 
        waterfallImg = GenImageColor(Config::DotCount, waterfallHeight, Color{0, 0, 0, 0}); 
        waterfallTex = LoadTextureFromImage(waterfallImg);
    }
    ~Renderer() {
        UnloadTexture(waterfallTex);
        UnloadImage(waterfallImg);
    }

    void UpdateAndDraw(const std::vector<float>& yHeights, const std::vector<float>& xFreqs, int waterfallSpeed, bool isPaused, bool showUI) {
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
        
        float graphRatio = 0.5f;
        if (globalState.visMode == 0) graphRatio = 1.0f;
        if (globalState.visMode == 1) graphRatio = 0.33f;
        if (globalState.visMode == 2) graphRatio = 0.50f;
        if (globalState.visMode == 3) graphRatio = 0.0f;
        
        float graphH = h * graphRatio;
        if (globalState.visMode == 0) graphH = h - topMargin - textGap;
        
        float waterH = h - (topMargin + graphH + textGap);
        if (globalState.visMode == 0) waterH = 0;
        if (globalState.visMode == 3) {
            graphH = 0; textGap = 0; waterH = h - topMargin;
        }

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

        bool isOverlayActive = globalState.overlayMode && !showUI;

        BeginDrawing();
        
        // PURE BLACK CLEAR - Windows LWA_COLORKEY will instantly delete this from the screen!
        if (isOverlayActive) {
            ClearBackground(Color{0, 0, 0, 255});
        } else {
            ClearBackground(Color{12, 12, 12, 255}); 
        }

        if (waterH > 0) {
            Rectangle src = Rectangle{ 0.0f, 0.0f, (float)Config::DotCount, (float)waterfallHeight };
            Rectangle dest = Rectangle{ (float)leftMargin, topMargin + graphH + textGap, (float)(w - leftMargin), waterH };
            DrawTexturePro(waterfallTex, src, dest, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        }

        // Hide grey margins in overlay mode so the desktop shines through
        if (!isOverlayActive) {
            DrawRectangle(0, 0, leftMargin, h, Color{8, 8, 8, 255}); 
            if (graphH > 0) DrawRectangle(leftMargin, topMargin + graphH, w - leftMargin, textGap, Color{8, 8, 8, 255}); 
        }

        Color gridColor = Color{45, 45, 45, 255};
        Color textColor = Color{150, 150, 150, 255};

        if (graphH > 0) {
            int dbSteps[] = {0, -25, -50, -75, -100, -125};
            for(int db : dbSteps) {
                float normalizedY = (db - Config::MinDB) / (Config::MaxDB - Config::MinDB);
                float y = topMargin + (graphH - (normalizedY * graphH));
                DrawLine(leftMargin, y, w, y, gridColor);
                const char* text = TextFormat("%d", db);
                int textW = MeasureText(text, dbFontSize); 
                DrawText(text, leftMargin - textW - 5, y - (dbFontSize/2), dbFontSize, textColor);
            }
        }

        if (waterH > 0) {
            for(int db = 0; db >= -130; db -= 5) { 
                float normalizedY = (db - Config::MinDB) / (Config::MaxDB - Config::MinDB);
                float y = (topMargin + graphH + textGap) + (1.0f - normalizedY) * waterH;
                const char* text = TextFormat("%d", db);
                int textW = MeasureText(text, dbFontSize);
                Color dbColor = GetWaterfallColor(std::clamp(normalizedY, 0.0f, 1.0f));
                DrawText(text, leftMargin - textW - 5, y - (dbFontSize/2), dbFontSize, dbColor);
            }
        }

        float logMin = std::log10(Config::MinFreq);
        float logMax = std::log10(Config::MaxFreq);
        int freqs[] = {40, 60, 100, 200, 400, 600, 1000, 2000, 4000, 6000, 10000, 20000};
        
        for(int f : freqs) {
            float t = (std::log10((float)f) - logMin) / (logMax - logMin);
            float x = leftMargin + t * (w - leftMargin);
            
            if (graphH > 0) DrawLine(x, topMargin, x, topMargin + graphH, gridColor);
            if (waterH > 0) DrawLine(x, topMargin + graphH + textGap, x, h, Color{255, 255, 255, 15});
            
            const char* fText = (f >= 1000) ? TextFormat("%dk", f / 1000) : TextFormat("%d", f);
            int textW = MeasureText(fText, freqFontSize); 
            float textX = std::clamp(x - (textW / 2.0f), (float)leftMargin + 5, (float)w - textW - 5);
            
            if (graphH > 0 || waterH > 0) {
                DrawText(fText, textX, topMargin + graphH + 3, freqFontSize, textColor);
            }
        }

        if (graphH > 0) {
            for (int i = 0; i < Config::DotCount; ++i) {
                int x1 = leftMargin + (int)(((float)i / Config::DotCount) * (w - leftMargin));
                int x2 = leftMargin + (int)(((float)(i + 1) / Config::DotCount) * (w - leftMargin));
                int barW = std::max(1, x2 - x1);
                float height = yHeights[i] * graphH;
                int y = topMargin + (int)(graphH - height);
                DrawRectangle(x1, y, barW, std::max(1, (int)height), GetWaterfallColor(yHeights[i]));
            }
        }

        float mx = GetMouseX();
        float my = GetMouseY();
        if (globalState.cursorOn && !showUI && mx >= leftMargin && mx < w && my >= 0 && my < h && !isOverlayActive) {
            Color crossColor = Color{0, 255, 200, 200}; 
            DrawLine(mx, 0, mx, h, crossColor);
            float hoverFreq = Config::MinFreq * std::pow(10.0f, ((mx - leftMargin) / (w - leftMargin)) * (logMax - logMin));
            DrawText(TextFormat("%.0f Hz", hoverFreq), w - MeasureText("20000 Hz", crosshairFontSize) - 20, 20 + topMargin, crosshairFontSize, crossColor);
            
            if (my >= topMargin && my < topMargin + graphH) {
                DrawLine(leftMargin, my, w, my, crossColor);
                float hoverDB = Config::MinDB + (1.0f - ((my - topMargin) / graphH)) * (Config::MaxDB - Config::MinDB);
                DrawText(TextFormat("%.0f dB", hoverDB), w - MeasureText("20000 Hz", crosshairFontSize) - 20, 20 + topMargin + crosshairFontSize + 5, crosshairFontSize, crossColor);
            }
        }

        if (showUI) {
            DrawRectangle(0, 0, w, h, Color{0, 0, 0, 220}); 
            
            DrawText("Press F8 globally or TAB to show/hide this menu", w/2 - MeasureText("Press F8 globally or TAB to show/hide this menu", 16)/2, 20, 16, WHITE);
            
            int boxW = std::min(800, w - 40);
            int boxH = std::min(600, h - 40);
            int boxX = w/2 - boxW/2;
            int boxY = h/2 - boxH/2;
            
            DrawRectangleLinesEx(Rectangle{(float)boxX, (float)boxY, (float)boxW, (float)boxH}, 1.0f, WHITE);
            DrawLine(boxX, boxY + 60, boxX + boxW, boxY + 60, WHITE);
            
            int fSize = std::max(16, (int)(24.0f * avgScale));
            int smSize = std::max(12, (int)(16.0f * avgScale));
            Color activeCol = Color{255, 120, 0, 255}; 
            Color inactiveCol = WHITE;
            
            DrawText("Settings", boxX + 20, boxY + 15, fSize + 6, WHITE);
            DrawText("press tab to hide", boxX + 20, boxY + 45, smSize, Color{150, 150, 150, 255});
            
            int rowX = boxX + 30;
            int rowY = boxY + 90;
            
            DrawText("STFT", rowX, rowY, fSize, WHITE);
            Rectangle lwBtn = Rectangle{(float)rowX, (float)rowY + 25, 140, 30};
            Rectangle zpBtn = Rectangle{(float)rowX + 140, (float)rowY + 25, 140, 30};
            if (DrawCustomButton(lwBtn, "Long-Window", !globalState.isZeroPadded ? activeCol : inactiveCol, !globalState.isZeroPadded ? activeCol : inactiveCol, smSize)) globalState.isZeroPadded = false;
            if (DrawCustomButton(zpBtn, "Zero-Padded", globalState.isZeroPadded ? activeCol : inactiveCol, globalState.isZeroPadded ? activeCol : inactiveCol, smSize)) globalState.isZeroPadded = true;

            rowY += 90;
            DrawText("Visual mode", rowX, rowY, fSize, WHITE);
            Rectangle visDrop = Rectangle{(float)rowX, (float)rowY + 25, 280, 30};
            if (DrawCustomButton(visDrop, TextFormat("%s   v", VIS_NAMES[globalState.visMode]), WHITE, WHITE, smSize)) {
                activeDropdown = (activeDropdown == 0) ? -1 : 0;
            }

            rowY += 90;
            DrawText("Sampling Rate", rowX, rowY, fSize, WHITE);
            Rectangle fftDrop = Rectangle{(float)rowX, (float)rowY + 25, 280, 30};
            if (DrawCustomButton(fftDrop, TextFormat("%s   v", FFT_NAMES[globalState.fftSizeIndex]), WHITE, WHITE, smSize)) {
                activeDropdown = (activeDropdown == 1) ? -1 : 1;
            }

            rowY += 90;
            DrawText("Cursor", rowX, rowY, fSize, WHITE);
            Rectangle cOnBtn = Rectangle{(float)rowX, (float)rowY + 25, 60, 30};
            Rectangle cOffBtn = Rectangle{(float)rowX + 60, (float)rowY + 25, 60, 30};
            if (DrawCustomButton(cOnBtn, "ON", globalState.cursorOn ? activeCol : inactiveCol, globalState.cursorOn ? activeCol : inactiveCol, smSize)) globalState.cursorOn = true;
            if (DrawCustomButton(cOffBtn, "OFF", !globalState.cursorOn ? activeCol : inactiveCol, !globalState.cursorOn ? activeCol : inactiveCol, smSize)) globalState.cursorOn = false;

            DrawText("Overlay Mode", rowX + 160, rowY, fSize, WHITE);
            Rectangle oOnBtn = Rectangle{(float)rowX + 160, (float)rowY + 25, 60, 30};
            Rectangle oOffBtn = Rectangle{(float)rowX + 220, (float)rowY + 25, 60, 30};
            if (DrawCustomButton(oOnBtn, "ON", globalState.overlayMode ? activeCol : inactiveCol, globalState.overlayMode ? activeCol : inactiveCol, smSize)) globalState.overlayMode = true;
            if (DrawCustomButton(oOffBtn, "OFF", !globalState.overlayMode ? activeCol : inactiveCol, !globalState.overlayMode ? activeCol : inactiveCol, smSize)) globalState.overlayMode = false;

            rowY += 90;
            DrawText("Window Opacity", rowX, rowY, fSize, WHITE);
            Rectangle opSlider = Rectangle{(float)rowX, (float)rowY + 25, 280, 20};
            DrawCustomSlider(opSlider, "Opacity", globalState.opacity, 0.1f, 1.0f, activeCol);

            Rectangle saveBtn = Rectangle{(float)boxX + boxW - 140, (float)rowY - 30, 110, 55};
            if (DrawCustomButton(saveBtn, "SAVE", Color{50, 150, 255, 255}, Color{50, 150, 255, 255}, fSize)) {
                presets[globalState.activePreset] = globalState;
            }

            int pSize = 40;
            int gap = 15;
            int totalW = (9 * pSize) + (8 * gap);
            int startX = boxX + (boxW / 2) - (totalW / 2);
            int pY = boxY + boxH - 60;
            
            for (int i = 0; i < 9; i++) {
                Rectangle pBtn = Rectangle{(float)startX + i*(pSize+gap), (float)pY, (float)pSize, (float)pSize};
                Color c = PRESET_COLORS[i];
                if (globalState.activePreset != i) { c.r /= 2; c.g /= 2; c.b /= 2; } 
                
                if (DrawCustomButton(pBtn, TextFormat("%d", i+1), (globalState.activePreset == i) ? WHITE : c, c, fSize + 4)) {
                    globalState = presets[i];
                    globalState.activePreset = i;
                }
            }

            if (activeDropdown == 0) { 
                for (int i = 0; i < 4; i++) {
                    Rectangle opt = Rectangle{visDrop.x, visDrop.y + 30.0f + (i*30), visDrop.width, 30.0f};
                    DrawRectangleRec(opt, Color{15, 15, 15, 255}); 
                    if (DrawCustomButton(opt, VIS_NAMES[i], WHITE, WHITE, smSize, true)) {
                        globalState.visMode = i;
                        activeDropdown = -1;
                    }
                }
            }
            if (activeDropdown == 1) { 
                for (int i = 0; i < 6; i++) {
                    Rectangle opt = Rectangle{fftDrop.x, fftDrop.y + 30.0f + (i*30), fftDrop.width, 30.0f};
                    DrawRectangleRec(opt, Color{15, 15, 15, 255}); 
                    if (DrawCustomButton(opt, FFT_NAMES[i], WHITE, WHITE, smSize, true)) {
                        globalState.fftSizeIndex = i;
                        activeDropdown = -1;
                    }
                }
            }
            
            if (activeDropdown != -1 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Vector2 mouse = GetMousePosition();
                Rectangle dropArea = (activeDropdown == 0) ? 
                    Rectangle{visDrop.x, visDrop.y, visDrop.width, 150.0f} : 
                    Rectangle{fftDrop.x, fftDrop.y, fftDrop.width, 210.0f};
                    
                if (!CheckCollisionPointRec(mouse, dropArea)) activeDropdown = -1;
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
    int waterfallSpeed = 2; 
    bool showUI = true;
    
    for(int i = 0; i < 9; i++) presets[i] = globalState; 

    std::ifstream inFile(configPath);
    if (inFile.is_open()) {
        inFile >> winW >> winH >> winX >> winY >> waterfallSpeed >> showUI >> globalState.activePreset;
        for(int i = 0; i < 9; i++) {
            inFile >> presets[i].isZeroPadded >> presets[i].fftSizeIndex >> presets[i].visMode >> presets[i].cursorOn;
            char c;
            while (inFile.get(c) && c != '\n') {
                if (c >= '0' && c <= '9') {
                    inFile.unget(); 
                    inFile >> presets[i].opacity >> presets[i].overlayMode;
                    break;
                }
            }
        }
        inFile.close();
        if (globalState.activePreset >= 0 && globalState.activePreset < 9) {
            globalState = presets[globalState.activePreset];
        }
    }

    if (winX < -8000 || winY < -8000) { winX = -9999; winY = -9999; }
    if (globalState.opacity < 0.1f) globalState.opacity = 1.0f;
    for (int i = 0; i < 9; i++) { if (presets[i].opacity < 0.1f) presets[i].opacity = 1.0f; }
    if (winW < 400) winW = Config::WindowWidth;
    if (winH < 300) winH = Config::WindowHeight;
    if (waterfallSpeed < 1 || waterfallSpeed > 20) waterfallSpeed = 2;

    // FIX 1: Absolutely NO MSAA or Raylib Transparency flags! 
    // They corrupt the final render buffer and break Windows colorkeying!
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(winW, winH, "FallingWave 159");
    SetExitKey(0); 

    if (winX != -9999 && winY != -9999) SetWindowPosition(winX, winY);
    
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
    int windowedW = winW, windowedH = winH;
    int windowedX = winX, windowedY = winY;
    static bool f8WasDown = false; 

    while (!WindowShouldClose()) {
        bool f8IsDown = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        if (f8IsDown && !f8WasDown) showUI = !showUI;
        f8WasDown = f8IsDown;
        
        if (IsKeyPressed(KEY_F11) || IsKeyPressed(KEY_F)) {
            int monitor = GetCurrentMonitor();
            if (IsWindowFullscreen()) {
                ToggleFullscreen();
                SetWindowSize(windowedW, windowedH);
                SetWindowPosition(windowedX, windowedY);
                isFullscreen = false;
            } else {
                windowedW = GetScreenWidth(); windowedH = GetScreenHeight();
                Vector2 pos = GetWindowPosition();
                windowedX = pos.x; windowedY = pos.y;
                SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
                ToggleFullscreen();
                isFullscreen = true;
            }
        }

        if (IsKeyPressed(KEY_SPACE) && !showUI) isPaused = !isPaused; 
        if (IsKeyPressed(KEY_TAB)) showUI = !showUI;
        if (IsKeyPressed(KEY_UP)) waterfallSpeed = std::min(20, waterfallSpeed + 1);
        if (IsKeyPressed(KEY_DOWN)) waterfallSpeed = std::max(1, waterfallSpeed - 1);

        LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        bool isCurrentlyOverlay = (exStyle & WS_EX_TRANSPARENT) != 0;
        bool shouldBeOverlay = globalState.overlayMode && !showUI;

        // FIX 2: Added SWP_FRAMECHANGED. 
        // Without this, Windows forgets to recalculate the click-boxes when we exit overlay mode!
        if (shouldBeOverlay != isCurrentlyOverlay) {
            if (shouldBeOverlay) {
                SetWindowState(FLAG_WINDOW_UNDECORATED); 
                SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT | WS_EX_LAYERED);
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
            } else {
                ClearWindowState(FLAG_WINDOW_UNDECORATED);
                SetWindowLong(hwnd, GWL_EXSTYLE, (exStyle & ~WS_EX_TRANSPARENT) | WS_EX_LAYERED);
                SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
            }
        }
        
        if ((GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) == 0) {
            SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
        }
        
        // FIX 3: Native LWA_COLORKEY transparency.
        // It looks for EXACTLY RGB(0,0,0) and deletes it while applying Opacity to everything else.
        if (shouldBeOverlay) {
            SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), (BYTE)(globalState.opacity * 255), LWA_COLORKEY | LWA_ALPHA);
        } else {
            SetLayeredWindowAttributes(hwnd, 0, (BYTE)(globalState.opacity * 255), LWA_ALPHA);
        }

        if (!isPaused) {
            extractor.ProcessNewAudio(audio);
            extractor.ExtractXY(activeHeights, activeFreqs);
        }
        
        renderer.UpdateAndDraw(activeHeights, activeFreqs, waterfallSpeed, isPaused, showUI);
    }

    presets[globalState.activePreset] = globalState; 

    std::ofstream outFile(configPath);
    if (outFile.is_open()) {
        if (isFullscreen) {
            outFile << windowedW << " " << windowedH << " " << windowedX << " " << windowedY << " ";
        } else {
            Vector2 pos = GetWindowPosition();
            if (pos.x < -8000 || pos.y < -8000) { pos.x = windowedX; pos.y = windowedY; }
            outFile << GetScreenWidth() << " " << GetScreenHeight() << " " << (int)pos.x << " " << (int)pos.y << " ";
        }
        outFile << waterfallSpeed << " " << showUI << " " << globalState.activePreset << "\n";
        
        for(int i = 0; i < 9; i++) {
            outFile << presets[i].isZeroPadded << " " << presets[i].fftSizeIndex << " " << presets[i].visMode << " " << presets[i].cursorOn << " " << presets[i].opacity << " " << presets[i].overlayMode << "\n";
        }
        outFile.close();
    }

    audio.Stop();
    CloseWindow();
    return 0;
}