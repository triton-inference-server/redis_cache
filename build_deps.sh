#!/bin/bash

if [[ ! -d "./third-party" ]]; then
    mkdir ./third-party
fi

if [[ ! -d "./build/install" ]]; then
    mkdir -p ./build/install
fi

cd ./third-party

CMAKE=$(which cmake)

# Install Hiredis
if ls ../build/install/lib/libhiredis.a 1>/dev/null 2>&1; then
    echo "Hiredis has already been installed"
else
    if [[ ! -d "./hiredis" ]]; then
	git clone https://github.com/redis/hiredis.git hiredis --branch v1.0.2 --depth=1
	echo "Hiredis downloaded"
    fi
    cd hiredis

    LIBRARY_PATH=lib CC=gcc CXX=g++ make PREFIX="$(pwd)/../../build/install" static -j 4
    LIBRARY_PATH=lib CC=gcc CXX=g++ make PREFIX="$(pwd)/../../build/install" install
    cd ../
    # delete shared libraries
    rm ../build/install/lib/*.so
    rm ../build/install/lib/*.dylib
    echo "Finished installing Hiredis"
fi


#Install Redis-plus-plus
if ls ../build/install/lib/libredis++.a 1>/dev/null 2>&1; then
    echo "Redis-plus-plus has already been installed"
else
    if [[ ! -d "./redis-plus-plus" ]]; then
        git clone https://github.com/sewenew/redis-plus-plus.git redis-plus-plus --branch 1.3.2 --depth=1
	    echo "Redis-plus-plus downloaded"
    fi
    cd redis-plus-plus
    mkdir compile
    cd compile

    $CMAKE -DCMAKE_BUILD_TYPE=Release -DREDIS_PLUS_PLUS_BUILD_TEST=OFF -DREDIS_PLUS_PLUS_BUILD_SHARED=OFF -DCMAKE_PREFIX_PATH="$(pwd)../../../build/install/lib/" -DCMAKE_INSTALL_PREFIX="$(pwd)/../../../build/install" -DCMAKE_CXX_STANDARD=17 ..
    CC=gcc CXX=g++ make -j 4
    CC=gcc CXX=g++ make install
    cd ../../
    echo "Finished installing Redis-plus-plus"
fi

cd ../
