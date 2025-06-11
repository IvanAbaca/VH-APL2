#include <ctime>
#include <cerrno>

#include "ahorcado.h"

volatile sig_atomic_t terminar_sig1 = 0;
volatile sig_atomic_t terminar_sig2 = 0;


void handle_sigusr1(int) { terminar_sig1 = 1; }
void handle_sigusr2(int) { terminar_sig2 = 1; }

vector<string> leer_frases(const string& nombre_archivo){
    vector<string> frases;
    ifstream archivo(nombre_archivo);

    if(!archivo.is_open()){
        cerr << "[Servidor] Error: No se pudo abrir el archivo de frases (" << nombre_archivo << ")" << endl;
        cerr << "[Servidor] Abortando servidor..." <<endl;
        exit(1);
    }

    if(archivo.peek() == std::ifstream::traits_type::eof()){
        cerr << "[Servidor] Error: El archivo de frases ("<< nombre_archivo << ") se encuentra vacio" << endl;
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

string toLowerString(const string& input) {
    string result = input;
    transform(result.begin(), result.end(), result.begin(),
              [](unsigned char c) { return tolower(c); });
    return result;
}

bool esFraseCorrecta(const string fraseOriginal, const string fraseSugerida) {
    return toLowerString(fraseOriginal) == toLowerString(fraseSugerida);
}

void help(){
    cout << "Ayuda" << endl;
}


void mostrarRanking(const vector<rankingEntry>& ranking) {

    vector<rankingEntry> ordenado = ranking;
    sort(ordenado.begin(), ordenado.end(), [](const rankingEntry& a, const rankingEntry& b) {
        return a.tiempo_segundos < b.tiempo_segundos;
    });

    cout << "\n=========  RANKING DE JUGADORES  =========\n\n";
    cout << left << setw(5) << "#" 
         << setw(15) << "Jugador" 
         << setw(25) << "Frase Adivinada" 
         << setw(12) << "Tiempo (mm:ss)"
         << "\n" << string(60, '-') << "\n";

    for (size_t i = 0; i < ordenado.size(); ++i) {
        int minutos = static_cast<int>(ordenado[i].tiempo_segundos) / 60;
        double segundos_restantes = ordenado[i].tiempo_segundos - (minutos * 60);

        stringstream tiempo_formateado;
        tiempo_formateado << setfill('0') << setw(2) << minutos << ":"
                        << fixed << setprecision(2)
                        << setw(5) << segundos_restantes;

        cout << left << setw(5) << (i + 1)
            << setw(15) << ordenado[i].nickname
            << setw(25) << ordenado[i].frase.substr(0, 24)
            << setw(10) << tiempo_formateado.str()
            << endl;
    }

    cout << "\n==============================================\n";
}


int main(int argc, char* argv[]) {

    string archivo;
    int intentos = -1;

    //tomo los parametros
    for(int i = 1; i < argc; i++){
        string arg = argv[i];
        if ((arg == "-a" || arg == "--archivo") && i + 1 < argc) {
            archivo = argv[++i];
        }
        else if ((arg == "-c" || arg == "--cantidad") && i + 1 < argc) {
            intentos = stoi(argv[++i]);
        }
        else if (arg == "-h" || arg == "--help") {
            help();   
            return 0;
        }
    }

    if(archivo.empty()){
        cerr << "[Servidor] Error: archivo de frases no especificado." << endl;
        help();
        return 1;
    }
    else if (intentos == -1){
        cerr << "[Servidor] Error: debe especificar la cantidad de intentos." << endl;
        help();
        return 1;
    }
    
    vector<string> frases = leer_frases(archivo);
    vector<rankingEntry> ranking;
    
    //valido que no exista otro servidor corriendo usando un lockfile
    int fd = open("/tmp/ahorcado_server.lock", O_CREAT | O_EXCL, 0644);
    if (fd == -1) {
        cerr << "[Servidor] Error: Ya hay un servidor en ejecución, abortando iniciación." << endl;
        exit(1);
    }

    //guardo el pid en el lockfile para que el cliente pueda verificar si el proceso sigue vivo
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    write(fd, pid_str, strlen(pid_str));
    fsync(fd);

    cout << "[Servidor] Inicializando servidor..." << endl;
    srand(time(nullptr));

    // Registrar señales
    signal(SIGUSR1, handle_sigusr1);
    signal(SIGUSR2, handle_sigusr2);

    // Compartir memoria y abrir semáforos
    key_t key = ftok("shmfile", 65);
    int shmid = shmget(key, SHM_SIZE, 0666 | IPC_CREAT);
    void* data = shmat(shmid, nullptr, 0);
    juegoCompartido* juego = static_cast<juegoCompartido*>(data);

    sem_t* sem_mutex = sem_open(SEM_MUTEX_NAME, O_CREAT, 0666, 1);
    sem_t* sem_letra_lista = sem_open(SEM_LETRA_LISTA_NAME, O_CREAT, 0666, 0);
    sem_t* sem_resultado_listo = sem_open(SEM_RESULTADO_LISTO_NAME, O_CREAT, 0666, 0);
    sem_t* sem_nuevo_cliente = sem_open(SEM_NUEVO_CLIENTE_NAME, O_CREAT, 0666, 0);
    sem_t* sem_frase_lista = sem_open(SEM_FRASE_LISTA_NAME, O_CREAT, 0666, 0);
    sem_t* sem_opcion_lista = sem_open(SEM_OPCION_LISTA_NAME, O_CREAT, 0666, 0);
    sem_t* sem_inicio_op1 = sem_open(SEM_INICIO_1_NAME, O_CREAT, 0666, 0);
    sem_t* sem_inicio_op2 = sem_open(SEM_INICIO_2_NAME, O_CREAT, 0666, 0);
    sem_t* sem_frase_intento_lista = sem_open(SEM_FRASE_I_NAME, O_CREAT, 0666, 0);
    
    cout << "[Servidor] Inicialización finalizada" << endl;

    while (true) {
        if (terminar_sig2) break;

        timespec ts; //espero un nuevo cliente con timeout para chequear señales
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        int wait_result = sem_timedwait(sem_nuevo_cliente, &ts); 
        if (wait_result == -1) {
            if (errno == ETIMEDOUT || errno == EINTR){
                if(terminar_sig1){
                    break;
                }
                continue;
            }
            perror("[Servidor] Error en sem_timedwait");
            break;
        }

        rankingEntry jugador;

        bool juego_terminado = false; //iniciar nueva partida
        int indice = rand() % frases.size();
        string frase_original = frases[indice];
        string frase_ofuscada = ofuscarFrase(frase_original);

        sem_wait(sem_mutex);
        strncpy(juego->frase, frase_original.c_str(), 128);
        strncpy(juego->progreso, frase_ofuscada.c_str(), 128);
        juego->intentos_restantes = intentos;
        juego->juego_terminado = false;
        jugador.frase = frase_original;
        jugador.nickname = juego->usuario_nickname;
        sem_post(sem_mutex);
        sem_post(sem_frase_lista);

        auto ini = chrono::steady_clock::now();

        while (!juego_terminado) {
            if (terminar_sig2) {
                cout << "[Servidor] Terminando partida actual por SIGUSR2...\n";
                sem_wait(sem_mutex);
                juego->juego_terminado = true;
                strncpy(juego->progreso, frase_original.c_str(), 128);
                sem_post(sem_mutex);
                sem_post(sem_resultado_listo);
                juego_terminado = true;
                break;
            }

            sem_wait(sem_opcion_lista);

            switch (juego->opcion) {
                //opcion 1: adivinar letra
                case '1': {
                    sem_post(sem_inicio_op1);
                    sem_wait(sem_letra_lista);

                    sem_wait(sem_mutex);
                    char letra = juego->letra_sugerida;
                    sem_post(sem_mutex);

                    cout << "[Servidor] Letra recibida: " << letra << endl;

                    if (!descubrirLetra(frase_original, frase_ofuscada, letra)) {
                        cout << "[Servidor] Letra incorrecta. Restando intento.\n";
                        sem_wait(sem_mutex);
                        if (--juego->intentos_restantes == 0) {
                            juego->juego_terminado = true;
                            juego_terminado = true;
                        }
                        sem_post(sem_mutex);
                    }

                    cout << "[Servidor] Frase actualizada: " << frase_ofuscada << endl;

                    sem_wait(sem_mutex);
                    strncpy(juego->progreso, frase_ofuscada.c_str(), 128);
                    sem_post(sem_mutex);

                    sem_post(sem_resultado_listo);
                    break;
                }

                //opcion 2: adivinar frase 
                case '2': {
                    sem_post(sem_inicio_op2);
                    sem_wait(sem_frase_intento_lista);

                    char f_sugerida[128];
                    sem_wait(sem_mutex);
                    strncpy(f_sugerida, juego->frase_sugerida, 128);
                    cout << "Frase recibida: " << f_sugerida << endl;
                    sem_post(sem_mutex);

                    if (esFraseCorrecta(frase_original, f_sugerida)) {
                        cout << "[Servidor] Frase sugerida correcta, finalizando partida." << endl;
                        auto fin = chrono::steady_clock::now();
                        chrono::duration<double> duracion = fin - ini;
                        jugador.tiempo_segundos = duracion.count();
                        sem_wait(sem_mutex);
                        juego->juego_terminado = true;
                        juego->victoria = true;
                        sem_post(sem_mutex);
                        juego_terminado = true;
                    }
                    else {
                        cout << "[Servidor] Frase sugerida incorrecta, restando intento." << endl;
                        sem_wait(sem_mutex);
                        if (--juego->intentos_restantes == 0) {
                            auto fin = chrono::steady_clock::now();
                            chrono::duration<double> duracion = fin - ini;
                            jugador.tiempo_segundos = duracion.count();
                            juego->juego_terminado = true;
                            juego->victoria = false;
                            juego_terminado = true;
                        }
                        sem_post(sem_mutex);
                    }
                    sem_post(sem_resultado_listo);
                    break;
                }
                case '3': {
                    sem_post(sem_inicio_op1);
                    sem_wait(sem_mutex);
                    juego->victoria = false;
                    juego->juego_terminado = true;
                    juego_terminado = true;
                    sem_post(sem_mutex);
                    sem_post(sem_resultado_listo);
                }
            }
        }
        
        ranking.push_back(jugador);

        if (terminar_sig1) {
            cout << "[Servidor] Terminación suave solicitada. Cerrando luego de la partida...\n";
            break;
        }

    }


    if(ranking.empty()){
        cout << "[Servidor] No se registraron partidas" << endl;
    }
    else {
        mostrarRanking(ranking);
    }


    // Liberar recursos
    cout << "[Servidor] Finalizando, liberando recursos...\n";

    sem_close(sem_mutex);
    sem_close(sem_letra_lista);
    sem_close(sem_resultado_listo);
    sem_close(sem_nuevo_cliente);
    sem_close(sem_frase_lista);
    sem_close(sem_opcion_lista);
    sem_close(sem_inicio_op1);
    sem_close(sem_inicio_op2);
    sem_close(sem_frase_intento_lista);

    sem_unlink(SEM_MUTEX_NAME);
    sem_unlink(SEM_LETRA_LISTA_NAME);
    sem_unlink(SEM_RESULTADO_LISTO_NAME);
    sem_unlink(SEM_NUEVO_CLIENTE_NAME);
    sem_unlink(SEM_FRASE_LISTA_NAME);
    sem_unlink(SEM_OPCION_LISTA_NAME);
    sem_unlink(SEM_INICIO_1_NAME);
    sem_unlink(SEM_INICIO_2_NAME);
    sem_unlink(SEM_FRASE_I_NAME);


    //libero el lockfile para permitir crear otro servidor
    close(fd);
    unlink("/tmp/ahorcado_server.lock");


    shmdt(data);
    shmctl(shmid, IPC_RMID, nullptr);

    return 0;
}
