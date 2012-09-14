#!/bin/sh
# Fake libtool script to install libraries properly to an Android device
#echo "$0 $*"
progdir=.libs

while : ; do
	case $1 in
		"--mode=install")
			shift
			instscript=$1
			shift 
			while test "$1" = -c; do shift; done
			bin=$1
			shift
			dest=$1
			break
			;;
		--mode=*)
			echo "$0: $1 is not supported" >&2
			exit 1
			;;
		*)
			echo "$0: not sure what to do with '$1'" >&2
			exit 2
			;;
	esac
	! shift && break
done
libtool_install_magic="%%%MAGIC variable%%%"
source ${PWD}/${bin}

if test -n "${dlname}"; then
	#  Library
	${instscript} ${progdir}/${dlname} ${libdir}
	for name in ${library_names}; do
		${ADB} shell ln -s ${libdir}/${dlname}  ${libdir}/${name}
	done
else
	# Program
	${instscript} ${progdir}/${bin} ${dest}
fi
