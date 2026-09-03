# Third Party source code tips.

## nvtop

```
wget https://github.com/Syllo/nvtop/archive/refs/tags/3.3.2.tar.gz -P tarball
tar -xzvf tarball/3.3.2.tar.gz
cmake -B build  -DCMAKE_INSTALL_PREFIX=`pwd`/../..
cmake --build build -j 4
cmake --install build
```

### nvtop, after install, user memory usage view

```
sudo setcap cap_perfmon=ep ../../bin/nvtop
```

## libuv

```
wget https://dist.libuv.org/dist/v1.52.0/libuv-v1.52.0.tar.gz -P tarball
tar -xzvf tarball/libuv-v1.52.0.tar.gz

cmake -B build  -DCMAKE_INSTALL_PREFIX=`pwd`/../..
cmake --build build -j 4
cmake --install build
```

## uSockets

```
## uSockets
wget https://github.com/uNetworking/uSockets/archive/refs/tags/v0.8.8.tar.gz -P tarball
tar -xzvf tarball/v0.8.8.tar.gz
WITH_LIBUV=1 WITH_OPENSSL=1 WITH_LTO=0 make CFLAGS=-I../../include
cp uSockets.a ../../lib/libuSockets.a
cp src/libusockets.h ../../include/
```

