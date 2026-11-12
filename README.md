# ECU ESP32 - Chevette (Roda Fônica 60-2 + Bobina EA111 + Alpha-N)

## Validar a interface no Windows

Execute `python simulator.py --open` e acesse **http://127.0.0.1:8080**.
O simulador não exige dependências externas e usa os mesmos arquivos da interface
da ESP32, com telemetria sintética. Veja [instruções e testes](VALIDACAO_WINDOWS.md).

⚠️ **Projeto experimental.** Leia toda a documentação antes de conectar ao motor. Erros na calibração do ponto de ignição podem causar detonação e danos mecânicos graves. Realize sempre os primeiros testes em bancada.

---

## 1. Pinagem e Ligações da ESP32 (DevKit 38 pinos)

| Componente | Função | Pino ESP32 | Observações |
|---|---|---|---|
| **Sensor Hall Roda Fônica 60-2** | Entrada de RPM / Sincronismo | **GPIO 4** | Interrupção por hardware + pull-up interno |
| **Bobina - Pino 1 (Cil. 1+4)** | Disparo de Ignição Par 1 | **GPIO 25** | **Obrigatório driver** (transistor/MOSFET/optoacoplador) |
| **Bobina - Pino 3 (Cil. 2+3)** | Disparo de Ignição Par 2 | **GPIO 26** | **Obrigatório driver** (transistor/MOSFET/optoacoplador) |
| **Sensor TPS (Borboleta)** | Entrada Analógica | **GPIO 34** | ADC1_CH6 (input-only, 0 a 3.3V) |
| **Sensor Temp. Água (CLT / NTC)** | Entrada Analógica | **GPIO 35** | ADC1_CH7 com resistor pull-up 2.2kΩ para 3.3V |
| **Relé da Ventoinha** | Saída Digital de Controle | **GPIO 33** | Aciona relé/módulo de potência da ventoinha |
| **Servo MG996R (Dosador)** | Sinal PWM 50Hz | **GPIO 13** | Controle de dosagem Alpha-N |
| **Botão 2-Step (Largada)** | Entrada Digital (GND) | **GPIO 27** | Ativo em nível baixo com pull-up interno |
| **Display GMT020-02 (ST7789)** | Barramento SPI + Controle | **GPIOs 18, 23, 5, 2, 15, 32** | SCK: 18, MOSI: 23, CS: 5, DC: 2, RST: 15, BLK: 32 |

---

## 2. Recursos e Funcionalidades Implementadas

### A. Ignição Alpha-N e Agendamento em Hardware (`esp_timer_isr`)
* **Mapa 2D ($16 \times 16$):** Avanço em graus BTDC interpolado continuamente em tempo real a partir de RPM e TPS%.
* **Disparo Determinístico:** 4 temporizadores de hardware em modo ISR dedicados exclusivamente para o início do Dwell e disparo de cada par de cilindros (wasted spark), garantindo precisão de microssegundos sem sobrecarregar a CPU.
* **Corte de Giro Final (Hard Cut):** Limitador de rotação máxima configurável (`maxRpm`) para proteção do motor.
* **Corte 2-Step (Largada):** Ativado por botão no volante/painel com rotação de corte ajustável (`twoStepRpm`) e telemetria de botão no WebApp.

### B. Gerador de Superfície 3D Matemática $\mathbb{R}^3$ ($z = f(\text{RPM}, \text{TPS})$)
* **Geração Paramétrica:** Permite definir parâmetros físicos (Avanço na Marcha Lenta, Avanço em Alta, Expoente $\gamma$ da Curva Centrífuga e Retardo por Carga de Borboleta) para criar instantaneamente uma superfície 3D suave para todo o mapa.
* **Ferramentas de Ajuste em Lote (Trimming):** Botões de ajuste rápido ($+1.0^\circ, -1.0^\circ, +0.5^\circ, -0.5^\circ, +5\%, -5\%, \text{Suavizar / Smooth}$) e clique individual nas células para ajuste fino.
* **Heatmap com Gradiente Térmico Calibrado:** Escala baseada em limites físicos reais e contraste dinâmico de texto (branco/preto automático) para legibilidade nítida.

### C. Calibração Avançada de Sensores
* **Calibração do Ponto 0 / PMS:**
  * Configuração de **Dentes pós-falha da polia fônica até o PMS** (`triggerTeethOffset`, ex: 14 dentes na 60-2).
  * **Ajuste fino em graus** (`triggerFineOffsetDeg`) para aferição precisa com lâmpada estroboscópica.
* **Calibração de TPS com Zonas Mortas (Deadbands):**
  * Captura direta dos valores ADC com borboleta fechada (0%) e 100% aberta.
  * Limiares de corte configuráveis (`tpsMinPercent` e `tpsMaxPercent`) para evitar oscilações em marcha lenta e garantir 100% em plena abertura.

### D. Controle Térmico & Ventoinha com Histerese
* **Leitura NTC:** Conversão precisa de temperatura (°C) com filtragem de ruído.
* **Janela de Histerese Configurável:** Define a temperatura de acionamento (`fanTargetTemp`, ex: 92°C) e a janela de histerese (`fanHysteresis`, ex: 5°C $\to$ desliga aos 87°C), evitando que a ventoinha ligue e desligue rapidamente.
* **Alerta Vermelho no LCD GMT020-02:** Ao ultrapassar a temperatura de superaquecimento (`overheatTemp`), a tela do LCD pisca com fundo **VERMELHO** e escrita gigante em **AMARELO** ("TEMP").

### E. Área de Teste Manual de Atuadores (Modo Teste)
* Interface dedicada para diagnóstico de bancada ou oficina:
  * **Teste da Ventoinha:** Forçar LIGADA (ON) ou DESLIGADA (OFF).
  * **Teste do Servo Dosador:** Slider contínuo de 0° a 180° e botões de atalho (0°, 45°, 90°, 135°, 180°) para verificar o curso mecânico.
  * **Botão "Desabilitar Modo de Teste / Parar":** Desativa instantaneamente qualquer override e devolve o controle automático total à ECU.

### F. Telemetria WebApp & Datalogging
* **Painel Web em Tempo Real (WebSocket):** Manômetros digitais de RPM, TPS %, Ponto (° BTDC), Temp. Motor (°C), Estado da Ventoinha, Ângulo do Dosador, Status do Botão 2-Step e Ordem de Disparo Dinâmica.
* **Datalogger em Memória RAM:** Amostragem contínua de RPM, TPS, Ponto e Temperatura com visualização em gráfico de linhas e download em arquivo CSV.

---

## 3. Circuito Driver da Bobina (OBRIGATÓRIO)

Os pinos 1 e 3 da bobina EA111 necessitam de um sinal de comando ativo-alto amplificado por transistor/driver. **Não ligue o GPIO diretamente na bobina**.

```
ESP32 GPIO (25 ou 26) --[Resistor 220-330Ω]-- Base do Transistor NPN (2N2222 / TIP31)
                                                     |
Pino 1 ou 3 da Bobina -------------------------------+---- Coletor (Pull-up 1k para 5V/12V conforme módulo)
                                                     |
                                                    GND --- Emissor
```
* **Recomendado:** Utilizar optoacoplador (**PC817**) para isolar o microcontrolador do ruído de alta tensão da bobina.

---

## 4. Instalação e Gravação

### Bibliotecas Necessárias (Arduino Library Manager)

⚠️ **Importante:** para `ESPAsyncWebServer` e `AsyncTCP` existem várias versões/forks
espalhados pelo Library Manager com o mesmo nome. Usar a combinação errada causa
erros de compilação (`mbedtls_md5_starts_ret`, `passing 'const AsyncServer' ...
discards qualifiers`, etc.) mesmo sem nenhum erro no código do projeto em si.

| # | Biblioteca | Fork/autor a usar | Observação |
|---|---|---|---|
| 1 | **ESPAsyncWebServer** | `ESP32Async` (mathieucarbou) — github.com/ESP32Async/ESPAsyncWebServer | NÃO usar a versão antiga de `me-no-dev` nem a de `lacamera`/ESPHome — não compilam nos cores ESP32 3.x atuais. |
| 2 | **AsyncTCP** | `ESP32Async` (mathieucarbou) — github.com/ESP32Async/AsyncTCP | Precisa ser da MESMA família/autor da `ESPAsyncWebServer` acima. Apague qualquer `AsyncTCP` antigo (ex: `me-no-dev`, v1.1.x) da pasta de libraries — ter as duas instaladas ao mesmo tempo causa conflito de símbolos. |
| 3 | **ArduinoJson** | Benoit Blanchon — v6.x ou v7.x | Qualquer uma das duas funciona com o código deste projeto. |
| 4 | **Adafruit GFX Library** | Adafruit | — |
| 5 | **Adafruit ST7735 and ST7789 Library** | Adafruit | Só define as cores `BLACK/WHITE/RED/GREEN/BLUE/CYAN/MAGENTA/YELLOW/ORANGE` — não existe `ST77XX_DARKGREY` nela (isso é de outra lib); o `display.cpp` já define essa cor manualmente. |
| 6 | **ESP32Servo** | Kevin Harrington / Jim Kim | Usada para o servo do dosador Alpha-N. |
| 7 | **LittleFS** | integrada ao core ESP32 Arduino ≥ 2.x | Não precisa instalar — já vem com o board package `esp32` da Espressif. |

### Ferramenta extra necessária (não é uma lib do Library Manager)

- **Upload do LittleFS:** na Arduino IDE 2.x, instale a extensão
  [`arduino-littlefs-upload`](https://github.com/earlephilhower/arduino-littlefs-upload)
  (arquivo `.vsix` em `~/.arduinoIDE/plugins/`). Sem ela não tem como enviar a pasta
  `data/` — o antigo menu *Ferramentas → ESP32 Sketch Data Upload* só existe na IDE 1.8.x.
  Veja a seção 4 abaixo.

### Passos de Gravação
1. Abra `esp32ECU.ino` na Arduino IDE ou PlatformIO.
2. Compile e grave o firmware no ESP32.
3. Faça o upload dos arquivos da pasta `data/` para a partição **LittleFS**:
   - **Arduino IDE 1.8.x:** Ferramentas → *ESP32 Sketch Data Upload*.
   - **Arduino IDE 2.x (a maioria das instalações atuais):** esse menu NÃO existe mais. É
     preciso instalar a extensão `arduino-littlefs-upload`
     (https://github.com/earlephilhower/arduino-littlefs-upload/releases, arquivo `.vsix`,
     colocar em `~/.arduinoIDE/plugins/`, reiniciar a IDE) e então rodar
     **Ctrl+Shift+P → "Upload LittleFS to Pico/ESP8266/ESP32"**. Confirme também que o
     *Partition Scheme* selecionado em Ferramentas tem espaço reservado pra sistema de
     arquivos (ex: algo com "spiffs" no nome) - sem isso o `LittleFS.begin()` falha sempre.
   - **PlatformIO:** `pio run -t uploadfs`.
   - Se esse passo não for feito (ou falhar), o webapp abre uma página preta com
     "Nao encontrado" - o Monitor Serial (115200 baud) mostra
     `ERRO: falha ao montar LittleFS - faça o upload da pasta data/` nesse caso.
4. Conecte-se à rede Wi-Fi **`ECU-Chevette`** (senha padrão: `corrida123`) e acesse `http://192.168.4.1` no navegador.
