
// all these libraries are required for the Teensy Audio Library
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

//#include <SmartLEDShieldV4.h>  // comment out this line for if you're not using SmartLED Shield V4 hardware (this line needs to be before #include <SmartMatrix3.h>)
#include <SmartMatrix3.h>
#include <FastLED.h>

#define COLOR_DEPTH 24                  // known working: 24, 48 - If the sketch uses type `rgb24` directly, COLOR_DEPTH must be 24
const uint8_t kMatrixWidth = 64;        // known working: 32, 64, 96, 128
const uint8_t kMatrixHeight = 32;       // known working: 16, 32, 48, 64
const uint8_t kRefreshDepth = 48;       // known working: 24, 36, 48
const uint8_t kDmaBufferRows = 4;       // known working: 2-4, use 2 to save memory, more to keep from dropping frames and automatically lowering refresh rate
const uint8_t kPanelType = SMARTMATRIX_HUB75_32ROW_MOD16SCAN; // use SMARTMATRIX_HUB75_16ROW_MOD8SCAN for common 16x32 panels, or use SMARTMATRIX_HUB75_64ROW_MOD32SCAN for common 64x64 panels
const uint8_t kMatrixOptions = (SMARTMATRIX_OPTIONS_NONE);      // see http://docs.pixelmatix.com/SmartMatrix for options
const uint8_t kBackgroundLayerOptions = (SM_BACKGROUND_OPTIONS_NONE);
const uint8_t freqBands = 32;

SMARTMATRIX_ALLOCATE_BUFFERS(matrix, kMatrixWidth, kMatrixHeight, kRefreshDepth, kDmaBufferRows, kPanelType, kMatrixOptions);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(backgroundLayer, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kBackgroundLayerOptions);

#define ADC_INPUT_PIN   A14

AudioInputAnalog         input(ADC_INPUT_PIN);
AudioAnalyzeFFT256       fft;
AudioConnection          audioConnection(input, 0, fft, 0);

// The scale sets how much sound is needed in each frequency range to
// show all 32 bars.  Higher numbers are more sensitive.
// This is automatically set based on highest volume
float scale = 0.0;

// used to quell the screen when there is no audio
float scaleCutoff = 5000.0;

// An array to hold the 32 frequency bands
float level[freqBands];

int fallLevel[freqBands] = {0};
unsigned long fallTimes[freqBands] = {0.0};

// This array holds the on-screen levels.  When the signal drops quickly,
// these are used to lower the on-screen level 1 bar per update, which
// looks more pleasing to corresponds to human sound perception.
int shown[freqBands];

// holds the maximum level across all bars
// used for auto-scaling with volume
float maxVal = 0.0;

// How long to wait before decreasing scale to account for lower volume
unsigned long maxResetTime = 5.0 * 1000;
// Last time scale was reset
unsigned long lastReset = 0.0;

// Time in ms before lowering the white bars one line
int fallTime = 200;

// White bar color   currently white
SM_RGB barColor = CRGB(255,255,255);



const SM_RGB black = CRGB(0, 0, 0);

byte status = 0;

void setup()
{
    Serial.begin(9600);

    // Initialize Matrix
    matrix.addLayer(&backgroundLayer); 
    matrix.begin();

    matrix.setBrightness(255);

    // Audio requires memory to work.
    AudioMemory(12);
}

void loop()
{
    // did each channel reach a new maximum this sample
    // used to move white bar up
    bool newPeak[freqBands] = {false};

    
    unsigned long currTime = millis();

    // see if we reset the scaling due to timeout from quiet sounds
    if (currTime > lastReset + maxResetTime) {
          maxVal = 0;
     }
     
    if (fft.available()) {
        // read the 128 FFT frequencies into 'freqBands' levels
        // music is heard in octaves, but the FFT data
        // is linear, so for the higher octaves, read
        // many FFT bins together.

        level[0] = fft.read(2);
        level[1] = fft.read(3);
        level[2] = fft.read(4);
        level[3] = fft.read(5);
        level[4] = fft.read(6);
        level[5] = fft.read(7);
        level[6] = fft.read(8);
        level[7] = fft.read(9);
        level[8] = fft.read(10);
        level[9] = fft.read(11);
        level[10] = fft.read(12);
        level[11] = fft.read(13, 14);
        level[12] = fft.read(15, 16);
        level[13] = fft.read(17, 19);
        level[14] = fft.read(20, 22);
        level[15] = fft.read(23, 25);
        level[16] = fft.read(26, 28);
        level[17] = fft.read(29, 31);
        level[18] = fft.read(32, 34);
        level[19] = fft.read(35, 38);
        level[20] = fft.read(39, 43);
        level[21] = fft.read(44, 47);
        level[22] = fft.read(48, 52);
        level[23] = fft.read(53, 57);
        level[24] = fft.read(58, 63);
        level[25] = fft.read(64, 69);
        level[26] = fft.read(70, 78);
        level[27] = fft.read(79, 85);
        level[28] = fft.read(86, 92);
        level[29] = fft.read(93, 102);
        level[30] = fft.read(103, 115);
        level[31] = fft.read(116, 127);

        // check if we need to scale to account for louder sounds
        for (int i = 0; i < freqBands; i++) {
          if (level[i] > maxVal) {
            maxVal = level[i];
            lastReset = currTime;
          }
        }
        
        // calculate scale so loudest value fits on the display
        scale = (kMatrixHeight - 1) / maxVal;
        Serial.println(scale);

//        if (scale > scaleCutoff) {
//          scale = scaleCutoff;
//        }

        backgroundLayer.fillScreen(black);

        for (int i = 0; i < freqBands; i++) {

            // scale incoming values from the fft to represent lines on the display
            int val = level[i] * scale;
            int lvl[freqBands];

            // reset the falling timer and position of the white bar if we need to move it up
            if (val > fallLevel[i]) {
              fallLevel[i] = val;
              fallTimes[i] = currTime;
              newPeak[i] = true;
            }

            // trim the bars vertically to fill the matrix height
            if (val >= kMatrixHeight) val = kMatrixHeight - 1;

            if (val >= shown[i]) {
                shown[i] = val;
            }
            else {
                if (shown[i] > 0) shown[i] = shown[i] - 1;
                val = shown[i];
            }


            // draw the levels on the matrix
            // 0,0 is the top left of the display so 'kMatrixHeight - value' is used to draw from the bottom up
            if (shown[i] >= 0) {

              // if the column didn't need to move the white line up we need to make it fall
              if (!newPeak[i]) {
                
                // calculates new position of falling white bar
                // exponential over time to mimic gravity
                lvl[i] = (fallLevel[i] - (currTime - fallTimes[i])/fallTime);
                
                if (lvl[i] > kMatrixHeight) {
                  lvl[i] = kMatrixHeight;
                }
                if (lvl[i] < 1) {
                  lvl[i] = 1;
                }
                fallLevel[i] = lvl[i];
              }
              else {
                lvl[i] = fallLevel[i];
              }
                // scale the bars horizontally to fill the matrix width
                for (int j = 0; j < kMatrixWidth / 16; j++) {
                    for (int k = 0; k <= val; k++) {

                      // color is based on distance from bottom of display
                      SM_RGB color = CRGB(CHSV((128 - k * 4), 255, 255));
                      backgroundLayer.drawPixel(i * 4 + j, kMatrixHeight - k, color);

                      // draw white bar
                      backgroundLayer.drawPixel(i * 4 + j, kMatrixHeight - lvl[i], barColor);
                    }
                }
            }
        }

        backgroundLayer.swapBuffers();
    }
}
