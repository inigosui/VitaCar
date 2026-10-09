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
la Vita la música, los mensajes, las llamadas, el GPS, las indicaciones de navegación, el
tiempo y tu agenda, y desde la Vita se controla todo con la pantalla táctil o los botones.

> **Estado: en pruebas.** Ya se ha probado en una PS Vita y un móvil Android reales: la conexión
> automática, la música, los controles y las indicaciones de Google Maps funcionan. Aún hay limitaciones conocidas
> (ver [Problemas conocidos](#problemas-conocidos)). Si lo pruebas, cuéntanos qué tal
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
   - **«Acceso a notificaciones» es imprescindible**: sin él no se ven la canción que suena
     (título, artista, portada), los mensajes ni las indicaciones de Google Maps. Los botones
     de música sí funcionan sin él, porque usan las teclas multimedia de Android.
   - Como la app no viene de Play Store, Android bloquea ese permiso y muestra *Ajuste
     restringido*. Para desbloquearlo: Ajustes › Aplicaciones › VitaCar › ⋮ › **Permitir
     ajustes restringidos** (pide el PIN o la huella). Después vuelve a la app y actívalo.
2. **Una WiFi de 2,4 GHz** compartida por el móvil y la Vita (la Vita no ve la de 5 GHz). Sirve:
   - **La WiFi de casa** o **el punto de acceso de otro móvil** (en iPhone, activa
     «Maximizar compatibilidad»).
   - **El punto de acceso del propio móvil Android** (sin apagado automático). Funciona aunque
     el móvil no tenga datos.
   - No sirven las redes que aíslan a sus clientes (invitados, hoteles, cafeterías…).
3. **Vita:** Ajustes › Red › Configuración de Wi-Fi › conéctate a esa red.
4. En la app del móvil pulsa **Iniciar**. Abre VitaCar en la Vita: encuentra el móvil sola y
   el punto «Móvil» de la barra lateral se pone verde.
5. **Solo si la Vita se queda en «Buscando»:** abre la app Teléfono de la Vita, que muestra
   debajo de los pasos cuántas preguntas ha enviado y cuántas respuestas ha recibido; la app del
   móvil dice cuántas veces la Vita lo ha buscado. Si la red no deja pasar esas preguntas, puedes
   indicarle a la Vita la IP del móvil a mano: en la app del móvil, al pulsar «Iniciar», aparece
   «Direcciones de este móvil». Copia la que lleva `(wlan0)`, por ejemplo `192.168.1.50`. Crea
   un archivo de texto `phone_ip.txt` que contenga solo esa IP y cópialo en la Vita (con
   VitaShell) a `ux0:data/VitaCar/phone_ip.txt`. Si el router le da otra IP al móvil más
   adelante, actualiza el archivo; para volver a la búsqueda automática, bórralo.

El sonido sale del móvil, que se conecta al coche como siempre (Bluetooth o AUX). La Vita no
reproduce sonido (ver [Problemas conocidos](#problemas-conocidos)).

### Ruta en el mapa

VitaCar calcula su propia ruta y la dibuja en el mapa de la Vita, con lo que queda y la hora de
llegada. Para elegir el destino:

- En Google Maps, abre un sitio y pulsa **Compartir › VitaCar**.
- O, en la app del móvil, escríbelo en **Ruta** y pulsa Buscar.

Elige **Coche** o **A pie**. Si te desvías, la ruta se recalcula sola. Al llegar desaparece, y
también puedes quitarla con «Quitar ruta». La ruta se calcula con OSRM (datos de OpenStreetMap)
y necesita datos móviles.

### Sonido por la Vita (experimental)

En la app del móvil, **Sonido por la Vita › Activar** (Android 10 o superior). El sonido de las
apps sale entonces por la Vita: su altavoz, unos auriculares o un transmisor Bluetooth. Límites
de Android:

- Spotify y otras apps no dejan capturar su sonido.
- No se capturan las llamadas ni la voz de Google Maps.
- Android pide permiso para «emitir pantalla» cada vez que pulsas Iniciar. Solo se usa el
  sonido, no la imagen.
- Gasta más batería. El móvil sigue sonando: baja su volumen a 0.

### Agenda

En la app del móvil, **Agenda › Abrir agenda**. Toca un día y pulsa **Añadir nota** (o toca dos
veces el día): título, detalles, hora (o todo el día) y color. Toca una nota para editarla o
borrarla.

En la Vita, la app **Agenda** muestra el calendario del mes con cada día que tiene notas pintado
del color de su primera nota (y un punto por nota si hay varias). A la derecha, un reloj grande,
el tiempo previsto para el día elegido (hasta unos 15 días) y sus notas. La Vita guarda la última
copia en `ux0:data/VitaCar/agenda.json`, así que la agenda se ve también sin el móvil.

## Qué hace cada app

| App | Funciona con |
|---|---|
| Música | Cualquier reproductor del móvil (Spotify, YouTube Music…): título, portada, progreso, anterior / pausa / siguiente |
| Mapas | GPS del móvil, mapa de OpenStreetMap en modo noche, velocidad, zoom; ruta propia dibujada con distancia, tiempo y hora de llegada; indicaciones (texto) de Google Maps / Waze cuando hay una ruta activa en el móvil |
| Teléfono | Estado de la conexión; llamada entrante a pantalla completa (contestar / rechazar); colgar |
| Mensajes | Notificaciones del móvil, aviso emergente, respuestas rápidas (WhatsApp, Telegram…), borrar |
| Agenda | Calendario del mes con las notas del móvil por colores, reloj, pronóstico y notas del día elegido |
| Tiempo | Open-Meteo según tu ubicación |
| Ajustes | Batería, móvil conectado, por dónde sale el sonido, red |

Las teselas del mapa que llegan del móvil se guardan en `ux0:data/VitaCar/tiles`, así que
las zonas ya vistas funcionan después sin conexión.

## Problemas conocidos

| Problema | Causa y solución |
|---|---|
| La Vita se queda en «Buscando» | Comprueba que los dos estén en la misma WiFi de 2,4 GHz y que no sea una red que aísla a sus clientes. Si aun así no conecta, usa `phone_ip.txt` (paso 5 de la puesta en marcha). |
| No se ve la canción ni los mensajes, aunque los botones de música funcionan | Falta «Acceso a notificaciones» (paso 1 de la puesta en marcha). |
| No aparecen las indicaciones de Google Maps | Hace falta el mismo permiso y una **ruta iniciada**: con Maps solo abierto no hay indicaciones. |
| El mapa muestra las indicaciones de Google Maps, pero no su ruta | Google Maps no comparte la ruta con otras apps, solo el texto de la indicación. Comparte el destino con VitaCar (ver [Ruta en el mapa](#ruta-en-el-mapa)) para que dibuje la suya. Si navegas con los dos a la vez, pueden ir por caminos distintos. |
| El sonido sale por el móvil, no por la Vita | Es lo normal. Hay una opción experimental para enviarlo a la Vita (ver [Sonido por la Vita](#sonido-por-la-vita-experimental)), pero Android no deja capturar el sonido de algunas apps (como Spotify), de las llamadas ni de las indicaciones por voz. |
| Al cambiar el zoom, el mapa se ve gris un momento | Cada tesela se descarga en el móvil, viaja por WiFi y la Vita la descomprime y oscurece para el modo noche. Las zonas ya vistas se quedan y cargan al momento. |
| El mapa no se puede mover con el dedo | Por ahora el mapa sigue tu posición y solo tiene zoom. Está previsto poder moverlo. |
| La hora de llegada no coincide con la de Google Maps | VitaCar calcula su propia ruta con OSRM, que no tiene en cuenta el tráfico. |
| La app es solo para Android | iOS no permite a otras apps leer las notificaciones ni controlar la música, así que no hay versión para iPhone. Un iPhone sí puede servir para compartir la WiFi (ver la puesta en marcha). |

## Próximamente

- Mover el mapa con el dedo.
- Mapa más fluido.

## Controles de la Vita

| Botón | Acción |
|---|---|
| Pantalla táctil | Todo; deslizar para recorrer los mensajes |
| Cruceta | Mover el foco · en Mapas: zoom · en Agenda: cambiar de día |
| X | Abrir / pulsar · contestar llamada · en Agenda: volver a hoy |
| O | Volver |
| L / R | Pista anterior / siguiente · en Mapas: zoom · en Agenda: mes anterior / siguiente |

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
src/app_*.c          Música, Mapas, Teléfono, Mensajes, Agenda, Tiempo y Ajustes
src/agenda.c         Notas de la agenda (llegan del móvil y se guardan en la tarjeta)
src/phone.c          Conexión con el móvil (hilo de red, protocolo, estado)
src/audio.c          Sonido del móvil por la Vita (experimental)
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
- Rutas: [OSRM](https://project-osrm.org) en `routing.openstreetmap.de` (FOSSGIS) y
  `router.project-osrm.org`. Búsqueda de sitios: el buscador de Android o
  [Nominatim](https://nominatim.openstreetmap.org).
- Tiempo: [Open-Meteo](https://open-meteo.com) (CC BY 4.0).
- Fuente Noto Sans: SIL Open Font License 1.1 ([assets/fonts/OFL.txt](assets/fonts/OFL.txt)).
- cJSON: MIT ([src/third_party/cjson/LICENSE](src/third_party/cjson/LICENSE)).
