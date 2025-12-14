#!/bin/bash

echo "Démarrage du serveur VM Manager..."
node back/server.js &
SERVER_PID=$!

echo "Attente du démarrage du serveur..."
sleep 2

echo "Ouverture du navigateur..."
xdg-open http://localhost:3000

echo "Serveur démarré (PID: $SERVER_PID)"
echo "Press Ctrl+C to stop"

# Attendre et arrêter proprement
trap "kill $SERVER_PID; exit" INT
wait $SERVER_PID