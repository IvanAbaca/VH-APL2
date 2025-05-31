/*
43262563 Abaca, Ivan
43971511 Guardati, Francisco
43780360 Romero, Lucas Nicolas
45542385 Palmieri, Marco
41566741 Piegari, Lucila Quiroga
*/

#include <iostream>
#include <fstream>
#include <random>
#include <iomanip>
#include <string>

using namespace std;
namespace fs = std::filesystem;

// Ayuda y parámetros por línea de comandos
void mostrar_ayuda() {
    std::cout << "Uso: ./ejercicio2 [opciones]\n"
              << "Opciones:\n"
              << "  -d  --directorio <path>         Ruta del directorio a analizar (requerido)\n"
              << "  -g  --generadores <número>      Cantidad de threads a ejecutar concurrentemente para generarlos
                                                    archivos del directorio (Requerido). El número ingresado debe ser un
                                                    entero positivo.\n"
              << "  -c  --consumidores <número>     Cantidad de threads a ejecutar concurrentemente para procesar los
                                                    archivos del directorio (Requerido). El número ingresado debe ser un
                                                    entero positivo.\n"
              << "  -p  --paquetes <número>         Cantidad de paquetes a generar (Requerido). El número ingresado
                                                    debe ser un entero positivo.\n"
              << "  -h  --help                      Muestra esta ayuda\n"
              << endl;
}

struct Args {
    std::string dir;
    int generadores = 0;
    int consumidores = 0;
    int paquetes = 0;
};

// Inicialización y limpieza de directorio
void inicializarDirectorios(const std::string& path) {
    if (!fs::exists(path)) {
        std::cout << "El directorio no existe. Creando: " << path << "\n";
        fs::create_directories(path);
        return;
    }

    // se eliminan los archivos .paq del directorio
    for (auto& p : fs::directory_iterator(path)) {
        if (p.is_regular_file() && p.path().extension() == ".paq") {
            fs::remove(p.path());
        }
    }

    // se crea el directorio 'procesados' para no volver a leer los archivos procesados
    fs::path procesadosDir = fs::path(path) / "procesados";
    if (!fs::exists(procesadosDir)) {
        fs::create_directory(procesadosDir);
    }
}

//Generar archivos de paquetes
int generarArchivos() {
    // Crear el directorio ".files" si no existe
    fs::path dir = ".files";
    if (!fs::exists(dir)) {
        fs::create_directory(dir);
    }

    // Generador aleatorio
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> peso_dist(0.0, 300.0);
    std::uniform_int_distribution<> destino_dist(1, 50);

    for (int id = 1; id <= 1000; ++id) {
        // Ruta del archivo dentro del directorio .files
        std::string filename = (dir / (std::to_string(id) + ".paq")).string();

        // Crear archivo y escribir contenido
        std::ofstream file(filename);
        if (!file) {
            std::cerr << "No se pudo abrir el archivo " << filename << "\n";
            return 1;
        }

        double peso = peso_dist(gen);
        int destino = destino_dist(gen);

        file << id << ";" << std::fixed << std::setprecision(2) << peso << ";" << destino << "\n";
        file.close();
    }

    std::cout << "Archivos generados en el directorio .files\n";
    return 0;
}

// Thread Generadores

// Thread Consumidores

// Buffer virtual compartido

// Sincronización (mutex + condición)

// Procesamiento y resumen