#!/bin/bash

[[ -f buildhelpers.sh ]] &&
    source buildhelpers.sh

# Always remove the bgprocess sentinel on exit so runBackgroundProcess
# doesn't loop forever if we exit early due to a build failure.
trap 'rm -f "$BGPROCESSFILE"' EXIT

[[ -z "$LIBDVDPREFIX" ]] && { echo "ERROR: LIBDVDPREFIX is not set"; exit 1; }

MAKEFLAGS="${MAKEFLAGS:--j$(( $(nproc) / 2 + 1 ))}"

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
ac_cv_path_GIT= ./configure \
      --prefix=$LIBDVDPREFIX \
      CC="gcc -static-libgcc" \
      CFLAGS="-DNDEBUG" \
      --disable-doc \
      --enable-shared \
      --disable-static \
      --build="$MINGW_CHOST" || exit 1
# Sources are a tarball, not a git repo.  The Makefile ChangeLog rule runs
# 'git log > ChangeLog-tmp' (exits 128, ignored) then
# 'test -s ChangeLog-tmp && mv ChangeLog-tmp ChangeLog' (exits 1, fatal).
# Replace that second command with a plain 'touch ChangeLog' so make succeeds.
sed -i 's/test -s ChangeLog-tmp && mv ChangeLog-tmp ChangeLog/touch ChangeLog/' \
    Makefile 2>/dev/null || true
touch ChangeLog
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

for objdir in "libdvdread/src" "libdvdnav/src" "libdvdnav/src/vm"; do
  objs=( "$LOCALBUILDDIR/$objdir"/*.o )
  if [[ ! -e "${objs[0]}" ]]; then
    echo "ERROR: No .o files found in $objdir — aborting link"
    exit 1
  fi
done

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