# OpenA2DP

Proyecto FOSS para investigar una alternativa transparente a Alternative A2DP
Driver en Windows 10/11.

## Estado real

El repositorio comienza con una **PoC de consola para WASAPI Loopback → libldac**.
Enumera endpoints de reproducción, captura PCM en modo compartido y event-driven,
codifica LDAC en un worker separado y guarda los paquetes en un contenedor de
diagnóstico propio.

**Esta PoC todavía no transmite audio por Bluetooth, no instala un endpoint
virtual y no reemplaza Alternative A2DP Driver.** La captura/codificación valida
la ruta de audio y el encoder; no resuelve la interfaz de transporte A2DP de
Windows.

La limitación no se puede resolver añadiendo Winsock o WinRT: las APIs públicas
de Winsock documentan RFCOMM, mientras que los BRB L2CAP necesarios para AVDTP
son interfaces de drivers de perfil en modo kernel. Un producto que comparta el
adaptador con HID necesita un driver de perfil/audio integrado con `BthPort`;
Windows requiere que el paquete de producción esté firmado. El camino de
publicación y firma, por tanto, es un requisito del proyecto y no una tarea que
pueda sustituirse por un hook o por un servicio user-mode.

## Funciones de la PoC

- Lista endpoints de reproducción activos y sus IDs WASAPI.
- Captura el endpoint multimedia predeterminado o uno indicado por ID.
- WASAPI compartido, loopback, event-driven y conversión a float32 estéreo.
- Selecciona LDAC HQ/SQ/MQ y 44,1/48/88,2/96 kHz.
- Usa una cola PCM acotada entre captura y codificación.
- Escribe registros de paquetes LDAC en un archivo `.oa2dpcap`.
- No accede al adaptador Bluetooth, no instala drivers y no requiere privilegios.

## Requisitos

- Windows 10 1703 o posterior; recomendado Windows 10 22H2 o Windows 11.
- Visual Studio 2022 con Desktop development with C++.
- CMake 3.25+ y Git disponibles en `PATH`.
- Conexión a Internet la primera vez para descargar el wrapper CMake de
  `libldac` y su submódulo AOSP, ambos fijados en el build.
- El build de MSVC añade shims privados para los dos encabezados/extensiones
  POSIX/GCC no utilizados por el encoder.

## Compilar

Desde Developer PowerShell for VS 2022:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Uso

Listar endpoints WASAPI:

```powershell
.\build\Release\opena2dp.exe --list-devices
```

Capturar 30 segundos del endpoint multimedia predeterminado, codificando LDAC HQ
a 48 kHz:

```powershell
.\build\Release\opena2dp.exe --duration 30 --rate 48000 --quality hq
```

LDAC HQ a 96 kHz (990 kbps nominales):

```powershell
.\build\Release\opena2dp.exe --duration 30 --rate 96000 --quality hq `
  --device-id "<ID mostrado por --list-devices>" --output test.oa2dpcap
```

La salida no es WAV, FLAC, RTP ni un archivo de reproducción estándar. Es un
contenedor interno con configuración y registros `[longitud, número de frames,
payload LDAC]`, para validar y depurar el encoder y desarrollar posteriormente
la paquetización AVDTP.

## Limitaciones conocidas

- El núcleo maneja señalización AVDTP aislada y paquetiza SBC en RTP, pero no
  mantiene una sesión negociada ni conecta con L2CAP/Bluetooth.
- No se implementa un driver de audio virtual ni un servicio permanente.
- No se inyectan códecs en `btha2dp.sys`.
- La salida de Loopback corresponde al endpoint que Windows está renderizando;
  no se cambia por sí sola la ruta de audio del usuario.
- No se promete que el PCM fuente tenga 24 bits efectivos; el motor de audio
  entrega float32 y puede hacer conversión desde el formato de origen.
- No se redistribuye un binario precompilado de libldac.

## Licencias y distribución

El código propio se publica bajo MIT. `libldac` se descarga al configurar el
proyecto y conserva su licencia Apache-2.0 y avisos. La licencia del código
fuente no concede automáticamente derechos sobre marcas, certificación o
patentes del producto. El `NOTICE` de libldac declara que se requiere completar
la certificación de Sony para usar LDAC en productos. Esta PoC no afirma estar
certificada; antes de distribuir un producto LDAC deben confirmarse con Sony los
requisitos vigentes. CMake copia las licencias y el `NOTICE` al directorio del
ejecutable.

aptX, AAC y otros códecs no están incluidos en esta PoC. Sus dependencias,
patentes y condiciones de redistribución requieren decisiones por separado.

## Núcleo AVDTP en desarrollo

El proyecto incluye ahora un núcleo portable para mensajes AVDTP single y
fragmentados, descubrimiento de SEID, listas de capacidades, generación de comandos
`GetCapabilities`/`SetConfiguration`/`GetConfiguration`/`Open`/`Start`/
`Suspend`/`Close`/`Abort`, validación SBC y construcción de paquetes de medios
RTP/SBC. También correlaciona las 16 etiquetas de transacción y modela las
transiciones básicas del SEP Source. Tiene pruebas unitarias, pero aún faltan
timeout/retry de sesión, negociación conectada a un dispositivo y el endpoint de
audio integrado.

En `driver/opena2dp` también hay un driver KMDF en desarrollo que abre y cierra
el canal AVDTP usando BRB de `BthPort`. No usa WinUSB ni cambia el driver del
adaptador. Actualmente sólo establece el canal de señalización; todavía no crea
un endpoint de audio ni transmite contenido. Su INF es experimental y no debe
instalarse en un equipo de producción.

Para ejecutar únicamente las pruebas del protocolo en una máquina con CMake y
un compilador C++20:

```sh
cmake -S . -B build-protocol -DOPENA2DP_BUILD_WASAPI_POC=OFF
cmake --build build-protocol
ctest --test-dir build-protocol --output-on-failure
```

## Siguiente hito para llegar al producto

El siguiente hito no es otro socket user-mode: es un vertical slice WDK de un
driver A2DP/audio por dispositivo, empezando por SBC y usando las interfaces
documentadas de `BthPort`. Para distribución normal también se necesita una
organización editora y el proceso de firma/certificación de drivers de Microsoft.
El diseño y los límites están en [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).
