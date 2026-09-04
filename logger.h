#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================================
// logger.h - Buffer circular em RAM com amostras de RPM/TPS/Ponto ao longo
// do tempo, para exibir como gráfico de linhas ("espaguete") no webapp.
// Não grava em flash continuamente (evitaria desgaste da NVS/flash) — o
// buffer vive em RAM e é lido pelo webapp via HTTP quando o usuário abre a
// aba de logs. Se quiser log permanente entre sessões, dá pra exportar via
// endpoint /api/logs/export (CSV) e salvar no PC.
// ============================================================================

struct LogSample {
  uint32_t t_ms;
  float rpm;
  float tps;
  float advance;
};

void logger_init();
void logger_task();                 // chamar periodicamente (respeita LOG_SAMPLE_INTERVAL_MS internamente)
int  logger_getCount();
const LogSample* logger_getBuffer(); // buffer circular bruto
int  logger_getHead();               // índice do próximo slot a ser escrito
void logger_clear();
