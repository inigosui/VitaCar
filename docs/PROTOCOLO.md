# Protocolo Vita ↔ móvil (v1)

TCP. El **móvil escucha** en el puerto `47474` y la **Vita se conecta**.

## Descubrimiento

UDP, también en el puerto `47474`. Todo es texto ASCII:

- **Pregunta de la Vita:** mientras busca, la Vita envía `VITACAR?` cada 0,5 s a la dirección
  de broadcast de su subred y a `255.255.255.255`.
  La Vita pregunta desde el puerto `47474`; si no puede usarlo, desde uno libre.
- **Respuesta del móvil:** el móvil contesta `VITACAR1` directamente a la IP y al puerto de la
  Vita. Durante la búsqueda mantiene un `MulticastLock` para que la WiFi no descarte los broadcasts.
- **Aviso del móvil:** además, mientras no hay ninguna Vita conectada, el móvil envía `VITACAR1`
  cada segundo a la dirección de broadcast de cada interfaz activa (WiFi y punto de acceso) y a
  `255.255.255.255`.

En los dos casos, la Vita toma como IP del móvil el remitente del `VITACAR1`. Así funciona en
cualquier WiFi que no aísle a sus clientes.

La Vita elige la IP en este orden:

1. La escrita en `ux0:data/VitaCar/phone_ip.txt`, si existe. En ese caso no busca.
2. El remitente del último `VITACAR1` recibido (busca durante 2,5 s).
3. Su puerta de enlace (el caso del punto de acceso del móvil, si la red bloquea los broadcasts).

Mientras busca, la app Teléfono de la Vita muestra su IP, las preguntas enviadas, las
respuestas recibidas, la fuente que está probando y el último error de red. La app del móvil
muestra cuántas veces le ha llegado la pregunta de la Vita.

## Tramas

```
[u32 longitud, big endian][u8 tipo][datos]      longitud = 1 + tamaño de datos
```

| Tipo | Datos |
|---|---|
| 1 | JSON en UTF-8 con un campo `"t"` que indica el mensaje |
| 2 | Portada: `u32 art_id` + JPEG (300×300) |
| 3 | Tesela de mapa: `u8 z`, `u32 x`, `u32 y` + PNG de 256×256 |
| 4 | Sonido (móvil → Vita): PCM de 16 bits little endian, estéreo, 48 kHz, en trozos de 20 ms |

Máximo 4 MB por trama. La Vita envía `ping` cada 3 s; el móvil responde `pong`. Si
alguno de los dos no recibe nada en 12–15 s, corta y la Vita vuelve a conectar.

## Móvil → Vita

| `t` | Campos |
|---|---|
| `hello` | `v`, `name` (nombre del móvil). La Vita vacía su lista de notificaciones. |
| `battery` | `pct`, `charging` |
| `media` | `active`, `app`, `title`, `artist`, `dur` (ms), `pos` (ms), `playing`, `art_id` (0 = sin portada) |
| `notif` | `id`, `app`, `title`, `text`, `time` ("HH:mm"), `can_reply`, `silent` (no mostrar aviso emergente) |
| `notif_rm` | `id` |
| `call` | `state` (`ringing` / `active` / `idle`), `name`, `number` |
| `gps` | `lat`, `lon`, `speed` (m/s), `bearing` (grados) |
| `nav` | `active`, `title`, `text`, `sub` (de la notificación de Google Maps, Waze…) |
| `route` | `active`, `dest` (nombre), `dist` (m), `dur` (s), `arrive` ("HH:mm"), `pts` (`[lat0, lon0, lat1, lon1, …]`, 5 decimales, simplificada). Con `active: false` se borra; `arrived: true` si es porque se ha llegado. |
| `route_left` | `dist` (m), `dur` (s), `arrive`, `idx` (primer punto de `pts` que queda por delante) |
| `audio` | `active`, `rate` (48000), `ch` (2). Con `active: true` llegan tramas de tipo 4. |
| `weather` | `temp`, `max`, `min`, `code` (WMO), `is_day`, `place` |
| `tile_err` | `z`, `x`, `y` (no se pudo obtener la tesela) |

Al recibir `hello` de la Vita, el móvil responde con su `hello` y el estado completo
(batería, música con portada, notificaciones con `silent`, GPS, navegación, ruta, tiempo, llamada,
sonido).

La ruta la calcula el móvil con OSRM. Se envía entera (`route`) al calcularla o recalcularla, y
después, con cada posición, solo lo que queda (`route_left`). La Vita dibuja desde la flecha hasta
el destino, a partir del punto `idx`.

El sonido (experimental) son unos 1,5 Mbit/s. Si la WiFi se atasca, el móvil descarta trozos en
vez de acumular retraso (como mucho 200 ms pendientes). La Vita espera a tener 150 ms antes de
sonar y descarta lo acumulado por encima de 450 ms.

## Vita → móvil

| `t` | Campos |
|---|---|
| `hello` | `v`, `device` |
| `media_cmd` | `action`: `play_pause`, `next`, `prev` |
| `reply` | `id`, `text` (respuesta rápida a una notificación) |
| `notif_dismiss` | `id` |
| `call_cmd` | `action`: `answer`, `hangup` |
| `tile` | `z`, `x`, `y` |
