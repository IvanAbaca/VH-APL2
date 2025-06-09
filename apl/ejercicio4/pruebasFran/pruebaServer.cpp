#include "ahorcado.h"
#define CANT_INTENTOS 4

volatile sig_atomic_t terminar = 0;

void handle_sigusr1(int) {
    terminar = 1;
}

void aumentar_semaforo(int semid,short unsigned int semindex){
    struct sembuf signal = {semindex,1,0};
    semop(semid,&signal,1);
}

void decrementar_semaforo(int semid,short unsigned int semindex){
    struct sembuf signal = {semindex,-1,0};
    semop(semid,&signal,1);
}

vector<string> leer_frases(const string& nombre_archivo){
    vector<string> frases;
    ifstream archivo(nombre_archivo);

    if(!archivo.is_open()){
        cerr << "[Servidor] Error: No se pudo abrir el archivo de frases (" << nombre_archivo << ")" << endl;
        cerr << "[Servidor] Abortando servidor..." <<endl;
        exit(1);
    }

    string linea;

    while(getline(archivo,linea)){
        if(!linea.empty()){
            frases.push_back(linea);
        }
    }

    archivo.close();
    return frases;
}

void limpiar_sem_y_mem(key_t key) {
    int semid = semget(key, 0, 0666);
    if (semid != -1) {
        semctl(semid, 0, IPC_RMID);
    }
    int shmid = shmget(key, SHM_SIZE, 0666);
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, nullptr);
    }
}


int main() {
    cout << "[Servidor] Inicializando servidor..." << endl;
    srand(time(nullptr));    

    signal(SIGUSR1, handle_sigusr1);
    signal(SIGINT,SIG_IGN);

    key_t key = ftok("shmfile", 65);
    limpiar_sem_y_mem(key); // Limpia cualquier rastro anterior
    int shmid = shmget(key, SHM_SIZE, 0666 | IPC_CREAT);
    void* data = shmat(shmid, nullptr, 0);
    juegoCompartido* juego = static_cast<juegoCompartido*>(data);

    int semid = semget(key, 5, 0666 | IPC_CREAT);
    semctl(semid,SEM_CLIENTE_LISTO,SETVAL,0);  
    semctl(semid,SEM_SERVIDOR_OCUPADO,SETVAL,0);  
    semctl(semid,SEM_MUTEX,SETVAL,1);    
    semctl(semid,SEM_LETRA_LISTA,SETVAL,0);
    semctl(semid,SEM_RESPUESTA_LISTA,SETVAL,0); 

    int cliente_id = 1;

    vector frases = leer_frases("frases.txt");
    
    cout << "[Servidor] Inicialización finalizada." << endl;
    aumentar_semaforo(semid,SEM_SERVIDOR_OCUPADO);
    decrementar_semaforo(semid,SEM_CLIENTE_LISTO); //atiendo un cliente
    decrementar_semaforo(semid,SEM_SERVIDOR_OCUPADO); //me pongo en ocupado
    while (!terminar) {
        int indice = rand() % frases.size();

        string mensaje = frases[indice];

        decrementar_semaforo(semid,SEM_MUTEX); //solicito region critica
        strncpy(juego->frase, mensaje.c_str(),128);
        aumentar_semaforo(semid,SEM_MUTEX); //libero region critica
        cout << "Holaaaaa" << endl;
        char letra;
        /*for(int i = 0; i < CANT_INTENTOS; i++){
            decrementar_semaforo(semid,SEM_LETRA_LISTA);
            decrementar_semaforo(semid,SEM_MUTEX);
            //strcpy(&letra,&juego->letra_sugerida);
            cout << juego->letra_sugerida << endl;
            aumentar_semaforo(semid,SEM_MUTEX);
        }
        */
        cout << "[Servidor] Esperando letra" << endl;
        decrementar_semaforo(semid,SEM_LETRA_LISTA);
        decrementar_semaforo(semid,SEM_MUTEX);
        //strcpy(&letra,&juego->letra_sugerida);
        cout << "[Servidor] Letra: ";
        cout << juego->letra_sugerida << endl;
        aumentar_semaforo(semid,SEM_MUTEX);
        aumentar_semaforo(semid,SEM_RESPUESTA_LISTA);
    }



    cout << "[Servidor] Finalizando, liberando recursos...\n";
    shmdt(data);
    shmctl(shmid, IPC_RMID, nullptr);
    semctl(semid, 0, IPC_RMID);

    return 0;
}
