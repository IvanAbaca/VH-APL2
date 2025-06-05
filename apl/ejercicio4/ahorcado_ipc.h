// ahorcado_ipc.h

#ifndef AHORCADO_IPC_H
#define AHORCADO_IPC_H

#include <ctime>
#include <cstring>

static constexpr const char* SHM_NAME        = "/ahorcado_shm";
static constexpr const char* SEM_SERVER_LOCK = "/sem_ahorcado_server_lock";
static constexpr const char* SEM_CLIENT_MUTEX= "/sem_ahorcado_client_mutex";
static constexpr const char* SEM_CLIENT_READY= "/sem_ahorcado_client_ready";
static constexpr const char* SEM_PHRASE_READY= "/sem_ahorcado_phrase_ready";
static constexpr const char* SEM_LETTER      = "/sem_ahorcado_letter";
static constexpr const char* SEM_UPDATE      = "/sem_ahorcado_update";

static constexpr int MAX_PHRASE_LEN  = 256;
static constexpr int MAX_NICK_LEN    = 32;

// game_state: indica en qué paso está el juego
//   0 = esperando frase lista (cliente todavía no recibió nada)
//   1 = frase enviada y esperando letra del cliente
//   2 = actualización de estado lista (Servidor ya procesó letra)
//   3 = juego terminado, resultado disponible
// result: válido sólo si game_state == 3
//   0 = ganó el cliente
//   1 = se acabaron los intentos (cliente perdió)
struct SharedData {
    char    nickname[MAX_NICK_LEN];    // Nickname del cliente
    char    phrase[MAX_PHRASE_LEN];    // Frase completa a adivinar
    char    masked[MAX_PHRASE_LEN];    // Frase con guiones bajos y letras adivinadas
    char    guess;                     // Letra enviada por el cliente
    int     attempts_left;             // Intentos que quedan
    int     max_attempts;              // Cantidad inicial de intentos (se copia desde parámetro)
    int     game_state;                // Estado del juego (ver arriba)
    int     result;                    // Resultado final (0=win, 1=lose), válido si game_state==3
    time_t  start_time;                // Timestamp al inicio de la partida
    time_t  end_time;                  // Timestamp al final de la partida
};

#endif // AHORCADO_IPC_H
