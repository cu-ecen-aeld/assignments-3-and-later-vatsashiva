#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v6.18
  #v5.1.10 
#KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

sudo apt-get update
sudo apt-get install -y \
   git build-essential gcc-aarch64-linux-gnu bc u-boot-tools kmod cpio \
   flex bison libssl-dev libncurses-dev psmisc device-tree-compiler \
   qemu-system-arm
    
    

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION} linux-stable
fi

##############################################################################################################


##########################################################################################################################
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here  , refered docker
      
    # Patch dtc-parser to avoid yylloc multiple definition
#sed -i '1i %define api.pure full\n%define api.location.type {YYLTYPE_IS_DECLARED 1}' scripts/dtc/dtc-parser.y
#git reset --hard
#git clean -fdx


#sed -i 's/^hostprogs-y := dtc$/hostprogs-y :=/' scripts/dtc/Makefile


# Set environment variables
#export ARCH=arm64
#export CROSS_COMPILE=aarch64-linux-gnu-

# Clean previous build artifacts
make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" mrproper
#sed -i 's/^hostprogs-y := dtc$/hostprogs-y :=/' scripts/dtc/Makefile

# Build kernel
echo "Building Linux kernel (Image, modules, dtbs)..."
make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" defconfig
make -j$(nproc) ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE"  all
make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" modules
make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" dtbs

echo "Kernel build completed!" 
echo "Device Tree Compiler used: $(which dtc)" 
    
    ##################################################################################
    
    #cd linux-stable
    #sed -i 's/%locations//g' scripts/dtc/dtc-parser.y
    
  #  sed -i '1i #define YYLTYPE_IS_DECLARED 1' scripts/dtc/dtc-parser.tab.h
    #make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" scripts_clean
    #make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" mrproper
   # make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" defconfig
    #make -j4 ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" all
   # make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" modules
   # make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" dtbs
    
        
fi

echo "Adding the Image in outdir"

#cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/Image

cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/Image


echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
        rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir -p rootfs
cd rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var 
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log
#mkdir -p home/conf

#cd "$OUTDIR"
cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
#git clone git://busybox.net/busybox.git
#git clone https://git.busybox.net/busybox/
git clone https://github.com/mirror/busybox.git busybox

    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox

    #make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" distclean 

    #make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" defconfig
else
    cd busybox
fi

# TODO: Make and install busybox
#make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" distclean
make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" defconfig
make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" -j$(nproc)
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" install 
 
cd ${OUTDIR}/rootfs
echo "Library dependencies"

${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs

sudo apt update 
sudo apt install -y gcc-aarch64-linux-gnu libc6-dev-arm64-cross libc6-arm64-cross

SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

echo "sysroot detected: $SYSROOT"

if [ ! -d "${SYSROOT}" ]; then
        echo "error: sysroot not found , make sure gcc-arm64 is installed"
        exit 1
fi


#rootfs=${OUTDIR}/rootfs

#mkdir -p ${rootfs}/lib64
#cp -av ${Sysroot}/lib/ld-linux-aarch64.so.1 ${rootfs}/lib64/
#cp -av ${Sysroot}/lib/libc.so* ${rootfs}/lib64/
#cp -av ${Sysroot}/lib/libm.so* ${rootfs}/lib64/
#cp -av ${Sysroot}/lib/libresolv.so* ${rootfs}/lib64/

# Detect actual ARM64 library folder

if [ -d "${SYSROOT}/lib/aarch64-linux-gnu" ]; then
    LIBDIR="${SYSROOT}lib/aarch64-linux-gnu"
elif [ -d "/usr/aarch64-linux-gnu/lib" ]; then
    LIBDIR="/usr/aarch64-linux-gnu/lib"
else
    echo "Error: ARM64 libraries not found!"
    exit 1
fi

LIBDIR="/usr/aarch64-linux-gnu/lib"

echo "Using library directory:$LIBDIR"

ROOTFS=${OUTDIR}/rootfs
mkdir -p ${ROOTFS}/lib

# Copy required libraries to rootfs
cp -av ${LIBDIR}/ld-linux-aarch64.so.1 ${ROOTFS}/lib/
cp -av ${LIBDIR}/libc.so* ${ROOTFS}/lib/
cp -av ${LIBDIR}/libm.so* ${ROOTFS}/lib/
cp -av ${LIBDIR}/libresolv.so* ${ROOTFS}/lib/

echo "ARM64 runtime libraries copied successfully to ${ROOTFS}/lib"


# TODO: Make device nodes

mkdir -p ${ROOTFS}/dev

mknod -m 666 ${ROOTFS}/dev/null c 1 3
mknod -m 666 ${ROOTFS}/dev/console c 5 1


# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean 
make CROSS_COMPILE="$CROSS_COMPILE" #cc=${CROSS_COMPILE}gcc

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs

rootFS=${OUTDIR}/rootfs
cp -rfv writer finder.sh finder-test.sh autorun-qemu.sh ${rootFS}/home/
cp -rfv conf/ ${rootFS}/home/

chmod +x ${rootFS}/home/finder-test.sh
chmod +x ${rootFS}/home/finder.sh

# TODO: Chown the root directory
chown -R root:root ${OUTDIR}/rootfs

# TODO: Create initramfs.cpio.gz
cd "$OUTDIR/rootfs"
find .| cpio -H newc -ov --owner root:root > "${OUTDIR}/initramfs.cpio"
gzip -f "${OUTDIR}/initramfs.cpio" 

