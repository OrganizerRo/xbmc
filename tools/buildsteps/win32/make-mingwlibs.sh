
Win32BuildSetup=/xbmc/project/Win32BuildSetup
ERRORFILE=$Win32BuildSetup/errormingw
NOPFILE=$Win32BuildSetup/noprompt
MAKECLEANFILE=$Win32BuildSetup/makeclean
BGPROCESSFILE=$Win32BuildSetup/bgprocess
TOUCH=/bin/touch
RM=/bin/rm
if [[ -n "$CI" || -n "$GITHUB_ACTIONS" ]]; then
  NOPROMPT=1
else
  NOPROMPT=0
fi
MAKECLEAN=""
MAKEFLAGS=""
TOOLS="mingw"

export _WIN32_WINNT=0x0600
export NTDDI_VERSION=0x06000000

while true; do
  case $1 in
    --tools=* ) TOOLS="${1#*=}"; shift ;;
    --build32=* ) build32="${1#*=}"; shift ;;
    --build64=* ) build64="${1#*=}"; shift ;;
    --prompt=* ) PROMPTLEVEL="${1#*=}"; shift ;;
    --mode=* ) BUILDMODE="${1#*=}"; shift ;;
    -- ) shift; break ;;
    -* ) shift ;;
    * ) break ;;
  esac
done

throwerror() {
  $TOUCH $ERRORFILE
  echo "failed to compile $1"
  if [[ "$NOPROMPT" == "0" ]]; then
	read
  fi
}

setfilepath() {
  FILEPATH=$1
}

checkfiles() {
  for i in "$@"; do
    if [ ! -f "$FILEPATH/$i" ]; then
      throwerror "$FILEPATH/$i"
      exit 1
    fi
  done
}

#start the process backgrounded
runBackgroundProcess() {
  $TOUCH $BGPROCESSFILE
  echo "backgrounding: bash $1 $BGPROCESSFILE $TOOLS & (workdir: $PWD)"
  bash $1 $BGPROCESSFILE $targetBuild $TOOLS &
  local bgPID=$!
  echo "waiting on bgprocess (PID $bgPID)..."
  while [ -f $BGPROCESSFILE ]; do
    echo -n "."
    sleep 5
  done
  # Reap the background process and return its exit code so callers can
  # detect failures immediately instead of continuing past a broken build.
  wait $bgPID
  return $?
}


buildProcess() {
cd /xbmc/tools/buildsteps/win32

# compile our mingw dlls
echo "-------------------------------------------------------------------------------"
echo "compiling mingw libs $BITS"
echo
echo " NOPROMPT  = $NOPROMPT"
echo " MAKECLEAN = $MAKECLEAN"
echo " WORKSPACE = $WORKSPACE"
echo " TOOLCHAIN = $TOOLS"
echo
echo "-------------------------------------------------------------------------------"

echo -ne "\033]0;building FFmpeg $BITS\007"
echo "-------------------------------------------------"
echo " building FFmpeg $BITS"
echo "-------------------------------------------------"
runBackgroundProcess "./buildffmpeg.sh $MAKECLEAN" || exit 1
# Check that all 7 expected FFmpeg DLLs are present (version-agnostic names)
setfilepath /xbmc/system
for pattern in avcodec avformat avutil postproc swscale avfilter swresample; do
  if ! ls "$FILEPATH/${pattern}-"*.dll 1>/dev/null 2>&1; then
    throwerror "$FILEPATH/${pattern}-*.dll"
    exit 1
  fi
done
echo "-------------------------------------------------"
echo " building of FFmpeg $BITS done..."
echo "-------------------------------------------------"

echo -ne "\033]0;building libdvd $BITS\007"
echo "-------------------------------------------------"
echo " building libdvd $BITS"
echo "-------------------------------------------------"
runBackgroundProcess "./buildlibdvd.sh $MAKECLEAN" || exit 1
setfilepath /xbmc/system
for pattern in libdvdcss libdvdnav; do
  if ! ls "$FILEPATH/${pattern}"*.dll 1>/dev/null 2>&1 && \
     ! ls "$FILEPATH/${pattern}-"*.dll 1>/dev/null 2>&1; then
    throwerror "$FILEPATH/${pattern}*.dll"
    exit 1
  fi
done
echo "-------------------------------------------------"
echo " building of libdvd $BITS done..."
echo "-------------------------------------------------"

echo "-------------------------------------------------------------------------------"
echo
echo "compile mingw libs $BITS done..."
echo
echo "-------------------------------------------------------------------------------"

}

run_builds() {
    new_updates="no"
    new_updates_packages=""
    if [[ $build32 = "yes" ]]; then
        if [[ ! -f /local32/etc/profile.local ]]; then
            echo "ERROR: /local32/etc/profile.local not found. Run prepare-env first."
            exit 1
        fi
        source /local32/etc/profile.local
        buildProcess
        echo "-------------------------------------------------------------------------------"
        echo "compile all libs 32bit done..."
        echo "-------------------------------------------------------------------------------"
    fi

    if [[ $build64 = "yes" ]]; then
        if [[ ! -f /local64/etc/profile.local ]]; then
            echo "ERROR: /local64/etc/profile.local not found. Run prepare-env first."
            exit 1
        fi
        source /local64/etc/profile.local
        buildProcess
        echo "-------------------------------------------------------------------------------"
        echo "compile all libs 64bit done..."
        echo "-------------------------------------------------------------------------------"
    fi
}

# cleanup
if [ -f $ERRORFILE ]; then
  $RM $ERRORFILE
fi

# check for noprompt
if [ "$PROMPTLEVEL" == "noprompt" ]; then
  NOPROMPT=1
fi

if [ "$BUILDMODE" == "clean" ]; then
  MAKECLEAN="clean"
else
  MAKECLEAN="noclean"
fi

if [ "${NUMBER_OF_PROCESSORS:-0}" -gt 1 ]; then
  MAKEFLAGS="-j$(( ${NUMBER_OF_PROCESSORS:-2} + ${NUMBER_OF_PROCESSORS:-2} / 2 ))"
fi

run_builds

echo -e "\033]0;compiling done...\007"
echo

# wait for key press
if [[ "$NOPROMPT" == "0" ]]; then
  echo "press a key to close the window"
  read
fi
