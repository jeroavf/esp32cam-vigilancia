# ESP32-CAM Stream — Documentação do projeto

Firmware MVP para **ESP32-CAM AI-Thinker** com **shield CAM-MB**: stream de vídeo ao vivo (MJPEG) no navegador, na **rede local**, com Wi‑Fi via portal, **DHCP ou IP fixo**, e resolução/qualidade configuráveis na página.

Este README reúne o **planejamento**, as **decisões de projeto**, a **arquitetura**, o **uso**, e os **acertos/aprendizados** obtidos durante implementação e testes.

---

## 1. Objetivo

Disponibilizar uma câmera IP simples e autônoma:

- Ver o stream em **qualquer navegador** (sem app nativo).
- Configurar Wi‑Fi **sem recompilar** (portal captivo).
- Escolher **DHCP ou IP fixo** dentro da LAN.
- Ajustar **resolução e qualidade JPEG** pela interface web.
- Lidar com as limitações conhecidas do conjunto **ESP32-CAM + CAM-MB** (alimentação, boot, serial).

### Fora do escopo do MVP

| Item | Motivo |
|------|--------|
| Acesso pela internet | Complexidade (NAT, túnel, segurança) |
| Autenticação (usuário/senha) | Rede local considerada suficiente no MVP |
| Snapshot / gravação / microSD | Não pedidos no MVP |
| LED flash, detecção de movimento, IA | Pós-MVP |
| App nativo / Home Assistant | Integração futura opcional |
| Vários clientes de stream simultâneos | Limitação do `WebServer` síncrono |

---

## 2. Planejamento e decisões (definição com o usuário)

Decisões tomadas **antes** da implementação, em rodadas de perguntas:

| Tema | Decisão |
|------|----------|
| Objetivo principal | **Streaming ao vivo** |
| Cliente | **Navegador** (web) |
| Rede | **Somente local** (Wi‑Fi de casa) |
| Hardware | **ESP32-CAM-MB** (AI-Thinker + shield USB) |
| Qualidade de imagem | **Configurável na página web** |
| Autenticação | **Nenhuma** (rede local basta) |
| Extras no MVP | **Só o stream** (mínimo viável) |
| Wi‑Fi | **Portal WiFiManager** |
| IP | **DHCP ou IP fixo** (configurável no portal, persistido em NVS) |
| Entrega inicial | Plano primeiro; depois implementação do MVP |

### Requisitos técnicos adicionados no plano

1. **IP configurável**
   - DHCP (padrão) ou estático (IP, gateway, máscara, DNS).
   - Persistência em **Preferences/NVS** (sobrevive a reboot).
   - Validação básica; IP inválido → fallback DHCP + log no Serial.
   - Recomendação: IP fixo **fora do pool DHCP** do roteador.

2. **Problemas de inicialização com CAM-MB**
   - Brownout / picos de corrente com USB.
   - Mitigações no firmware e documentação de hardware.
   - Sequência de boot pensada para portal e estabilidade.

---

## 3. Arquitetura

```
[Celular/PC mesma LAN]
        │
        │  http://IP/  ou  http://esp32cam.local/
        ▼
┌───────────────────────────────────────┐
│              ESP32-CAM                │
│                                       │
│  boot     → brownout off, logs        │
│  network  → WiFiManager, DHCP/static  │
│  camera   → OV2640, retry, framesize  │
│  webserver→ HTML + MJPEG + /control   │
└───────────────────────────────────────┘
```

### Ordem de boot (versão atual)

```
1. Desliga brownout detector
2. Settle + Serial (115200)
3. Wi-Fi / portal WiFiManager   ← AP sobe cedo se não houver rede
4. Câmera OV2640 (retry)
5. Servidor HTTP (página + stream)
```

> **Histórico:** a primeira versão iniciava a **câmera antes do Wi‑Fi** (menos pico simultâneo). Nos testes, isso **atrasava ou impedia** o AP de configuração se a câmera falhasse/reiniciasse.  
> **Acerto:** portal **antes** da câmera — prioriza configuração e acesso à rede.

### Stack

| Camada | Tecnologia |
|--------|------------|
| Framework | Arduino + ESP32 core |
| Board FQBN | `esp32:esp32:esp32cam` |
| Wi‑Fi config | WiFiManager 2.x |
| Persistência IP | Preferences (NVS), namespace `netcfg` |
| HTTP | `WebServer` (porta 80) |
| Stream | MJPEG (`multipart/x-mixed-replace`) |
| Descoberta | mDNS `esp32cam.local` (quando o SO resolver) |

### Estrutura de arquivos

```
esp32cam/
├── esp32cam.ino       # setup()/loop() — só orquestra
├── config.h           # pinos, SSID do portal, defaults
├── boot.h / boot.cpp  # brownout, settle, motivo de reset
├── camera.h / camera.cpp
├── network.h / network.cpp
├── webserver.h / webserver.cpp
└── README.md          # este documento
```

**Regra de modularização:** o `.ino` não implementa lógica de Wi‑Fi/câmera/HTTP; cada módulo expõe API em `.h` e esconde estado `static` no `.cpp`.

---

## 4. Hardware

### Material

- Módulo **ESP32-CAM** (AI-Thinker), sensor **OV2640**.
- **Shield CAM-MB** (USB-serial + botões IO0/RST) para programação e alimentação via USB.

### Pinout da câmera (AI-Thinker)

Definido em `config.h` — PWDN 32, XCLK 0, SIOD 26, SIOC 27, Y2–Y9, VSYNC 25, HREF 23, PCLK 22, etc.

### Placa no Arduino

- Board: **AI Thinker ESP32-CAM**
- PSRAM: **Enabled** quando a opção existir (melhora buffers de frame)

### Cuidados de alimentação (críticos)

A ESP32-CAM puxa **picos altos** ao ligar Wi‑Fi e câmera. Com CAM-MB + porta USB fraca:

| Sintoma | Causa típica |
|---------|----------------|
| Reboot em loop | Brownout / queda de tensão |
| `camera init failed` | Energia ou flat da câmera |
| Funciona num PC e noutro não | Corrente da porta USB |
| Trava ao abrir serial | Reset por DTR + boot pesado de novo |

**Recomendações:**

1. Cabo USB **curto** e de qualidade (dados + alimentação).  
2. Preferir porta com boa corrente ou **hub USB alimentado**.  
3. Se instável: fonte **5 V ≥ 1 A** em **5V/GND** da placa.  
4. Opcional: capacitor **100–470 µF** entre 5V e GND.  
5. Wi‑Fi da casa em **2.4 GHz** (ESP32 não usa 5 GHz).

---

## 5. Funcionalidades do MVP

### 5.1 Stream e página web

| URL | Função |
|-----|--------|
| `http://IP/` | UI: preview + resolução + qualidade (porta **80**) |
| `http://IP:81/stream` | Stream MJPEG contínuo (porta **81**, servidor separado) |
| `http://IP/status` | JSON (IP, modo DHCP/STATIC, framesize, quality, heap, PSRAM) |
| `http://IP/control?var=framesize&val=N` | Altera resolução (`framesize_t`) |
| `http://IP/control?var=quality&val=N` | JPEG quality **10–63** (menor = melhor) |

> **Por que duas portas?** Com um único `WebServer` síncrono, o `/stream` bloqueava a API: a página **não atualizava IP/modo** e o botão **Aplicar** não alterava framesize/quality. Solução (padrão Espressif): UI/API na **80**, stream na **81** via `esp_http_server`.

Defaults no boot: **VGA (8)**, quality **12**.

### 5.2 Portal Wi‑Fi

| Item | Valor |
|------|--------|
| SSID do AP de setup | **`ESP32CAM-Setup`** |
| Senha do AP | **Nenhuma** (rede aberta) |
| URL do portal | `http://192.168.4.1` |
| Hostname / mDNS | `esp32cam` → `http://esp32cam.local/` |

Comportamento:

- **Sem credenciais salvas:** abre o AP **na hora**, **sem timeout** (fica até configurar).  
- **Com credenciais:** tenta a rede salva; se falhar, reabre o portal.  
- Campos extras no portal: IP fixo (0/1), IP, gateway, máscara, DNS.

### 5.3 DHCP vs IP fixo

1. Usuário escolhe no portal.  
2. Valores vão para NVS (`netcfg`).  
3. No boot: se estático → `WiFi.config(...)` **antes** de conectar.  
4. Após portal com IP fixo: desconecta, reaplica config e reconecta (evita ficar só com DHCP da primeira associação).  
5. Página e Serial mostram IP e modo (`DHCP` / `STATIC`).

---

## 6. Compilar, gravar e monitorar

### Dependências

```bash
arduino-cli lib install WiFiManager
```

Core ESP32 instalado; FQBN: `esp32:esp32:esp32cam`.

### Comandos

```bash
cd /caminho/para/esp32cam

# Compilar
arduino-cli compile --fqbn esp32:esp32:esp32cam .

# Porta
arduino-cli board list

# Upload (ajuste a porta)
arduino-cli upload --fqbn esp32:esp32:esp32cam --port /dev/ttyUSB0 .

# Monitor SEM forçar reset contínuo (muito importante no CAM-MB)
stty -F /dev/ttyUSB0 -hupcl 2>/dev/null || true
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200,dtr=off,rts=off
```

### Upload manual se auto-reset falhar

1. Segure **IO0** (program).  
2. Toque **RST**.  
3. Solte IO0 e envie o sketch.

---

## 7. Como usar no dia a dia

### Primeira configuração

1. Grave o firmware e alimente a placa.  
2. No celular/PC (**Wi‑Fi 2.4 GHz**), conecte em **`ESP32CAM-Setup`** (aberta).  
3. Abra `http://192.168.4.1`.  
4. Selecione a rede da casa + senha.  
5. IP: `0` = DHCP · `1` = fixo (preencha IP/GW/máscara/DNS).  
6. Salve e volte à Wi‑Fi da casa.  
7. Acesse `http://IP/` (IP no Serial ou via nmap/roteador).

### Acessar o stream

- Navegador na **mesma LAN**: `http://IP/`  
- Atalho mDNS (se funcionar no SO): `http://esp32cam.local/`  
- **Não use HTTPS** (só HTTP).  
- Serial **não é necessária** para uso normal após o Wi‑Fi configurado.

### Achar o IP com nmap

Troque a rede pelo prefixo da sua LAN (`ip -4 route`):

```bash
# Hosts com porta 80 aberta
nmap -p 80 --open 192.168.1.0/24

# Só listar IPs candidatos
nmap -p 80 --open -oG - 192.168.1.0/24 | awk '/80\/open/{print $2}'
```

Para cada IP candidato: abrir `http://IP/` ou `curl -sI http://IP/`.

### Reconfigurar Wi‑Fi

Apagar credenciais (erase flash + reupload) ou sketch mínimo:

```cpp
#include <WiFi.h>
void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(1000);
  ESP.restart();
}
void loop() {}
```

No boot seguinte, o AP **`ESP32CAM-Setup`** volta a aparecer.

---

## 8. Acertos e aprendizados nos testes

Registro do que foi observado e **corrigido** durante o uso real com CAM-MB.

### 8.1 AP `ESP32CAM-Setup` não aparecia

| Hipótese | O que vimos | Acerto no firmware |
|----------|-------------|--------------------|
| Portal só depois da câmera | Init da OV2640 demora/falha/reinicia; usuário nunca vê o AP | **Wi‑Fi/portal primeiro**, câmera depois |
| `autoConnect` com credenciais antigas | Tenta STA e atrasa o AP | Sem rede salva → **`startConfigPortal` imediato**, timeout 0 |
| Celular em 5 GHz | ESP32 só 2.4 GHz | Documentar; canal AP fixo **1** |
| AP “invisível” | Potência/canal | `WiFi.setTxPower` alto, canal 1, rede aberta, logs claros no Serial |

**Mensagem esperada no Serial quando o portal está ativo:**

```text
[NET ] sem rede salva — abrindo AP "ESP32CAM-Setup" agora
========================================
 PORTAL WiFi ATIVO
 SSID:  ESP32CAM-Setup
 Senha: (nenhuma — rede aberta)
 URL:   http://192.168.4.1
========================================
```

### 8.2 Abrir a porta serial “trava” ou parece reset

| Fato | Detalhe |
|------|---------|
| Causa | Shield USB (CH340 etc.) pulsa **DTR/RTS** → pino **RST** da ESP32 |
| Efeito | **Um reboot** ao abrir o monitor é **normal** |
| Loop de reset | Reboot + pico Wi‑Fi/câmera + USB fraco → brownout ou crash em loop |
| Default ruim | `arduino-cli monitor` usa **`dtr=on`, `rts=on`** |

**Acerto na prática:**

```bash
stty -F /dev/ttyUSB0 -hupcl 2>/dev/null || true
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200,dtr=off,rts=off
```

**Acerto no firmware:**

- Brownout desligado cedo.  
- `BOOT_SETTLE_MS` maior (500 ms) após reset.  
- Log do **motivo de reset** (`POWERON`, `EXT` = DTR/RST, `BROWNOUT`, `PANIC`, etc.).  
- Dicas no Serial quando o motivo for `EXT` ou `BROWNOUT`.

**Boa ordem de debug:** deixar a placa bootar ~10–15 s → só então abrir o monitor com DTR/RTS off.  
Para só usar o stream, **não precisa** conectar serial.

### 8.3 Brownout e alimentação

| Mitigação de software | Mitigação de hardware |
|----------------------|------------------------|
| `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)` | Cabo/porta/fonte melhores |
| Defaults VGA (não UXGA no boot) | 5 V externo ≥ 1 A se preciso |
| Retry na câmera | Capacitor em 5V/GND se persistir |
| Separar no tempo Wi‑Fi e câmera | Evitar cabo longo / hub sem alimentação |

Desligar o brownout **mascara** a queda de tensão para não ficar em loop de detecção; a corrente ainda precisa ser suficiente para Wi‑Fi + JPEG estáveis.

### 8.4 IP e acesso após configurar o Wi‑Fi

Fluxo validado nos testes:

1. Portal configura SSID/senha (e opcionalmente IP).  
2. Placa associa na LAN.  
3. Serial (se estável) mostra `[NET ] WiFi OK IP=...` e `[HTTP] http://...`.  
4. Sem serial: **nmap** na porta 80 ou lista de clientes do roteador.  
5. Navegador: `http://IP/` — mesma rede, HTTP, 2.4 GHz.

### 8.5 Limitações confirmadas no MVP

- **Um cliente** de stream por vez: o handler de `/stream` bloqueia o `WebServer` enquanto a conexão MJPEG está aberta.  
- Resoluções muito altas (SXGA/UXGA) exigem **PSRAM** e bom Wi‑Fi; se travar, voltar para **VGA/SVGA**.  
- mDNS (`esp32cam.local`) depende do SO/rede (funciona melhor em Linux/macOS; Windows é inconsistente).

---

## 9. Logs úteis no Serial (boot saudável)

Exemplo aproximado de sequência OK:

```text
========================================
 ESP32-CAM Stream MVP
========================================
[BOOT] brownout detector disabled
[BOOT] reset reason: 1 (POWERON)   # ou EXT se abriu serial
[BOOT] heap free=... psram=...
[NET ] iniciando WiFi/portal (antes da camera)
[NET ] modo DHCP (padrao)
[NET ] credenciais WiFi salvas: sim
[NET ] WiFi OK  IP=192.168.x.x  RSSI=...  mode=DHCP
[HTTP] http://192.168.x.x/
[CAM ] PSRAM found — fb_count=2
[CAM ] init OK
[MAIN] setup concluido
```

| Sintoma no log | Ação |
|----------------|------|
| `reset reason: BROWNOUT` | Melhorar alimentação |
| `reset reason: EXT` | Normal ao abrir serial; use dtr/rts off |
| `reset reason: PANIC` | Crash de software — copiar stack se houver |
| `esp_camera_init failed` | Flat OV2640, board, PSRAM, energia |
| Portal sem sumir e sem rede | Ainda no setup; configure ou apague credenciais |
| WiFi OK mas página vazia | Testar `/stream`; baixar framesize |

---

## 10. Troubleshooting resumido

| Problema | O que fazer |
|----------|-------------|
| Não vejo `ESP32CAM-Setup` | Firmware atual? 2.4 GHz? Serial mostra portal? Sem credenciais antigas? Energia OK? |
| Configurei Wi‑Fi e não acho a placa | nmap porta 80; roteador; Serial se possível |
| Abrir Serial reinicia | Esperado no CAM-MB; `dtr=off,rts=off` + `stty -hupcl` |
| Loop de reset | Fonte 5 V, cabo, hub alimentado; ver motivo no Serial |
| Stream não carrega | Mesma rede; HTTP; um cliente; VGA; `/status` |
| Quero mudar de rede | Apagar Wi‑Fi NVS / erase flash e reconfigurar portal |
| Upload falha | IO0 + RST; driver CH340; cabo dados |

---

## 11. Evolução futura (pós-MVP)

Ideias alinhadas ao que ficou de fora, por prioridade sugerida:

1. Botão/hold para **forçar portal** (reset de Wi‑Fi sem erase flash).  
2. Snapshot (`/capture`) e opcional LED flash.  
3. Autenticação HTTP básica se a LAN não for confiável.  
4. `esp_http_server` assíncrono (vários viewers).  
5. Integração Home Assistant / MQTT.  
6. Gravação em microSD ou envio periódico de fotos.  
7. Acesso remoto (túnel controlado + auth).

---

## 12. Constantes principais (`config.h`)

| Símbolo | Valor típico | Significado |
|---------|--------------|-------------|
| `WIFI_PORTAL_NAME` | `ESP32CAM-Setup` | SSID do AP de configuração |
| `WIFI_HOSTNAME` / `WIFI_MDNS_NAME` | `esp32cam` | Hostname e mDNS |
| `CAM_DEFAULT_FRAMESIZE` | `8` (VGA) | Resolução no boot |
| `CAM_DEFAULT_QUALITY` | `12` | JPEG (10–63) |
| `CAM_INIT_RETRIES` | `5` | Tentativas de init da câmera |
| `BOOT_SETTLE_MS` | `500` | Pausa pós-reset / serial |
| `WEB_SERVER_PORT` | `80` | HTTP |
| `NET_PREFS_NS` | `netcfg` | Namespace NVS do IP |

---

## 13. Linha do tempo do projeto (resumo)

| Etapa | Resultado |
|-------|-----------|
| Definição de objetivo | Stream local no navegador, MVP mínimo |
| Requisitos de IP e CAM-MB | DHCP/fixo + plano anti-brownout |
| Implementação modular | boot, camera, network, webserver |
| Compilação | OK com `esp32:esp32:esp32cam` |
| Teste: AP não listado | Portal antes da câmera; `startConfigPortal` sem timeout |
| Teste: Wi‑Fi configurado | Acesso via IP na LAN |
| Teste: serial “trava” | Documentado DTR/RST; monitor com dtr/rts off; logs de reset |
| Documentação | Este README unificado |

---

## 14. Licença e uso

Projeto de uso pessoal/estudo. Use apenas em redes em que você tem autorização. Sem autenticação no MVP: **não exponha a porta 80 na internet** sem camadas extras de segurança.

---

*Documento gerado a partir do planejamento, implementação e testes do MVP ESP32-CAM Stream.*
