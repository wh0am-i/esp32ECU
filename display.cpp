#include "display.h"
#include "config.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

static Adafruit_ST7789 tft = Adafruit_ST7789(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

// valores anteriores para só redesenhar o que mudou (evita flicker/piscadas)
static int lastRpm = -1;
static int lastTps = -1;
static int lastAdv = -1000;
static bool lastSynced = false;
static bool firstDraw = true;

void display_init() {
  pinMode(PIN_TFT_BLK, OUTPUT);
  digitalWrite(PIN_TFT_BLK, HIGH); // backlight ligado, sem dimming por padrão

  tft.init(240, 320);
  tft.setRotation(1); // paisagem (320x240)
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);

  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("TPS:");
  tft.setCursor(10, 90);
  tft.print("RPM:");
  tft.setCursor(10, 170);
  tft.print("PONTO:");
}

void display_update(float tpsPercent, float rpm, float advanceDeg, bool twoStepActive, bool synced) {
  int tpsInt = (int)tpsPercent;
  int rpmInt = (int)rpm;
  int advInt = (int)(advanceDeg * 10); // 1 casa decimal, comparação por inteiro

  // TPS
  if (tpsInt != lastTps || firstDraw) {
    tft.fillRect(150, 10, 160, 30, ST77XX_BLACK);
    tft.setTextSize(3);
    tft.setCursor(150, 10);
    tft.print(tpsInt);
    tft.print(" %");
    lastTps = tpsInt;
  }

  // RPM
  if (rpmInt != lastRpm || firstDraw) {
    tft.fillRect(150, 90, 160, 30, ST77XX_BLACK);
    tft.setTextSize(3);
    tft.setCursor(150, 90);
    tft.print(rpmInt);
    lastRpm = rpmInt;
  }

  // Ponto (avanço)
  if (advInt != lastAdv || firstDraw) {
    tft.fillRect(150, 170, 160, 30, ST77XX_BLACK);
    tft.setTextSize(3);
    tft.setCursor(150, 170);
    tft.print(advanceDeg, 1);
    tft.print((char)247); // símbolo de grau aproximado
    lastAdv = advInt;
  }

  // status de sincronismo / 2-step, linha inferior
  if (synced != lastSynced || firstDraw) {
    tft.fillRect(10, 210, 300, 25, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 210);
    tft.print(synced ? "SYNC OK" : "SEM SINCRONISMO");
    lastSynced = synced;
  }
  if (twoStepActive) {
    tft.setTextSize(2);
    tft.setCursor(200, 210);
    tft.print("2-STEP");
  }

  firstDraw = false;
}
