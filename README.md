# 🎧 OpenA2DP

<div align="center">

### La alternativa libre y de alto rendimiento para audio Bluetooth Hi-Res en Windows

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen?style=flat-square)](#)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg?style=flat-square)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078D6?style=flat-square&logo=windows)](#)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-orange.svg?style=flat-square&logo=c%2B%2B)](#)
[![Hi-Res Audio](https://img.shields.io/badge/Audio-LDAC%2096kHz%20%7C%20990kbps-gold?style=flat-square)](#)

**OpenA2DP** es un proyecto open-source diseñado para transformar la experiencia de audio inalámbrico en Windows, trayendo capacidades de transmisión en ultra alta fidelidad (**Sony LDAC™**) y control granular del stack Bluetooth sin depender de software propietario de pago.

[Características](#-características-clave) •
[Comparativa](#-por-qué-opena2dp) •
[Inicio Rápido](#-inicio-rápido) •
[Hoja de Ruta](#-hoja-de-ruta) •
[Arquitectura](#-arquitectura-técnica)

---

</div>

## 🚀 ¿Por qué OpenA2DP?

Por defecto, Windows limita el audio Bluetooth a códecs estándar con compresión agresiva. Durante años, la única forma de obtener calidad de audio profesional (como **LDAC a 990 kbps / 96 kHz**) en una PC con Windows fue pagar licencias de utilidades comerciales propietarias como *Alternative A2DP Driver*.

**OpenA2DP** nace para cambiar esto: una solución **100% libre, transparente y comunitaria**, construida directamente sobre las tecnologías nativas de audio y kernel de Windows para entregar la máxima calidad de sonido a tus auriculares y altavoces.

---

## ⚡ Características Clave

* 💎 **Sonido Hi-Res Inalámbrico:** Soporte integral para **LDAC** en sus perfiles de máxima calidad (*HQ: 990 kbps*, *SQ: 660 kbps*, *MQ: 330 kbps*) a **44.1, 48, 88.2 y 96 kHz**.
* 🆓 **Completamente Open-Source:** Licencia libre MIT. Sin suscripciones, sin pagos por licencia y sin versiones de prueba que expiran a los 7 días.
* 🎙️ **Captura WASAPI de Ultrabaja Latencia:** Motor de captura *Loopback* en modo compartido y basado en eventos, procesando audio PCM float32 en tiempo real con buffers acotados de mínimo overhead.
* 🧩 **Núcleo AVDTP Portable en C++20:** Pipeline modular para gestión de señalización AVDTP, fragmentación inteligente y paquetización RTP/SBC y LDAC lista para streaming continuo.
* 🛡️ **Enfoque Nativo y Seguro:** Diseñado para coexistir con el stack oficial de Windows (`BthPort`) mediante drivers de perfil KMDF estándar, preservando el uso simultáneo de teclados, ratones y otros periféricos Bluetooth.

---

## 📊 Comparativa de Soluciones

| Característica | Windows Nativo | Alternative A2DP Driver | OpenA2DP |
| :--- | :---: | :---: | :---: |
| **Modelo de Licencia** | Propietario (Microsoft) | De Pago (Shareware 7 días) | **Open Source (MIT)** |
| **Soporte Sony LDAC™** | ❌ No | ✅ Sí (Comercial) | **✅ Sí (HQ 96 kHz / 990 kbps)** |
| **SBC / Configuración Avanzada** | ⚠️ Básico y cerrado | ✅ Sí | **✅ Núcleo AVDTP / SBC configurable** |
| **Privacidad y Auditoría** | Código cerrado | Código cerrado | **100% Auditable en GitHub** |
| **Costo** | Incluido en Windows | ~$10 - $15 USD | **Gratis para siempre** |

---

## 🛠️ Inicio Rápido

### Requisitos del Sistema
* **Sistema Operativo:** Windows 10 (1703+) o Windows 11 (recomendado 22H2 / 24H2).
* **Entorno de Compilación:** Visual Studio 2022 (con el workload *Desarrollo para el escritorio con C++*).
* **Herramientas:** [CMake](https://cmake.org/) (3.25+) y Git.

### Compilación en 1 Minuto

Abre una consola **Developer PowerShell for VS 2022** y ejecuta:

```powershell
# Clonar el repositorio
git clone https://github.com/arthur-arturo/OpenA2DP.git
cd OpenA2DP

# Configurar y compilar en modo Release
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# Ejecutar pruebas automatizadas
ctest --test-dir build -C Release --output-on-failure
```

---

## 🎮 Uso de la Herramienta

La suite incluye una utilidad de consola de alto rendimiento para validar la captura y codificación en tiempo real:

#### 1. Listar dispositivos de audio disponibles
```powershell
.\build\Release\opena2dp.exe --list-devices
```

#### 2. Transmisión de prueba en LDAC HQ (48 kHz)
Captura el audio de tu sistema y lo procesa al instante:
```powershell
.\build\Release\opena2dp.exe --duration 30 --rate 48000 --quality hq
```

#### 3. Máxima Fidelidad Audiófila (96 kHz / 990 kbps)
```powershell
.\build\Release\opena2dp.exe --duration 30 --rate 96000 --quality hq --output capturas_ldac.oa2dpcap
```

---

## 🗺️ Hoja de Ruta (Roadmap)

- [x] **Fase 1: Motor de Captura y Codificación**  
  Integración con WASAPI Loopback event-driven y codificador `libldac` de alto desempeño.
- [x] **Fase 2: Protocolo de Transporte AVDTP / RTP**  
  Diseño del núcleo C++20 para mensajes AVDTP, fragmentación de paquetes y paquetizador de medios.
- [🔄] **Fase 3: Driver de Perfil KMDF (En desarrollo activo)**  
  Implementación del driver de perfil en modo kernel para negociar canales AVDTP directos mediante `BthPort`.
- [ ] **Fase 4: Endpoint Virtual de Audio & Panel de Control**  
  Instalación como dispositivo de reproducción predeterminado en Windows y aplicación de bandeja moderna para selección de códecs con un solo clic.

---

## 🏛️ Arquitectura Técnica

OpenA2DP separa meticulosamente las capas de aplicación, codificación y transporte para garantizar estabilidad y rendimiento de baja latencia sin caídas de frames:

```
┌────────────────────────────────────────────────────────┐
│             Windows Audio Session (WASAPI)             │
│            Loopback Capture (PCM Float32)              │
└───────────────────────────┬────────────────────────────┘
                            │ (Queue Concurrente)
┌───────────────────────────▼────────────────────────────┐
│           Motor de Codificación Hi-Res LDAC            │
│       44.1 / 48 / 88.2 / 96 kHz @ hasta 990 kbps       │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│              AVDTP Core & RTP Packetizer               │
│         Fragmentación y señalización Bluetooth         │
└───────────────────────────┬────────────────────────────┘
                            │ (IOCTL / BRB)
┌───────────────────────────▼────────────────────────────┐
│           KMDF Profile Driver (`BthPort.sys`)          │
│          Canales de Transporte L2CAP Bluetooth         │
└────────────────────────────────────────────────────────┘
```

Para una explicación exhaustiva de los módulos internos, consulta [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

---

## 🤝 Cómo Colaborar

Las contribuciones son más que bienvenidas. Si tienes experiencia en desarrollo de drivers de Windows (KMDF/WDK), ingeniería de protocolos Bluetooth o diseño de interfaces modernas:

1. Haz un fork del repositorio.
2. Crea una rama para tu función (`git checkout -b feature/nueva-mejora`).
3. Envía tus cambios mediante un Pull Request.

---

## 📄 Licencia y Marcas

* El código de **OpenA2DP** está distribuido bajo la licencia [MIT](LICENSE).
* `libldac` está licenciado bajo Apache-2.0 (AOSP). LDAC™ es una marca registrada de Sony Corporation.

<div align="center">
<sub>Desarrollado con dedicación para la comunidad de audio de alta fidelidad y código abierto.</sub>
</div>
