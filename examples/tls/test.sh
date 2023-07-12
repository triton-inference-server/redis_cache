#!/bin/bash
sleep 5
classificationResult=$(/workspace/install/bin/image_client -u http://triton-server:8000 -m densenet_onnx -c 3 -s INCEPTION /workspace/images/mug.jpg | grep "COFFEE MUG")

if [ -z "$classificationResult" ]; then
    echo "Classification failed"
    exit 1
fi

numKeys=$(redis-cli -h triton-redis --tls --cert /certs/redis.crt --key /certs/redis.key --cacert /certs/ca.crt DBSIZE) # check that there's only one key

if [[ $numKeys -eq 1 ]]; then
    exit 0
else
    echo "Redis did not have the expected number of keys."
    exit 1
fi