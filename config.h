#pragma once
#include <Arduino.h>

// ============================================================================
// ECU ESP32 - Chevette EA111-coil / roda fônica 60-2
// config.h - pinagem e constantes globais
// ============================================================================

// ---------------------------------------------------------------------------
// PINAGEM (ESP32 DevKit 38 pinos) - evita pinos strapping problemáticos
// (GPIO0, GPIO12) e pinos input-only (34-39) exceto onde só precisamos ler.
// ---------------------------------------------------------------------------

// Sensor de rotação (roda fônica 60-2, sensor HALL 3 fios: VCC/GND/Sinal)
#define PIN_CRANK_SENSOR      4    // GPIO com suporte a interrupção + pull-up interno

// Saídas de disparo da bobina Magneti Marelli BI0017MM / F000ZS0210 / 032905106B
// Pino 1 da bobina = cilindros 1+4 | Pino 3 da bobina = cilindros 2+3
// IMPORTANTE: NÃO ligar direto no GPIO. Usar transistor/MOSFET driver
// (ver README) — o GPIO aciona a BASE/GATE do driver, não a bobina diretamente.
#define PIN_IGN_DRIVER_1      25   // -> driver -> pino 1 da bobina (cil 1+4)
#define PIN_IGN_DRIVER_2      26   // -> driver -> pino 3 da bobina (cil 2+3)
#define IGN_ACTIVE_HIGH       true // assumido; confirme com osciloscópio e troque se preciso

// TPS (sensor de posição de borboleta) - entrada analógica
#define PIN_TPS_ADC           34   // ADC1_CH6, input-only, ok para leitura analógica

// Botão de corte de giro (2-step) - ativo em nível baixo (botão para GND)
#define PIN_BUTTON_2STEP      27

// Servo MG996R (dosador de combustível, alpha-N) - sinal PWM 50Hz
#define PIN_SERVO_FUEL        13

// Display GMT020-02 (SPI, driver ST7789V, 240x320, 3.3V nativo)
#define PIN_TFT_SCK           18
#define PIN_TFT_MOSI          23
#define PIN_TFT_CS            5
#define PIN_TFT_DC            2
#define PIN_TFT_RST           15
#define PIN_TFT_BLK           32   // backlight (PWM opcional p/ brilho)

// ---------------------------------------------------------------------------
// PARÂMETROS DO MOTOR / RODA FÔNICA
// ---------------------------------------------------------------------------
#define WHEEL_TEETH_TOTAL     60   // roda 60-2
#define WHEEL_TEETH_MISSING   2
#define DEG_PER_TOOTH         (360.0f / WHEEL_TEETH_TOTAL)  // 6 graus/dente físico da roda

#define ENGINE_CYLINDERS      4
#define WASTED_SPARK          true // bobina dupla, não precisa sensor de fase (cam)

// ---------------------------------------------------------------------------
// MAPA DE IGNIÇÃO - dimensões da tabela (estilo profissional RPM x TPS)
// ---------------------------------------------------------------------------
#define IGN_MAP_RPM_BINS      12
#define IGN_MAP_TPS_BINS      8

// ---------------------------------------------------------------------------
// REDE WIFI (Access Point criado pela própria ESP32)
// ---------------------------------------------------------------------------
#define WIFI_AP_SSID          "ECU-Chevette"
#define WIFI_AP_PASSWORD      "corrida123"   // troque aqui - mínimo 8 caracteres
#define WIFI_AP_CHANNEL       6
#define WEBSOCKET_PUSH_MS     100            // taxa de atualização do monitor em tempo real

// ---------------------------------------------------------------------------
// LOGGING
// ---------------------------------------------------------------------------
#define LOG_SAMPLE_INTERVAL_MS  50     // 20 amostras/segundo
#define LOG_BUFFER_SAMPLES      2400   // ~2 min de log em RAM (circular)
