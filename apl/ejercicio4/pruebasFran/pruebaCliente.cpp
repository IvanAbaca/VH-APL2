#include "ahorcado.h"

void aumentar_semaforo(int semid,short unsigned int semindex){
    struct sembuf signal = {semindex,1,0};
    semop(semid,&signal,1);
}

void decrementar_semaforo(int semid,short unsigned int semindex){
    struct sembuf signal = {semindex,-1,0};
    semop(semid,&signal,1);
}

int main() {

    key_t key = ftok("shmfile", 65);
    int shmid = shmget(key, SHM_SIZE, 0666);
    int semid = semget(key, 4, 0666);
    void* data = shmat(shmid, nullptr, 0);
    
    if(shmid == -1 || semid == -1){
        cerr << "Servidor no disponible, abortando cliente." << endl;
        exit(1);
    }
    
    juegoCompartido* juego = static_cast<juegoCompartido*>(data);
    
    decrementar_semaforo(semid,SEM_SERVIDOR_OCUPADO); //solicito servidor
    aumentar_semaforo(semid,SEM_CLIENTE_LISTO); //aviso al servidor que estoy

    decrementar_semaforo(semid,SEM_MUTEX); //solicito region critica
    std::cout << "[Cliente] Mensaje recibido: " << juego->frase << "\n";
    aumentar_semaforo(semid,SEM_MUTEX); //libero region critica

    char letra;

    cout << "Ingrese una letra: ";
    cin >> letra;

    decrementar_semaforo(semid,SEM_MUTEX);
    juego->letra_sugerida = letra;
    aumentar_semaforo(semid,SEM_MUTEX);
    aumentar_semaforo(semid,SEM_LETRA_LISTA);
    decrementar_semaforo(semid,SEM_RESPUESTA_LISTA);
    
    shmdt(data);
    return 0;
}
