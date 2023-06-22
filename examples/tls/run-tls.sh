#!/bin/bash

sh gen-certs.sh
chmod +x ./certs/ca.crt ./certs/redis.crt ./certs/redis.key
docker-compose build
docker-compose up