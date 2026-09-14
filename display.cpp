#include "display.h"
#include "config.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// CORREÇÃO: a lib Adafruit_ST7789/Adafruit_ST77xx só define BLACK, WHITE,
// RED, GREEN, BLUE, CYAN, MAGENTA, YELLOW e ORANGE - não existe
// ST77XX_DARKGREY (isso é de outra lib, tipo a do ILI9341). Definimos a cor
// manualmente em RGB565 (mesmo valor usado como "darkgrey" em outras libs
// Adafruit: 0x7BEF).
#define COLOR_DARKGREY 0x7BEF

// Antes desta correção, o construtor abaixo era Adafruit_ST7789(CS, DC, RST),
// que usa o objeto SPI global e o barramento de hardware PADRÃO do ESP32
// (VSPI: SCK=18, MOSI=23). Isso só "funcionava" porque PIN_TFT_SCK/MOSI em
// config.h coincidiam com esses valores - os defines não tinham efeito real.
// Agora criamos um SPIClass próprio e chamamos .begin() explicitamente com
// os pinos de config.h, então esses defines passam a valer de verdade (você
// pode remapear o barramento SPI só mudando config.h).
static SPIClass tftSPI(VSPI);
static Adafruit_ST7789 tft = Adafruit_ST7789(&tftSPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

// valores anteriores para só redesenhar o que mudou (evita flicker/piscadas)
static int lastRpm = -1;
static int lastTps = -1;
static int lastAdv = -1000;
static int lastTemp = -1000;
static bool lastFan = false;
static bool lastSynced = false;
static bool lastTwoStep = false;
static bool lastRevLimit = false;
static bool firstDraw = true;
static bool inOverheatMode = false;
static uint32_t lastBlinkMs = 0;
static bool blinkState = false;

static void drawBaseLayout() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);

  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("TPS:");

  tft.setCursor(170, 10);
  tft.print("TEMP:");

  tft.setCursor(10, 70);
  tft.print("RPM:");

  tft.setCursor(10, 130);
  tft.print("PONTO:");

  tft.setCursor(170, 130);
  tft.print("VENT:");
}

void display_init() {
  pinMode(PIN_TFT_BLK, OUTPUT);
  digitalWrite(PIN_TFT_BLK, HIGH); // backlight ligado

  // MISO não é usado pelo display (ele é write-only), passamos -1.
  tftSPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);

  tft.init(240, 320);
  tft.setRotation(1); // paisagem (320x240)
  drawBaseLayout();
}

void display_update(float tpsPercent, float rpm, float advanceDeg, float temperature, bool fanActive, bool twoStepActive, bool revLimitActive, bool synced, bool overheat) {
  uint32_t now = millis();

  // =========================================================================
  // MODO DE ALERTA VERMELHO CRÍTICO: SUPERAQUECIMENTO (PISCA LCD EM VERMELHO COM "TEMP" EM AMARELO)
  // =========================================================================
  if (overheat) {
    inOverheatMode = true;
    if (now - lastBlinkMs >= 350) {
      lastBlinkMs = now;
      blinkState = !blinkState;

      if (blinkState) {
        tft.fillScreen(ST77XX_RED);
        tft.setTextColor(ST77XX_YELLOW);
        tft.setTextSize(4);
        tft.setCursor(40, 30);
        tft.print("ALERTA!");

        tft.setTextSize(5);
        tft.setCursor(75, 90);
        tft.print("TEMP");

        tft.setTextSize(4);
        tft.setCursor(55, 160);
        tft.print(temperature, 1);
        tft.print(" C");
      } else {
        tft.fillScreen(ST77XX_BLACK);
        tft.setTextColor(ST77XX_RED);
        tft.setTextSize(5);
        tft.setCursor(75, 90);
        tft.print("TEMP");
      }
    }
    return;
  }

  // Se saiu do modo de superaquecimento, restaura a tela normal
  if (inOverheatMode) {
    inOverheatMode = false;
    firstDraw = true;
    drawBaseLayout();
  }

  int tpsInt = (int)tpsPercent;
  int rpmInt = (int)rpm;
  int advInt = (int)(advanceDeg * 10);
  int tempInt = (int)(temperature * 10);

  // TPS (topo esquerda)
  if (tpsInt != lastTps || firstDraw) {
    tft.fillRect(70, 8, 90, 26, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(70, 10);
    tft.print(tpsInt);
    tft.print("%");
    lastTps = tpsInt;
  }

  // TEMPERATURA (topo direita)
  if (tempInt != lastTemp || firstDraw) {
    tft.fillRect(240, 8, 75, 26, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(temperature >= 95.0f ? ST77XX_ORANGE : ST77XX_WHITE);
    tft.setCursor(240, 10);
    tft.print((int)temperature);
    tft.print((char)247);
    tft.print("C");
    lastTemp = tempInt;
  }

  // RPM (linha do meio, grande)
  if (rpmInt != lastRpm || firstDraw) {
    tft.fillRect(70, 65, 240, 45, ST77XX_BLACK);
    tft.setTextSize(4);
    tft.setTextColor(revLimitActive ? ST77XX_RED : ST77XX_ORANGE);
    tft.setCursor(70, 70);
    tft.print(rpmInt);
    lastRpm = rpmInt;
  }

  // PONTO DE IGNIÇÃO (linha 3 esquerda)
  if (advInt != lastAdv || firstDraw) {
    tft.fillRect(90, 128, 75, 26, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(90, 130);
    tft.print(advanceDeg, 1);
    tft.print((char)247);
    lastAdv = advInt;
  }

  // VENTOINHA (linha 3 direita)
  if (fanActive != lastFan || firstDraw) {
    tft.fillRect(240, 128, 75, 26, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(fanActive ? ST77XX_GREEN : COLOR_DARKGREY);
    tft.setCursor(240, 130);
    tft.print(fanActive ? "LIG" : "DESL");
    lastFan = fanActive;
  }

  // STATUS INFERIOR (sincronismo, 2-step e corte de giro)
  if (synced != lastSynced || twoStepActive != lastTwoStep || revLimitActive != lastRevLimit || firstDraw) {
    tft.fillRect(10, 195, 300, 35, ST77XX_BLACK);
    tft.setTextSize(2);

    tft.setCursor(10, 200);
    tft.setTextColor(synced ? ST77XX_GREEN : ST77XX_RED);
    tft.print(synced ? "SYNC OK" : "SEM SYNC");

    if (revLimitActive) {
      tft.setCursor(170, 200);
      tft.setTextColor(ST77XX_RED);
      tft.print("CORTE MAX");
    } else if (twoStepActive) {
      tft.setCursor(190, 200);
      tft.setTextColor(ST77XX_YELLOW);
      tft.print("2-STEP");
    }

    lastSynced = synced;
    lastTwoStep = twoStepActive;
    lastRevLimit = revLimitActive;
  }

  firstDraw = false;
}