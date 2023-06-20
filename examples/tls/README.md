# TLS Testing Environment

This environment demonstrates how to run the Triton Redis Cache against a TLS enabled Redis instance.

## How to Run

* clone this repo: `git clone https://github.com/triton-inference-server/redis_cache`
* follow build instructions enumerated [in the README](https://github.com/triton-inference-server/redis_cache#build-the-cache)
* clone the Triton server repo parallel to the redis_cache repo: `git clone https://github.com/triton-inference-server`
* Add the following to: `docs/examples/model_repository/densenet_onnx/config.pbtxt`
```
response_cache{
  enable:true
}
```
* cd into `redis_cache/docker/tls`
* Install [NVIDIA's container toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html)
* Create an account on [NGC](https://ngc.nvidia.com/)
* Log docker into to NVIDIA's container repository:
```
docker login nvcr.io

Username: $oauthtoken
Password: <MY API KEY>
```
> NOTE: Username: $oauthtoken in this context means that your username is literally $oauthtoken - your API key serves as the unique part of your credentials
* run `sh run-tls.sh`
* In a separate terminal run `docker run -it --rm --net=host nvcr.io/nvidia/tritonserver:23.03-py3-sdk`
* Run `/workspace/install/bin/image_client -m densenet_onnx -c 3 -s INCEPTION /workspace/images/mug.jpg`
    * on the first run - this will miss the cache
    * subsequent runs will pull the inference out of the cache
    * you can validate this by watching Redis with `docker exec -it redis_cache_triton-redis_1 redis-cli monitor`
* You can use the Redis CLI to talk to redis by running `redis-cli --tls --cert ./certs/redis.crt --key ./certs/redis.key --cacert ./certs/ca.crt`