from nvcr.io/nvidia/tritonserver:23.03-py3

RUN mkdir /opt/tritonserver/caches/redis
COPY ./cmake-build-debug/libtritoncache_redis.so /opt/tritonserver/caches/redis