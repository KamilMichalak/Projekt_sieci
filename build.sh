#!/bin/bash
# build.sh

mkdir -p build
cd build
cmake ..
make

if [ $? -eq 0 ]; then
    echo "Kompilacja zakończona sukcesem!"
    echo "Uruchom:"
    echo "  ./client   # klient"
    echo "  ./server   # serwer"
else
    echo "Błąd kompilacji!"
    exit 1
fi