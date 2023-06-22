FROM nvcr.io/nvidia/tritonserver:23.03-py3-sdk

RUN apt install -y lsb-release curl gpg 
RUN curl -fsSL https://packages.redis.io/gpg | gpg --dearmor -o /usr/share/keyrings/redis-archive-keyring.gpg 
RUN echo "deb [signed-by=/usr/share/keyrings/redis-archive-keyring.gpg] https://packages.redis.io/deb $(lsb_release -cs) main" | tee /etc/apt/sources.list.d/redis.list 
RUN apt-get update 
RUN apt-get install -y redis

CMD "/app/test.sh"