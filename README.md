# ESP32-CAM Stream — Documentação do projeto

**[English version](#english)**

Firmware para **ESP32-CAM AI-Thinker** com **shield CAM-MB**: stream de vídeo ao vivo (MJPEG) no navegador, na **rede local**, com Wi‑Fi via portal, **DHCP ou IP fixo**, resolução/qualidade configuráveis na página, e **MQTT** (captura de imagem + toggle do LED flash).

Este README reúne o **planejamento**, as **decisões de projeto**, a **arquitetura**, o **uso**, e os **acertos/aprendizados** obtidos durante implementação e testes.

---

## 1. Objetivo

Disponibilizar uma câmera IP simples e autônoma:

- Ver o stream em **qualquer navegador** (sem app nativo).
- Configurar Wi‑Fi **sem recompilar** (portal captivo).
- Escolher **DHCP ou IP fixo** dentro da LAN.
- Ajustar **resolução e qualidade JPEG** pela interface web.
- Receber **comandos MQTT** (captura JPEG + LED) e publicar a imagem em um tópico.
- Configurar o broker MQTT **pela mesma página web** (sem recompilar).
- Lidar com as limitações conhecidas do conjunto **ESP32-CAM + CAM-MB** (alimentação, boot, serial).

### Fora do escopo atual

| Item | Motivo |
|------|--------|
| Acesso pela internet | Complexidade (NAT, túnel, segurança) |
| Autenticação HTTP (usuário/senha) | Rede local considerada suficiente |
| Gravação contínua / microSD | Não implementado |
| Detecção de movimento, IA | Pós-MVP |
| Home Assistant MQTT discovery | Integração futura opcional |
| TLS MQTT (`mqtts`) | Pós-MVP |
| Vários viewers de stream simultâneos | Limitação prática de CPU/Wi‑Fi (1 cliente de stream é o alvo) |

---

## 2. Planejamento e decisões

| Tema | Decisão |
|------|----------|
| Objetivo principal | **Streaming ao vivo** + **MQTT** (cmd → captura/LED) |
| Cliente | **Navegador** (web) |
| Rede | **Somente local** (Wi‑Fi de casa) |
| Hardware | **ESP32-CAM-MB** (AI-Thinker + shield USB) |
| Qualidade de imagem | **Configurável na página web** |
| Autenticação web | **Nenhuma** (rede local basta) |
| Wi‑Fi | **Portal WiFiManager** (só rede/IP) |
| IP | **DHCP ou IP fixo** (portal, NVS `netcfg`) |
| MQTT | **Página web** (painel próprio + NVS `mqttcfg`) |
| Stream HTTP | **Porta 80** (`/stream`, mesma origem da UI) |

### Requisitos técnicos

1. **IP configurável** — DHCP ou estático; persistência NVS; fallback DHCP se inválido.
2. **Estabilidade CAM-MB** — brownout off, settle no boot, portal antes da câmera.
3. **MQTT** — host/porta/user/pass/tópicos na UI; comandos de texto; JPEG binário no tópico de saída.
4. **Stream confiável** — não depender da porta 81 (em algumas redes ela não abre).

---

## 3. Arquitetura

```
[Celular/PC mesma LAN]
        │
        │  http://IP/  ou  http://esp32cam.local/
        ▼
┌───────────────────────────────────────────┐
│                 ESP32-CAM                 │
│                                           │
│  boot     → brownout off, logs            │
│  led      → flash GPIO 4                  │
│  network  → WiFiManager, DHCP/static      │
│  camera   → OV2640, retry, framesize      │
│  webserver→ UI + /stream + /control + MQTT│
│  mqtt     → sub cmd / pub JPEG            │
└───────────────────────────────────────────┘
        │
        │  MQTT (opcional)
        ▼
   [broker na LAN]
```

### Ordem de boot

```
1. Desliga brownout detector
2. Settle + Serial (115200)
3. LED init (GPIO 4)
4. Wi-Fi / portal WiFiManager   ← AP sobe cedo se não houver rede
5. Câmera OV2640 (retry)
6. MQTT carrega prefs NVS (conecta se host + WiFi OK)
7. Servidor HTTP (UI + /stream na porta 80; :81 opcional)
```

> **Histórico:** a primeira versão iniciava a **câmera antes do Wi‑Fi**. Nos testes, isso atrasava ou impedia o AP de configuração.  
> **Acerto:** portal **antes** da câmera.  
> **Stream:** a UI usava `http://IP:81/stream`; em testes a **porta 81 não aceitava conexão** enquanto a 80 funcionava.  
> **Acerto:** stream em **`/stream` na porta 80** (mesma origem).

### Stack

| Camada | Tecnologia |
|--------|------------|
| Framework | Arduino + ESP32 core |
| Board FQBN | `esp32:esp32:esp32cam` |
| Wi‑Fi config | WiFiManager 2.x |
| Persistência IP | Preferences (NVS), namespace `netcfg` |
| Persistência MQTT | Preferences (NVS), namespace `mqttcfg` |
| MQTT | PubSubClient 2.x |
| HTTP | `esp_http_server` (porta **80**; porta **81** opcional) |
| Stream | MJPEG (`multipart/x-mixed-replace`) em `/stream` |
| Descoberta | mDNS `esp32cam.local` (quando o SO resolver) |

### Estrutura de arquivos

```
esp32cam/
├── esp32cam.ino       # setup()/loop() — só orquestra
├── config.h           # pinos, SSID do portal, defaults MQTT/LED
├── boot.h / boot.cpp  # brownout, settle, motivo de reset
├── led.h / led.cpp    # flash onboard (GPIO 4)
├── camera.h / camera.cpp
├── network.h / network.cpp
├── mqtt.h / mqtt.cpp  # subscribe cmd + publish JPEG
├── webserver.h / webserver.cpp
└── README.md          # este documento
```

**Regra de modularização:** o `.ino` não implementa lógica de Wi‑Fi/câmera/HTTP/MQTT; cada módulo expõe API em `.h` e esconde estado `static` no `.cpp`.

---

## 4. Hardware

### Material

- Módulo **ESP32-CAM** (AI-Thinker), sensor **OV2640**.
- **Shield CAM-MB** (USB-serial + botões IO0/RST) para programação e alimentação via USB.

### Pinout da câmera (AI-Thinker)

Definido em `config.h` — PWDN 32, XCLK 0, SIOD 26, SIOC 27, Y2–Y9, VSYNC 25, HREF 23, PCLK 22, etc.

### LED

| Uso | GPIO | Notas |
|-----|------|--------|
| Flash branco (toggle MQTT) | **4** | Active-high; brilhante |

### Placa no Arduino

- Board: **AI Thinker ESP32-CAM**
- PSRAM: **Enabled** quando a opção existir (melhora buffers de frame)

### Cuidados de alimentação (críticos)

| Sintoma | Causa típica |
|---------|----------------|
| Reboot em loop | Brownout / queda de tensão |
| `camera init failed` | Energia ou flat da câmera |
| Funciona num PC e noutro não | Corrente da porta USB |
| Trava ao abrir serial | Reset por DTR + boot pesado de novo |

**Recomendações:** cabo USB curto e de qualidade; porta com boa corrente ou hub alimentado; se instável, fonte **5 V ≥ 1 A** em 5V/GND; Wi‑Fi **2.4 GHz**.

---

## 5. Funcionalidades

### 5.1 Página web e stream

A UI tem dois painéis:

1. **Imagem** — resolução, qualidade JPEG, botão **Aplicar imagem**, status (IP, modo, MQTT, LED).
2. **MQTT** — servidor, porta, usuário, senha, tópicos; botão próprio **Salvar MQTT**.

| URL | Função |
|-----|--------|
| `http://IP/` | UI completa (porta **80**) |
| `http://IP/stream` | Stream MJPEG (**porta 80**, mesma origem) |
| `http://IP:81/stream` | Espelho opcional (só se o 2º servidor subir) |
| `http://IP/status` | JSON de status (inclui MQTT e LED) |
| `http://IP/control?var=framesize&val=N` | Altera resolução |
| `http://IP/control?var=quality&val=N` | JPEG quality **10–63** (menor = melhor) |
| `GET /mqtt` | JSON da config MQTT |
| `POST /mqtt` | Salva MQTT (`application/x-www-form-urlencoded`) e reconecta |

> **Por que stream na 80?** Em testes, a porta **81 não abria** (timeout) enquanto a 80 respondia. Com `esp_http_server`, cada conexão tem worker próprio: API e stream convivem na mesma porta.

Defaults no boot: **VGA (8)**, quality **12**.

### 5.2 Portal Wi‑Fi

| Item | Valor |
|------|--------|
| SSID do AP de setup | **`ESP32CAM-Setup`** |
| Senha do AP | **Nenhuma** (rede aberta) |
| URL do portal | `http://192.168.4.1` |
| Hostname / mDNS | `esp32cam` → `http://esp32cam.local/` |

- **Sem credenciais salvas:** AP imediato, **sem timeout**.  
- **Com credenciais:** tenta a rede salva; se falhar, reabre o portal.  
- Campos do portal: IP fixo (0/1), IP, gateway, máscara, DNS.  
- **MQTT não está no portal** — configure na página web após a placa estar na LAN.

### 5.3 DHCP vs IP fixo

1. Escolha no portal → NVS (`netcfg`).  
2. Boot estático: `WiFi.config(...)` **antes** de conectar.  
3. Após portal com IP fixo: desconecta, reaplica e reconecta.  
4. Página e Serial mostram `DHCP` ou `STATIC`.

### 5.4 MQTT (comandos + imagem)

Configurado em `http://IP/` → painel **MQTT** → **Salvar MQTT**. Persistido em NVS (`mqttcfg`).

| Campo | Função |
|-------|--------|
| Servidor | Host/IP do broker (`vazio` = MQTT desligado) |
| Porta | Padrão `1883` |
| Usuário / senha | Opcional |
| Tópico entrada | Subscribe — comandos de texto |
| Tópico saída | Publish — JPEG binário |

**Comandos** (payload texto, case-insensitive):

| Payload | Ação |
|---------|------|
| `capture` / `snapshot` / `photo` | Captura 1 frame e publica JPEG no tópico de saída |
| `led_toggle` / `led` / `toggle` | Alterna o LED flash (GPIO 4) |
| `led_on` / `led_off` | Liga / desliga o LED |

**Exemplo (mosquitto):**

```bash
# Terminal 1 — salvar a próxima imagem
mosquitto_sub -h 192.168.1.10 -t 'esp32cam/image' -C 1 > /tmp/esp32cam.jpg

# Terminal 2 — comandos
mosquitto_pub -h 192.168.1.10 -t 'esp32cam/cmd' -m 'capture'
mosquitto_pub -h 192.168.1.10 -t 'esp32cam/cmd' -m 'led_toggle'
```

**Notas:**

- Host vazio desliga o cliente MQTT.  
- Ao salvar na UI, a placa desconecta e reconecta **sem reboot**.  
- Buffer padrão `48 KB` (`MQTT_BUFFER_SIZE`) — prefira VGA/SVGA para captura MQTT.  
- Reconnect automático ~5 s se o broker cair.  
- `/status` expõe `mqtt_enabled`, `mqtt_connected`, tópicos, `led`.

---

## 6. Compilar, gravar e monitorar

### Dependências

```bash
arduino-cli lib install WiFiManager
arduino-cli lib install PubSubClient
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

# Monitor SEM forçar reset contínuo (importante no CAM-MB)
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
2. No celular/PC (**Wi‑Fi 2.4 GHz**), conecte em **`ESP32CAM-Setup`**.  
3. Abra `http://192.168.4.1` — configure SSID/senha e opcionalmente IP.  
4. Volte à Wi‑Fi da casa.  
5. Acesse `http://IP/` (IP no Serial, nmap ou roteador).  
6. Ajuste resolução/qualidade se quiser.  
7. No painel **MQTT**, preencha broker e tópicos → **Salvar MQTT**.

### Acessar o stream

- Navegador na **mesma LAN**: `http://IP/`  
- Stream direto: `http://IP/stream`  
- mDNS (se funcionar): `http://esp32cam.local/`  
- **Não use HTTPS**.  
- Serial **não é necessária** após o Wi‑Fi configurado.

### Achar o IP com nmap

```bash
nmap -p 80 --open 192.168.1.0/24
nmap -p 80 --open -oG - 192.168.1.0/24 | awk '/80\/open/{print $2}'
```

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
A config MQTT em NVS (`mqttcfg`) **não** é apagada só por limpar o Wi‑Fi (a menos que faça erase completo da flash).

---

## 8. Acertos e aprendizados nos testes

### 8.1 AP `ESP32CAM-Setup` não aparecia

| Hipótese | Acerto |
|----------|--------|
| Portal só depois da câmera | **Wi‑Fi/portal primeiro**, câmera depois |
| `autoConnect` com credenciais antigas | Sem rede salva → **`startConfigPortal` imediato**, timeout 0 |
| Celular em 5 GHz | Documentar; canal AP fixo **1** |
| AP “invisível” | TX power alto, rede aberta, logs claros |

### 8.2 Abrir a porta serial “trava” ou reseta

- Causa: shield USB pulsa **DTR/RTS** → **RST**. Um reboot ao abrir o monitor é **normal**.  
- Monitor: `dtr=off,rts=off` + `stty -hupcl`.  
- Firmware: brownout off, `BOOT_SETTLE_MS`, log do motivo de reset.

### 8.3 Brownout e alimentação

Software (brownout off, VGA no boot, retry, Wi‑Fi antes da câmera) + hardware (cabo/fonte/hub).

### 8.4 Stream na página em preto (porta 81)

| Observação | Acerto |
|------------|--------|
| `http://IP/` e `/status` OK; `IP:81` timeout | Stream passou para **`/stream` na porta 80** |
| UI apontava `hostname:81` | URL relativa `/stream` (mesma origem) |
| 2º httpd na 81 | Opcional; falha não derruba a UI |

### 8.5 Limitações confirmadas

- Preferir **um cliente** de stream por vez.  
- SXGA/UXGA exigem **PSRAM** e bom Wi‑Fi; se travar, voltar a **VGA/SVGA**.  
- mDNS depende do SO (melhor em Linux/macOS).  
- JPEG MQTT limitado pelo buffer (`MQTT_BUFFER_SIZE`).  
- Sem autenticação: **não exponha a porta 80 na internet**.

---

## 9. Logs úteis no Serial (boot saudável)

```text
========================================
 ESP32-CAM Stream + MQTT
========================================
[BOOT] brownout detector disabled
[BOOT] reset reason: 1 (POWERON)
[LED ] GPIO 4 init (off)
[NET ] iniciando WiFi/portal (antes da camera)
[NET ] modo DHCP (padrao)
[NET ] credenciais WiFi salvas: sim
[NET ] WiFi OK  IP=192.168.x.x  RSSI=...  mode=DHCP
[CAM ] PSRAM found — fb_count=2
[CAM ] init OK
[MQTT] host=... port=1883 in=... out=...
[MQTT] OK  out=...
[HTTP] UI/API:  http://192.168.x.x/
[HTTP] stream:  http://192.168.x.x/stream
[MAIN] setup concluido
```

| Sintoma no log | Ação |
|----------------|------|
| `reset reason: BROWNOUT` | Melhorar alimentação |
| `reset reason: EXT` | Normal ao abrir serial; dtr/rts off |
| `esp_camera_init failed` | Flat OV2640, board, PSRAM, energia |
| `MQTT desligado (host vazio…)` | Configurar na página web |
| `MQTT falha rc=…` | Broker, porta, user/pass, firewall |
| WiFi OK mas imagem preta | Testar `http://IP/stream`; refresh forçado; VGA |

---

## 10. Troubleshooting resumido

| Problema | O que fazer |
|----------|-------------|
| Não vejo `ESP32CAM-Setup` | Firmware atual? 2.4 GHz? Serial mostra portal? Energia OK? |
| Configurei Wi‑Fi e não acho a placa | nmap porta 80; roteador; Serial |
| Abrir Serial reinicia | Esperado no CAM-MB; `dtr=off,rts=off` |
| Loop de reset | Fonte 5 V, cabo, hub alimentado |
| Stream não carrega / tela preta | Use `http://IP/` (não `:81`); force refresh; `curl -I http://IP/stream`; VGA |
| MQTT não conecta | Host/porta na UI; broker na LAN; Serial `[MQTT]` |
| Capture MQTT falha | Buffer/resolução; confira tópico de saída |
| LED não reage | Comando `led_toggle`; GPIO 4 |
| Quero mudar de rede | Limpar Wi‑Fi NVS / erase flash + portal |
| Upload falha | IO0 + RST; driver CH340; cabo de dados |

---

## 11. Evolução futura

1. Botão/hold para **forçar portal** (sem erase flash).  
2. Snapshot HTTP (`/capture`) além do MQTT.  
3. Autenticação HTTP básica.  
4. Vários viewers de stream (se viável).  
5. Home Assistant MQTT discovery.  
6. TLS MQTT (`mqtts`), LWT / birth.  
7. microSD ou fotos periódicas.  
8. Acesso remoto (túnel + auth).

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
| `WEB_SERVER_PORT` | `80` | HTTP UI + stream |
| `STREAM_SERVER_PORT` | `81` | Espelho opcional do stream |
| `NET_PREFS_NS` | `netcfg` | NVS do IP |
| `MQTT_PREFS_NS` | `mqttcfg` | NVS MQTT |
| `MQTT_DEFAULT_TOPIC_IN` | `esp32cam/cmd` | Tópico de comandos |
| `MQTT_DEFAULT_TOPIC_OUT` | `esp32cam/image` | Tópico da imagem |
| `MQTT_BUFFER_SIZE` | `48*1024` | Buffer do pacote MQTT |
| `LED_GPIO` | `4` | Flash branco AI-Thinker |

---

## 13. Linha do tempo do projeto (resumo)

| Etapa | Resultado |
|-------|-----------|
| Definição de objetivo | Stream local no navegador |
| IP e CAM-MB | DHCP/fixo + anti-brownout |
| Implementação modular | boot, camera, network, webserver |
| Compilação | OK `esp32:esp32:esp32cam` |
| Testes portal / serial / alimentação | Correções documentadas |
| MQTT | Comandos + JPEG + LED; config na página web |
| Stream porta 81 inacessível | Stream movido para `/stream` na porta 80 |
| Documentação | README PT + EN unificado |

---

## 14. Licença e uso

Projeto de uso pessoal/estudo. Use apenas em redes em que você tem autorização. Sem autenticação: **não exponha a porta 80 na internet** sem camadas extras de segurança.

---

*Documento gerado a partir do planejamento, implementação e testes do ESP32-CAM Stream + MQTT.*

---
---

<a id="english"></a>

# ESP32-CAM Stream — Project Documentation

Firmware for **ESP32-CAM AI-Thinker** with **CAM-MB shield**: live video stream (MJPEG) in the browser, on the **local network**, with Wi‑Fi via portal, **DHCP or static IP**, resolution/quality configurable on the page, and **MQTT** (image capture + flash LED toggle).

This README gathers the **planning**, **design decisions**, **architecture**, **usage**, and the **fixes/lessons learned** from implementation and testing.

---

## 1. Goal

Provide a simple, self-contained IP camera:

- View the stream in **any browser** (no native app).
- Configure Wi‑Fi **without recompiling** (captive portal).
- Choose **DHCP or static IP** on the LAN.
- Adjust **resolution and JPEG quality** from the web UI.
- Receive **MQTT commands** (JPEG capture + LED) and publish the image to a topic.
- Configure the MQTT broker **from the same web page** (no recompile).
- Handle known limitations of the **ESP32-CAM + CAM-MB** stack (power, boot, serial).

### Out of current scope

| Item | Reason |
|------|--------|
| Internet access | Complexity (NAT, tunnel, security) |
| HTTP authentication (user/password) | Local network considered enough |
| Continuous recording / microSD | Not implemented |
| Motion detection, AI | Post-MVP |
| Home Assistant MQTT discovery | Optional future integration |
| TLS MQTT (`mqtts`) | Post-MVP |
| Multiple simultaneous stream viewers | Practical CPU/Wi‑Fi limit (one stream client is the target) |

---

## 2. Planning and decisions

| Topic | Decision |
|-------|----------|
| Main goal | **Live streaming** + **MQTT** (cmd → capture/LED) |
| Client | **Browser** (web) |
| Network | **Local only** (home Wi‑Fi) |
| Hardware | **ESP32-CAM-MB** (AI-Thinker + USB shield) |
| Image quality | **Configurable on the web page** |
| Web authentication | **None** (local network is enough) |
| Wi‑Fi | **WiFiManager portal** (network/IP only) |
| IP | **DHCP or static** (portal, NVS `netcfg`) |
| MQTT | **Web page** (dedicated panel + NVS `mqttcfg`) |
| HTTP stream | **Port 80** (`/stream`, same origin as the UI) |

### Technical requirements

1. **Configurable IP** — DHCP or static; NVS persistence; DHCP fallback if invalid.  
2. **CAM-MB stability** — brownout off, boot settle, portal before camera.  
3. **MQTT** — host/port/user/pass/topics in the UI; text commands; binary JPEG on output topic.  
4. **Reliable stream** — do not depend on port 81 (it does not open on some networks).

---

## 3. Architecture

```
[Phone/PC on same LAN]
        │
        │  http://IP/  or  http://esp32cam.local/
        ▼
┌───────────────────────────────────────────┐
│                 ESP32-CAM                 │
│                                           │
│  boot     → brownout off, logs            │
│  led      → flash GPIO 4                  │
│  network  → WiFiManager, DHCP/static      │
│  camera   → OV2640, retry, framesize      │
│  webserver→ UI + /stream + /control + MQTT│
│  mqtt     → sub cmd / pub JPEG            │
└───────────────────────────────────────────┘
        │
        │  MQTT (optional)
        ▼
   [broker on LAN]
```

### Boot order

```
1. Disable brownout detector
2. Settle + Serial (115200)
3. LED init (GPIO 4)
4. Wi-Fi / WiFiManager portal   ← AP comes up early if no network
5. OV2640 camera (retry)
6. MQTT loads NVS prefs (connects if host + WiFi OK)
7. HTTP server (UI + /stream on port 80; :81 optional)
```

> **History:** the first version started the **camera before Wi‑Fi**. In testing, that delayed or prevented the config AP.  
> **Fix:** portal **before** the camera.  
> **Stream:** the UI used `http://IP:81/stream`; in tests **port 81 timed out** while port 80 worked.  
> **Fix:** stream at **`/stream` on port 80** (same origin).

### Stack

| Layer | Technology |
|-------|------------|
| Framework | Arduino + ESP32 core |
| Board FQBN | `esp32:esp32:esp32cam` |
| Wi‑Fi config | WiFiManager 2.x |
| IP persistence | Preferences (NVS), namespace `netcfg` |
| MQTT persistence | Preferences (NVS), namespace `mqttcfg` |
| MQTT | PubSubClient 2.x |
| HTTP | `esp_http_server` (port **80**; port **81** optional) |
| Stream | MJPEG (`multipart/x-mixed-replace`) at `/stream` |
| Discovery | mDNS `esp32cam.local` (when the OS resolves it) |

### File structure

```
esp32cam/
├── esp32cam.ino       # setup()/loop() — orchestration only
├── config.h           # pins, portal SSID, MQTT/LED defaults
├── boot.h / boot.cpp  # brownout, settle, reset reason
├── led.h / led.cpp    # onboard flash (GPIO 4)
├── camera.h / camera.cpp
├── network.h / network.cpp
├── mqtt.h / mqtt.cpp  # subscribe cmd + publish JPEG
├── webserver.h / webserver.cpp
└── README.md          # this document
```

**Modularization rule:** the `.ino` does not implement Wi‑Fi/camera/HTTP/MQTT logic; each module exposes an API in `.h` and hides `static` state in the `.cpp`.

---

## 4. Hardware

### Bill of materials

- **ESP32-CAM** module (AI-Thinker), **OV2640** sensor.
- **CAM-MB shield** (USB-serial + IO0/RST buttons) for programming and USB power.

### Camera pinout (AI-Thinker)

Defined in `config.h` — PWDN 32, XCLK 0, SIOD 26, SIOC 27, Y2–Y9, VSYNC 25, HREF 23, PCLK 22, etc.

### LED

| Use | GPIO | Notes |
|-----|------|--------|
| White flash (MQTT toggle) | **4** | Active-high; bright |

### Board settings in Arduino

- Board: **AI Thinker ESP32-CAM**
- PSRAM: **Enabled** when the option exists (improves frame buffers)

### Power supply notes (critical)

| Symptom | Typical cause |
|---------|----------------|
| Reboot loop | Brownout / voltage sag |
| `camera init failed` | Power or camera ribbon cable |
| Works on one PC, not another | USB port current |
| Freezes when opening serial | Reset via DTR + heavy boot again |

**Recommendations:** short good USB cable; high-current port or powered hub; if unstable, **5 V ≥ 1 A** on 5V/GND; home Wi‑Fi on **2.4 GHz**.

---

## 5. Features

### 5.1 Web page and stream

The UI has two panels:

1. **Image** — resolution, JPEG quality, **Apply image** button, status (IP, mode, MQTT, LED).
2. **MQTT** — server, port, user, password, topics; dedicated **Save MQTT** button.

| URL | Function |
|-----|----------|
| `http://IP/` | Full UI (port **80**) |
| `http://IP/stream` | Continuous MJPEG (**port 80**, same origin) |
| `http://IP:81/stream` | Optional mirror (only if the 2nd server starts) |
| `http://IP/status` | Status JSON (includes MQTT and LED) |
| `http://IP/control?var=framesize&val=N` | Change resolution |
| `http://IP/control?var=quality&val=N` | JPEG quality **10–63** (lower = better) |
| `GET /mqtt` | MQTT config JSON |
| `POST /mqtt` | Save MQTT (`application/x-www-form-urlencoded`) and reconnect |

> **Why stream on 80?** In testing, port **81 did not accept connections** (timeout) while 80 responded. With `esp_http_server`, each connection has its own worker: API and stream coexist on the same port.

Boot defaults: **VGA (8)**, quality **12**.

### 5.2 Wi‑Fi portal

| Item | Value |
|------|--------|
| Setup AP SSID | **`ESP32CAM-Setup`** |
| AP password | **None** (open network) |
| Portal URL | `http://192.168.4.1` |
| Hostname / mDNS | `esp32cam` → `http://esp32cam.local/` |

- **No saved credentials:** AP immediately, **no timeout**.  
- **With credentials:** tries the saved network; on failure, reopens the portal.  
- Portal fields: static IP (0/1), IP, gateway, netmask, DNS.  
- **MQTT is not in the portal** — configure it on the web page after the board is on the LAN.

### 5.3 DHCP vs static IP

1. Choose in the portal → NVS (`netcfg`).  
2. Static boot: `WiFi.config(...)` **before** connecting.  
3. After portal with static IP: disconnect, reapply, reconnect.  
4. Page and Serial show `DHCP` or `STATIC`.

### 5.4 MQTT (commands + image)

Configured at `http://IP/` → **MQTT** panel → **Save MQTT**. Persisted in NVS (`mqttcfg`).

| Field | Function |
|-------|----------|
| Server | Broker host/IP (`empty` = MQTT off) |
| Port | Default `1883` |
| User / password | Optional |
| Input topic | Subscribe — text commands |
| Output topic | Publish — binary JPEG |

**Commands** (text payload, case-insensitive):

| Payload | Action |
|---------|--------|
| `capture` / `snapshot` / `photo` | Capture one frame and publish JPEG on the output topic |
| `led_toggle` / `led` / `toggle` | Toggle flash LED (GPIO 4) |
| `led_on` / `led_off` | Turn LED on / off |

**Example (mosquitto):**

```bash
# Terminal 1 — save the next image
mosquitto_sub -h 192.168.1.10 -t 'esp32cam/image' -C 1 > /tmp/esp32cam.jpg

# Terminal 2 — commands
mosquitto_pub -h 192.168.1.10 -t 'esp32cam/cmd' -m 'capture'
mosquitto_pub -h 192.168.1.10 -t 'esp32cam/cmd' -m 'led_toggle'
```

**Notes:**

- Empty host disables the MQTT client.  
- Saving in the UI disconnects and reconnects **without reboot**.  
- Default buffer `48 KB` (`MQTT_BUFFER_SIZE`) — prefer VGA/SVGA for MQTT capture.  
- Automatic reconnect ~5 s if the broker drops.  
- `/status` exposes `mqtt_enabled`, `mqtt_connected`, topics, `led`.

---

## 6. Build, flash, and monitor

### Dependencies

```bash
arduino-cli lib install WiFiManager
arduino-cli lib install PubSubClient
```

ESP32 core installed; FQBN: `esp32:esp32:esp32cam`.

### Commands

```bash
cd /path/to/esp32cam

# Compile
arduino-cli compile --fqbn esp32:esp32:esp32cam .

# Port
arduino-cli board list

# Upload (adjust the port)
arduino-cli upload --fqbn esp32:esp32:esp32cam --port /dev/ttyUSB0 .

# Monitor WITHOUT continuous forced reset (important on CAM-MB)
stty -F /dev/ttyUSB0 -hupcl 2>/dev/null || true
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200,dtr=off,rts=off
```

### Manual upload if auto-reset fails

1. Hold **IO0** (program).  
2. Tap **RST**.  
3. Release IO0 and upload the sketch.

---

## 7. Day-to-day usage

### First-time setup

1. Flash the firmware and power the board.  
2. On phone/PC (**2.4 GHz Wi‑Fi**), connect to **`ESP32CAM-Setup`**.  
3. Open `http://192.168.4.1` — set SSID/password and optionally IP.  
4. Return to home Wi‑Fi.  
5. Open `http://IP/` (IP from Serial, nmap, or router).  
6. Adjust resolution/quality if desired.  
7. In the **MQTT** panel, fill broker and topics → **Save MQTT**.

### Accessing the stream

- Browser on the **same LAN**: `http://IP/`  
- Direct stream: `http://IP/stream`  
- mDNS (if supported): `http://esp32cam.local/`  
- **Do not use HTTPS**.  
- Serial is **not required** after Wi‑Fi is configured.

### Finding the IP with nmap

```bash
nmap -p 80 --open 192.168.1.0/24
nmap -p 80 --open -oG - 192.168.1.0/24 | awk '/80\/open/{print $2}'
```

### Reconfiguring Wi‑Fi

Erase credentials (erase flash + reupload) or use a minimal sketch:

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

On the next boot, the **`ESP32CAM-Setup`** AP appears again.  
MQTT config in NVS (`mqttcfg`) is **not** cleared by Wi‑Fi-only reset (unless you erase the full flash).

---

## 8. Fixes and lessons from testing

### 8.1 `ESP32CAM-Setup` AP did not appear

| Hypothesis | Fix |
|------------|-----|
| Portal only after camera | **Wi‑Fi/portal first**, camera later |
| `autoConnect` with old credentials | No saved network → **immediate `startConfigPortal`**, timeout 0 |
| Phone on 5 GHz | Document; AP channel fixed to **1** |
| AP “invisible” | High TX power, open network, clear logs |

### 8.2 Opening serial “freezes” or resets

- Cause: USB shield pulses **DTR/RTS** → **RST**. One reboot when opening the monitor is **normal**.  
- Monitor: `dtr=off,rts=off` + `stty -hupcl`.  
- Firmware: brownout off, `BOOT_SETTLE_MS`, reset-reason log.

### 8.3 Brownout and power

Software (brownout off, VGA at boot, retry, Wi‑Fi before camera) + hardware (cable/supply/hub).

### 8.4 Black stream on the page (port 81)

| Observation | Fix |
|-------------|-----|
| `http://IP/` and `/status` OK; `IP:81` timeout | Stream moved to **`/stream` on port 80** |
| UI pointed at `hostname:81` | Relative URL `/stream` (same origin) |
| Second httpd on 81 | Optional; failure does not take down the UI |

### 8.5 Confirmed limitations

- Prefer **one stream client** at a time.  
- SXGA/UXGA need **PSRAM** and good Wi‑Fi; if it hangs, drop to **VGA/SVGA**.  
- mDNS depends on the OS (better on Linux/macOS).  
- MQTT JPEG limited by buffer (`MQTT_BUFFER_SIZE`).  
- No authentication: **do not expose port 80 to the internet**.

---

## 9. Useful Serial logs (healthy boot)

```text
========================================
 ESP32-CAM Stream + MQTT
========================================
[BOOT] brownout detector disabled
[BOOT] reset reason: 1 (POWERON)
[LED ] GPIO 4 init (off)
[NET ] iniciando WiFi/portal (antes da camera)
[NET ] modo DHCP (padrao)
[NET ] credenciais WiFi salvas: sim
[NET ] WiFi OK  IP=192.168.x.x  RSSI=...  mode=DHCP
[CAM ] PSRAM found — fb_count=2
[CAM ] init OK
[MQTT] host=... port=1883 in=... out=...
[MQTT] OK  out=...
[HTTP] UI/API:  http://192.168.x.x/
[HTTP] stream:  http://192.168.x.x/stream
[MAIN] setup concluido
```

| Log symptom | Action |
|-------------|--------|
| `reset reason: BROWNOUT` | Improve power supply |
| `reset reason: EXT` | Normal when opening serial; dtr/rts off |
| `esp_camera_init failed` | OV2640 ribbon, board, PSRAM, power |
| `MQTT desligado (host vazio…)` | Configure on the web page |
| `MQTT falha rc=…` | Broker, port, user/pass, firewall |
| WiFi OK but black image | Test `http://IP/stream`; hard refresh; VGA |

---

## 10. Troubleshooting summary

| Problem | What to do |
|---------|------------|
| Cannot see `ESP32CAM-Setup` | Current firmware? 2.4 GHz? Serial shows portal? Power OK? |
| Configured Wi‑Fi but cannot find the board | nmap port 80; router; Serial |
| Opening Serial reboots | Expected on CAM-MB; `dtr=off,rts=off` |
| Reset loop | 5 V supply, cable, powered hub |
| Stream does not load / black screen | Use `http://IP/` (not `:81`); hard refresh; `curl -I http://IP/stream`; VGA |
| MQTT does not connect | Host/port in UI; broker on LAN; Serial `[MQTT]` |
| MQTT capture fails | Buffer/resolution; check output topic |
| LED does not respond | Command `led_toggle`; GPIO 4 |
| Want to change network | Clear Wi‑Fi NVS / erase flash + portal |
| Upload fails | IO0 + RST; CH340 driver; data cable |

---

## 11. Future evolution

1. Button/hold to **force portal** (no flash erase).  
2. HTTP snapshot (`/capture`) in addition to MQTT.  
3. Basic HTTP authentication.  
4. Multiple stream viewers (if feasible).  
5. Home Assistant MQTT discovery.  
6. TLS MQTT (`mqtts`), LWT / birth.  
7. microSD or periodic photos.  
8. Remote access (tunnel + auth).

---

## 12. Main constants (`config.h`)

| Symbol | Typical value | Meaning |
|--------|---------------|---------|
| `WIFI_PORTAL_NAME` | `ESP32CAM-Setup` | Config AP SSID |
| `WIFI_HOSTNAME` / `WIFI_MDNS_NAME` | `esp32cam` | Hostname and mDNS |
| `CAM_DEFAULT_FRAMESIZE` | `8` (VGA) | Resolution at boot |
| `CAM_DEFAULT_QUALITY` | `12` | JPEG (10–63) |
| `CAM_INIT_RETRIES` | `5` | Camera init attempts |
| `BOOT_SETTLE_MS` | `500` | Post-reset / serial pause |
| `WEB_SERVER_PORT` | `80` | HTTP UI + stream |
| `STREAM_SERVER_PORT` | `81` | Optional stream mirror |
| `NET_PREFS_NS` | `netcfg` | NVS for IP |
| `MQTT_PREFS_NS` | `mqttcfg` | NVS for MQTT |
| `MQTT_DEFAULT_TOPIC_IN` | `esp32cam/cmd` | Command topic |
| `MQTT_DEFAULT_TOPIC_OUT` | `esp32cam/image` | Image topic |
| `MQTT_BUFFER_SIZE` | `48*1024` | MQTT packet buffer |
| `LED_GPIO` | `4` | AI-Thinker white flash |

---

## 13. Project timeline (summary)

| Stage | Outcome |
|-------|---------|
| Goal definition | Local browser stream |
| IP and CAM-MB | DHCP/static + anti-brownout |
| Modular implementation | boot, camera, network, webserver |
| Build | OK `esp32:esp32:esp32cam` |
| Portal / serial / power tests | Documented fixes |
| MQTT | Commands + JPEG + LED; config on web page |
| Stream port 81 unreachable | Stream moved to `/stream` on port 80 |
| Documentation | Unified PT + EN README |

---

## 14. License and usage

Personal/study project. Use only on networks you are authorized to use. No authentication: **do not expose port 80 to the internet** without extra security layers.

---

*Document produced from the planning, implementation, and testing of the ESP32-CAM Stream + MQTT project.*
