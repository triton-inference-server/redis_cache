#!/bin/bash

# lightly modified from redis' `gen-test-certs.sh` https://github.com/redis/redis/blob/93708c7f6a0e702657e4f296ea6fc299225eea8d/utils/gen-test-certs.sh
generate_cert() {
    local name=$1
    local cn="$2"
    local opts="$3"

    local keyfile=certs/${name}.key
    local certfile=certs/${name}.crt

    [ -f $keyfile ] || openssl genrsa -out $keyfile 2048
    openssl req \
        -new -sha256 \
        -subj "/O=Redis Test/CN=$cn" \
        -key $keyfile | \
        openssl x509 \
            -req -sha256 \
            -CA certs/ca.crt \
            -CAkey certs/ca.key \
            -CAserial certs/ca.txt \
            -CAcreateserial \
            -days 365 \
            $opts \
            -out $certfile
}

mkdir -p certs
[ -f certs/ca.key ] || openssl genrsa -out certs/ca.key 4096
openssl req \
    -x509 -new -nodes -sha256 \
    -key certs/ca.key \
    -days 3650 \
    -subj '/O=Redis Test/CN=Certificate Authority' \
    -out certs/ca.crt

cat > certs/openssl.cnf <<_END_
[ server_cert ]
keyUsage = digitalSignature, keyEncipherment
nsCertType = server

[ client_cert ]
keyUsage = digitalSignature, keyEncipherment
nsCertType = client
_END_

generate_cert server "Server-only" "-extfile certs/openssl.cnf -extensions server_cert"
generate_cert client "Client-only" "-extfile certs/openssl.cnf -extensions client_cert"
generate_cert redis "Generic-cert"

[ -f certs/redis.dh ] || openssl dhparam -out certs/redis.dh 2048