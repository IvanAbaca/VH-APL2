#include "ahorcado.h"

int main() {

    key_t key = ftok("shmfile", 65);
    int shmid = shmget(key, SHM_SIZE, 0666);
    void* data = shmat(shmid, nullptr, 0);
    
    sem_t* sem_mutex = sem_open(SEM_MUTEX_NAME, 0);
    sem_t* sem_letra_lista = sem_open(SEM_LETRA_LISTA_NAME, 0);
    sem_t* sem_resultado_listo = sem_open(SEM_RESULTADO_LISTO_NAME, 0);
    sem_t* sem_nuevo_cliente = sem_open(SEM_NUEVO_CLIENTE_NAME, 0);
    sem_t* sem_frase_lista = sem_open(SEM_FRASE_LISTA_NAME, 0);

    juegoCompartido* juego = static_cast<juegoCompartido*>(data);
    
    string letraS;
    char letra;
    char progreso[128];

    sem_post(sem_nuevo_cliente);
    sem_wait(sem_frase_lista);

    sem_wait(sem_mutex);
    strncpy(progreso,juego->progreso,128);
    int intentos = juego->intentos_restantes;
    sem_post(sem_mutex);
    cout << "/// NUEVO JUEGO ///" << endl;
    cout << "Frase: " << progreso << endl;
    
    while(intentos){ 
        cout << "Ingrese una letra: ";
        getline(cin,letraS);
        letra = letraS[0];
        cout << "Enviando letra: " << letra << endl;
        sem_wait(sem_mutex);
        juego->letra_sugerida = letra;
        sem_post(sem_mutex);
        sem_post(sem_letra_lista);
        sem_wait(sem_resultado_listo);
        sem_wait(sem_mutex);
        intentos = juego->intentos_restantes;
        strncpy(progreso,juego->progreso,128);
        sem_post(sem_mutex);
        cout << "Cantidad de intentos restantes: " << intentos << endl;  
        cout << "Progreso de frase: " << progreso << endl;
    }
    
    sem_close(sem_mutex);
    sem_close(sem_letra_lista);
    sem_close(sem_resultado_listo);

    
    shmdt(data);
    return 0;
}
