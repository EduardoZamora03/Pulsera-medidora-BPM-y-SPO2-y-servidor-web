#include <Wire.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"
#include <HardwareSerial.h>

// ======================
// SERIAL PARA ENVÍO
// ======================
HardwareSerial Datos(2);  // TX=17, RX=16

// ======================
// OBJETOS
// ======================
TFT_eSPI tft = TFT_eSPI();
MAX30105 particleSensor;

// ======================
// VARIABLES BPM
// ======================
const byte RATE_SIZE = 6;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
int bpmAvg = 0;
int bpmPrev = -1;

// ======================
// VARIABLES SpO2
// ======================
uint32_t irBuffer[50];
uint32_t redBuffer[50];
int32_t bufferLength = 50;
int spo2Value = 0;
int hrFromSpO2 = 0;
int spo2Prev = -1;
int8_t validSPO2, validHR;

// ======================
// GUI
// ======================
int hudX = 30;
int hudBPM_Y = 40;
int hudSPO2_Y = 160;
int hudW = 180;
int hudH = 80;

// ======================
// TIMERS
// ======================
unsigned long lastSwitch = 0;
const unsigned long interval = 5000; // 5 segundos por modo
bool modoBPM = true;

void configurarSensorBPM() {
  particleSensor.setup(0x1F, 4, 2, 400, 411, 4096);
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);
}

void configurarSensorSpO2() {
  particleSensor.setup(60, 4, 2, 100, 411, 4096); // modo robusto
}

//Dibujar HUD
void dibujarHUD(int y, String titulo, uint16_t color) {
  tft.fillRoundRect(hudX, y, hudW, hudH, 20, TFT_DARKGREY);
  tft.drawRoundRect(hudX, y, hudW, hudH, 20, TFT_WHITE);

  // Icono corazón
  int cx = hudX + 35;
  int cy = y + 40;
  tft.fillCircle(cx - 6, cy - 5, 10, TFT_RED);
  tft.fillCircle(cx + 6, cy - 5, 10, TFT_RED);
  tft.fillTriangle(cx - 16, cy - 2, cx + 16, cy - 2, cx, cy + 18, TFT_RED);

  tft.setTextColor(color, TFT_DARKGREY);
  tft.setTextSize(2);
  tft.setCursor(hudX + 90, y + 10);
  tft.print(titulo);
}

void actualizarNumero(int valor, int y, uint16_t color, bool isSpO2=false) {
  tft.fillRect(hudX + 90, y + 35, 80, 40, TFT_DARKGREY);
  tft.setTextColor(color, TFT_DARKGREY);
  tft.setTextSize(4);
  tft.setCursor(hudX + 90, y + 35);
  if(valor>0) {
    tft.print(valor);
    if(isSpO2) { tft.setTextSize(2); tft.print("%"); }
  } else {
    tft.setTextSize(3);
    tft.print("--");
  }
}

void ejecutarBPM() {
  //configurarSensorBPM();
  long irValue = particleSensor.getIR();

  if(irValue < 50000) { 
    bpmAvg = 0;
    actualizarNumero(0, hudBPM_Y, TFT_YELLOW);
    String payload = "{\"ir\":" + String(irValue) + ",\"bpm\":0}";
    Datos.println(payload);
    return;
  }

  if(checkForBeat(irValue)) {
    long delta = millis() - lastBeat;
    if(delta < 250) return;

    lastBeat = millis();
    float bpm = 60 / (delta / 1000.0);

    if(bpm>40 && bpm<255) {
      bool valido = true;
      if(bpmAvg>0 && abs(bpmAvg - bpm)>30) valido=false;
      if(valido || bpmAvg==0){
        rates[rateSpot++] = (byte)bpm;
        rateSpot %= RATE_SIZE;

        long sum=0; byte count=0;
        for(byte i=0;i<RATE_SIZE;i++){if(rates[i]!=0){sum+=rates[i]; count++;}}
        if(count>0) bpmAvg = sum/count;

        if(bpmAvg!=bpmPrev){
          actualizarNumero(bpmAvg, hudBPM_Y, TFT_YELLOW);
          bpmPrev = bpmAvg;
        }
      }
    }
  }

  String payload = "{\"ir\":" + String(irValue) +
                 ",\"bpm\":" + String(bpmAvg) +
                 ",\"spo2\":" + String(spo2Value) + "}";
  Datos.println(payload);
}

void ejecutarSpO2() {
  // Mover los valores viejos del buffer hacia atrás
  //configurarSensorSpO2();
  for (byte i = 25; i < 50; i++) {
    redBuffer[i - 25] = redBuffer[i];
    irBuffer[i - 25] = irBuffer[i];
  }

  // Leer nuevas 25 muestras
  for (byte i = 25; i < 50; i++) {
    while(!particleSensor.available()) particleSensor.check();
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
  }

  // Calcular SpO2 y HR
  maxim_heart_rate_and_oxygen_saturation(irBuffer, 50, redBuffer, &spo2Value, &validSPO2, &hrFromSpO2, &validHR);

  // Actualizar pantalla solo si hay valor válido
  if(validSPO2 && spo2Value > 60 && spo2Value <= 100){
    if(spo2Value != spo2Prev){
      actualizarNumero(spo2Value, hudSPO2_Y, TFT_CYAN, true);
      spo2Prev = spo2Value;
    }
  }
  
  String payload = "{\"ir\":0,"          // No usas IR aquí
                  "\"bpm\":" + String(bpmAvg) +
                 ",\"spo2\":" + String(spo2Value) + "}";
  Datos.println(payload);
}

void setup() {
  Serial.begin(115200);
  Datos.begin(115200, SERIAL_8N1, 16, 17);
  tft.init(); tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);

  Wire.begin(21,22);

  if(!particleSensor.begin(Wire, I2C_SPEED_FAST)){
    tft.fillScreen(TFT_RED); while(1);
  }

  configurarSensorBPM();
  configurarSensorSpO2();
  dibujarHUD(hudBPM_Y,"BPM",TFT_YELLOW);
  dibujarHUD(hudSPO2_Y,"SpO2",TFT_CYAN);

  for(int i=0;i<RATE_SIZE;i++) rates[i]=0;
  lastBeat = millis();
}

void loop() {
  unsigned long now = millis();
  if(now - lastSwitch >= interval){
    modoBPM = !modoBPM;
    lastSwitch = now;

  }

  if(modoBPM) ejecutarBPM();
  else ejecutarSpO2();
}




