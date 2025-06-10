#include "ahorcado.h"
#define CANT_INTENTOS 4

volatile sig_atomic_t terminar = 0;

void handle_sigusr1(int) {
    terminar = 1;
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


string ofuscarFrase(const string& frase) {
    string resultado;
    for (char c : frase) {
        if (c == ' ')
            resultado += ' ';
        else
            resultado += '_';
    }
    return resultado;
}


bool descubrirLetra(const string& fraseOriginal, string& fraseOculta, char letra) {
    bool hit = 0;
    for (size_t i = 0; i < fraseOriginal.size(); ++i) {
        if (tolower(fraseOriginal[i]) == tolower(letra)) {
            fraseOculta[i] = fraseOriginal[i];
            hit = 1;
        }
    }
    return hit;
}


int main() {
    cout << "[Servidor] Inicializando servidor..." << endl;
    srand(time(nullptr));    

    signal(SIGUSR1, handle_sigusr1);
    //signal(SIGINT,SIG_IGN);

    key_t key = ftok("shmfile", 65);
    int shmid = shmget(key, SHM_SIZE, 0666 | IPC_CREAT);
    void* data = shmat(shmid, nullptr, 0);
    juegoCompartido* juego = static_cast<juegoCompartido*>(data);

    sem_t* sem_mutex = sem_open(SEM_MUTEX_NAME, O_CREAT, 0666, 1);
    sem_t* sem_letra_lista = sem_open(SEM_LETRA_LISTA_NAME, O_CREAT, 0666, 0);
    sem_t* sem_resultado_listo = sem_open(SEM_RESULTADO_LISTO_NAME, O_CREAT, 0666, 0);
    sem_t* sem_nuevo_cliente = sem_open(SEM_NUEVO_CLIENTE_NAME, O_CREAT, 0666, 0);
    sem_t* sem_frase_lista = sem_open(SEM_FRASE_LISTA_NAME, O_CREAT, 0666, 0);
    
    vector frases = leer_frases("frases.txt");
    
    while (!terminar)
    {
        sem_wait(sem_nuevo_cliente);
        //Enviar palabra y turnos disponibles a cliente
        bool juego_terminado = 0;
        int indice = rand() % frases.size();
        string frase_original = frases[indice];
        string frase_ofuscada = ofuscarFrase((const string&) frase_original);

        sem_wait(sem_mutex);
        strncpy(juego->frase,frase_ofuscada.c_str(),128);
        strncpy(juego->progreso,frase_ofuscada.c_str(),128);
        juego->intentos_restantes = 4;
        sem_post(sem_mutex);
        sem_post(sem_frase_lista);
     
        //Recibir letra desde cliente
        while(!juego_terminado){
            sem_wait(sem_letra_lista);
            sem_wait(sem_mutex);
            char letra = juego->letra_sugerida;
            sem_post(sem_mutex);
            cout << "[Servidor] Letra recibida: " << letra << endl;
            if(!descubrirLetra((const string&) frase_original,(string&) frase_ofuscada,letra)){
                cout << "[Servidor] Letra no encontrada, restando intento"<< endl;
                sem_wait(sem_mutex);
                if(!--juego->intentos_restantes){
                    juego->juego_terminado = 1;
                    juego_terminado = 1;
                }
                sem_post(sem_mutex);
            }    
            cout << "[Servidor]: Frase actualizada: ";
            cout << frase_ofuscada<< endl;
            sem_wait(sem_mutex);
            strncpy(juego->progreso,frase_ofuscada.c_str(),128);
            sem_post(sem_mutex);
            sem_post(sem_resultado_listo);
        }
    }
    

    cout << "[Servidor] Inicialización finalizada." << endl;



    cout << "[Servidor] Finalizando, liberando recursos...\n";
    sem_close(sem_mutex);
    sem_close(sem_letra_lista);
    sem_close(sem_resultado_listo);


    sem_unlink(SEM_MUTEX_NAME);
    sem_unlink(SEM_LETRA_LISTA_NAME);
    sem_unlink(SEM_RESULTADO_LISTO_NAME);

    shmdt(data);
    shmctl(shmid, IPC_RMID, nullptr);
    return 0;
}
