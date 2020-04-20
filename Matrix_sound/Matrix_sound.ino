
// all these libraries are required for the Teensy Audio Library
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <Math.h>

//#include <SmartLEDShieldV4.h>  // comment out this line for if you're not using SmartLED Shield V4 hardware (this line needs to be before #include <SmartMatrix3.h>)
#include <SmartMatrix3.h>
#include <FastLED.h>

#define COLOR_DEPTH 24                  // known working: 24, 48 - If the sketch uses type `rgb24` directly, COLOR_DEPTH must be 24
const uint8_t matrixWidth = 128;        // known working: 32, 64, 96, 128
const uint8_t matrixHeight = 32;       // known working: 16, 32, 48, 64
const uint8_t refreshDepth = 48;       // known working: 24, 36, 48
const uint8_t dmaBufferRows = 4;       // known working: 2-4, use 2 to save memory, more to keep from dropping frames and automatically lowering refresh rate
const uint8_t panelType = SMARTMATRIX_HUB75_32ROW_MOD16SCAN; // use SMARTMATRIX_HUB75_16ROW_MOD8SCAN for common 16x32 panels, or use SMARTMATRIX_HUB75_64ROW_MOD32SCAN for common 64x64 panels
const uint8_t matrixOptions = (SMARTMATRIX_OPTIONS_NONE);      // see http://docs.pixelmatix.com/SmartMatrix for options
const uint8_t backgroundLayerOptions = (SM_BACKGROUND_OPTIONS_NONE);
const uint8_t freqBands = 128;

SMARTMATRIX_ALLOCATE_BUFFERS(matrix, matrixWidth, matrixHeight, refreshDepth, dmaBufferRows, panelType, matrixOptions);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(backgroundLayer, matrixWidth, matrixHeight, COLOR_DEPTH, backgroundLayerOptions);

// microphone input pin
#define MIC_PIN   A14

AudioInputAnalog         input(MIC_PIN);
AudioAnalyzeFFT1024      fft;
AudioConnection          audioConnection(input, 0, fft, 0);

// maximum frequency to show on display in Hz
int frequencyCutoff = 6000;
// audio is recorded at 44100 samples/second, FFT can only detect at half of sample frequency so divide by 2
// FFT size is 1024, half is unreal, half real, we only care about real, so 1024/2 = 512 buckets
// dividing maximum detectable frequency by number of buckets gives us Hz per bucket
int cutoffBucket = frequencyCutoff / ((44100.0/2.0) / 512.0);

// The scale sets how much sound is needed in each frequency range to
// show all 32 bars.  Higher numbers are more sensitive.
// This is automatically set based on highest volume
float scale = 0.0;

// used to quell the screen when there is no audio
float scaleCutoff = 8000.0;

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

float currMaxVal = 0.0;

float brightnessScalar = 5000.0;

int lastBrightness = 0;

// How long to wait before decreasing scale to account for lower volume
unsigned long maxResetTime = 0.5 * 1000;
// Last time scale was reset
unsigned long lastReset = 0.0;
// how long to fade the scale change over
unsigned long fadeDuration = 5.0 * 1000;

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
    backgroundLayer.enableColorCorrection(true);
    matrix.begin();

    matrix.setBrightness(255);

    // Audio requires memory to work.
    AudioMemory(12);
}

void loop()
{
    // did each channel reach a new maximum this sample
    // used to move white bar up
    int brightness = sq(currMaxVal) * brightnessScalar;
    brightness = constrain(brightness, 0, 255);
    if (brightness != lastBrightness) {
        Serial.println(brightness);
        matrix.setBrightness(brightness);
        lastBrightness = brightness;
    }
    
    bool newPeak[freqBands] = {false};

    
    unsigned long currTime = millis();

    // see if we reset the scaling due to timeout from quiet sounds
    if (currTime > lastReset + maxResetTime) {
            // Serial.println(maxVal);
            currMaxVal = lerp(lastReset + maxResetTime, currTime, lastReset + maxResetTime + fadeDuration, maxVal, 0);
            
            // maxVal = 0;
     }
     
    if (fft.available()) {

        readFFT(cutoffBucket, freqBands);

        // check if we need to scale to account for louder sounds
        for (int i = 0; i < freqBands; i++) {
          if (level[i] > currMaxVal) {
            maxVal = level[i];
            currMaxVal = maxVal;
            lastReset = currTime;
          }
        }
        
        // calculate scale so loudest value fits on the display
        scale = (matrixHeight - 1) / currMaxVal;
        // Serial.println(scale);

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
            if (val >= matrixHeight) val = matrixHeight - 1;

            if (val >= shown[i]) {
                shown[i] = val;
            }
            else {
                if (shown[i] > 0) shown[i] = shown[i] - 1;
                val = shown[i];
            }


            // draw the levels on the matrix
            // 0,0 is the top left of the display so 'matrixHeight - value' is used to draw from the bottom up
            if (shown[i] >= 0) {

              // if the column didn't need to move the white line up we need to make it fall
              if (!newPeak[i]) {
                
                // calculates new position of falling white bar
                // exponential over time to mimic gravity
                lvl[i] = (fallLevel[i] - (currTime - fallTimes[i])/fallTime);
                lvl[i] = constrain(lvl[i], 1, matrixHeight);
                fallLevel[i] = lvl[i];
              }
              else {
                lvl[i] = fallLevel[i];
              }
                // scale the bars horizontally to fill the matrix width
                for (int j = 0; j < (matrixWidth / freqBands); j++) {
                    for (int k = 0; k <= val; k++) {

                      // color is based on distance from bottom of display
                      SM_RGB color = CRGB(CHSV((128 - k * 4), 255, 255));
                      backgroundLayer.drawPixel(i * (matrixWidth / freqBands) + j, matrixHeight - k, color);

                      // draw white bar
                      backgroundLayer.drawPixel(i * (matrixWidth / freqBands) + j, matrixHeight - lvl[i], barColor);
                    }
                }
            }
        }

        backgroundLayer.swapBuffers();
    }
}

// Transition from one value to another over a set time duration
float lerp(float startTime, float currTime, float stopTime, float startValue, float endValue) {
    float timeOffset = currTime - startTime;
    float timePct = timeOffset / (stopTime - startTime);
    float output = startValue - (startValue * timePct);
    output = constrain(output,endValue, startValue);
    return output;
}

// Convert the buckets of frequencies that the FFT outputs into bars for our display
// Because hearing is non-linear but the FFT output is,
// we group multiple higer frequency buckets into one bar for a better representation of human hearing

// 'fftSize' is the number of buckets from the FFT we are willing to consider
// set this to less than the maximum FFT size to cut off higher end frequencies

void readFFT (int fftSize, int numBars) {
    // pow(2.0,((X - 1)/12.0)) is the equation for converting music notes into frequencies
    // we use this to group our frequencies and select which FFT buckets align to which bar on the display

    // calculate the highest value bucket we will select based on the number of bars we want to display
    float max = pow(2.0,((numBars - 1)/12.0))-1;
    float scalar = (fftSize - 1) / max;

    // we can read from a range of buckets at the same time
    // this is how we blend higher frequencies to create a more 'hear accurate' display
    int bucket[numBars];
    int minBucket[numBars] = {1};

    for (int i = 0; i <= numBars; i++) {
        // Find the corresponding buckets for each bar
        bucket[i] = (pow(2.0,(i/12.0))-1)*scalar + 2;
        // if our bucket is the same as the last one, we'll have 2 bars on the display showing the same data
        // so we make sure each bucket is a lest 1 bigger than the last
        if (i > 0 && bucket[i] <= bucket[i-1]) {
            bucket[i] = bucket[i-1] + 1;
        }

        // makes the 'minBucket' value 1 greater than the previous 'bucket' value
        if (bucket[i] - bucket[i-1] > 1) {
            minBucket[i] = bucket[i-1] + 1;
        }
        else {
            minBucket[i] = bucket[i];
        }

        if (minBucket[i] == bucket[i]) {
            level[i] = fft.read(bucket[i]);
        }
        else {
            level[i] = fft.read(minBucket[i], bucket[i]);
            // when requesting more than one bucket, fft returns the sum of the values
            // we divide by the number of buckets requested to get an average
            level[i] = level[i] / (bucket[i] - minBucket[i] + 1);
        }

        // lower frequencies have more power so we need to scale audio power into linear 
        level[i] *= sqrt(float(minBucket[i])) / (fftSize);
        level[i] *= fftSize;
    }      
}
