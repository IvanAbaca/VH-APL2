#include "ahorcado.h"
#include <thread>
#include <atomic>

void *shm_ptr = nullptr;
int lock_fd = -1;
juegoCompartido *juego = nullptr;

sem_t *sem_mutex = SEM_FAILED;
sem_t *sem_letra_lista = SEM_FAILED;
sem_t *sem_resultado_listo = SEM_FAILED;
sem_t *sem_nuevo_cliente = SEM_FAILED;
sem_t *sem_frase_lista = SEM_FAILED;
sem_t *sem_opcion_lista = SEM_FAILED;
sem_t *sem_inicio_1 = SEM_FAILED;
sem_t *sem_inicio_op2 = SEM_FAILED;
sem_t *sem_frase_intento_lista = SEM_FAILED;

std::atomic<bool> servidor_activo(true);

void help()
{
    cout << "Uso: ./cliente [opciones]\n"
         << "Opciones:\n"
         << "  -n  --nickname <path>           Nickname del jugador (Requerido)\n"
         << "  -h  --help                      Muestra esta ayuda\n";
}

void finalizar_partida()
{
    cout << "\n\t\t////////////////////////////////////////////////////////////////////\n";
    cout << "\t\t///                          ATENCIÓN                           ///\n";
    cout << "\t\t///     El servidor de juego ya no se encuentra disponible      ///\n";
    cout << "\t\t///         Finalizando y liberando recursos...                 ///\n";
    cout << "\t\t///                          ATENCIÓN                           ///\n";
    cout << "\t\t////////////////////////////////////////////////////////////////////\n";

    if (sem_mutex != SEM_FAILED)
        sem_close(sem_mutex);
    if (sem_letra_lista != SEM_FAILED)
        sem_close(sem_letra_lista);
    if (sem_resultado_listo != SEM_FAILED)
        sem_close(sem_resultado_listo);
    if (sem_nuevo_cliente != SEM_FAILED)
        sem_close(sem_nuevo_cliente);
    if (sem_frase_lista != SEM_FAILED)
        sem_close(sem_frase_lista);
    if (sem_opcion_lista != SEM_FAILED)
        sem_close(sem_opcion_lista);
    if (sem_inicio_1 != SEM_FAILED)
        sem_close(sem_inicio_1);
    if (sem_inicio_op2 != SEM_FAILED)
        sem_close(sem_inicio_op2);
    if (sem_frase_intento_lista != SEM_FAILED)
        sem_close(sem_frase_intento_lista);

    if (lock_fd != -1)
    {
        close(lock_fd);
        unlink("/tmp/ahorcado_cliente.lock");
    }

    if (shm_ptr != nullptr)
    {
        shmdt(shm_ptr);
    }

    exit(0); // salida limpia
}

void monitor_servidor(pid_t pid_servidor)
{
    while (servidor_activo)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (kill(pid_servidor, 0) == -1)
        {
            cout << "\n[Cliente] El servidor se ha desconectado inesperadamente.\n";
            finalizar_partida();
        }
    }
}

void handle_sigusr1(int)
{
    finalizar_partida();
};

void handle_sigusr2(int)
{
    finalizar_partida();
};

bool esperaServidor(sem_t *sem, sem_t *mutex, pid_t pid_server)
{ // funcion a la que le pasas el semaforo que queres esperar y el mutex para el acceso a la shm, para verificar si el cliente sigue activo
    while (true)
    {
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        int wait_result = sem_timedwait(sem, &ts);
        if (wait_result == -1 && errno == ETIMEDOUT)
        {
            // cout << "[Servidor] TIMEOUT" << endl;
            bool desconectado = false;
            desconectado = (kill(pid_server, 0) == -1);

            if (desconectado)
            {
                // cout << "[Servidor] Muerte detectada" << endl;
                return true;
            }
            continue;
        }
        else if (wait_result != -1)
        {
            // cout << "[Servidor] Espera finalizada" << endl;
            return false;
        }
    }
}

int main(int argc, char *argv[])
{

    string nickname;

    signal(SIGINT, SIG_IGN);
    signal(SIGUSR1, handle_sigusr1);
    signal(SIGUSR2, handle_sigusr2);
    signal(SIGTERM, handle_sigusr2);

    // tomo los parametros
    for (int i = 1; i < argc; i++)
    {
        string arg = argv[i];
        if ((arg == "-n" || arg == "--nickname") && i + 1 < argc)
        {
            nickname = argv[++i];
        }
        else if (arg == "-h" || arg == "--help")
        {
            help();
            return 0;
        }
    }

    if (nickname.empty())
    {
        cerr << "Error: nickname no especificado." << endl;
        help();
        return 1;
    }

    sem_t *sem_mutex = sem_open(SEM_MUTEX_NAME, 0);
    if (sem_mutex == SEM_FAILED)
    {
        std::cerr << "No se detectó un servidor activo. Abortando...\n";
        sem_close(sem_mutex);
        exit(1);
    }

    // verifico que no haya un cliente activo
    lock_fd = open("/tmp/ahorcado_cliente.lock", O_CREAT | O_EXCL, 0444);
    if (lock_fd == -1)
    {
        cerr << "Error: Ya hay un cliente en ejecución." << endl;
        exit(1);
    }

    // inicializo cliente
    key_t key = ftok("shmfile", 65);
    int shmid = shmget(key, SHM_SIZE, 0666);
    void *shm_ptr = shmat(shmid, nullptr, 0);

    sem_letra_lista = sem_open(SEM_LETRA_LISTA_NAME, 0);
    sem_resultado_listo = sem_open(SEM_RESULTADO_LISTO_NAME, 0);
    sem_nuevo_cliente = sem_open(SEM_NUEVO_CLIENTE_NAME, 0);
    sem_frase_lista = sem_open(SEM_FRASE_LISTA_NAME, 0);
    sem_opcion_lista = sem_open(SEM_OPCION_LISTA_NAME, 0);
    sem_inicio_1 = sem_open(SEM_INICIO_1_NAME, 0);
    sem_inicio_op2 = sem_open(SEM_INICIO_2_NAME, 0);
    sem_frase_intento_lista = sem_open(SEM_FRASE_I_NAME, 0);

    juegoCompartido *juego = static_cast<juegoCompartido *>(shm_ptr);

    string letraS, opcion;
    char letra;
    char progreso[128];
    char frase_sugerida[128];
    bool juego_terminado = false;
    int intentos = 0;
    pid_t pid_server;

    sem_wait(sem_mutex);
    strncpy(juego->usuario_nickname, nickname.c_str(), 128);
    juego->pid_cliente = getpid();
    pid_server = juego->pid_servidor;
    std::thread watchdog(monitor_servidor, pid_server);
    sem_post(sem_mutex);

    sem_post(sem_nuevo_cliente);
    sem_wait(sem_frase_lista);

    sem_wait(sem_mutex);
    strncpy(progreso, juego->progreso, 128);
    intentos = juego->intentos_restantes;
    juego_terminado = juego->juego_terminado;
    sem_post(sem_mutex);

    cout << "\n/// NUEVO JUEGO ///\n";
    cout << "¡Hola " << nickname << "!\n";
    cout << "Frase: " << progreso << "\n";
    cout << "/// NUEVO JUEGO ///\n\n";

    while (!juego_terminado)
    {

        sem_wait(sem_mutex);
        juego_terminado = juego->juego_terminado;
        sem_post(sem_mutex);

        if (juego_terminado)
        {
            cout << "\n/// PARTIDA FINALIZADA ///\n";
            break;
        }

        cout << "Menú de juego:\n";
        cout << "1 - Enviar letra\n";
        cout << "2 - Adivinar palabra\n";
        cout << "3 - Salir\n";
        cout << "Seleccione una opción: ";

        do
        {
            getline(cin, opcion);
        } while (opcion.empty() || (opcion[0] != '1' && opcion[0] != '2' && opcion[0] != '3'));

        sem_wait(sem_mutex);
        juego->opcion = opcion[0];
        sem_post(sem_mutex);
        sem_post(sem_opcion_lista);

        switch (opcion[0])
        {
        // opcion 1: adivinar letra
        case '1':
        {
            // sem_wait(sem_inicio_1);

            juego_terminado = esperaServidor(sem_inicio_1, sem_mutex, pid_server);
            if (juego_terminado)
            {
                finalizar_partida();
            }

            do
            {
                cout << "Ingrese una letra: ";
                getline(cin, letraS);

                if (letraS.size() != 1)
                {
                    cout << "Error: debe ingresar exactamente un carácter" << endl;
                }
            } while (letraS.size() != 1);

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
            break;
        }
        // opcion 2: adivinar frase
        case '2':
        {
            // sem_wait(sem_inicio_op2);

            juego_terminado = esperaServidor(sem_inicio_op2, sem_mutex, pid_server);
            if (juego_terminado)
            {
                finalizar_partida();
            }

            char frase_ingresada[128];
            cout << "Ingrese una frase: ";
            cin.getline(frase_ingresada, 128);
            sem_wait(sem_mutex);
            strncpy(juego->frase_sugerida, frase_ingresada, 128);
            sem_post(sem_mutex);
            sem_post(sem_frase_intento_lista);
            sem_wait(sem_resultado_listo);
            break;
        }
        case '3':
        {
            // sem_wait(sem_inicio_1);

            juego_terminado = esperaServidor(sem_inicio_1, sem_mutex, pid_server);
            if (juego_terminado)
            {
                finalizar_partida();
            }

            sem_wait(sem_resultado_listo);
        }
        }
    }

    sem_wait(sem_mutex);
    bool victoria = juego->victoria;
    char frase[128];
    strncpy(frase, juego->frase, 128);
    sem_post(sem_mutex);

    if (victoria)
    {
        cout << "\n/// VICTORIA ///\n";
        cout << "Felicitaciones, ¡adivinaste la frase!\n";
        cout << "/// VICTORIA ///\n\n";
    }
    else
    {
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

    shmdt(shm_ptr);

    servidor_activo = false;
    if (watchdog.joinable())
    {
        watchdog.join();
    }

    return 0;
}
