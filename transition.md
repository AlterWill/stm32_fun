
STM32 Nucleo-F446RE & ESP32 Audio Node
1. System Architecture & Overview
This document outlines the hardware connections, pin mapping, build environment, and software responsibilities for the dual-MCU audio monitoring and streaming edge node.

Main MCU: STM32 Nucleo-F446RE (ARM Cortex-M4 @ 180 MHz) running the Arduino framework via PlatformIO.
Network Co-processor: ESP32 (handling Wi-Fi stack and server communication).
Audio Input: INMP441 6-Pin Digital I2S Microphone.
Local Display: 4-Pin I2C OLED Display (SSD1306).
2. Hardware Pinout & Wiring Specification
2.1 INMP441 Digital I2S Microphone (6-Pin)
Microphone Pin
Nucleo Pin / Header
Function / Signal
Notes
VDD
3V3 (CN7 / Power Header)
Power Supply
3.3V DC
GND
GND (CN7 / CN8)
Common Ground
Connected to system ground
L/R
GND
Channel Select
Tied to GND for Left channel output
SCK
D13 (PA5)
I2S2 Clock (BCLK)
Serial Clock
WS
D6 (PB10)
I2S2 Word Select
LRCLK (Left/Right Clock)
SD
D3 (PB5)
I2S2 Serial Data
Audio PCM data input to STM32

2.2 4-Pin I2C OLED Display (SSD1306)
Display Pin
Nucleo Pin / Header
Function / Signal
Notes
GND
GND (CN8)
Ground
Common Ground
VCC
3V3 (CN7)
Power Supply
3.3V DC
SCL
D15 (PB8)
I2C1 SCL
I2C Clock
SDA
D14 (PB9)
I2C1 SDA
I2C Data

2.3 ESP32 Wi-Fi Co-Processor (UART Link)
ESP32 Pin
Nucleo Pin / Header
Function / Signal
Notes
TX (GPIO17)
D0 (RX / PA3)
UART RX
Receives data sent from STM32
RX (GPIO16)
D1 (TX / PA2)
UART TX
Transmits data/commands to ESP32
GND
GND
Common Ground
Mandatory shared reference
5V / VIN
External USB / 5V Supply
Power Supply
Independent power source (do not power via STM32 3.3V)

3. PlatformIO Configuration (platformio.ini)
The project is configured for PlatformIO using the Arduino framework.i
[env:nucleo_f446re]
platform = ststm32
board = nucleo_f446re
framework = arduino
monitor_speed = 115200
lib_deps =
adafruit/Adafruit SSD1306 @ ^2.5.7
adafruit/Adafruit GFX Library @ ^1.11.9

## 4. Firmware Responsibilities

### 4.1 STM32 Nucleo-F446RE (Main Controller)

* **Audio Acquisition**: Interface with the INMP441 microphone using I2S peripheral configured with DMA (Direct Memory Access) to ensure non-blocking, high-speed PCM audio sampling.

* **Signal Processing**: Compute audio metrics (e.g., RMS levels, decibels, peak detection, or basic FFT frequency analysis) on sampled buffers.

* **Local UI Rendering**: Update the 4-pin I2C OLED display in real-time to show status, audio levels, or connection state.

* **Inter-MCU Communication**: Package processed audio metrics or streaming packets into structured JSON or binary frames and transmit them over UART (`Serial2` / `D0`/`D1`) to the ESP32.

### 4.2 ESP32 (Network Co-Processor)

* **Wi-Fi Management**: Handle reliable connection, auto-reconnection, and secure socket/TLS operations to the target server.

* **Data Ingestion**: Listen on the UART interface for incoming data packets from the STM32.

* **Server Uplink**: Transmit payloads upstream to the backend server via HTTP POST, WebSockets, or MQTT protocols.

