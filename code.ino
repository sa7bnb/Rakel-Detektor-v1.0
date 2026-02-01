/*
 * =============================================================================
 *     Rakel-Detektor-v1.0
 * =============================================================================
 * 
 * Hårdvara:
 *   - ESP32-C3 SuperMini
 *   - AD8313 logaritmisk detektor (med 380-385 MHz LC-filter)
 *   - SSD1306 OLED 0.96" (I2C: SDA=GPIO6, SCL=GPIO7)
 *   - Passiv buzzer (GPIO3)
 *   - AD8313 VOUT -> GPIO0 (ADC)
 * 
 * Beskrivning:
 *   Läser av RF-signalstyrka från AD8313-modulen (via egenbyggt LC-filter
 *   för 380-385 MHz TETRA-bandet). Visar signalstyrka i dBm på OLED med
 *   bargraf, peak-hold, och signalhistorik. Buzzer ger akustisk indikering
 *   proportionell mot signalstyrkan.
 * 
 * AD8313 Specifikationer (RSSI-läge, standardkoppling):
 *   - Slope:     ~18 mV/dB
 *   - Intercept: ~-100 dBm (50 Ohm)
 *   - VOUT = slope_mV * (Pin_dBm - intercept_dBm) 
 *   - Dynamiskt område: ca -70 dBm till 0 dBm
 * 
 * Kalibrering:
 *   Justera AD8313_SLOPE_MV och AD8313_INTERCEPT_DBM efter din hårdvara.
 *   Se kalibreringsrutinen nedan.
 * 
 * Av: SA7BNB
 * Datum: 2026-02-01
 * =============================================================================
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =============================================================================
// PIN-KONFIGURATION (ESP32-C3 SuperMini)
// =============================================================================
#define PIN_AD8313_VOUT   0    // ADC-ingång för AD8313 VOUT (GPIO0 = ADC1_CH0)
#define PIN_BUZZER        3    // Buzzer PWM-utgång
#define PIN_SDA           6    // I2C SDA för OLED
#define PIN_SCL           7    // I2C SCL för OLED

// =============================================================================
// OLED-KONFIGURATION
// =============================================================================
#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     64
#define OLED_RESET        -1
#define OLED_I2C_ADDR     0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =============================================================================
// AD8313 KALIBRERING
// =============================================================================
// Standardvärden från databladet. Justera efter din specifika modul!
// 
// Formel: dBm = (VOUT_mV / slope_mV) + intercept_dBm
//
// Kalibreringsförfarande:
// 1. Mata in en känd signalnivå (t.ex. -30 dBm) till AD8313
// 2. Läs av raw ADC-värdet / spänningen
// 3. Upprepa med en annan nivå (t.ex. -60 dBm)
// 4. Beräkna slope = (V1 - V2) / (dBm1 - dBm2)
// 5. Beräkna intercept = dBm1 - (V1 / slope)

#define AD8313_SLOPE_MV       18.0    // mV per dB (typiskt 17-20 mV/dB)
#define AD8313_INTERCEPT_DBM  -100.0  // dBm intercept (typiskt -100 dBm @ 50 Ohm)

// ADC-konfiguration ESP32-C3
#define ADC_RESOLUTION    12          // 12-bit ADC
#define ADC_MAX_VALUE     4095        // 2^12 - 1
#define ADC_REF_VOLTAGE   3300.0      // ESP32-C3 ADC referensspänning i mV (3.3V)

// =============================================================================
// DETEKTOR-INSTÄLLNINGAR
// =============================================================================
#define SIGNAL_THRESHOLD_DBM   -65.0  // Tröskel för "signal detekterad" (dBm)
#define ALARM_THRESHOLD_DBM    -40.0  // Tröskel för stark signal-alarm (dBm)
#define NOISE_FLOOR_DBM        -75.0  // Brusgolv (under detta = inget att visa)
#define MAX_SIGNAL_DBM           0.0  // Maximal förväntad signal

// Buzzer
#define BUZZER_MIN_FREQ       800     // Lägsta buzzer-frekvens (Hz)
#define BUZZER_MAX_FREQ       4000    // Högsta buzzer-frekvens (Hz)
#define BUZZER_CHANNEL        0       // LEDC-kanal för PWM

// Sampling & medelvärdesbildning
#define NUM_ADC_SAMPLES       32      // Antal ADC-samplingar för medelvärde
#define SAMPLE_INTERVAL_MS    50      // Tid mellan uppdateringar (ms)
#define PEAK_HOLD_TIME_MS     2000    // Peak-hold tid (ms)

// Historik/graf
#define HISTORY_LENGTH        64      // Antal historikpunkter (pixlar bred)
#define HISTORY_HEIGHT        20      // Höjd på historikgrafen (pixlar)

// =============================================================================
// GLOBALA VARIABLER
// =============================================================================
float currentDbm = NOISE_FLOOR_DBM;
float peakDbm = NOISE_FLOOR_DBM;
unsigned long peakTime = 0;
float signalHistory[HISTORY_LENGTH];
int historyIndex = 0;
bool buzzerEnabled = true;
unsigned long lastSampleTime = 0;
unsigned long lastDisplayTime = 0;
unsigned long signalDetectedCount = 0;
float maxSessionDbm = NOISE_FLOOR_DBM;
float avgDbm = NOISE_FLOOR_DBM;

// EMA (Exponential Moving Average) filter
#define EMA_ALPHA  0.3  // 0.0–1.0, högre = snabbare respons
float emaDbm = NOISE_FLOOR_DBM;

// =============================================================================
// FUNKTIONSPROTOTYPER
// =============================================================================
float readAD8313_dBm();
float adcToMillivolts(int adcValue);
float millivoltsToDbm(float mv);
void updateDisplay();
void updateBuzzer(float dBm);
void updateHistory(float dBm);
void drawBargraph(int x, int y, int width, int height, float dBm);
void drawHistory(int x, int y);
void drawSignalIndicator(int x, int y, float dBm);
void showSplashScreen();

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println(F("================================"));
  Serial.println(F("  TETRA Signal Detector v1.0"));
  Serial.println(F("  380-385 MHz LC-Filter + AD8313"));
  Serial.println(F("  ESP32-C3 SuperMini"));
  Serial.println(F("  SA7BNB"));
  Serial.println(F("================================"));

  // --- ADC Setup ---
  analogReadResolution(ADC_RESOLUTION);
  // ESP32-C3: ADC1 tillgänglig, med 11dB attenuation för 0-3.3V
  analogSetAttenuation(ADC_11db);
  
  // --- Buzzer Setup (LEDC PWM) ---
  ledcAttach(PIN_BUZZER, 2000, 8);  // pin, freq, resolution
  ledcWrite(PIN_BUZZER, 0);         // Starta tyst
  
  // --- I2C & OLED Setup ---
  Wire.begin(PIN_SDA, PIN_SCL);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println(F("OLED SSD1306 hittades inte!"));
    // Blinka buzzer som felindikering
    for (int i = 0; i < 5; i++) {
      ledcWriteTone(PIN_BUZZER, 1000);
      delay(100);
      ledcWrite(PIN_BUZZER, 0);
      delay(100);
    }
    while (1) { delay(1000); } // Häng här vid OLED-fel
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  // --- Initiera historik ---
  for (int i = 0; i < HISTORY_LENGTH; i++) {
    signalHistory[i] = NOISE_FLOOR_DBM;
  }
  
  // --- Splash Screen ---
  showSplashScreen();
  
  // --- Startton ---
  ledcWriteTone(PIN_BUZZER, 1000);
  delay(100);
  ledcWriteTone(PIN_BUZZER, 1500);
  delay(100);
  ledcWriteTone(PIN_BUZZER, 2000);
  delay(100);
  ledcWrite(PIN_BUZZER, 0);
  
  Serial.println(F("System redo. Börjar mätning..."));
  Serial.println(F(""));
  Serial.println(F("RAW_ADC, mV, dBm, Peak_dBm"));
}

// =============================================================================
// HUVUDLOOP
// =============================================================================
void loop() {
  unsigned long now = millis();
  
  // --- Sampling med jämna intervall ---
  if (now - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = now;
    
    // Läs AD8313
    float rawDbm = readAD8313_dBm();
    
    // EMA-filtrering för mjukare display
    emaDbm = (EMA_ALPHA * rawDbm) + ((1.0 - EMA_ALPHA) * emaDbm);
    currentDbm = emaDbm;
    
    // Begränsa till rimligt intervall
    if (currentDbm < NOISE_FLOOR_DBM) currentDbm = NOISE_FLOOR_DBM;
    if (currentDbm > MAX_SIGNAL_DBM)  currentDbm = MAX_SIGNAL_DBM;
    
    // Uppdatera peak-hold
    if (currentDbm > peakDbm) {
      peakDbm = currentDbm;
      peakTime = now;
    } else if (now - peakTime > PEAK_HOLD_TIME_MS) {
      // Sakta sänk peak-värdet
      peakDbm -= 0.5;
      if (peakDbm < currentDbm) peakDbm = currentDbm;
    }
    
    // Session-max
    if (currentDbm > maxSessionDbm) {
      maxSessionDbm = currentDbm;
    }
    
    // Detekteringsräknare
    if (currentDbm > SIGNAL_THRESHOLD_DBM) {
      signalDetectedCount++;
    }
    
    // Uppdatera historik
    updateHistory(currentDbm);
    
    // Buzzer
    updateBuzzer(currentDbm);
    
    // Seriell logg (för kalibrering och debugging)
    // Läs raw ADC en gång till för logg
    int rawAdc = analogRead(PIN_AD8313_VOUT);
    float mv = adcToMillivolts(rawAdc);
    Serial.printf("%d, %.1f, %.1f, %.1f\n", rawAdc, mv, currentDbm, peakDbm);
  }
  
  // --- Display-uppdatering (lite långsammare än sampling) ---
  if (now - lastDisplayTime >= 100) {
    lastDisplayTime = now;
    updateDisplay();
  }
}

// =============================================================================
// AD8313 AVLÄSNING
// =============================================================================
float readAD8313_dBm() {
  long sum = 0;
  
  // Översampling för bättre precision och brusreduktion
  for (int i = 0; i < NUM_ADC_SAMPLES; i++) {
    sum += analogRead(PIN_AD8313_VOUT);
    delayMicroseconds(50);  // Kort delay mellan sampel
  }
  
  float avgAdc = (float)sum / NUM_ADC_SAMPLES;
  float mv = adcToMillivolts((int)avgAdc);
  float dBm = millivoltsToDbm(mv);
  
  return dBm;
}

float adcToMillivolts(int adcValue) {
  // ESP32-C3 ADC med 11dB attenuation: 0-2500mV effektivt (icke-linjärt i övre delen)
  // Vi använder den inbyggda kalibreringen om möjlig
  return (float)adcValue * ADC_REF_VOLTAGE / ADC_MAX_VALUE;
}

float millivoltsToDbm(float mv) {
  // AD8313 formel: VOUT = slope * (Pin - intercept)
  // Omvänt:  Pin(dBm) = (VOUT_mV / slope_mV) + intercept_dBm
  return (mv / AD8313_SLOPE_MV) + AD8313_INTERCEPT_DBM;
}

// =============================================================================
// BUZZER-STYRNING
// =============================================================================
void updateBuzzer(float dBm) {
  if (!buzzerEnabled) {
    ledcWrite(PIN_BUZZER, 0);
    return;
  }
  
  if (dBm > SIGNAL_THRESHOLD_DBM) {
    // Mappa signalstyrka till frekvens
    float range = MAX_SIGNAL_DBM - SIGNAL_THRESHOLD_DBM;
    float normalized = (dBm - SIGNAL_THRESHOLD_DBM) / range;
    if (normalized > 1.0) normalized = 1.0;
    if (normalized < 0.0) normalized = 0.0;
    
    int freq = BUZZER_MIN_FREQ + (int)(normalized * (BUZZER_MAX_FREQ - BUZZER_MIN_FREQ));
    
    // Stark signal: kontinuerlig ton
    // Svag signal: pulserande ton
    if (dBm > ALARM_THRESHOLD_DBM) {
      // Stark signal - kontinuerlig ton med hög frekvens
      ledcWriteTone(PIN_BUZZER, freq);
    } else {
      // Svagare signal - pulserande ton
      unsigned long pulseRate = map(
        constrain((int)(dBm * 10), (int)(SIGNAL_THRESHOLD_DBM * 10), (int)(ALARM_THRESHOLD_DBM * 10)),
        (int)(SIGNAL_THRESHOLD_DBM * 10),
        (int)(ALARM_THRESHOLD_DBM * 10),
        500,   // Långsam puls vid svag signal (ms)
        50     // Snabb puls vid starkare signal (ms)
      );
      
      if ((millis() % (pulseRate * 2)) < pulseRate) {
        ledcWriteTone(PIN_BUZZER, freq);
      } else {
        ledcWrite(PIN_BUZZER, 0);
      }
    }
  } else {
    // Ingen signal över tröskeln - tyst
    ledcWrite(PIN_BUZZER, 0);
  }
}

// =============================================================================
// SIGNALHISTORIK
// =============================================================================
void updateHistory(float dBm) {
  signalHistory[historyIndex] = dBm;
  historyIndex = (historyIndex + 1) % HISTORY_LENGTH;
}

// =============================================================================
// DISPLAY
// =============================================================================
void updateDisplay() {
  display.clearDisplay();
  
  // ---- Rad 1: Titel och statusikoner (y=0) ----
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("TETRA 380-385MHz"));
  
  // Buzzer-ikon
  if (buzzerEnabled) {
    display.setCursor(115, 0);
    display.print(F("B"));
  }
  
  // ---- Rad 2: Signalstyrka i dBm (y=10, stor text) ----
  display.setTextSize(2);
  display.setCursor(0, 11);
  
  if (currentDbm > SIGNAL_THRESHOLD_DBM) {
    // Signal detekterad - visa dBm
    display.printf("%.1f", currentDbm);
    display.setTextSize(1);
    display.print(F("dBm"));
  } else {
    // Ingen signal
    display.print(F("---.-"));
    display.setTextSize(1);
    display.print(F("dBm"));
  }
  
  // ---- Signal-styrka-ikon (höger sida, y=11) ----
  drawSignalIndicator(110, 11, currentDbm);
  
  // ---- Rad 3: Peak och info (y=29) ----
  display.setTextSize(1);
  display.setCursor(0, 29);
  display.printf("Pk:%.0f Max:%.0f", peakDbm, maxSessionDbm);
  
  // ---- Rad 4: Bargraf (y=38) ----
  drawBargraph(0, 38, 128, 6, currentDbm);
  
  // Peak-markör på bargraf
  if (peakDbm > NOISE_FLOOR_DBM) {
    float peakNorm = (peakDbm - NOISE_FLOOR_DBM) / (MAX_SIGNAL_DBM - NOISE_FLOOR_DBM);
    if (peakNorm > 1.0) peakNorm = 1.0;
    if (peakNorm < 0.0) peakNorm = 0.0;
    int peakX = (int)(peakNorm * 127);
    display.drawFastVLine(peakX, 37, 8, SSD1306_WHITE);
  }
  
  // ---- Rad 5: Historikgraf (y=46) ----
  drawHistory(0, 46);
  
  display.display();
}

// =============================================================================
// BARGRAF
// =============================================================================
void drawBargraph(int x, int y, int width, int height, float dBm) {
  // Ram
  display.drawRect(x, y, width, height, SSD1306_WHITE);
  
  // Fyllning proportionell mot dBm
  float normalized = (dBm - NOISE_FLOOR_DBM) / (MAX_SIGNAL_DBM - NOISE_FLOOR_DBM);
  if (normalized > 1.0) normalized = 1.0;
  if (normalized < 0.0) normalized = 0.0;
  
  int fillWidth = (int)(normalized * (width - 2));
  if (fillWidth > 0) {
    display.fillRect(x + 1, y + 1, fillWidth, height - 2, SSD1306_WHITE);
  }
}

// =============================================================================
// SIGNALHISTORIK-GRAF
// =============================================================================
void drawHistory(int x, int y) {
  // Rita en linjegraf med historikdata
  int graphWidth = HISTORY_LENGTH;
  int graphHeight = HISTORY_HEIGHT;
  
  // Basline
  display.drawFastHLine(x, y + graphHeight - 1, graphWidth, SSD1306_WHITE);
  
  // Tröskelmarkering (streckad linje)
  float threshNorm = (SIGNAL_THRESHOLD_DBM - NOISE_FLOOR_DBM) / (MAX_SIGNAL_DBM - NOISE_FLOOR_DBM);
  int threshY = y + graphHeight - 1 - (int)(threshNorm * (graphHeight - 1));
  for (int i = x; i < x + graphWidth; i += 4) {
    display.drawPixel(i, threshY, SSD1306_WHITE);
  }
  
  // Rita historiklinjen
  for (int i = 0; i < graphWidth - 1; i++) {
    int idx1 = (historyIndex + i) % HISTORY_LENGTH;
    int idx2 = (historyIndex + i + 1) % HISTORY_LENGTH;
    
    float norm1 = (signalHistory[idx1] - NOISE_FLOOR_DBM) / (MAX_SIGNAL_DBM - NOISE_FLOOR_DBM);
    float norm2 = (signalHistory[idx2] - NOISE_FLOOR_DBM) / (MAX_SIGNAL_DBM - NOISE_FLOOR_DBM);
    
    norm1 = constrain(norm1, 0.0f, 1.0f);
    norm2 = constrain(norm2, 0.0f, 1.0f);
    
    int y1 = y + graphHeight - 1 - (int)(norm1 * (graphHeight - 1));
    int y2 = y + graphHeight - 1 - (int)(norm2 * (graphHeight - 1));
    
    display.drawLine(x + i, y1, x + i + 1, y2, SSD1306_WHITE);
  }
  
  // Skala: min/max labels
  display.setTextSize(1);
  display.setCursor(x + graphWidth + 2, y);
  display.printf("%.0f", MAX_SIGNAL_DBM);
  display.setCursor(x + graphWidth + 2, y + graphHeight - 7);
  display.printf("%.0f", NOISE_FLOOR_DBM);
}

// =============================================================================
// SIGNAL-STYRKA-IKON (som WiFi/mobilstaplar)
// =============================================================================
void drawSignalIndicator(int x, int y, float dBm) {
  int bars = 0;
  
  if (dBm > SIGNAL_THRESHOLD_DBM)  bars = 1;
  if (dBm > -55.0)                 bars = 2;
  if (dBm > -45.0)                 bars = 3;
  if (dBm > -30.0)                 bars = 4;
  if (dBm > -15.0)                 bars = 5;
  
  int barWidth = 3;
  int spacing = 1;
  int maxHeight = 14;
  
  for (int i = 0; i < 5; i++) {
    int barHeight = 2 + (i * 3);
    int bx = x + i * (barWidth + spacing);
    int by = y + maxHeight - barHeight;
    
    if (i < bars) {
      display.fillRect(bx, by, barWidth, barHeight, SSD1306_WHITE);
    } else {
      display.drawRect(bx, by, barWidth, barHeight, SSD1306_WHITE);
    }
  }
}

// =============================================================================
// SPLASH SCREEN
// =============================================================================
void showSplashScreen() {
  display.clearDisplay();
  
  display.setTextSize(2);
  display.setCursor(10, 0);
  display.print(F("TETRA"));
  
  display.setTextSize(1);
  display.setCursor(10, 20);
  display.print(F("Signal Detector"));
  
  display.setCursor(10, 32);
  display.print(F("380-385 MHz"));
  
  display.setCursor(10, 44);
  display.print(F("AD8313 + LC-Filter"));
  
  display.setCursor(10, 56);
  display.print(F("SA7BNB  v1.0"));
  
  display.display();
  delay(2000);
}
