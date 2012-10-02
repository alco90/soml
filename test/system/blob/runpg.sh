#!/bin/sh

PGPATH=`dirname ${POSTGRES}`

port=4004
pgport=5433
exp=blobgen_exp
db=${exp}
long=$1
dir=
if [ "x$long" = "x--long" ]; then
	dir=longpg
else
	dir=shortpg
fi

rm -rf $dir ${dir}_db
${PGPATH}/initdb -U oml2 ${dir}_db
sed -i "s/^#bytea_output *=.*/bytea_output = 'hex'/g" ${dir}_db/postgresql.conf
${POSTGRES} -D ${PWD}/${dir}_db -p $pgport &
pg_pid=$!
echo PG=${pg_pid}
sleep 1
${PGPATH}/createdb -U oml2 -p $pgport ${db}

${top_builddir}/server/oml2-server -d 4 -l $port --logfile=${dir}/blobgen-server.log --backend=postgresql --pg-user=oml2 '--pg-connect=host=localhost port=5433' &
server_pid=$!
echo SERVER=${server_pid}

mkdir -p $dir
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
echo "Blob generating client finished OK"
cd ..

sleep 1
kill $server_pid

echo "Analyzing blobs"

# Grab blobs
PSQL_OPTS="-p $pgport $db oml2 -P tuples_only=on"
seqs=$(${PGPATH}/psql ${PSQL_OPTS} -c 'SELECT oml_seq FROM blobgen_blobmp') 

printf "Extracting server-stored blobs from $db\n"
for i in $seqs; do
	printf "\r-->$i  "
	echo -n `${PGPATH}/psql ${PSQL_OPTS} -c "SELECT blob FROM blobgen_blobmp WHERE oml_seq=$i" | sed "/^$/d;y/abcdef/ABCDEF/;s/ *\\\\\x//"` > ${dir}/s$i.hex
done

printf "\n...done\n"

# Calculate the diffs, produce result
echo Making diffs...
${srcdir}/diff.sh ${dir}
status=$?
echo Finished diffs

sleep 1
kill $pg_pid

exit $status
