#/bin/sh

h="$(dirname $0)"
f=false
bd=build
eval set -- $(getopt -o b:f -l buildir:,force -- "$@") || exit
while :; do
	case "$1" in
	-b|--buildir) bd="$2"; shift 2;;
	-f|--force) force=true; shift;;
	--) shift; break;;
	esac
done

mkdir -p "$h/$bd" || exit
cd "$h/$bd" || exit
$force && rm -r * 2>/dev/null || rm CMakeCache.txt 2>/dev/null

cmake \
	-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX:=$HOME/.local} \
	-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:=Debug} \
	..

make -j "$@"

