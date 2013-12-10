#!/bin/sh
#
# This script tests that an OML client does not block when a collection point is down.
#
# Can be run manually as
#  srcdir=. top_builddir=../.. TIMEOUT=`which timeout` ./non-block.sh

BN=`basename $0`
LOG=$PWD/${BN%%sh}log
. ${srcdir}/tap_helper.sh

exp=${BN%%.}

firstif=`ifconfig  | sed -n 1s/:.*//p`

tap_message "testing non-blocking socket connections"

test_plan

tap_test "find timeout(1) utility" yes test -n ${TIMEOUT}

# Under Linux, net.ipv4.tcp_syn_retries = 5 [0]; this is the number of times
# the kernel will retry connecting to an unresponsive host, exponentially
# increasing the timeout delay [0] from TCP_TIMEOUT_INIT = 1s [2].
# This gives a total delay of
#
#   \sum_{i=0,...,net.ipv4.tcp_syn_retries} TCP_TIMEOUT_INIT * 2^x -1 = 62s
#
# However, most distros seem to be using net.ipv4.tcp_syn_retries = 6, which
# bumps the delay to 127s. Also, in parallel, OML backs off exponentially from
# trying to send data, and might not be in sync with the kernel, adding a
# potential 2^7s delay. We take a timeout of 260s as a safe margin before
# concluding that the test failed.
#
# [0] https://git.kernel.org/cgit/linux/kernel/git/stable/linux-stable.git/tree/include/net/tcp.h#n98
# [1] https://git.kernel.org/cgit/linux/kernel/git/stable/linux-stable.git/tree/net/ipv4/tcp_timer.c?id=v3.12.4#n122
# [2] http://lxr.free-electrons.com/source/include/net/tcp.h#L133
TIMEOUT="${TIMEOUT} -k 10s -s KILL 260s"

tap_test "run blobgen to an invalid collection URI in a finite time" no \
	${TIMEOUT} ./blobgen -n 1 --oml-id a --oml-domain ${exp} --oml-collect [fe80::%$firstif]
ret=$?
# 137 is the return we get when kill(1)ing -9
tap_test "confirm that timeout did not occur" no test ! $ret = 137

exit $fail
