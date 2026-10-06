cmake_minimum_required(VERSION 3.16)

# HOST_BUILD=ON compila para el PC (necesita libsdl2-dev y libsdl2-ttf-dev)
# para iterar la interfaz sin pasar por la Vita.
option(HOST_BUILD "Compilar para el PC en lugar de para la PS Vita" OFF)

if(NOT HOST_BUILD AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Define VITASDK (source env.sh) o usa -DHOST_BUILD=ON")
  endif()
endif()

project(VitaCar C)

set(VITA_APP_NAME "VitaCar")
set(VITA_TITLEID  "VCAR00001")
set(VITA_VERSION  "00.02")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")

add_executable(${PROJECT_NAME}
  src/main.c
  src/ui.c
  src/icons.c
  src/screens.c
  src/app_music.c
  src/app_maps.c
  src/app_phone.c
  src/app_messages.c
  src/app_info.c
  src/sysinfo.c
  src/net.c
  src/phone.c
  src/tiles.c
  src/third_party/cjson/cJSON.c
)

if(HOST_BUILD)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL REQUIRED sdl2 SDL2_ttf SDL2_image)
  target_include_directories(${PROJECT_NAME} PRIVATE ${SDL_INCLUDE_DIRS})
  target_link_libraries(${PROJECT_NAME} ${SDL_LIBRARIES} m)
  target_compile_definitions(${PROJECT_NAME} PRIVATE ASSET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/")
  return()
endif()

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

target_compile_definitions(${PROJECT_NAME} PRIVATE ASSET_PATH="app0:/assets/")
target_link_libraries(${PROJECT_NAME}
  SDL2_image
  SDL2_ttf
  freetype
  harfbuzz
  webpdemux
  webp
  sharpyuv
  jpeg
  png
  bz2
  z
  SDL2
  SceDisplay_stub
  SceCtrl_stub
  SceAudio_stub
  SceAudioIn_stub
  SceSysmodule_stub
  SceGxm_stub
  SceCommonDialog_stub
  SceAppUtil_stub
  SceTouch_stub
  SceHid_stub
  SceMotion_stub
  ScePower_stub
  SceRtc_stub
  SceIme_stub
  SceNet_stub
  SceNetCtl_stub
  pthread
  m
)

set(VPK_RESOURCES
  sce_sys/icon0.png
  sce_sys/livearea/contents/bg.png
  sce_sys/livearea/contents/startup.png
  sce_sys/livearea/contents/template.xml
  assets/fonts/NotoSans-Regular.ttf
  assets/fonts/NotoSans-Bold.ttf
  assets/fonts/OFL.txt
)

# vita.cmake parte las rutas de FILE por espacios y esta carpeta tiene uno
# ("Navegador psvita"). Copiamos los recursos al directorio de build y los
# pasamos con rutas relativas: vita-pack-vpk se ejecuta desde ahí.
set(VITA_PACK_VPK_FLAGS "")
foreach(res ${VPK_RESOURCES})
  configure_file(${res} vpk_res/${res} COPYONLY)
  set(VITA_PACK_VPK_FLAGS "${VITA_PACK_VPK_FLAGS} -a vpk_res/${res}=${res}")
endforeach()

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)

# Desarrollo rápido con vitacompanion en la consola:
#   cmake -DVITA_IP=192.168.1.50 .. && make send
set(VITA_IP "" CACHE STRING "IP de la PS Vita con vitacompanion")
add_custom_target(send
  COMMAND curl --ftp-method nocwd -T ${PROJECT_NAME}.self ftp://${VITA_IP}:1337/ux0:/app/${VITA_TITLEID}/eboot.bin
  COMMAND echo destroy | nc -q 1 ${VITA_IP} 1338 || true
  COMMAND echo launch ${VITA_TITLEID} | nc -q 1 ${VITA_IP} 1338
  DEPENDS ${PROJECT_NAME}.self
)
