# Validação no Windows

Na pasta do projeto, com Python 3.10 ou superior:

```powershell
python simulator.py --open
```

Abra http://127.0.0.1:8080. Encerre com Ctrl+C. Para outra porta:

```powershell
python simulator.py --port 8081 --open
```

Não precisa instalar pacotes para executar o simulador. Ele serve a pasta `data/`
e implementa os endpoints de status, configuração, mapa, atuadores e CSV.
Configurações e mapa ficam em `.simulator-state.json`; logs e overrides ficam
apenas em RAM. Para começar com outra configuração, use `--state outro-estado.json`.

A barra de simulação permite selecionar aceleração, marcha lenta, motor parado,
superaquecimento, limitador e 2-step. Esses dados são sintéticos: o programa não
executa o firmware C++, não aciona hardware e não valida sincronismo, dwell,
precisão temporal ou calibração para um motor real. Os contadores de bobina são
ilustrativos. O simulador usa HTTP periódico; a ESP32 mantém WebSocket.

## Interface

Interface técnica em grafite, vermelho e branco, com painéis compactos,
telemetria resumida persistente, escala de RPM e diagnóstico de entradas,
saídas, limites e contadores. Simulador e ESP32 servem os mesmos arquivos.
Somente a barra de cenários é exclusiva do simulador.

O mapa agora tem **16×16 (256 células)**. O limitador de RPM não altera os eixos.
O formato legado 12×8 é reamostrado por interpolação bilinear. A migração mantém
o intervalo dos eixos, mas pode suavizar mudanças de inclinação entre os pontos
antigos: revise o mapa migrado antes de usar no motor.
Na ESP32, a chave NVS `map` original permanece intacta e o novo mapa usa `map16`.
No Windows, o backup usa o sufixo `.12x8.bak.json`. Novos mapas usam 500–7000 RPM.

- Configuração dividida em cinco abas, com navegação por teclado.
- Planilha do mapa responsiva: as 16 colunas e a coluna TPS cabem na largura
  disponível, sem rolagem interna. Células e tipografia se ajustam à tela.
  Clique numa célula para digitar dentro dela. Arraste entre células para selecionar
  uma região retangular: Enter aplica o valor digitado a toda a seleção; Esc cancela
  a entrada. Shift+clique estende a seleção. Tab aplica e avança à próxima coluna.
  Ajustes de graus, porcentagem e suavização respeitam a seleção; sem seleção,
  afetam o mapa inteiro. Desfazer (ou Ctrl+Z com foco na grade) recupera até 30 edições.
- Edição de células e ajustes em lote ficam locais até clicar em salvar o mapa.
  Há indicação de alterações pendentes, descarte e aviso ao sair.
- Erros HTTP aparecem como notificações; um erro não confirma gravação.
- Datalogger com seleção de quatro canais, cursor, mínimos/máximos, tempo real
  das amostras e janelas de 10/30/60 segundos ou sessão completa.
- Cada canal ocupa uma faixa com escala numérica, grade e cursor sincronizado.
  Arraste no gráfico para ampliar um trecho, use a roda do mouse ou Zoom +/−.
  Anterior/Próximo deslocam a janela; a visão geral permite selecionar outro trecho.
  Ver sessão inteira desfaz o zoom. A autoescala vertical é opcional. O zoom pausa
  a visualização, mantendo os dados do trecho estáveis; Retomar ao vivo volta à coleta.
- Pausar congela a visualização; a coleta da ECU continua. O CSV exportado contém
  a sessão carregada inteira, incluindo canais ocultos e amostras fora da janela.
- CSV local usa o cabeçalho `t_ms,rpm,tps,advance,temp`. Abrir um arquivo pausa a
  visualização ao vivo; retomar volta ao buffer da ECU. Limite de importação: 5 MB.
- Perda de comunicação sinaliza dados desatualizados. O simulador é identificado
  explicitamente em todas as telas.

## Referências de produto

Foram consultados os documentos oficiais da FuelTech para adaptar padrões de
organização e análise, mantendo a identidade e as capacidades deste projeto:

- [FTManager](https://www.fueltech.com.br/pages/software): configuração e análise
  de datalogger no mesmo ambiente.
- [Guia FT700, interface de datalogger](https://ftmanager.fueltech.com.br/downloads/manual/Portugues/Guia_rapido_FT700.pdf):
  seleção de canais, valores no cursor e mínimos/máximos.
- [Manual FT450/FT550/FT600](https://ftmanager.fueltech.com.br/downloads/manual/Portugues/FT450_FT550_FT600.pdf):
  organização de configuração e aquisição de dados.

Não há compatibilidade com arquivos proprietários FuelTech. Taxa de aquisição
configurável, gravação por gatilho e novos canais de sensores não foram adicionados:
o firmware mantém os quatro canais e o buffer de 2.400 amostras a 20 Hz.

## Verificação

```powershell
python -m unittest discover -s tests -v
```

Teste opcional da interface (simulador ativo na porta 8080):

```powershell
python -m pip install playwright
python -m playwright install chromium
python tests/browser_smoke.py
```

O teste de navegador cobre cenários, abas, descarte de edição, falha HTTP,
importação/exportação CSV, tela móvel e perda de conexão. Capturas ficam em
`artifacts/`, fora do versionamento.

## Levar à ESP32

Envie **todos** os arquivos de `data/` ao LittleFS, incluindo `config-ui.js` e
`logs.js`, `map-editor.js` e `technical.js`. Python e testes ficam apenas no PC. O JSON de status em `webserver.cpp`
recebeu mais capacidade (2.048 bytes) para acomodar os campos existentes no
ArduinoJson 6; os endpoints foram preservados.

A compilação e os testes físicos da ESP32 ainda precisam ser realizados. O servidor
agora acumula POSTs fragmentados (até 16 KB), reserva 12 KB para o JSON do mapa e
valida as 256 células antes de aplicar uma tabela completa. A validação Windows
não substitui testes de rede, memória e concorrência no firmware.

Para testar o navegador em outra porta, defina `$env:ECU_TEST_URL='http://127.0.0.1:8081'`
antes de `python tests/browser_smoke.py`.
