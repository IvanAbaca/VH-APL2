/*
43262563 Abaca, Ivan
43971511 Guardati, Francisco
43780360 Romero, Lucas Nicolas
45542385 Palmieri, Marco
41566741 Piegari, Lucila Quiroga
*/

#include <iostream>
#include <cstring>
#include <csignal>
#include <ctime>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <semaphore.h>

#include "ahorcado_ipc.h"

using namespace std;

void sigint_handler(int signo)
{
    (void)signo;
}

void mostrar_ayuda(const char *prog_name)
{
    cout << "Uso: ./cliente [opciones]\n"
         << "Opciones:\n"
         << "  -n  --nickname <path>           Nickname del jugador (Requerido)\n"
         << "  -h  --help                      Muestra esta ayuda\n";
}

int main(int argc, char *argv[])
{

    if (argc == 1)
    {
        mostrar_ayuda(argv[0]);
        return 0;
    }

    string nickname;
    int opt;
    while ((opt = getopt(argc, argv, "n:h")) != -1)
    {
        switch (opt)
        {
        case 'n':
            nickname = optarg;
            break;
        case 'h':
        default:
            mostrar_ayuda(argv[0]);
            return 0;
        }
    }
    if (nickname.empty())
    {
        mostrar_ayuda(argv[0]);
        return 1;
    }
    if (nickname.size() >= MAX_NICK_LEN)
    {
        cerr << "Nickname demasiado largo (máx. " << (MAX_NICK_LEN - 1) << " caracteres).\n";
        return 1;
    }

    struct sigaction sa_int{};
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, nullptr);

    sem_t *sem_client_mutex = sem_open(SEM_CLIENT_MUTEX, 0);
    sem_t *sem_client_ready = sem_open(SEM_CLIENT_READY, 0);
    sem_t *sem_phrase_ready = sem_open(SEM_PHRASE_READY, 0);
    sem_t *sem_letter = sem_open(SEM_LETTER, 0);
    sem_t *sem_update = sem_open(SEM_UPDATE, 0);
    if (sem_client_mutex == SEM_FAILED ||
        sem_client_ready == SEM_FAILED ||
        sem_phrase_ready == SEM_FAILED ||
        sem_letter == SEM_FAILED ||
        sem_update == SEM_FAILED)
    {
        cerr << "Error al abrir semáforos. ¿Está corriendo el servidor?\n";
        return 1;
    }

    if (sem_wait(sem_client_mutex) < 0)
    {
        perror("sem_wait(SEM_CLIENT_MUTEX)");
        return 1;
    }

    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0);
    if (shm_fd < 0)
    {
        cerr << "Error al abrir memoria compartida: ¿Servidor corriendo?\n";
        sem_post(sem_client_mutex);
        return 1;
    }
    void *addr = mmap(nullptr, sizeof(SharedData),
                      PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (addr == MAP_FAILED)
    {
        cerr << "Error en mmap: " << strerror(errno) << "\n";
        close(shm_fd);
        sem_post(sem_client_mutex);
        return 1;
    }
    SharedData *shm_ptr = reinterpret_cast<SharedData *>(addr);

    memset(shm_ptr->nickname, 0, MAX_NICK_LEN);
    strncpy(shm_ptr->nickname, nickname.c_str(), MAX_NICK_LEN - 1);

    if (sem_post(sem_client_ready) < 0)
    {
        perror("sem_post(SEM_CLIENT_READY)");
        munmap(shm_ptr, sizeof(SharedData));
        close(shm_fd);
        sem_post(sem_client_mutex);
        return 1;
    }

    if (sem_wait(sem_phrase_ready) < 0)
    {
        perror("sem_wait(SEM_PHRASE_READY)");
        munmap(shm_ptr, sizeof(SharedData));
        close(shm_fd);
        sem_post(sem_client_mutex);
        return 1;
    }

    // Inicio del juego
    cout << "=== ¡Juego del Ahorcado! ===\n";
    cout << "Jugador: " << nickname << "\n";
    cout << "La frase a adivinar tiene "
         << strlen(shm_ptr->masked) << " caracteres.\n\n";
    cout << "Frase: " << shm_ptr->masked << "\n";
    cout << "Intentos restantes: " << shm_ptr->attempts_left << "\n";

    while (true)
    {
        if (shm_ptr->game_state == 3)
            break;

        cout << "\nIngrese una letra: ";
        string line;
        if (!getline(cin, line))
        {
            cout << "\n(Saliendo del juego)\n";
            shm_ptr->guess = '\0';
            sem_post(sem_letter);
            break;
        }
        if (line.empty())
        {
            cout << "Debe ingresar al menos un caracter.\n";
            continue;
        }
        if (line.size() > 1)
        {
            cout << "Por favor ingrese solo UN caracter.\n";
            continue;
        }
        char letra = line[0];
        shm_ptr->guess = letra;

        if (sem_post(sem_letter) < 0)
        {
            perror("sem_post(SEM_LETTER)");
            break;
        }
        if (sem_wait(sem_update) < 0)
        {
            perror("sem_wait(SEM_UPDATE)");
            break;
        }

        cout << "\nFrase: " << shm_ptr->masked << "\n";
        cout << "Intentos restantes: " << shm_ptr->attempts_left << "\n";

        if (shm_ptr->game_state == 3)
        {
            if (shm_ptr->result == 0)
            {
                cout << "\n¡Felicidades, adivinaste la frase!\n";
            }
            else
            {
                cout << "\nSe te acabaron los intentos. Frase completa: \""
                     << shm_ptr->phrase << "\"\n";
            }
            break;
        }
    }

    munmap(shm_ptr, sizeof(SharedData));
    close(shm_fd);
    sem_post(sem_client_mutex);

    sem_close(sem_client_mutex);
    sem_close(sem_client_ready);
    sem_close(sem_phrase_ready);
    sem_close(sem_letter);
    sem_close(sem_update);

    cout << "\nCliente finalizado.\n";
    return 0;
}
