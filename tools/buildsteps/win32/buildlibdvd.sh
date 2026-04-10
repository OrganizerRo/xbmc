#!/bin/bash

[[ -f buildhelpers.sh ]] &&
    source buildhelpers.sh

LIBDVDPREFIX=/xbmc/lib/libdvd
PKG_CONFIG_PATH=$LIBDVDPREFIX/lib/pkgconfig:$LIBDVDPREFIX/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}
export PKG_CONFIG_PATH

# Ensure output directories exist before any install step writes to them.
mkdir -p "$LIBDVDPREFIX/bin" "$LIBDVDPREFIX/lib/pkgconfig" "$LIBDVDPREFIX/include"

do_load_autoconf() {
  do_loaddeps $1
  do_clean_get $MAKEFLAGS
  do_print_status "$LIBNAME-$VERSION (${BITS})" "$blue_color" "Configuring"
  do_autoreconf
}

#libdvdcss
do_load_autoconf /xbmc/tools/depends/target/libdvdcss/DVDCSS-VERSION
./configure \
      --prefix=$LIBDVDPREFIX \
      CC="gcc -static-libgcc" \
      CFLAGS="-DNDEBUG" \
      --disable-doc \
      --enable-shared \
      --disable-static \
      --build="$MINGW_CHOST" || exit 1
do_makelib $MAKEFLAGS || exit 1

strip -S $LIBDVDPREFIX/bin/libdvdcss-2.dll &&
cp "$LIBDVDPREFIX/bin/libdvdcss-2.dll" /xbmc/system/

#libdvdread
do_load_autoconf /xbmc/tools/depends/target/libdvdread/DVDREAD-VERSION
./configure \
    --prefix=$LIBDVDPREFIX \
   --disable-shared \
   --enable-static \
   --with-libdvdcss \
   CC="gcc -static-libgcc" \
   CSS_CFLAGS="-I$LIBDVDPREFIX/include" \
   CSS_LIBS="-L$LIBDVDPREFIX/lib -ldvdcss" \
   CFLAGS="-DHAVE_DVDCSS_DVDCSS_H -D_XBMC -DNDEBUG -I$LIBDVDPREFIX/include" \
   --build="$MINGW_CHOST" || exit 1
do_makelib $MAKEFLAGS || exit 1

#libdvdnav
do_load_autoconf /xbmc/tools/depends/target/libdvdnav/DVDNAV-VERSION
./configure \
   --prefix=$LIBDVDPREFIX \
   --disable-shared \
   --enable-static \
   CC="gcc -static-libgcc" \
   DVDREAD_CFLAGS="-I$LIBDVDPREFIX/include" \
   DVDREAD_LIBS="-L$LIBDVDPREFIX/lib -ldvdread" \
   CFLAGS="-D_XBMC -DNDEBUG -I$LIBDVDPREFIX/include" \
   --build="$MINGW_CHOST" || exit 1
do_makelib $MAKEFLAGS || exit 1

cd $LOCALBUILDDIR
gcc \
   -shared \
   -o $LIBDVDPREFIX/bin/libdvdnav.dll \
   -Wl,--out-implib,$LIBDVDPREFIX/lib/libdvdnav.dll.a \
   -ldl \
   libdvdread/src/*.o libdvdnav/src/*.o libdvdnav/src/vm/*.o $LIBDVDPREFIX/lib/libdvdcss.dll.a \
   -Wl,--enable-auto-image-base \
   -Xlinker --enable-auto-import \
   -static-libgcc

strip -S $LIBDVDPREFIX/bin/libdvdnav.dll &&
cp $LIBDVDPREFIX/bin/libdvdnav.dll /xbmc/system/
do_print_status "libdvd (${BITS})" "$green_color" "Done"

#remove the bgprocessfile for signaling the process end
if [ -f $BGPROCESSFILE ]; then
  rm $BGPROCESSFILE
fi