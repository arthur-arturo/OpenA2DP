# Arquitectura y ruta hacia A2DP integrado

## Resumen

La arquitectura final debe reemplazar únicamente el driver A2DP del auricular
seleccionado y continuar utilizando la pila Bluetooth de Windows. El servicio
de configuración y la UI no deben reclamar el adaptador ni participar en la
ruta crítica de audio.

La primera entrega de este repositorio valida solo una parte: WASAPI Loopback y
codificación LDAC en user-mode. No es el driver final.

El núcleo `opena2dp_protocol` implementa actualmente la codificación/lectura de
headers AVDTP single-packet, comandos Source básicos, parsing de Discover y
Service Capabilities, y parsing/validación de la información SBC. Rechaza
explícitamente fragmentos AVDTP; todavía no posee una conexión L2CAP ni controla
un auricular.

## Estado de las interfaces Windows

### Bluetooth

- Winsock Bluetooth usa `AF_BTH`, `SOCK_STREAM` y `BTHPROTO_RFCOMM`.
- WinRT publica RFCOMM para Bluetooth Classic y GATT para Bluetooth LE.
- Los drivers de perfil acceden a L2CAP/SDP mediante los BRB de `BthPort`,
  incluyendo `BRB_L2CA_OPEN_CHANNEL` y `BRB_L2CA_ACL_TRANSFER`.
- No hay una API pública de plugin para registrar un encoder de software en
  `btha2dp.sys`.

Fuentes:

- [Bluetooth and socket](https://learn.microsoft.com/windows/win32/bluetooth/bluetooth-and-socket)
- [Bluetooth driver stack](https://learn.microsoft.com/windows-hardware/drivers/bluetooth/bluetooth-driver-stack)
- [Using the Bluetooth driver stack](https://learn.microsoft.com/windows-hardware/drivers/bluetooth/using-the-bluetooth-driver-stack)
- [Bluetooth profile drivers](https://learn.microsoft.com/windows-hardware/drivers/bluetooth/bluetooth-profile-drivers-overview)

El transporte A2DP requiere señalización AVDTP por L2CAP y un canal de
transporte de medios. A2DP no equivale a enviar bytes LDAC por un socket RFCOMM.

### Audio

WASAPI Loopback captura la mezcla del endpoint de **renderizado** indicado. Es
compartido y no añade un nuevo endpoint a Windows. Un endpoint seleccionable por
aplicaciones requiere un driver de audio, típicamente SYSVAD/PortCls/WaveRT o
ACX/KMDF. ACX no es UMDF.

Fuentes:

- [Loopback recording](https://learn.microsoft.com/windows/win32/coreaudio/loopback-recording)
- [SYSVAD sample](https://learn.microsoft.com/windows-hardware/drivers/audio/sample-audio-drivers)
- [ACX overview](https://learn.microsoft.com/windows-hardware/drivers/audio/acx-audio-class-extensions-overview)

## Topología de producción

```text
Aplicaciones Windows
        │
        ▼
Windows Audio Engine / WASAPI
        │ PCM por endpoint de auricular
        ▼
Driver A2DP/audio FOSS asociado al dispositivo
        │ buffer WaveRT + encoder + paquetizador RTP
        ▼
AVDTP / L2CAP mediante BRB de BthPort
        │
        ├──────── HidBth → ratón/teclado
        ├──────── RFCOMM → otros perfiles
        └──────── otros dispositivos Bluetooth
        ▼
Driver del adaptador + radio Bluetooth compartida
        ▼
Auricular A2DP Sink
```

La configuración seguiría otra ruta, fuera del audio:

```text
UI WinUI ── named pipe con ACL ── servicio/helper ── IOCTL versionado ── driver
```

## Planos y responsabilidades

### Driver de datos

1. Asociarse por PnP al servicio A2DP del dispositivo seleccionado.
2. Exponer un endpoint que el motor de audio de Windows pueda usar.
3. Negociar capacidades AVDTP y el códec admitido por ambos extremos.
4. Codificar PCM en un worker seguro para el nivel de ejecución elegido.
5. Crear paquetes de medios, mantener secuencias/timestamps y enviarlos por
   L2CAP con los BRB documentados.
6. Implementar cancelación, retirada PnP, suspensión y recuperación.

El driver no debe ejecutar codificación pesada desde callbacks en IRQL elevado,
asignar memoria por bloque de audio ni confiar en datos de AVDTP sin validar.

### Servicio y panel

- Almacenar preferencias por instancia de dispositivo.
- Seleccionar códec, calidad y búfer dentro de las capacidades negociadas.
- Leer telemetría y reportar el códec activo.
- No abrir canales L2CAP ni recibir PCM en la arquitectura final.
- La UI debe ejecutarse bajo el usuario normal. El helper privilegiado debe ser
  pequeño, tener ACL explícitas y aceptar solo mensajes versionados.

## Estrategia de códecs

1. **SBC**: primero, para validar la negociación y transporte.
2. **LDAC**: integrar el encoder AOSP/libldac en una ABI estable; 128 muestras
   por canal por llamada. Respetar calidad, frecuencia y MTU negociados.
3. **AAC**: iniciar con Media Foundation AAC-LC; evaluar FDK solo si las tasas
   y el control adicional justifican sus obligaciones.
4. **aptX/HD/LL**: no anunciar disponibilidad por la sola presencia de una
   biblioteca de encoder. Verificar capacidades A2DP, latencia, interoperabilidad,
   licencias de patentes y marcas.
5. **SBC XQ**: ofrecer como configuración SBC avanzada, no como códec separado.

La radio puede degradar LDAC HQ aunque la máquina tenga CPU suficiente. El
control de calidad debe reaccionar a la cola, errores y condiciones de radio;
debe poder regresar a un perfil estable sin desconectar HID.

El `NOTICE` del submódulo libldac indica que se requiere certificación de Sony
para usar LDAC en productos. El encoder abierto no debe interpretarse como una
autorización de marca/certificación para una distribución comercial o pública;
resolverlo antes de declarar el códec como función de producto.

## Firma, seguridad y actualizaciones

- Los drivers kernel de producción necesitan un paquete firmado por Microsoft.
- La cuenta de Hardware Dev Center requiere EV asociado para los envíos
  aplicables; puede ser la cuenta de una fundación o de un partner editor.
- `TESTSIGNING` es para desarrollo controlado, no para los usuarios finales.
- No se parchean drivers del sistema, no se reutilizan catálogos ajenos y no se
  distribuyen claves de firma.
- Usar interfaces documentadas, INF por dispositivo, rollback al driver inbox,
  pruebas HLK/Driver Verifier y publicación controlada de actualizaciones.

Fuentes:

- [Driver code signing requirements](https://learn.microsoft.com/windows-hardware/drivers/dashboard/code-signing-reqs)
- [Driver signing options](https://learn.microsoft.com/windows-hardware/drivers/dashboard/driver-signing-offerings)
- [Loading test-signed code](https://learn.microsoft.com/windows-hardware/drivers/install/the-testsigning-boot-configuration-option)

## Hitos de implementación

### Hito 1 — completado parcialmente en este repo

- WASAPI Loopback event-driven.
- Selección/listado de endpoint.
- Cola PCM acotada y worker de codificación.
- LDAC HQ/SQ/MQ a las frecuencias soportadas.
- Archivo de captura para pruebas.
- Núcleo portable AVDTP single-packet y negociación/validación de capacidades SBC.
- Pruebas unitarias del parser y de los builders de comandos.

### Hito 2 — driver mínimo de audio y transporte

- Acordar el editor/firma.
- Crear proyecto WDK y verificar binding por dispositivo en una máquina de
  laboratorio con Secure Boot según el proceso de desarrollo aplicable.
- Completar fragmentación/validación AVDTP y conectarlo a un driver de perfil
  usando BRB de `BthPort`.
- Exponer audio y transmitir SBC básico.
- Probar HID simultáneamente, suspensión/reanudación y rollback.

### Hito 3 — producto

- Agregar LDAC y las otras opciones con capacidades negociadas.
- Añadir servicio/UI bajo demanda y telemetría local.
- Verificar HLK, HVCI, Secure Boot, matriz de adaptadores/auriculares y versiones
  de Windows.
- Publicar únicamente paquetes firmados y con actualización/rollback.

## Criterio de aceptación de la PoC actual

- `--list-devices` enumera endpoints activos.
- La captura produce un archivo OA2DPCAP con encabezado y registros LDAC.
- No instala ni cambia drivers ni conecta/desconecta el adaptador Bluetooth.
- Un desbordamiento o fallo de WASAPI se informa como error; no se oculta
  descartando silenciosamente audio.
