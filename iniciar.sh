#!/usr/bin/env bash

rm -f /tmp/entropia_data.json

./entropia3d &
PID_C=$!

echo "kernel c iniciado (pid $PID_C)"
echo "aguardando primeiros dados..."

for i in $(seq 1 30); do
    if [ -f /tmp/entropia_data.json ]; then
        break
    fi
    sleep 0.3
done

python3 visualizador_entropia.py

kill $PID_C 2>/dev/null
wait $PID_C 2>/dev/null
echo "encerrado"
