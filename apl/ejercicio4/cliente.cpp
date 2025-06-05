// cliente.cpp

#include <iostream>
#include <cstring>
#include <csignal>
#include <ctime>
#include <cerrno>
#include <cstdlib>

#include <fcntl.h>      // O_* constantes
#include <sys/mman.h>   // shm_*
#include <sys/stat.h>   // mode constants
#include <unistd.h>     // close
#include <semaphore.h>  // sem_*

#include "ahorcado_ipc.h"

using namespace std;

// Para ignorar SIGINT (Ctrl-C)
void sigint_handler(int signo) {
    (void)signo;
    // No hace nada, simplemente evita que el proceso termine abruptamente
}

int main(int argc, char* argv[]) {
    // 1) Parseo de argumentos: -n <nickname>
    string nickname;
    int opt;
    while ((opt = getopt(argc, argv, "n:h")) != -1) {
        switch (opt) {
            case 'n': nickname = optarg; break;
            case 'h':
            default:
                cout << "Uso: " << argv[0] << " -n <nickname>\n";
                return 0;
        }
    }
    if (nickname.empty()) {
        cerr << "Debe indicar un nickname.\n"
             << "Uso: " << argv[0] << " -n <nickname>\n";
        return 1;
    }
    if (nickname.size() >= MAX_NICK_LEN) {
        cerr << "Nickname demasiado largo (máx. " << (MAX_NICK_LEN-1) << " caracteres).\n";
        return 1;
    }

    // 2) Ignoro SIGINT
    struct sigaction sa_int{};
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags   = 0;
    sigaction(SIGINT, &sa_int, nullptr);

    // 3) Abro semáforos que el Servidor creó:
    sem_t *sem_client_mutex = sem_open(SEM_CLIENT_MUTEX, 0);
    sem_t *sem_client_ready = sem_open(SEM_CLIENT_READY, 0);
    sem_t *sem_phrase_ready = sem_open(SEM_PHRASE_READY, 0);
    sem_t *sem_letter      = sem_open(SEM_LETTER, 0);
    sem_t *sem_update      = sem_open(SEM_UPDATE, 0);
    if (sem_client_mutex == SEM_FAILED ||
        sem_client_ready == SEM_FAILED ||
        sem_phrase_ready == SEM_FAILED ||
        sem_letter      == SEM_FAILED ||
        sem_update      == SEM_FAILED) {
        cerr << "Error al abrir semáforos. ¿Está corriendo el servidor?\n";
        return 1;
    }

    // 4) Intento “conectarme”: 
    //    sem_wait(client_mutex) para asegurar que no haya otro cliente.
    if (sem_wait(sem_client_mutex) < 0) {
        perror("sem_wait(SEM_CLIENT_MUTEX)");
        return 1;
    }

    // 5) Abro memoria compartida:
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0);
    if (shm_fd < 0) {
        cerr << "Error al abrir memoria compartida: ¿Servidor corriendo?\n";
        sem_post(sem_client_mutex); // libero para posible próximo cliente
        return 1;
    }
    void* addr = mmap(nullptr, sizeof(SharedData),
                      PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (addr == MAP_FAILED) {
        cerr << "Error en mmap: " << strerror(errno) << "\n";
        close(shm_fd);
        sem_post(sem_client_mutex);
        return 1;
    }
    SharedData* shm_ptr = reinterpret_cast<SharedData*>(addr);

    // 6) Escribo mi nickname en memoria compartida (primero pongo todo en 0 y luego copio):
    memset(shm_ptr->nickname, 0, MAX_NICK_LEN);
    strncpy(shm_ptr->nickname, nickname.c_str(), MAX_NICK_LEN - 1);

    // 7) Avisa al servidor que ya pusimos nickname:
    if (sem_post(sem_client_ready) < 0) {
        perror("sem_post(SEM_CLIENT_READY)");
        munmap(shm_ptr, sizeof(SharedData));
        close(shm_fd);
        sem_post(sem_client_mutex);
        return 1;
    }

    // 8) Espero a que el servidor haga post(SEM_PHRASE_READY):
    if (sem_wait(sem_phrase_ready) < 0) {
        perror("sem_wait(SEM_PHRASE_READY)");
        munmap(shm_ptr, sizeof(SharedData));
        close(shm_fd);
        sem_post(sem_client_mutex);
        return 1;
    }

    // 9) Ya tengo la frase en shm_ptr->masked, intentos en shm_ptr->attempts_left, etc.
    cout << "=== ¡Juego del Ahorcado! ===\n";
    cout << "Jugador: " << nickname << "\n";
    cout << "La frase a adivinar tiene " 
         << strlen(shm_ptr->masked) << " caracteres.\n\n";

    // Muestro estado inicial:
    cout << "Frase: " << shm_ptr->masked << "\n";
    cout << "Intentos restantes: " << shm_ptr->attempts_left << "\n";

    // 10) Bucle de interacción:
    while (true) {
        if (shm_ptr->game_state == 3) {
            // El servidor dictaminó que la partida terminó antes de que pidiera letra.
            break;
        }
        cout << "\nIngrese una letra: ";
        string line;
        if (!getline(cin, line)) {
            // EOF o error, forzamos salida
            cout << "\n(Saliendo del juego)\n";
            shm_ptr->guess = '\0';
            sem_post(sem_letter); // avisamos al servidor que “nos fuimos”
            break;
        }
        if (line.empty()) {
            cout << "Debe ingresar al menos un caracter.\n";
            continue;
        }
        char letra = line[0];

        // Coloco la letra en el struct
        shm_ptr->guess = letra;

        // Mando señal al servidor: “ya puse letra”
        if (sem_post(sem_letter) < 0) {
            perror("sem_post(SEM_LETTER)");
            break;
        }

        // Espero a que el servidor procese y haga post(SEM_UPDATE)
        if (sem_wait(sem_update) < 0) {
            perror("sem_wait(SEM_UPDATE)");
            break;
        }

        // Ahora el servidor actualizó shm_ptr->masked y shm_ptr->attempts_left, 
        // y tal vez game_state==3 indicando fin.

        // Muestro estado:
        cout << "\nFrase: " << shm_ptr->masked << "\n";
        cout << "Intentos restantes: " << shm_ptr->attempts_left << "\n";

        if (shm_ptr->game_state == 3) {
            // La partida terminó
            if (shm_ptr->result == 0) {
                cout << "\n¡Felicidades, adivinaste la frase!\n";
            } else {
                cout << "\nSe te acabaron los intentos. Frase completa: \""
                     << shm_ptr->phrase << "\"\n";
            }
            break;
        }
    }

    // 11) Desconexión limpia:
    munmap(shm_ptr, sizeof(SharedData));
    close(shm_fd);

    // Libero el mutex de cliente para que otro pueda entrar
    sem_post(sem_client_mutex);

    sem_close(sem_client_mutex);
    sem_close(sem_client_ready);
    sem_close(sem_phrase_ready);
    sem_close(sem_letter);
    sem_close(sem_update);

    cout << "\nCliente finalizado.\n";
    return 0;
}
