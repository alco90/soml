#!/bin/sh

port=4004
exp=blobgen_exp
long=$1
dir=
if [ "x$long" = "x--long" ]; then
	dir=long
else
	dir=short
fi

mkdir -p $dir
rm -f ${dir}/*.hex # Remove leftover blob data from last run

[ -f ${dir}/${exp}.sq3 ] && rm -f ${dir}/${exp}.sq3
[ -f ${dir}/blobgen-server.log ] && rm -f ${dir}/blobgen-server.log
${top_builddir}/server/oml2-server -l $port --logfile=${dir}/blobgen-server.log --data-dir=${dir} &
server_pid=$!
echo SERVER=${server_pid}

cd $dir

sleep 1

blobgen=../blobgen

if [ ! -x ${blobgen} ]; then
	echo "Could not find test blob generator \'${blobgen}\'"
	exit 1
fi

if [ ! -z "${TIMEOUT}" ]; then
	TIMEOUT="${TIMEOUT} 30s"
fi
${TIMEOUT} $blobgen -h -n 100 $long --oml-id a --oml-exp-id ${exp} --oml-server localhost:$port --oml-bufsize 110000 || exit 1

cd ..

echo "Blob generating client finished OK"
sleep 1
kill $server_pid
echo "Analyzing blobs"

printf "\n...done\n"

# Grab blobs from sqlite3
${srcdir}/fromsq3.sh ${dir}/${exp}

# Calculate the diffs, produce result
echo Making diffs...
${srcdir}/diff.sh ${dir}
status=$?
echo Finished diffs

exit $status
