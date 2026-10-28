#!/bin/zsh
# Desktop preview: renders every screen with the real gfx/ui/sessions code
# into a contact sheet (out/sheet.png). Needs SDL2 headers (brew install sdl2)
# and a prior `pio run` so LovyanGFX/ArduinoJson are in .pio/libdeps.
set -e
HERE=${0:A:h}
FW=${HERE:h}
LIBS=$(/bin/ls -d $FW/.pio/libdeps/*(/) | head -1)
LG=$LIBS/LovyanGFX/src
AJ=$LIBS/ArduinoJson/src
SDL_INC=$(brew --prefix 2>/dev/null || echo /usr/local)/include
SDL_LIB=${SDL_INC:h}/lib
OUT=$HERE/out
mkdir -p $OUT/obj
if [[ ! -f $OUT/liblgfx.a ]]; then
  echo "building LovyanGFX for desktop (one-off)..."
  for f in $(find $LG/lgfx -name '*.cpp'); do
    c++ -std=c++17 -O2 -w -DLGFX_SDL -I$LG -I$SDL_INC -c $f -o $OUT/obj/lg_$(echo $f | md5 | cut -c1-10).o
  done
  for f in $(find $LG/lgfx -name '*.c'); do
    cc -O2 -w -I$LG -I$SDL_INC -c $f -o $OUT/obj/lg_$(echo $f | md5 | cut -c1-10).o
  done
  ar rcs $OUT/liblgfx.a $OUT/obj/lg_*.o
fi
c++ -std=c++17 -O2 -w -DORB_HOST_PREVIEW -DLGFX_SDL -I$HERE/shim -I$FW/include -I$LG -I$AJ -I$SDL_INC \
  $HERE/preview.cpp $FW/src/gfx.cpp $FW/src/ui_common.cpp $FW/src/ui_session.cpp $FW/src/ui_agents.cpp \
  $FW/src/ui_limits.cpp $FW/src/ui_attention.cpp $FW/src/ui_idle.cpp $FW/src/sessions.cpp \
  $OUT/liblgfx.a -L$SDL_LIB -lSDL2 -o $OUT/preview
(cd $OUT && ./preview && sips -s format png sheet.ppm --out sheet.png >/dev/null && rm -f sheet.ppm)
echo "wrote $OUT/sheet.png"
