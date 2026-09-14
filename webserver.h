#pragma once

// ============================================================================
// webserver.h - Access Point Wi-Fi + servidor HTTP/WebSocket para o webapp.
// ============================================================================

void webserver_init();
void webserver_task();     // envia atualizações periódicas via WebSocket (chamar no loop)
