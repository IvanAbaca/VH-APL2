/*
43262563 Abaca, Ivan
43971511 Guardati, Francisco
43780360 Romero, Lucas Nicolas
45542385 Palmieri, Marco
41566741 Piegari, Lucila Quiroga
*/

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <csignal>
#include <ctime>
#include <cerrno>
#include <cstring>
#include <cstdlib>

#include <fcntl.h>      // O_* constantes
#include <sys/mman.h>   // shm_*
#include <sys/stat.h>   // mode constants
#include <unistd.h>     // ftruncate, close
#include <semaphore.h>  // sem_*

#include "ahorcado_ipc.h"

using namespace std;

// Globales para señalización:
static volatile sig_atomic_t terminate_now        = 0; // bandera para SIGUSR2 (forzar fin inmediato)
static volatile sig_atomic_t terminate_when_free = 0; // bandera para SIGUSR1 (esperar a terminar partida)

// Punteros al área de memoria compartida y al struct:
static SharedData* shm_ptr = nullptr;
static int          shm_fd  = -1;

// Semáforos POSIX:
static sem_t *sem_server_lock = SEM_FAILED;
static sem_t *sem_client_mutex= SEM_FAILED;
static sem_t *sem_client_ready= SEM_FAILED;
static sem_t *sem_phrase_ready= SEM_FAILED;
static sem_t *sem_letter      = SEM_FAILED;
static sem_t *sem_update      = SEM_FAILED;

// Ranking interno (guarda el mejor tiempo y nickname)
static double   best_time   = -1.0;
static char     best_nick[MAX_NICK_LEN] = "";

// Indica si hay una partida en curso:
static volatile bool game_in_progress = false;

// ------------------------------
// Manejador de señales
// ------------------------------
void sigint_handler(int signo) {
    // Simplemente ignoramos SIGINT (Ctrl-C)
    (void)signo;
}

void sigusr1_handler(int signo) {
    // Si no hay juego en curso, cortamos YA; si hay uno en curso, esperamos a que termine.
    (void)signo;
    if (!game_in_progress) {
        terminate_when_free = 1;
    } else {
        terminate_when_free = 1;
    }
}

void sigusr2_handler(int signo) {
    // Señal de “corte inmediato”: si hay juego en curso, terminamos esa partida YA; si no, salimos YA.
    (void)signo;
    terminate_now = 1;
}

// ------------------------------
// Función que despliega el ranking final
// ------------------------------
void print_ranking_and_cleanup() {
    cout << "\n=== Ranking final ===\n";
    if (best_time >= 0.0) {
        cout << "Mejor Jugador: " << best_nick
             << "  (Tiempo: " << best_time << " seg)" << endl;
    } else {
        cout << "No hubo partidas jugadas.\n";
    }
    cout << "======================\n\n";
}

// ------------------------------
// Lee todas las frases del archivo y las guarda en un vector
// ------------------------------
bool load_phrases(const string& filename, vector<string>& out_phrases) {
    ifstream ifs(filename);
    if (!ifs.is_open()) {
        cerr << "No se pudo abrir el archivo de frases: " << filename << "\n";
        return false;
    }
    string line;
    while (std::getline(ifs, line)) {
        if (!line.empty()) {
            out_phrases.push_back(line);
        }
    }
    ifs.close();
    return !out_phrases.empty();
}

// ------------------------------
// Crea la versión “enmascarada” de la frase:
// Reemplaza cada caracter distinto de espacio por '_'
// Mantiene los espacios tal como están.
// ------------------------------
void init_masked(const string& phrase, char* masked_out) {
    size_t len = phrase.size();
    for (size_t i = 0; i < len && i < MAX_PHRASE_LEN-1; ++i) {
        if (phrase[i] == ' ') {
            masked_out[i] = ' ';
        } else {
            masked_out[i] = '_';
        }
    }
    masked_out[len] = '\0';
}

// ------------------------------
// Actualiza el masked_out con la letra guess: si coincide en alguna posición, 
// la revela; devuelve true si la letra estaba en la frase, false si no.
// ------------------------------
bool apply_guess(const char* phrase, char* masked_out, char guess) {
    bool found = false;
    for (int i = 0; phrase[i] != '\0'; ++i) {
        if (tolower(phrase[i]) == tolower(guess)) {
            masked_out[i] = phrase[i]; // revela con la letra original (respeta mayúsculas/minúsculas)
            found = true;
        }
    }
    return found;
}

// ------------------------------
// Verifica si la frase está completamente adivinada:
// ------------------------------
bool is_fully_guessed(const char* masked_out) {
    for (int i = 0; masked_out[i] != '\0'; ++i) {
        if (masked_out[i] == '_') return false;
    }
    return true;
}

// ------------------------------
// Main del Servidor
// ------------------------------
int main(int argc, char* argv[]) {
    // 1) Parseo de argumentos: -a archivo -c cantidad
    string file_phrases;
    int max_attempts = -1;
    int opt;
    while ((opt = getopt(argc, argv, "a:c:h")) != -1) {
        switch (opt) {
            case 'a': file_phrases = optarg; break;
            case 'c': max_attempts = atoi(optarg); break;
            case 'h':
            default:
                cout << "Uso: " << argv[0] << " -a <archivo_frases> -c <cantidad_intentos>\n";
                return 0;
        }
    }
    if (file_phrases.empty() || max_attempts <= 0) {
        cerr << "Parámetros obligatorios faltantes.\n"
             << "Uso: " << argv[0] << " -a <archivo_frases> -c <cantidad_intentos>\n";
        return 1;
    }

    // 2) Configuro manejo de señales: ignoro SIGINT y manejo SIGUSR1 y SIGUSR2
    struct sigaction sa_int{}, sa_usr1{}, sa_usr2{};
    sa_int.sa_handler  = sigint_handler;
    sa_usr1.sa_handler = sigusr1_handler;
    sa_usr2.sa_handler = sigusr2_handler;
    sigemptyset(&sa_int.sa_mask);
    sigemptyset(&sa_usr1.sa_mask);
    sigemptyset(&sa_usr2.sa_mask);
    sa_int.sa_flags    = 0;
    sa_usr1.sa_flags   = 0;
    sa_usr2.sa_flags   = 0;
    sigaction(SIGINT,  &sa_int,  nullptr);
    sigaction(SIGUSR1, &sa_usr1, nullptr);
    sigaction(SIGUSR2, &sa_usr2, nullptr);

    // 3) Intento crear el semáforo de “lock” para asegurar que sólo haya 1 servidor:
    sem_unlink(SEM_SERVER_LOCK); // Por si quedó de corridas anteriores
    sem_server_lock = sem_open(SEM_SERVER_LOCK, O_CREAT | O_EXCL, 0600, 1);
    if (sem_server_lock == SEM_FAILED) {
        cerr << "Ya existe un servidor corriendo (no se pudo crear " << SEM_SERVER_LOCK << ").\n";
        return 1;
    }

    // 4) Creo o abro memoria compartida:
    shm_unlink(SHM_NAME); // Por si quedó de corridas anteriores
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (shm_fd < 0) {
        cerr << "Error al abrir memoria compartida: " << strerror(errno) << "\n";
        sem_unlink(SEM_SERVER_LOCK);
        return 1;
    }
    // Tamaño para un único SharedData
    if (ftruncate(shm_fd, sizeof(SharedData)) < 0) {
        cerr << "Error en ftruncate: " << strerror(errno) << "\n";
        close(shm_fd);
        sem_unlink(SEM_SERVER_LOCK);
        shm_unlink(SHM_NAME);
        return 1;
    }
    void* addr = mmap(nullptr, sizeof(SharedData),
                      PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (addr == MAP_FAILED) {
        cerr << "Error en mmap: " << strerror(errno) << "\n";
        close(shm_fd);
        sem_unlink(SEM_SERVER_LOCK);
        shm_unlink(SHM_NAME);
        return 1;
    }
    shm_ptr = reinterpret_cast<SharedData*>(addr);

    // 5) Inicializo el contenido de SharedData en 0:
    memset(shm_ptr, 0, sizeof(SharedData));

    // 6) Creo semáforos de sincronización:
    sem_unlink(SEM_CLIENT_MUTEX);
    sem_unlink(SEM_CLIENT_READY);
    sem_unlink(SEM_PHRASE_READY);
    sem_unlink(SEM_LETTER);
    sem_unlink(SEM_UPDATE);

    sem_client_mutex = sem_open(SEM_CLIENT_MUTEX,
                                O_CREAT | O_EXCL, 0600, 1);
    sem_client_ready = sem_open(SEM_CLIENT_READY,
                                O_CREAT | O_EXCL, 0600, 0);
    sem_phrase_ready = sem_open(SEM_PHRASE_READY,
                                O_CREAT | O_EXCL, 0600, 0);
    sem_letter       = sem_open(SEM_LETTER,
                                O_CREAT | O_EXCL, 0600, 0);
    sem_update       = sem_open(SEM_UPDATE,
                                O_CREAT | O_EXCL, 0600, 0);
    if (sem_client_mutex == SEM_FAILED ||
        sem_client_ready == SEM_FAILED ||
        sem_phrase_ready == SEM_FAILED ||
        sem_letter       == SEM_FAILED ||
        sem_update       == SEM_FAILED) {
        cerr << "Error al crear semáforos de IPC.\n";
        // Cleanup parcial:
        sem_unlink(SEM_SERVER_LOCK);
        sem_unlink(SEM_CLIENT_MUTEX);
        sem_unlink(SEM_CLIENT_READY);
        sem_unlink(SEM_PHRASE_READY);
        sem_unlink(SEM_LETTER);
        sem_unlink(SEM_UPDATE);
        munmap(shm_ptr, sizeof(SharedData));
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return 1;
    }

    // 7) Cargo las frases desde el archivo:
    vector<string> phrases;
    if (!load_phrases(file_phrases, phrases)) {
        cerr << "No se cargaron frases (¿archivo vacío o no existe?).\n";
        // Cleanup:
        sem_unlink(SEM_SERVER_LOCK);
        sem_unlink(SEM_CLIENT_MUTEX);
        sem_unlink(SEM_CLIENT_READY);
        sem_unlink(SEM_PHRASE_READY);
        sem_unlink(SEM_LETTER);
        sem_unlink(SEM_UPDATE);
        munmap(shm_ptr, sizeof(SharedData));
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return 1;
    }

    cout << "Servidor iniciado. Esperando clientes...\n";

    // 8) Bucle principal: atiendo clientes uno por uno
    srand(time(nullptr));
    while (true) {
        // Antes de esperar, verifico si me pidieron terminar sin partida en curso:
        if (terminate_when_free && !game_in_progress) {
            // sale inmediatamente
            break;
        }

        // Espero a que un cliente pida “conexión”: 
        // El cliente hará sem_wait(SEM_CLIENT_MUTEX) y luego sem_post(SEM_CLIENT_READY)
        if (sem_wait(sem_client_ready) < 0) {
            if (errno == EINTR) {
                // Si fue interrumpido por señal, reviso banderas
                if (terminate_now) {
                    // Fuerzo salida inmediata
                    break;
                }
                continue;
            } else {
                perror("sem_wait(SEM_CLIENT_READY)");
                break;
            }
        }
        // En este punto, un cliente se “conectó”. Bloqueo el mutex de cliente para nadie más se conecte
        // (ya lo hizo el cliente con sem_wait en su lado).
        // Pongo bandera de partida en curso:
        game_in_progress = true;

        // Inicializo la memoria compartida para esta partida:
        //  - el cliente ya escribió su nickname en shm_ptr->nickname antes de postear SEM_CLIENT_READY.
        //  - copio max_attempts
        //  - el resto lo completo aquí.
        shm_ptr->max_attempts  = max_attempts;
        shm_ptr->attempts_left = max_attempts;
        shm_ptr->guess         = '\0';
        shm_ptr->game_state    = 0;
        shm_ptr->result        = -1;
        shm_ptr->start_time    = time(nullptr);
        shm_ptr->end_time      = 0;

        // Selección aleatoria de una frase:
        int idx = rand() % static_cast<int>(phrases.size());
        strncpy(shm_ptr->phrase, phrases[idx].c_str(), MAX_PHRASE_LEN-1);
        shm_ptr->phrase[MAX_PHRASE_LEN-1] = '\0';

        // Creo la versión “enmascarada”
        init_masked(shm_ptr->phrase, shm_ptr->masked);

        // Cambio estado a 1: frase lista para que el cliente la lea
        shm_ptr->game_state = 1;

        // Aviso al cliente: frase lista
        if (sem_post(sem_phrase_ready) < 0) {
            perror("sem_post(SEM_PHRASE_READY)");
        }

        // 9) Bucle de juego: atender letras del cliente
        while (true) {
            // Si recibí SIGUSR2 y quieren forzar fin inmediato, salgo del juego YA:
            if (terminate_now) {
                // Marcaré el juego como terminado con derrota automática
                shm_ptr->result = 1; // perdió
                shm_ptr->game_state = 3;
                shm_ptr->end_time = time(nullptr);
                // Aviso al cliente que el juego terminó:
                sem_post(sem_update);
                break;
            }
            // Semáforo: espero a que el cliente envie una letra
            if (sem_wait(sem_letter) < 0) {
                if (errno == EINTR) {
                    // se interrumpió con señal
                    if (terminate_now) {
                        shm_ptr->result = 1;
                        shm_ptr->game_state = 3;
                        shm_ptr->end_time = time(nullptr);
                        sem_post(sem_update);
                        break;
                    }
                    continue;
                } else {
                    perror("sem_wait(SEM_LETTER)");
                    break;
                }
            }
            // El cliente ya puso su letra en shm_ptr->guess. La leo:
            char letra = shm_ptr->guess;
            bool found = apply_guess(shm_ptr->phrase, shm_ptr->masked, letra);
            if (!found) {
                shm_ptr->attempts_left--;
            }
            // Reviso si ganó:
            bool win = is_fully_guessed(shm_ptr->masked);
            if (win) {
                shm_ptr->result = 0; // ganó
                shm_ptr->game_state = 3;
                shm_ptr->end_time = time(nullptr);
            } else if (shm_ptr->attempts_left <= 0) {
                shm_ptr->result = 1; // perdió
                shm_ptr->game_state = 3;
                shm_ptr->end_time = time(nullptr);
            } else {
                shm_ptr->result = -1;   // sigue en curso
                shm_ptr->game_state = 2; // actualización lista
            }

            // Aviso al cliente: tengo actualización lista
            if (sem_post(sem_update) < 0) {
                perror("sem_post(SEM_UPDATE)");
            }

            // Si el juego terminó, salgo del bucle y registro tiempo/ranking
            if (shm_ptr->game_state == 3) {
                break;
            }
        }

        // 10) La partida finalizó. Registro tiempo del cliente:
        double elapsed = difftime(shm_ptr->end_time, shm_ptr->start_time);
        const char* nick = shm_ptr->nickname;
        cout << "\nPartida de \"" << nick << "\" finalizada. ";
        if (shm_ptr->result == 0) {
            cout << "¡Ganó en " << elapsed << " segundos!\n";
        } else {
            cout << "Perdió. Frase: \"" << shm_ptr->phrase << "\". Tiempo: " << elapsed << " segundos.\n";
        }

        // Actualizo ranking si ganó y mejora el mejor tiempo:
        if (shm_ptr->result == 0) {
            if (best_time < 0.0 || elapsed < best_time) {
                best_time = elapsed;
                strncpy(best_nick, nick, MAX_NICK_LEN-1);
                best_nick[MAX_NICK_LEN-1] = '\0';
            }
        }

        // Liberar mutex de cliente para que otro Cliente pueda conectar:
        game_in_progress = false;
        sem_post(sem_client_mutex);

        // Si recibimos SIGUSR1 y no queremos nuevas partidas, salimos:
        if (terminate_when_free) {
            break;
        }
    }

    // 11) Antes de terminar, imprimo el ranking y hago cleanup:
    print_ranking_and_cleanup();

    // Cierro semáforos y los desasigno:
    sem_close(sem_server_lock);
    sem_close(sem_client_mutex);
    sem_close(sem_client_ready);
    sem_close(sem_phrase_ready);
    sem_close(sem_letter);
    sem_close(sem_update);

    sem_unlink(SEM_SERVER_LOCK);
    sem_unlink(SEM_CLIENT_MUTEX);
    sem_unlink(SEM_CLIENT_READY);
    sem_unlink(SEM_PHRASE_READY);
    sem_unlink(SEM_LETTER);
    sem_unlink(SEM_UPDATE);

    // Desmapeo y destruyo memoria compartida
    munmap(shm_ptr, sizeof(SharedData));
    close(shm_fd);
    shm_unlink(SHM_NAME);

    cout << "Servidor finalizado.\n";
    return 0;
}
