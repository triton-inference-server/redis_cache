<!--
# Copyright 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
-->

[![License](https://img.shields.io/badge/License-BSD3-lightgrey.svg)](https://opensource.org/licenses/BSD-3-Clause)

# Triton Redis Cache

This repo contains an example
[cache](https://github.com/triton-inference-server/core/blob/main/include/triton/core/tritoncache.h)
for caching data with [Redis](https://redis.io/).

Ask questions or report problems in the main Triton [issues
page](https://github.com/triton-inference-server/server/issues).

## Build the Cache

If you don't have it installed already - install rapidjson-dev:

```bash
apt install rapidjson-dev
```

Use a recent cmake to build and run the following:

```
$ mkdir build
$ cd build
$ cmake -DCMAKE_INSTALL_PREFIX:PATH=`pwd`/install ..
$ make install
```

The following required Triton repositories will be pulled and used in
the build. By default the "main" branch/tag will be used for each repo
but the following CMake arguments can be used to override.

* triton-inference-server/core: `-D TRITON_CORE_REPO_TAG=[tag]`
* triton-inference-server/common: `-D TRITON_COMMON_REPO_TAG=[tag]`

## Using the Cache

### Deploying to Triton

In order for the Redis Cache to be deployed to triton, you must build the
binary (see build instructions), and copy the `libtritoncache_redis.so` file
to the folder `redis` in the cache directory on the server you are running
triton from, by default this will be `/opt/tritonserver/caches` - but this can
be adjusted by use of the `--cache-dir` CLI option as needed.

It is also required that Redis be running on a system reachable by Triton.
There are many ways to deploy Redis, to learn how to get started with Redis
look at Redis's [getting started guide](https://redis.io/docs/getting-started/).

### Configuration

The cache is configured by the using `--cache-config` CLI options.
The `--cache-config` option is variadic, meaning it can be repeated multiple
times to set multiple configuration fields. The format of a `--cache-config`
option is `<cache_name>,<key>=<value>`. At a minimum you must provide a `host`
and `port` to allow the client to connect to Redis e.g. let's try connecting to
a redis instance living on the host `redis-host` and listening on port `6379`:

```
tritonserver --cache-config redis,host=redis-host --cache-config redis,port=6379
```

### Available Configuration Options


| Configuration Option | Required | Description                                                                                                                                 | Default |
|----------------------|----------|---------------------------------------------------------------------------------------------------------------------------------------------|---------|
| host | Yes | The hostname or IP address of the server where Redis is running.                                                                            | N/A |
| port | Yes | The port number to connect to on the server.                                                                                                | N/A |
| user | No | The username to use for authentication of the ACLs to the Redis Server                                                                      | default |
| password | No | The password to Redis.                                                                                                                      | N/A |
| db | No | The db number to user. NOTE - use of the db number is considered an anti-pattern in Redis, so it is advised that you do not use this option | 0 |
| connect_timeout | No | The maximum time, in milliseconds to wait for a connection to be established to Redis. 0 means wait forever                                 | 0 |
| socket_timeout | No | The maximum time, in milliseconds the client will wait for a response from Redis. 0 means wait forever                                      | 0 |
| pool_size | No | The number pooled connections to Redis the client will maintain.                                                                            | 1 |
| wait_timeout | No | The maximum time, in milliseconds to wait for a connection from the pool.                                                                   | 100 |


## Monitoring and Observability

There are many ways to go about monitoring what's going on in Redis. One popular mode is to export metrics data from Redis to Prometheus, and use Grafana to observe them.

* If you're using OSS Redis, use the [Redis Exporter](https://grafana.com/oss/prometheus/exporters/redis-exporter/) to export metrics from Redis into Prometheus.
* If you're using [Redis Enterprise](https://docs.redis.com/latest/rs/clusters/monitoring/prometheus-integration/) or [Redis Cloud](https://docs.redis.com/latest/rc/cloud-integrations/prometheus-integration/) you can use the built-in integrations for Prometheus

## Example

You can try out the Redis Cache with Triton in docker:

* clone this repo: `git clone https://github.com/triton-inference-server/redis_cache`
* clone the Triton server repo: `git clone https://github.com/triton-inference-server`
* Add the following to: `docs/examples/model_repository/densenet_onnx/config.pbtxt`
```
response_cache{
  enable:true
}
```
* cd into `redis_cache`
* Install [NVIDIA's container toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html)
* Create an account on [NGC](https://ngc.nvidia.com/)
* Log docker into to NVIDIA's container repository:
```
docker login nvcr.io

Username: $oauthtoken
Password: <MY API KEY>
```
> NOTE: Username: $oauthtoken in this context means that your username is literally $oauthtoken - your API key serves as the unique part of your credentials
* run `docker-compose build`
* run `docker-compose up`
* In a separate terminal run `docker run -it --rm --net=host nvcr.io/nvidia/tritonserver:23.03-py3-sdk`
* Run `/workspace/install/bin/image_client -m densenet_onnx -c 3 -s INCEPTION /workspace/images/mug.jpg`
  * on the first run - this will miss the cache
  * subsequent runs will pull the inference out of the cache
  * you can validate this by watching Redis with `docker exec -it redis_cache_triton-redis_1 redis-cli monitor`
