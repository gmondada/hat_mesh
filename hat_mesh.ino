#include "painlessMesh.h"
#include "FastLED.h"


/*** literals ***/

// WiFi Credentials
#define MESH_PREFIX     "y-fablab.ch/hat"
#define MESH_PASSWORD   "keep.it.secret"
#define MESH_PORT       5678

#define NUM_LEDS        20
#define LED_PIN         5  // D1
#define LED_INTENSITY   16 // max is 256


/*** types ***/

struct Seq {
  void (* func)();
  int duration; // in ms
};


/*** prototypes ***/

void resyncSeq();


/*** globals ***/

extern Seq sequences[];
extern int seqCount;

Scheduler userScheduler; 
painlessMesh mesh;
CRGB leds[NUM_LEDS];

uint32_t seqStart;     // absolute time in us
uint32_t seqDuration;  // in us
uint32_t seqWaitTime;  // time from seqStart, in us
uint32_t prgDuration;  // duration of all sequneces, in us


/*** functions ***/

void receivedCallback( uint32_t from, String &msg ) {
  Serial.printf("Received message from nodeId=%u: %s\n", from, msg.c_str());
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("New Connection: nodeId=%u\n", nodeId);
}

void changedConnectionCallback() {
  Serial.printf("Changed connections\n");
}

void nodeTimeAdjustedCallback(int32_t offset) {
  Serial.printf("Adjusted time: t=%u offset=%d\n", mesh.getNodeTime(), offset);
}

/*
 * Wait some time by staying perfectly locked to the mesh network time.
 * If we cannot stay locked to the mesh network time, this funciton
 * returns true. It also returns true when we slide outside the time slot
 * reserved for the current sequence. In both cases, the caller has to stop
 * the sequence.
 */
bool lockedDelay(uint32_t ms) {
  uint32_t waitTime = ms * 1000;
  seqWaitTime += waitTime;
  for (;;) {
    mesh.update();

    // time since seq start
    int timeInSeq = mesh.getNodeTime() - seqStart;
    if (timeInSeq >= seqDuration)
      return true;
    if (timeInSeq < -500000)
      return true;

    // check is we reached the waited time
    int d = timeInSeq - seqWaitTime;
    if (d < -(int)waitTime - 500000)
      return true;
    if (d > 500000)
      return true;
    if (d >= 0)
      return false;
  }
}

/*
 * Wait some time by keeping the mesh network alive.
 * This function doesn't try to stay locked to the mesh network time.
 * It just waits the given time and returns false.
 * If we slide outside the time slot reserved for the current sequence,
 * this function return true, which means that the caller has to stop
 * the sequence. 
 */
bool unlockedDelay(uint32_t ms) {
  uint32_t waitTime = ms * 1000;
  uint32_t t1 = mesh.getNodeTime();
  for (;;) {
    mesh.update();

    uint32_t t2 = mesh.getNodeTime();

    // time since seq start
    int timeInSeq = t2 - seqStart;
    if (timeInSeq >= seqDuration)
      return true;
    if (timeInSeq < -500000)
      return true;

    // check is we reached the waited time
    int d = t2 - t1;
    if (d < -500000)
      return false;
    if (d > waitTime + 500000)
      return false;
    if (d >= waitTime)
      return false;
  }
}

inline byte scaleIntensity(byte value)
{
  return ((uint16_t)value * (uint16_t)LED_INTENSITY) >> 8;
}

void setPixel(int index, CRGB color) {
  if (index >= 0 && index < NUM_LEDS) {
    color.r = scaleIntensity(color.r);
    color.g = scaleIntensity(color.g);
    color.b = scaleIntensity(color.b);    
    leds[index] = color;
  }
}

inline void setPixel(int index, byte r, byte g, byte b) {
  setPixel(index, CRGB(r, g, b));
}

void setAll(CRGB color) {
  for (int i = 0; i < NUM_LEDS; i++) {
    setPixel(i, color);
  }
}

inline void setAll(byte r, byte g, byte b) {
  setAll(CRGB(r, g, b));
}

void showStrip() {
  FastLED.show();
}

void startSeq() {
  uint32_t t = mesh.getNodeTime();
  uint32_t timeInPrg = t % prgDuration;
  int seq = 0;
  uint32_t timeInSeq = timeInPrg;
  for (int i=0; i<seqCount; i++) {
    if (timeInSeq < sequences[i].duration * 1000) {
      seq = i;
      break;  
    }
    timeInSeq -= sequences[i].duration * 1000;
  }
  Serial.printf("start seq=%d timeInSeq=%dus\n", seq, timeInSeq);
  seqDuration = sequences[seq].duration * 1000;
  seqStart = t - timeInSeq;
  seqWaitTime = 0;
  if (timeInSeq > seqDuration - 500000) {
    // We are late to start this sequnece.
    // We are quite close to the end, so we just wait.
    setAll(0, 0, 0);
    showStrip();
    for (;;) {
      bool b = unlockedDelay(0);
      if (b)
        break;
    }
  } else if (timeInSeq > 500000) {
    // We are late to start this sequnece.
    // We prefer to start a placeholder sequence instead.
    resyncSeq();
  } else {
    // We are in time to start
    sequences[seq].func();
  }
}

void setup() {
  Serial.begin(115200);

  FastLED.addLeds<WS2811, LED_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.show();

  mesh.setDebugMsgTypes(ERROR | STARTUP);  // set before init() so that you can see startup messages
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
  
  // userScheduler.addTask(taskTimer);
  // taskTimer.enable();

  prgDuration = 0;
  for (int i=0; i<seqCount; i++) {
    prgDuration += sequences[i].duration * 1000;
  }
}

void loop() {
  startSeq();
}


/*** sequences ***/

/*
 * Sequence use when we are resynchronizing the program.
 * This is a sequence that can run forever.
 */
void resyncSeq() {
  for (;;) {
    for (int i=0; i<3; i++) {
      for (int j=0; j<NUM_LEDS; j++) {
        byte v = ((j % 3) == i) ? 255 : 0;
        setPixel(j, v, v, v);
      }
      showStrip();
      if (unlockedDelay(50))
        return;
    }
  }
}

// Sine

void sineSeq() {
  int width = 3;
  int t = 0;
  for (;;) {
    float posf = cosf((float)t * 2.0f * (float)M_PI * 0.01f) * 0.5f + 0.5f;
    int pos = (int)floorf(posf * ((float)(NUM_LEDS - width + 1) - 0.001f));
    setAll(0, 0, 0);
    setPixel(pos + 0, 255, 0, 255);
    setPixel(pos + 1, 255, 0, 255);
    setPixel(pos + 2, 255, 0, 255);
    showStrip();
    if (lockedDelay(10))
      return;
    t++;
  }
}

// Rainbow Cycle

CRGB wheel(byte WheelPos) {
  CRGB c;
  if (WheelPos < 85) {
    c.r = WheelPos * 3;
    c.g = 255 - WheelPos * 3;
    c.b = 0;
  } else if (WheelPos < 170) {
    WheelPos -= 85;
    c.r = 255 - WheelPos * 3;
    c.g = 0;
    c.b = WheelPos * 3;
  } else {
    WheelPos -= 170;
    c.r = 0;
    c.g = WheelPos * 3;
    c.b = 255 - WheelPos * 3;
  }
  return c;
}

void rainbowCycle() {
  for (int j=0; j<256*4; j++) { // 4 cycles of all colors on wheel
    for (int i=0; i< NUM_LEDS; i++) {
      CRGB c = wheel(((i * 256 / NUM_LEDS) + j) & 255);
      setPixel(i, c);
    }
    showStrip();
    if (lockedDelay(10))
      return;
  }
}

// Running Light

void runningLights(byte red, byte green, byte blue, int WaveDelay) {
  int Position = 0;
 
  for(int j=0; j<NUM_LEDS * 4; j++) {
      Position++; // = 0; //Position + Rate;
      for(int i=0; i<NUM_LEDS; i++) {
        // sine wave, 3 offset waves make a rainbow!
        //float level = sin(i+Position) * 127 + 128;
        //setPixel(i,level,0,0);
        //float level = sin(i+Position) * 127 + 128;
        setPixel(i, ((sin(i+Position) * 127 + 128)/255)*red,
                    ((sin(i+Position) * 127 + 128)/255)*green,
                    ((sin(i+Position) * 127 + 128)/255)*blue);
      }     
      showStrip();
      if (lockedDelay(WaveDelay))
        return;
  }
}

void orangeRunningLights() {
  runningLights(0xff, 0xa2, 0x03, 50);
}

// FBI warning

int shift(int index) {
  return index;
}

void FBI() {
  for (int cycle=0;cycle<8;cycle++) {
    for (int turn=0;turn<3;turn++) {
      for (int a=0; a<NUM_LEDS/2;a++) {
        setPixel(shift(a), CRGB(0xff, 0, 0));
      }
      showStrip();
      if (lockedDelay(35))
        return;
      setAll(CRGB(0,0,0));
      showStrip();
      if (lockedDelay(35))
        return;
    }
      
    for (int turn=0;turn<3;turn++) {
      for (int a=NUM_LEDS/2; a<NUM_LEDS;a++) {
        setPixel(shift(a), CRGB(0, 0, 0xff));
      }
      showStrip();
      if (lockedDelay(35))
        return;
      setAll(CRGB(0,0,0));
      showStrip();
      if (lockedDelay(35))
        return;
    }
  }
}

// Meteor rain

void fadeToBlack(int ledNo, byte fadeValue) {
   // FastLED
   leds[ledNo].fadeToBlackBy( fadeValue );
}

bool meteorRain(byte red, byte green, byte blue, byte meteorSize, byte meteorTrailDecay, boolean meteorRandomDecay, int SpeedDelay) {  
  setAll(0, 0, 0);
  showStrip();
 
  for (int i = 0; i < 3 * NUM_LEDS; i++) {
 
    // fade brightness all LEDs one step
    for (int j=0; j<NUM_LEDS; j++) {
      if ( (!meteorRandomDecay) || (random(10)>5) ) {
        leds[j].fadeToBlackBy(meteorTrailDecay);
      }
    }
   
    // draw meteor
    for (int j = 0; j < meteorSize; j++) {
      if ( ( i-j <NUM_LEDS) && (i-j>=0) ) {
        setPixel(i-j, red, green, blue);
      }
    }
   
    showStrip();
    if (lockedDelay(SpeedDelay))
      return true;
  }
  
  return false;
}

void meteorRainSeq() {
  for (int cycle=0; cycle<4; cycle++) {
    if (meteorRain(0xff,0xff,0xff,10, 64, true, 30))
      return;
  }
}

// Persisten Sparkle

void persistentSparkleSeq() {
  setAll(0, 0, 0);
  showStrip();
  for (;;) {
    for (int j=0; j<NUM_LEDS; j++) {
      leds[j].fadeToBlackBy(1);
    }
    int index = random(NUM_LEDS);
    setPixel(index, 63, 127, 255);
    showStrip();
    if (unlockedDelay(10))
      return;
  }
}

// Sequence Summary

/**
 * This array contais all sequences to execute.
 * Each sequence is composed of a function and a duration in milliseconds.
 * The function must never call Delay(). Instead, it has to call lockedDelay()
 * and take care of the returned value. In fact, when lockedDelay()
 * returns true, the sequence has to stop immediately.
 */
Seq sequences[] = {
  {sineSeq, 3000},
  {persistentSparkleSeq, 4000},
  {rainbowCycle, 256 * 4 * 10},
  {FBI, 8 * 2 * 3 * 70},
  {orangeRunningLights, NUM_LEDS * 4 * 50},
  {meteorRainSeq, 4 * 3 * NUM_LEDS * 30},
};

int seqCount = sizeof(sequences) / sizeof(*sequences);
