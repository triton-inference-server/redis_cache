# TLS Testing Environment

This environment demonstrates how to run the Triton Redis Cache against a TLS enabled Redis instance.

## How to Run

* clone this repo: `git clone https://github.com/triton-inference-server/redis_cache`
* follow build instructions enumerated [in the README](https://github.com/triton-inference-server/redis_cache#build-the-cache)
* cd into `redis_cache/docker/tls`
* run `sh run-tls.sh`
* In a separate terminal run `docker run -it --rm --net=host nvcr.io/nvidia/tritonserver:23.06-py3-sdk`
* Run `/workspace/install/bin/image_client -m densenet_onnx -c 3 -s INCEPTION /workspace/images/mug.jpg`
    * on the first run - this will miss the cache
    * subsequent runs will pull the inference out of the cache
    * you can validate this by checking what's in Redis with `docker exec -it tls_triton-redis_1 redis-cli --tls --cert /certs/redis.crt --key /certs/redis.key --cacert /certs/ca.crt SCAN 0`
* You can use the Redis CLI to talk to redis by running `docker exec -it tls_triton-redis_1 redis-cli --tls --cert /certs/redis.crt --key /certs/redis.key --cacert /certs/ca.crt`