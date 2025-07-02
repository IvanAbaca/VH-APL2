#ifndef AHORCADO_H
#define AHORCADO_H

#define SEM_MUTEX_NAME "/sem_mutex"
#define SEM_LETRA_LISTA_NAME "/sem_letra_lista"
#define SEM_RESULTADO_LISTO_NAME "/sem_resultado_listo"
#define SEM_FRASE_LISTA_NAME "/sem_frase_lista"
#define SEM_NUEVO_CLIENTE_NAME "/sem_nuevo_cliente"
#define SEM_OPCION_LISTA_NAME "/sem_opcion_lista"
#define SEM_INICIO_1_NAME "/sem_inicio_1"
#define SEM_INICIO_2_NAME "/sem_inicio_2"
#define SEM_FRASE_I_NAME "/sem_frase_intento"

#include <chrono>
#include <iostream>
#include <csignal>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <cstring>
#include <unistd.h>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <semaphore.h>
#include <fcntl.h>
#include <limits>
#include <algorithm>
#include <cctype>
#include <iomanip>



#define SHM_SIZE 1024

using namespace std;

struct juegoCompartido {
    char frase[128];
    char progreso[128];
    char frase_sugerida[128];
    char letra_sugerida;
    char usuario_nickname[128];
    char opcion;
    int intentos_restantes;
    bool letra_disponible;
    bool juego_terminado;
    bool victoria;
    pid_t pid_cliente;
};

struct rankingEntry {
    string nickname;
    string frase;
    double tiempo_segundos;
};


#endif //AHORCADO_H