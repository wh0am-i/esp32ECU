# ECU ESP32 - Chevette (roda fônica 60-2 + bobina Magneti Marelli BI0017MM)

⚠️ **Projeto experimental, nunca testado em motor real.** Leia tudo antes de ligar qualquer coisa no motor. Erros de calibração de ponto de ignição podem danificar o motor seriamente (detonação, batida de pino, etc). Comece testando em bancada.

## 1. Visão geral

| Peça | Função | Pino ESP32 |
|---|---|---|
| Sensor Hall roda fônica 60-2 | Entrada de RPM/sincronismo | GPIO 4 (interrupção) |
| Bobina - pino 1 (cil. 1+4) | Saída de disparo (via driver!) | GPIO 25 |
| Bobina - pino 3 (cil. 2+3) | Saída de disparo (via driver!) | GPIO 26 |
| TPS | Entrada analógica | GPIO 34 (ADC) |
| Servo MG996R (dosador) | Saída PWM | GPIO 13 |
| Botão 2-step | Entrada digital (pull-up) | GPIO 27 |
| Display GMT020-02 (ST7789) | SPI | SCK 18, MOSI 23, CS 5, DC 2, RST 15, BLK 32 |

A roda fônica 60-2 e a bobina de centelha perdida (wasted spark) dispensam sensor de fase (comando) — cada saída da bobina dispara 1x por volta do virabrequim, defasada 180° uma da outra. Isso já está implementado no firmware.

## 2. Circuito driver da bobina (OBRIGATÓRIO - não pule isso)

Os pinos 1 e 3 da bobina esperam um sinal de comando (ativo-alto, assumido) que aciona o módulo de ignição interno — **não é pra ligar o GPIO direto nesses pinos**. Circuito recomendado por saída (repita para os 2 canais):

```
ESP32 GPIO --[R 220-330R]-- Base do transistor NPN (ex: 2N2222/TIP31)
                                  |
Bobina pino 1/3 -----------------+---- Coletor
                                  |
                                 GND --- Emissor
```

- Melhor ainda: use um **optoacoplador (PC817)** entre o GPIO e a base do transistor, isolando eletricamente o ESP32 do sistema elétrico do carro (picos de alternador, ruído de ignição).
- Coloque um **diodo TVS ou zener** no lado da bobina para grampear transientes.
- **Confirme com osciloscópio** se o módulo espera nível ativo-alto ou ativo-baixo antes de energizar de verdade. Se estiver invertido, mude `IGN_ACTIVE_HIGH` em `config.h`.

## 3. Sensor de rotação (Hall, 3 fios)

Fio de sinal → GPIO 4 (o firmware já habilita pull-up interno). Fios de alimentação (VCC/GND do sensor) ligam na alimentação de 5V/12V que o sensor exigir (confira o datasheet do sensor que veio no kit Unique — normalmente 12V, mas o sinal de saída já vem em nível compatível ou precisa de um divisor resistivo pra não ultrapassar 3.3V no GPIO. **Meça a tensão de saída do sensor antes de ligar no ESP32** — se for maior que 3.3V, use um divisor resistivo ou level shifter).

## 4. Bibliotecas Arduino necessárias

Instale via *Library Manager* (Arduino IDE) ou `platformio.ini`:

- `ESPAsyncWebServer` (fork mathieucarbou ou me-no-dev)
- `AsyncTCP`
- `ArduinoJson` (v6.x)
- `Adafruit GFX Library`
- `Adafruit ST7735 and ST7789 Library`
- `ESP32Servo`
- `LittleFS` (já incluso no core ESP32 Arduino ≥ 2.x)

## 5. Upload

1. Abra `esp32_ecu.ino` na Arduino IDE (todos os `.h`/`.cpp` da pasta são compilados juntos automaticamente).
2. Instale as bibliotecas acima.
3. Faça upload do firmware normalmente.
4. Faça upload da pasta `data/` para o **LittleFS** (Ferramentas → "ESP32 Sketch Data Upload" — pode ser necessário instalar o plugin `arduino-littlefs-upload` pro Arduino IDE 2.x, ou use `pio run -t uploadfs` no PlatformIO). Sem isso o webapp não vai carregar.

## 6. Rede Wi-Fi

A ESP32 cria a rede **`ECU-Chevette`** (senha definida em `WIFI_AP_SSID`/`WIFI_AP_PASSWORD` no `config.h` — troque antes de usar). Conecte seu celular/notebook nela e acesse `http://192.168.4.1`.

O webapp é só para configurar/monitorar — a ECU roda o controle de ignição e combustível independente disso, mesmo sem ninguém conectado.

## 7. Procedimento de calibração (ordem recomendada)

1. **Bancada, sem o motor ligado**: gire a roda fônica manualmente ou com uma furadeira de baixa rotação. Confira no display/webapp se o RPM aparece e se "SYNC OK" acende.
2. **Osciloscópio nos pinos GPIO 25/26**: confirme que os pulsos aparecem e que a polaridade bate com `IGN_ACTIVE_HIGH`.
3. **Calibração do TPS**: na aba Configuração, solte a borboleta (fechada) e anote o "ADC bruto" mostrado; repita com a borboleta 100% aberta. Preencha `tpsAdcMin`/`tpsAdcMax` com esses valores.
4. **Calibração do servo dosador**: ajuste `servoAngleAtClosedThrottle`/`servoAngleAtFullThrottle` observando fisicamente o curso do dosador de combustível — não force o batente mecânico.
5. **Calibração do ponto (trigger offset)**: com o motor já rodando em marcha lenta (com um mapa bem conservador/baixo avanço), use uma **lâmpada de ponto estroboscópica** apontada pra marca de TDC da polia. Ajuste `triggerOffsetDeg` na aba Configuração até o ponto mostrado na lâmpada bater com o que o display mostra. Isso só precisa ser feito uma vez.
6. **Mapeamento fino do avanço**: só depois de tudo acima calibrado, ajuste célula por célula do mapa de ignição no heatmap, de preferência com o carro num dinamômetro/banco de potência e leitura de detonação (knock).

## 8. Limitações conhecidas / próximos passos

- **Precisão do disparo**: o agendamento de ignição usa 4 `esp_timer` de hardware (modo `ESP_TIMER_ISR`) — um para o início do dwell e um para o disparo de cada bobina. A task `ignition_task()` (core 0) só acorda a cada ~1ms pra recalcular avanço/dwell a partir do RPM x TPS atuais e reagendar, em cada bobina, o *próximo* evento (só quando o anterior já disparou); quem realmente liga/desliga o pino é o callback do `esp_timer`, direto em contexto de interrupção, com poucos microssegundos de latência documentados pela Espressif — independente de o webserver/logger estarem ocupados no outro core. Isso substitui a versão anterior (loop apertado comparando ângulo a cada iteração), que ocupava o core 0 o tempo todo e tinha resolução pior em RPM alto. Mesmo assim, teste no banco com osciloscópio antes de rodar no motor — isso nunca foi validado em hardware real.
- **Sem sensor de fase (cam)**: correto para essa bobina de centelha perdida — não precisa, mas significa que essa arquitetura não serve caso você troque futuramente pra bobinas individuais/ignição sequencial. Também significa que a ECU nunca sabe qual dos 2 cilindros de um par está na compressão (centelha útil) vs. no escape (centelha "perdida") — a ilustração de ordem de disparo no webapp mostra os dois cilindros do par acendendo juntos por isso mesmo, não é um bug do display.
- **Ignição em mapa RPM x TPS, dosagem em Alpha-N** — já é como este firmware funciona: `ignition.cpp` interpola um mapa 2D (`IgnitionMap`, RPM x TPS) pro avanço; `fueling.cpp` move o servo dosador proporcional ao TPS (Alpha-N), com uma correção linear opcional só por RPM (`rpmCompensationFactor`). Isso é a divisão de trabalho recomendada para carburador/dosador mecânico: a ignição precisa reagir à carga real do motor (TPS), e a dosagem por Alpha-N já é o método padrão quando não há sensor de MAP.
- **Enriquecimento por RPM na dosagem** é bem simplificado (fator linear opcional); não há malha fechada com sonda lambda — considere adicionar isso depois, usando a lambda que você já usa no carburador.
- **Log em RAM**: some ao desligar. Se quiser log persistente, dá pra adaptar `logger.cpp` pra gravar em cartão SD (mais indicado que a flash interna pra volume alto de escrita).
- **Webapp é para desktop** (tela ligada ao notebook/PC na bancada ou no carro) — não há necessidade de layout responsivo pra celular; o CSS não tenta se adaptar a telas pequenas de propósito.
