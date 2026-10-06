<p align="center"><img src="assets/logo.png" width="160" alt="Logo de VitaCar"></p>

<h1 align="center">VitaCar</h1>

<p align="center">Convierte tu PS Vita en la pantalla del coche, al estilo CarPlay, conectada a tu móvil Android.</p>

<p align="center">
  <img src="docs/capturas/inicio.png" width="45%" alt="Pantalla de inicio">
  <img src="docs/capturas/mapas.png" width="45%" alt="Mapa con navegación">
  <img src="docs/capturas/musica.png" width="45%" alt="Música">
  <img src="docs/capturas/mensajes.png" width="45%" alt="Mensajes">
</p>

VitaCar es una app homebrew para PS Vita y una app compañera para Android. El móvil envía a
la Vita la música, los mensajes, las llamadas, el GPS, las indicaciones de navegación y el
tiempo, y desde la Vita se controla todo con la pantalla táctil o los botones.

> **Estado: en pruebas.** Funciona en el emulador y con un móvil simulado, pero aún no se ha
> probado a fondo en consolas y móviles reales. Si lo pruebas, cuéntanos qué tal
> (ver [Feedback](#feedback)).

## Descarga

En [Releases](../../releases) están los instalables:

```
VitaCar.vpk   -> para la PS Vita (instalar con VitaShell; requiere HENkaku/Ensō)
VitaCar.apk   -> para el móvil Android (8.0 o superior)
```

## Puesta en marcha

1. **Móvil:** instala `VitaCar.apk` (permite «instalar apps desconocidas»). Ábrela y concede
   todos los permisos de la lista. En Realme/Oppo/Xiaomi, sigue también la tarjeta de batería.
   - Si al activar «Acceso a notificaciones» Android dice *Ajuste restringido*:
     Ajustes › Aplicaciones › VitaCar › ⋮ › **Permitir ajustes restringidos**.
2. **Punto de acceso del móvil** en la banda de **2,4 GHz** (la Vita no ve la de 5 GHz)
   y sin apagado automático.
3. **Vita:** Ajustes › Red › Configuración de Wi-Fi › conéctate a ese punto de acceso.
4. En la app del móvil pulsa **Iniciar**. Abre VitaCar en la Vita: el punto «Móvil» de la
   barra lateral se pone verde.

El audio va del móvil al coche como siempre (Bluetooth o AUX); la Vita no reproduce sonido.

## Qué hace cada app

| App | Funciona con |
|---|---|
| Música | Cualquier reproductor del móvil (Spotify, YouTube Music…): título, portada, progreso, anterior / pausa / siguiente |
| Mapas | GPS del móvil, mapa de OpenStreetMap en modo noche, velocidad, zoom; indicaciones de Google Maps / Waze si están navegando en el móvil |
| Teléfono | Estado de la conexión; llamada entrante a pantalla completa (contestar / rechazar); colgar |
| Mensajes | Notificaciones del móvil, aviso emergente, respuestas rápidas (WhatsApp, Telegram…), borrar |
| Tiempo | Open-Meteo según tu ubicación |
| Ajustes | Batería, móvil conectado, red |

Las teselas del mapa que llegan del móvil se guardan en `ux0:data/VitaCar/tiles`, así que
las zonas ya vistas funcionan después sin conexión.

## Controles de la Vita

| Botón | Acción |
|---|---|
| Pantalla táctil | Todo; deslizar para recorrer los mensajes |
| Cruceta | Mover el foco · en Mapas: zoom |
| X | Abrir / pulsar · contestar llamada |
| O | Volver |
| L / R | Pista anterior / siguiente · en Mapas: zoom |

## Compilar

### Vita

VitaSDK en `~/vitasdk` y CMake en `~/.local/bin`:

```sh
source env.sh
mkdir -p build && cd build && cmake .. && make      # build/VitaCar.vpk
```

Con [vitacompanion](https://github.com/devnoname120/vitacompanion) en la consola:
`cmake -DVITA_IP=192.168.x.x .. && make send` sube el ejecutable y relanza la app.

### Android

```sh
cd companion-android
JAVA_HOME=~/Android/jdk-17.0.20.1+1 ./gradlew assembleRelease
# app/build/outputs/apk/release/app-release.apk
```

Firmado con la clave de depuración (uso personal, no Play Store).

## Estructura

```
src/main.c           Bucle principal y entrada
src/screens.c        Barra lateral, inicio, avisos emergentes, llamada entrante
src/app_*.c          Música, Mapas, Teléfono, Mensajes, Tiempo y Ajustes
src/phone.c          Conexión con el móvil (hilo de red, protocolo, estado)
src/net.c            Sockets: sceNet en la Vita, POSIX en el PC
src/tiles.c          Teselas: memoria, tarjeta y móvil; modo noche
src/ui.c, icons.c    Dibujo, texto con caché y recorte, iconos vectoriales
src/third_party/     cJSON (MIT)
companion-android/   App Android (Kotlin, sin dependencias externas)
docs/PROTOCOLO.md    Protocolo entre Vita y móvil
tools/gen_logo.py    Logo de la app
tools/gen_sce_sys.py Icono y LiveArea a partir del logo
```

## Feedback

Este proyecto se hace por y para la comunidad. Toda ayuda cuenta:

- **¿Algo falla?** Abre un [issue de fallo](../../issues/new/choose) indicando modelo de Vita,
  móvil y versión de Android.
- **¿Una idea?** Propón una mejora en [Issues](../../issues/new/choose).
- **¿Quieres programar?** Los *pull requests* son bienvenidos.

## Licencia

VitaCar se distribuye bajo la [PolyForm Noncommercial License 1.0.0](LICENSE): puedes usarlo,
estudiarlo, modificarlo y compartirlo libremente, **pero no con fines comerciales**. No se
puede vender ni incluir en productos de pago.

## Aviso legal

VitaCar es un proyecto independiente y no está afiliado, patrocinado ni aprobado por Sony
Interactive Entertainment, Apple Inc. ni Google LLC. «PlayStation», «PS Vita», «CarPlay» y
«Android» son marcas de sus respectivos propietarios y se mencionan solo para describir el
proyecto. Usa la app con responsabilidad: no la manejes mientras conduces.

## Componentes y datos de terceros

Mantienen sus propias licencias:

- Mapa: © colaboradores de OpenStreetMap (ODbL). Teselas de `tile.openstreetmap.org`, cuyo
  uso está pensado para volumen bajo; para uso intensivo, configura otro servidor en la app.
- Tiempo: [Open-Meteo](https://open-meteo.com) (CC BY 4.0).
- Fuente Noto Sans: SIL Open Font License 1.1 ([assets/fonts/OFL.txt](assets/fonts/OFL.txt)).
- cJSON: MIT ([src/third_party/cjson/LICENSE](src/third_party/cjson/LICENSE)).
