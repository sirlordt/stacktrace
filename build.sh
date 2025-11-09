#!/bin/bash

mkdir -p ./build

g++ -std=c++23 -g -O0 -rdynamic main1.cpp -o ./build/app1 -ldw
g++ -std=c++23 -g -O0 -rdynamic main2.cpp -o ./build/app2 -ldw
g++ -std=c++23 -g -O0 -rdynamic main3.cpp -o ./build/app3 -ldw -lcurl

# Configurar URL del servidor
#export CRASH_REPORT_URL="https://your-server.com/api/crash-reports"

# O para testing local
#export CRASH_REPORT_URL="http://localhost:3000/crash"

#./app3