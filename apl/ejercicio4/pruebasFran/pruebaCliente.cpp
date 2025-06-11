#include "ahorcado.h"

int main() {

    const char* SERVER_LOCKFILE = "/tmp/ahorcado_server.lock";

    ifstream lockfile(SERVER_LOCKFILE);
    int server_pid = 0;

    if (!(lockfile >> server_pid)) {
        cerr << "No se detectó un servidor activo. Abortando...\n";
        exit(1);
    }
    lockfile.close();

   /* if (kill(server_pid, 0) == -1 && errno != EPERM) {
        cerr << "No se detectó un servidor activo. Abortando...\n";
        exit(1);
    }
*/
    //verifico que no haya un cliente activo
    int lock_fd = open("/tmp/ahorcado_cliente.lock", O_CREAT | O_EXCL, 0444);
    if (lock_fd == -1) {
        cerr << "Error: Ya hay un cliente en ejecución." << endl;
        exit(1);
    }

    //inicializo cliente
    key_t key = ftok("shmfile", 65);
    int shmid = shmget(key, SHM_SIZE, 0666);
    void* data = shmat(shmid, nullptr, 0);

    sem_t* sem_mutex = sem_open(SEM_MUTEX_NAME, 0);
    sem_t* sem_letra_lista = sem_open(SEM_LETRA_LISTA_NAME, 0);
    sem_t* sem_resultado_listo = sem_open(SEM_RESULTADO_LISTO_NAME, 0);
    sem_t* sem_nuevo_cliente = sem_open(SEM_NUEVO_CLIENTE_NAME, 0);
    sem_t* sem_frase_lista = sem_open(SEM_FRASE_LISTA_NAME, 0);
    sem_t* sem_opcion_lista = sem_open(SEM_OPCION_LISTA_NAME, 0);
    sem_t* sem_inicio_1 = sem_open(SEM_INICIO_1_NAME, 0); 
    sem_t* sem_inicio_op2 = sem_open(SEM_INICIO_2_NAME, 0);
    sem_t* sem_frase_intento_lista = sem_open(SEM_FRASE_I_NAME, 0);

    juegoCompartido* juego = static_cast<juegoCompartido*>(data);

    string letraS, opcion;
    char letra;
    char progreso[128];
    char frase_sugerida[128];
    bool juego_terminado = false; 
    int intentos = 0; 

    sem_post(sem_nuevo_cliente);
    sem_wait(sem_frase_lista);

    sem_wait(sem_mutex);
    strncpy(progreso, juego->progreso, 128);
    intentos = juego->intentos_restantes; 
    juego_terminado = juego->juego_terminado; 
    sem_post(sem_mutex);

    cout << "\n/// NUEVO JUEGO ///\n"; 
    cout << "Frase: " << progreso << "\n"; 
    cout << "/// NUEVO JUEGO ///\n\n"; 

    while (!juego_terminado) { 

        sem_wait(sem_mutex);
        juego_terminado = juego->juego_terminado;
        sem_post(sem_mutex);

        if(juego_terminado){
            cout << "\n/// PARTIDA FINALIZADA ///\n";
            break;
        }


        cout << "Menú de juego:\n"; 
        cout << "1 - Enviar letra\n";
        cout << "2 - Adivinar palabra\n"; 
        cout << "Seleccione una opción: ";

        do {
            getline(cin, opcion);
        } while (opcion.empty() || (opcion[0] != '1' && opcion[0] != '2')); 

        sem_wait(sem_mutex);
        juego->opcion = opcion[0];
        sem_post(sem_mutex);
        sem_post(sem_opcion_lista);

        //opcion 1: adivinar letra
        if (opcion[0] == '1') {
            sem_wait(sem_inicio_1); 

            cout << "Ingrese una letra: ";
            do {
                getline(cin, letraS);
            } while (letraS.empty()); 
            letra = letraS[0];

            cout << "Enviando letra: " << letra << "\n";

            sem_wait(sem_mutex);
            juego->letra_sugerida = letra;
            sem_post(sem_mutex);
            sem_post(sem_letra_lista);

            sem_wait(sem_resultado_listo);

            sem_wait(sem_mutex);
            intentos = juego->intentos_restantes; 
            juego_terminado = juego->juego_terminado; 
            strncpy(progreso, juego->progreso, 128); 
            sem_post(sem_mutex);

            cout << "\n/// RESULTADO OBTENIDO ///\n"; 
            cout << "Cantidad de intentos restantes: " << intentos << "\n"; 
            cout << "Progreso de frase: " << progreso << "\n"; 
            cout << "/// RESULTADO OBTENIDO ///\n\n"; 
        }
        //opcion 2: adivinar frase
        else {
            sem_wait(sem_inicio_op2);
            sem_wait(sem_mutex);
            cout << "Ingrese una frase: ";
            cin.getline(juego->frase_sugerida,128);
            sem_post(sem_mutex);
            sem_post(sem_frase_intento_lista);
            sem_wait(sem_resultado_listo);
        }
    }

    sem_wait(sem_mutex);
    bool victoria = juego->victoria;
    char frase[128];
    strncpy(frase,juego->frase,128);
    sem_post(sem_mutex);

    if(victoria){
            cout << "\n/// VICTORIA ///\n"; 
            cout << "Felicitaciones, ¡adivinaste la frase!\n"; 
            cout << "/// VICTORIA ///\n\n"; 
        }
    else{
            cout << "\n/// GAME OVER ///\n";
            cout << "La frase era: " << frase;
            cout << "\n/// GAME OVER ///\n";
    }
    cout << "Juego finalizado. Gracias por jugar!\n"; 


    sem_close(sem_mutex);
    sem_close(sem_letra_lista);
    sem_close(sem_resultado_listo);
    sem_close(sem_nuevo_cliente); 
    sem_close(sem_frase_lista); 
    sem_close(sem_opcion_lista); 
    sem_close(sem_inicio_1); 
    sem_close(sem_inicio_op2);
    sem_close(sem_frase_intento_lista);

    close(lock_fd);
    unlink("/tmp/ahorcado_cliente.lock");

    shmdt(data);

    return 0;
}
