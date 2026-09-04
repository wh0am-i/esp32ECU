#pragma once

// ============================================================================
// storage.h - Persistência em NVS (flash) via Preferences.
// A NVS do ESP32 é resistente a queda de energia: uma escrita só é
// considerada válida após o commit completar, então um desligamento no meio
// de uma gravação não corrompe o valor anterior (implementação wear-leveled
// da Espressif). Por isso é a escolha certa pro requisito "antifalha".
//
// Regra de uso: só chamamos storage_save*() quando o usuário edita algo pelo
// webapp (não em todo loop!), pra não gastar os ciclos de escrita da flash
// (NVS aguenta ~100k gravações por célula, mais que suficiente pra uso normal).
// ============================================================================

void storage_init();          // monta a NVS e carrega config salva (ou aplica defaults)
void storage_loadAll();
void storage_saveIgnitionMap();
void storage_saveIgnitionConfig();
void storage_saveFuelingConfig();
void storage_saveCrankCalibration();
void storage_saveWifiConfig();
