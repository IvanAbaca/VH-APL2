#ifndef AHORCADO_H
#define AHORCADO_H

#define SEM_CLIENTE_LISTO     0  // cliente pide jugar
#define SEM_SERVIDOR_OCUPADO  1  // servidor responde
#define SEM_MUTEX             2  // acceso seguro a memoria
#define SEM_LETRA_LISTA       3  // cliente puso letra
#define SEM_RESPUESTA_LISTA   4  // servidor respondió


#include <iostream>
#include <csignal>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <cstring>
#include <unistd.h>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <ctime>
#define SHM_SIZE 1024

using namespace std;

struct juegoCompartido {
    char frase[128];
    char progreso[128];
    char letra_sugerida;
    int intentos_restantes;
    bool letra_disponible;
    bool juego_terminado;
};


#endif //AHORCADO_H