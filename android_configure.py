#!/usr/bin/python2
# This script uses some tools of the Android toolchain to guess paths,
# update the environment accordingly, and calls the configure script.
# Call it as
#  ANDROID_BUILD_TOP==/PATH/TO/ANDROID/SOURCE python2 test.py [PRODUCT] [HOSTARCH] [NDK] [PLATFORM]

import sys, os

PRODUCT="full"
NDK="7"
PLATFORM="14"
HOSTARCH="arm-linux-androideabi"

if "ANDROID_BUILD_TOP" in os.environ.keys():
    ANDROID_BUILD_TOP=os.environ["ANDROID_BUILD_TOP"]
else:
    sys.stderr.write("usage: ANDROID_BUILD_TOP=/PATH/TO/ANDROID/SOURCE %s [PRODUCT=%s] [NDK=%s] [PLATFORM=%s] [HOSTARCH=%s]\n" % (sys.argv[0], PRODUCT, NDK, PLATFORM, HOSTARCH))
    sys.exit(1)
# Add the development scripts directory of the Android source
sys.path.insert(0, os.path.join(ANDROID_BUILD_TOP, "development/scripts/"))

def add_env(var, data, sep=" ", end=False):
    if var in os.environ.keys():
        if end:
            os.environ[var] += sep + data
        else:
            os.environ[var] = data + sep + os.environ[var]
    else:
        os.environ[var] = data
    return os.environ[var]

def show_env():
    for env in ("PATH", "CPPFLAGS", "CFLAGS", "LDFLAGS", "LIBS"):
        sys.stderr.write("%s='%s'\n" %(env, os.environ[env]))


import symbol

if len(sys.argv) > 1:
    PRODUCT=sys.argv[1]
if len(sys.argv) > 2:
    NDK=sys.argv[2]
if len(sys.argv) > 3:
    PLATFORM=sys.argv[3]
if len(sys.argv) > 4:
    HOSTARCH=sys.argv[4]

toolchain_dir = os.path.dirname(symbol.ToolPath('gcc'))
tools_dir = "%s/out/host/%s/bin" % (ANDROID_BUILD_TOP, symbol.Uname())
ndk_dir = "%s/prebuilt/ndk/android-ndk-r%s/platforms/android-%s/arch-arm" % (ANDROID_BUILD_TOP, NDK, PLATFORM)
lib_dir = "%s/out/target/product/%s/obj/lib" % (ANDROID_BUILD_TOP, PRODUCT)

for ddir in (toolchain_dir, ndk_dir, lib_dir):
    if not os.path.isdir(ddir):
        sys.stderr.write("path '%s' does not exist\n" % ddir)
        sys.exit(2)

add_env("PATH", toolchain_dir, sep=":")
# Using "-isysroot ... -isystem" rather than "-nostdinc ... -I" prevents
# warnings from system headers to be displayed.
add_env("CPPFLAGS", ("-isysroot %s -isystem %s/lib/gcc/arm-linux-androideabi/4.4.3/include -isystem =/usr/include -isystem =/usr/include/linux" %
    (ndk_dir, os.path.dirname(toolchain_dir))))
add_env("CPPFLAGS", "-I%s/external/libxml2/include -I%s/external/icu4c/common" %
        (ANDROID_BUILD_TOP, ANDROID_BUILD_TOP), end=True)
# The configure script has trouble finding some library headers.
# XXX: This should be configurable as a --with-X-inc.
add_env("CPPFLAGS", "-I%s/external/libpopt" %
        (ANDROID_BUILD_TOP,), end=True)
add_env("CPPFLAGS", "-I%s/external/sqlite/dist" %
        (ANDROID_BUILD_TOP,), end=True)
# No CFLAGS, but we define the variable so show_env() doesn't fail
add_env("CFLAGS", "")
add_env("LDFLAGS", ("-L%s" % lib_dir))
add_env("LDFLAGS", "-nostdlib")
add_env("LIBS", "-lc")

# Prevent configure from concluding that rpl_malloc is needed. This is the case
# when malloc(0) returns NULL, or when cross-compiling [0]. Android uses a libc
# called bionic, which malloc implementation (dlmalloc, in bionic/dlmalloc.c)
# pads the requested size if it is too small by adding some chunks: "If n is
# zero, malloc returns a minimum-sized chunk." Using rpl_malloc is therefore
# not needed.
# [0] http://wiki.buici.com/xwiki/bin/view/Programing+C+and+C%2B%2B/Autoconf+and+RPL_MALLOC
add_env("ac_cv_func_malloc_0_nonnull", "yes")

show_env()

src_dir = os.path.dirname(sys.argv[0])
configure_script = "%s/configure" % src_dir
ret = os.system("%s --host=%s --prefix=/system/usr --exec-prefix=/system --sysconfdir=/etc --bindir=/system/xbin --localstatedir=/system/var --disable-doc --without-python --without-ruby --with-adb=%s/adb" %
        (configure_script, HOSTARCH, tools_dir))

if ret != 0:
    sys.exit(ret)

sys.stderr.write("\nYou need to extend your $PATH before running make:\nPATH=%s:%s:$PATH\n" % (toolchain_dir, tools_dir))

# This is a trick: spawn a new shell so the environment has propagated.
#os.execl(os.environ["SHELL"], os.environ["SHELL"])

# /home/omehani/src/cm4mm/prebuilt/linux-x86/toolchain/arm-eabi-4.4.0/bin/
