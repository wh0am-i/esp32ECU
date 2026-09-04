#include "logger.h"
#include "crank.h"
#include "fueling.h"
#include "ignition.h"

static LogSample buffer[LOG_BUFFER_SAMPLES];
static int head = 0;
static int count = 0;
static uint32_t lastSampleMs = 0;

void logger_init() {
  logger_clear();
}

void logger_clear() {
  head = 0;
  count = 0;
  memset(buffer, 0, sizeof(buffer));
}

void logger_task() {
  uint32_t now = millis();
  if (now - lastSampleMs < LOG_SAMPLE_INTERVAL_MS) return;
  lastSampleMs = now;

  buffer[head].t_ms = now;
  buffer[head].rpm = crank_getRPM();
  buffer[head].tps = tps_getPercent();
  buffer[head].advance = ignition_lastAppliedAdvance();

  head = (head + 1) % LOG_BUFFER_SAMPLES;
  if (count < LOG_BUFFER_SAMPLES) count++;
}

int logger_getCount() { return count; }
const LogSample* logger_getBuffer() { return buffer; }
int logger_getHead() { return head; }
