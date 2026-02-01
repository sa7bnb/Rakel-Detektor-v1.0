# TETRA Signal Detector (380–385 MHz)

RF-signaldetektor för TETRA/Rakel-frekvensbandet baserad på AD8313 logaritmisk detektor, egenbyggt LC-bandpassfilter och ESP32-C3 mikrokontroller. Enheten ger en visuell och akustisk varning när den detekterar RF-aktivitet inom 380–385 MHz — det frekvensområde som används av Rakel (Sveriges blåljusnät baserat på TETRA-standarden).

> **⚠️ OBS: Detta är ett pågående hobbyprojekt under aktiv utveckling. Ingenting är färdigt eller redo att användas i praktiken. Filtret kommer att kräva justering och hela systemet behöver kalibreras. I ren teori och praktik bör konceptet fungera, men mycket arbete återstår.**

---

## Syfte

Målet med projektet är att bygga en bärbar enhet som ger en indikering — via OLED-display och buzzer — när man befinner sig i närheten av en aktiv Rakel-enhet (TETRA-radio som används av polis, räddningstjänst och ambulans). Tänk det som en enkel "RF-närvarodetektor" för ett specifikt frekvensband.

**Enheten avkodar inte, demodulerar inte och tolkar inte Rakel-trafiken på något sätt.** Den fungerar enbart som en bredbandig signalstyrkemätare som reagerar på RF-energi inom det filtrerade frekvensbandet. Ingen audio, data, identiteter eller protokollinformation kan extraheras. Det enda som mäts är den momentana signalstyrkan i dBm.

---

## Systemöversikt

Signalkedjan ser ut så här:

```
Antenn → LC-Bandpassfilter (380–385 MHz) → AD8313 (Log Detektor) → ESP32-C3 (ADC) → OLED + Buzzer
```

### 1. LC-Bandpassfilter (380–385 MHz)

Ett egendesignat och egenbyggt LC-bandpassfilter som begränsar de frekvenser som når detektorn till 380–385 MHz. Filtret är ett flerstegs LC-filter med tre induktorer och tillhörande kondensatorer i en klassisk kopplad resonator-topologi.

**Schematic:**

![Schematic 380–385MHz LC-Filter](Schematic_380–385MHz-LC-Filter-v1.0.png)

**PCB-layout:**

![PCB 380–385MHz LC-Filter](PCB_380–385MHz%20LC-Filter%20v1.0.png)

**3D-vy:**

![3D-vy 380–385MHz LC-Filter](3d-Image_380–385MHz-LC-Filter-v1.0.png)

**Filterkomponenter:**

| Ref | Värde | Funktion |
|-----|-------|----------|
| L1, L2, L3 | ~10 nH | Shunt-induktorer (justerbara) |
| C1, C2 | 2.2 pF | Seriekoppling in/ut |
| C3, C4 | 1 pF | Seriekoppling mellansektioner |
| C5, C7 | 8.2 pF | Shunt-kondensatorer |
| C6 | 8.2 pF | Central shunt-kondensator |
| C8, C9, C10 | 3 pF–10 pF | Trimmerkondensatorer för finjustering |

Filtret är byggt på ett eget PCB med SMA-kontakter i båda ändar (ANT-sida och AD8313-sida). Trimmerkondensatorerna (C8, C9, C10) gör det möjligt att finjustera filtrets centrumfrekvens och bandbredd efter tillverkning.

> Filtret är inte perfekt och kommer att kräva trimning med en VNA eller signalgenerator för att uppnå önskad respons. Det kan ha mer insertion loss än optimalt, men det bör räcka för att begränsa detektorn till rätt frekvensband.

### 2. AD8313 Logaritmisk Detektor

![AD8313 Modul](AD8313.jpg)

AD8313 från Analog Devices är en logaritmisk RF-detektor som omvandlar RF-signalstyrka till en DC-spänning proportionell mot ineffekten i dBm.

**Nyckelspecifikationer:**

- Frekvensområde: 0.1–2.5 GHz
- Dynamiskt område: ~70 dB
- Logaritmisk slope: ~18 mV/dB
- Intercept: ~−100 dBm (vid 50 Ω)
- Matningsspänning: 2.7–5.5 V (drivs av 3.3 V från ESP32)

AD8313-modulen tar emot den filtrerade RF-signalen via SMA från LC-filtret. Dess VOUT-pin ger en DC-spänning som ESP32:ans ADC läser av. Denna spänning omvandlas sedan matematiskt tillbaka till dBm i firmware.

### 3. ESP32-C3 SuperMini (Mikrokontroller)

En kompakt ESP32-C3 SuperMini-modul kör firmware skriven i Arduino IDE. Den läser AD8313:s utsignal via sin 12-bitars ADC, beräknar signalstyrkan i dBm, och presenterar resultatet på displayen samt styr buzzern.

**Firmware-funktioner:**

- 32x översampling av ADC med exponentiellt glidande medelvärde (EMA)
- Omvandling från ADC-spänning till dBm via AD8313-kalibreringskonstanter
- Peak-hold med tidsstyrd förfallning
- Sessionsmaximum
- Seriell utskrift av rådata (ADC, mV, dBm) för kalibrering

### 4. OLED-display (SSD1306, 0.96")

En 128×64 pixel OLED ansluten via I2C visar:

- Aktuell signalstyrka i dBm (stor text)
- Bargraf med peak-markör
- Signalhistorikgraf (rullande 64-punkters linjegraf)
- Signal-styrka-ikon (stapelindikator liknande mobilsignal)
- Peak- och sessionsmaxvärden

### 5. Buzzer

En passiv buzzer styrd via PWM ger akustisk feedback:

- **Ingen signal:** Tyst
- **Svag signal (> −65 dBm):** Långsam pulserande ton
- **Starkare signal:** Snabbare puls och högre frekvens
- **Stark signal (> −40 dBm):** Kontinuerlig hög ton

---

## Kopplingsschema

```
ESP32-C3 SuperMini          Komponenter
┌──────────────┐
│         GPIO0 ├──────────── AD8313 VOUT
│         GPIO3 ├──────────── Buzzer
│         GPIO6 ├──────────── OLED SDA
│         GPIO7 ├──────────── OLED SCL
│          3.3V ├──────────── AD8313 VPOS / OLED VCC
│           GND ├──────────── Gemensam GND
└──────────────┘

Antenn ──► [SMA] LC-Filter 380-385MHz [SMA] ──► AD8313 modul ──► VOUT till GPIO0
```

---

## Arduino IDE — Kom igång

**Board:** ESP32C3 Dev Module (via ESP32 board-paket)

**Bibliotek att installera:**

- Adafruit SSD1306
- Adafruit GFX Library

**Kalibrering:**

Standardvärdena i koden (`AD8313_SLOPE_MV = 18.0`, `AD8313_INTERCEPT_DBM = -100.0`) är hämtade från AD8313-databladet. Varje modul och filter har sin egen karakteristik. Använd seriellmonitorn (115200 baud) som skriver ut `RAW_ADC, mV, dBm, Peak_dBm` för att kalibrera mot en känd signalkälla.

---

## Projektstatus

- [x] Schematik och PCB-design för LC-filter
- [x] Grundläggande firmware med OLED, buzzer och AD8313-avläsning
- [ ] Kalibrering mot känd signalkälla
- [ ] Trimning av LC-filtret med VNA
- [ ] Test i fält
- [ ] Optimering av strömförbrukning
- [ ] Eventuellt kretskort för hela enheten

---

## Ansvarsfriskrivning

Detta projekt är ett icke-kommersiellt hobbyprojekt i utbildningssyfte.

Enheten är **enbart en mottagare** och saknar helt förmåga att sända radiosignaler. Den innehåller inga sändarkomponenter och ingen firmware eller hårdvara som möjliggör sändning.

Enheten **avkodar inte, demodulerar inte, dekrypterar inte, tolkar inte och lagrar inte** radiokommunikation. Den fungerar uteslutande som en bredbandig RF-signalstyrkemätare som ger en analog spänning proportionell mot mottagen RF-effekt inom ett filtrerat frekvensområde. Ingen audio, data, identiteter eller protokollinformation kan extraheras.

---

## Licens

Detta projekt delas som öppen källkod i utbildningssyfte. Användning sker helt på egen risk.

---

*SA7BNB — 2026*
