#!/bin/bash

bash gen-certs.sh
chmod +r ./certs/ca.crt ./certs/redis.crt ./certs/redis.key
cd ..
bash ./fetch_model.sh
cd tls
docker-compose build
docker-compose up