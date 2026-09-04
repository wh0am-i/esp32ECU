#pragma once

// ============================================================================
// display.h - Display GMT020-02 (ST7789V, 240x320, SPI)
// Fundo preto, letras brancas, layout simples: TPS%, RPM, Ponto (avanço).
// ============================================================================

void display_init();
void display_update(float tpsPercent, float rpm, float advanceDeg, bool twoStepActive, bool synced);
