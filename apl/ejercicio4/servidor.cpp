#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <csignal>
#include <ctime>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <set>
#include <fcntl.h>
#include <sys/file.h> 
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <semaphore.h>
#include <algorithm>

#include "ahorcado_ipc.h"

using namespace std;

// Variables para el ranking completo:
struct Result { string nick; double time; bool won; };
static vector<Result> results;

// Variables para la señalización:
static volatile sig_atomic_t terminate_now        = 0;
static volatile sig_atomic_t terminate_when_free = 0;

// Memoria y semáforos:
static SharedData* shm_ptr = nullptr;
static int shm_fd = -1;
static sem_t *sem_server_lock = SEM_FAILED;
static sem_t *sem_client_mutex= SEM_FAILED;
static sem_t *sem_client_ready= SEM_FAILED;
static sem_t *sem_phrase_ready= SEM_FAILED;
static sem_t *sem_letter      = SEM_FAILED;
static sem_t *sem_update      = SEM_FAILED;

static bool game_in_progress = false;

void sigint_handler(int signo){ (void)signo; }
void sigusr1_handler(int signo){ (void)signo; terminate_when_free = 1; }
void sigusr2_handler(int signo){ (void)signo; terminate_now = 1; }

void print_full_ranking_and_cleanup() {
    cout << "\n=== Ranking completo ===\n";
    if (results.empty()) {
        cout << "No hubo partidas jugadas.\n";
    } else {
        
        auto sorted = results;
        sort(sorted.begin(), sorted.end(),
             [](const Result &a, const Result &b) {
                 if (a.won != b.won) return a.won > b.won;
                 return a.time < b.time;
             });

        int idx = 1;
        for (auto &r : sorted) {
            cout << idx++ << ". " << r.nick
                 << " – " << (r.won ? "Ganó" : "Perdió")
                 << " en " << r.time << " seg\n";
        }
    }
    cout << "=========================\n\n";
}


bool load_phrases(const string& filename, vector<string>& out_phrases) {
    ifstream ifs(filename);
    if (!ifs.is_open()) { cerr<<"No se pudo abrir "<<filename<<"\n"; return false; }
    string line;
    while (getline(ifs, line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (!line.empty()) out_phrases.push_back(line);
    }
    return !out_phrases.empty();
}

void init_masked(const string& phrase, char* masked_out) {
    size_t len = phrase.size();
    for (size_t i=0;i<len && i<MAX_PHRASE_LEN-1;++i)
        masked_out[i] = (phrase[i]==' ' ? ' ' : '_');
    masked_out[len]='\0';
}

bool apply_guess(const char* phrase, char* masked_out, char guess) {
    bool found=false;
    for(int i=0; phrase[i]; ++i){
        if (tolower(phrase[i])==tolower(guess)) {
            masked_out[i]=phrase[i];
            found=true;
        }
    }
    return found;
}

bool is_fully_guessed(const char* masked_out) {
    for(int i=0; masked_out[i]; ++i)
        if (masked_out[i]=='_') return false;
    return true;
}

int main(int argc, char* argv[]) {

    string file_phrases; int max_attempts=-1, opt;
    while((opt=getopt(argc,argv,"a:c:h"))!=-1){
        if(opt=='a') file_phrases=optarg;
        else if(opt=='c') max_attempts=atoi(optarg);
        else { cout<<"Uso: "<<argv[0]<<" -a <archivo> -c <int> \n"; return 0;}
    }
    if(file_phrases.empty()||max_attempts<=0){
        cerr<<"Parámetros obligatorios.\n"; return 1;
    }

    struct sigaction sa_int{}, sa_usr1{}, sa_usr2{};
    sa_int.sa_handler = sigint_handler;
    sa_usr1.sa_handler= sigusr1_handler;
    sa_usr2.sa_handler= sigusr2_handler;
    sigemptyset(&sa_int.sa_mask);
    sigemptyset(&sa_usr1.sa_mask);
    sigemptyset(&sa_usr2.sa_mask);
    sigaction(SIGINT,  &sa_int,  nullptr);
    sigaction(SIGUSR1, &sa_usr1, nullptr);
    sigaction(SIGUSR2, &sa_usr2, nullptr);

    // Verifico que no haya otro servidor corriendo
    sem_unlink(SEM_SERVER_LOCK);
    sem_server_lock = sem_open(SEM_SERVER_LOCK, O_CREAT|O_EXCL, 0600, 1);
    if (sem_server_lock==SEM_FAILED){
        cerr<<"Ya hay un servidor corriendo.\n"; return 1;
    }
    int lock_fd = open("/var/lock/ahorcado_server.lock", O_CREAT|O_RDWR, 0666);
    if (lock_fd<0 || flock(lock_fd, LOCK_EX|LOCK_NB)<0){
        cerr<<"No se puede obtener el lock de archivo: ya hay un servidor.\n";
        return 1;
    }

    // Memoria compartida
    shm_unlink(SHM_NAME);
    shm_fd = shm_open(SHM_NAME, O_CREAT|O_RDWR, 0600);
    ftruncate(shm_fd, sizeof(SharedData));
    void* addr = mmap(nullptr, sizeof(SharedData),
                      PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0);
    shm_ptr = (SharedData*)addr;
    memset(shm_ptr,0,sizeof(SharedData));

    // Semáforos IPC
    sem_unlink(SEM_CLIENT_MUTEX);
    sem_unlink(SEM_CLIENT_READY);
    sem_unlink(SEM_PHRASE_READY);
    sem_unlink(SEM_LETTER);
    sem_unlink(SEM_UPDATE);
    sem_client_mutex = sem_open(SEM_CLIENT_MUTEX,O_CREAT|O_EXCL,0600,1);
    sem_client_ready = sem_open(SEM_CLIENT_READY,O_CREAT|O_EXCL,0600,0);
    sem_phrase_ready= sem_open(SEM_PHRASE_READY,O_CREAT|O_EXCL,0600,0);
    sem_letter      = sem_open(SEM_LETTER,O_CREAT|O_EXCL,0600,0);
    sem_update      = sem_open(SEM_UPDATE,O_CREAT|O_EXCL,0600,0);
    if (sem_client_mutex==SEM_FAILED||sem_client_ready==SEM_FAILED||
        sem_phrase_ready==SEM_FAILED||sem_letter==SEM_FAILED||sem_update==SEM_FAILED){
        cerr<<"Error al crear semáforos.\n"; return 1;
    }

    // Cargo frases
    vector<string> phrases;
    if (!load_phrases(file_phrases,phrases)){
        cerr<<"No se cargaron frases.\n"; return 1;
    }

    cout<<"Servidor iniciado. Esperando clientes...\n";
    srand(time(nullptr));

    while(true){
        if (terminate_when_free && !game_in_progress) break;

        if (sem_wait(sem_client_ready)<0){
            if (errno==EINTR){ if (terminate_now) break; else continue; }
            else { perror("sem_wait"); break; }
        }
        game_in_progress = true;

        // Inicializo partida
        shm_ptr->max_attempts  = max_attempts;
        shm_ptr->attempts_left = max_attempts;
        shm_ptr->guess         = '\0';
        shm_ptr->game_state    = 0;
        shm_ptr->result        = -1;
        shm_ptr->start_time    = time(nullptr);
        shm_ptr->end_time      = 0;
        int idx = rand() % phrases.size();
        strncpy(shm_ptr->phrase, phrases[idx].c_str(), MAX_PHRASE_LEN-1);
        shm_ptr->phrase[MAX_PHRASE_LEN-1]='\0';
        init_masked(shm_ptr->phrase, shm_ptr->masked);
        shm_ptr->game_state = 1;
        sem_post(sem_phrase_ready);

        set<char> used_letters;

        while(true){
            if (terminate_now){
                shm_ptr->result=1; shm_ptr->game_state=3;
                shm_ptr->end_time=time(nullptr);
                sem_post(sem_update);
                break;
            }
            if (sem_wait(sem_letter)<0){
                if (errno==EINTR && terminate_now){
                    shm_ptr->result=1; shm_ptr->game_state=3;
                    shm_ptr->end_time=time(nullptr);
                    sem_post(sem_update);
                    break;
                }
                continue;
            }
            char letra = shm_ptr->guess;
            bool already = used_letters.count(letra);
            used_letters.insert(letra);

            bool found = apply_guess(shm_ptr->phrase, shm_ptr->masked, letra);
            if (!found || already) {
                shm_ptr->attempts_left--;
            }
            bool win = is_fully_guessed(shm_ptr->masked);
            if (win) {
                shm_ptr->result=0; shm_ptr->game_state=3;
                shm_ptr->end_time=time(nullptr);
            } else if (shm_ptr->attempts_left<=0) {
                shm_ptr->result=1; shm_ptr->game_state=3;
                shm_ptr->end_time=time(nullptr);
            } else {
                shm_ptr->result=-1; shm_ptr->game_state=2;
            }
            sem_post(sem_update);
            if (shm_ptr->game_state==3) break;
        }

        // Registro resultado
        double elapsed = difftime(shm_ptr->end_time, shm_ptr->start_time);
        string nick(shm_ptr->nickname);
        cout<<"\nPartida de \""<<nick<<"\" finalizada. "
            << (shm_ptr->result==0 ? "Ganó" : "Perdió")
            << ". Tiempo: "<<elapsed<<" seg.\n";
        results.push_back({nick, elapsed, shm_ptr->result==0});

        // Libero mutex
        game_in_progress = false;
        sem_post(sem_client_mutex);
        if (terminate_when_free) break;
    }

    // Limpieza final
    print_full_ranking_and_cleanup();
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
    munmap(shm_ptr, sizeof(SharedData));
    close(shm_fd);

    cout<<"Servidor finalizado.\n";
    return 0;
}
