#!/bin/bash

sh gen-certs.sh
docker-compose build
docker-compose up