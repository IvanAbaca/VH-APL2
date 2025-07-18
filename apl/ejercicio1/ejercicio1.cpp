/*
43262563 Abaca, Ivan
43971511 Guardati, Francisco
43780360 Romero, Lucas Nicolas
45542385 Palmieri, Marco
41566741 Piegari, Lucila Quiroga
*/


#include <iostream>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>

using namespace std;


void mostrar_ayuda() {
    cout << "Ejercicio1: arbol de procesos\n"
        << "Opciones:\n"
        << "  -h  --help                  Muestra esta ayuda\n\n"
        <<"Importante: Las pausas pueden evitarse al presionar una tecla en la consola.\n\n"
        <<"Se muestra en un primer lugar la rama del hijo1, "
        <<"con una pausa para acceder al arbol de procesos (con ps -fea).\n"
        << " Luego se muestra la rama del hijo2 con una pausa, y al finalizar se puede observar"
        << " el arbol de procesos para ver al demonio creado.\n"
    << endl;
}


void imprimirMensaje(const string& nombre, const string& tabulation){
    cout << tabulation << "Soy el proceso " << nombre << " con PID=" << getpid() << ", mi padre es " << getppid() << endl;
}

int crearNieto(const int number){
    pid_t pid = fork();
    if (pid == 0){
        //Codigo unico para el proceso nieto
        imprimirMensaje("NIETO" + to_string(number), "        ");
        getchar(); // Espera para que el nieto no termine inmediatamente
    }else if(pid > 0){
        return pid;
    }else{
        cout << "Hubo error al crear el nieto" << endl;
    }
    return 0;
}

int crearZombie(){
    pid_t pid = fork();
    if (pid == 0)
    {
        imprimirMensaje("ZOMBIE", "        ");
        exit(EXIT_SUCCESS);
    }else if(pid > 0){
        return pid;
    }else{
        cout << "Hubo error al crear al zombie" << endl;
    }
    return 0;
}

int crearHijo1(){
    pid_t pid = fork();

    if (pid == 0){
        int status;
        imprimirMensaje("HIJO1", "    ");
        int pidnieto1 = crearNieto(1);
        if(pidnieto1>0){
            //ES HIJO1
            crearZombie(); //NO TENGO QUE HACER WAIT, POR LO TANTO NO ME INTERESA EL RETORNO
            int pidnieto2 = crearNieto(2);

            if(pidnieto2 > 0){
                //SIGUE SIENDO HIJO1
                waitpid(pidnieto2, &status, 0);
            }
            waitpid(pidnieto1, &status, 0);
            getchar();
        }
    }
    else if (pid > 0){
        //VUELVO AL PADRE
        return pid;
    }else{
        cout << "Hubo error al crear hijo1" << endl;
    }
    return 0;
}

int crearHijo2(){
    pid_t pid = fork();
    if(pid == 0){
        imprimirMensaje("HIJO2", "    ");
        pid_t piddem = fork();
        if(piddem == 0){
            //SOY DEMONIO
            imprimirMensaje("QUE SE CONVERTIRÁ EN DEMONIO", "    ");
            sleep(20);
        }else if(piddem > 0){
            //SOY HIJO2 Y TERMINO SIN ESPERAR AL DEMONIO
            exit(EXIT_SUCCESS);
        }else{
            cout << "Hubo error al crear demonio" << endl;
        }
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if(argc > 1 && (string(argv[1]) == "-h" || string(argv[1]) == "--help")){
        mostrar_ayuda();
        return EXIT_SUCCESS;
    }
    int status;
    imprimirMensaje("PADRE","");
    int pid = crearHijo1();
    if(pid > 0){
        waitpid(pid,&status,0);
        crearHijo2();
        getchar(); // Espera para que el padre no termine inmediatamente
    }
        
    wait(&status);
    return EXIT_SUCCESS;
}