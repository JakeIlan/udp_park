#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <regex.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#include "declaration.h"
#include "dexchange.h"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct tInfo {
    int port;
    char lic[10];
    int debt;
    int number;
    time_t start, end;
    int cond; // cond == 1 -> is parked; 0 -> new client, 2 -> leave request, 3 -> payed off, able to quit
} clients[5];

struct payLog {
    int client;
    int payment;
    int change;
} *pLog;

int clientQuantity = 0;
int operations = 0;

void initServerSocket(int *serverSocket, int port);

void initServerSocketWithRandomPort(int *serverSocket);

void *asyncTask(void *args);

int execClientCommand(const int socket, struct sockaddr_in *clientInfo, const struct Message *msg, char *errorString);

int parseCmd(struct Message *msg, char *errorString);

int validateCommand(const struct Message msg, char *errorString);

int parking(const int socket, struct sockaddr_in *client, char *licName, char *errorString);

int quit_client(const int socket, struct sockaddr_in *client, char *errorString);

int release_client(const int socket, struct sockaddr_in *client, char *errorString);

void *clientTimer(void *args);


int main(int argc, char **argv) {

    if (argc != 2) {
        fprintf(stdout, "%s\n%s\n", "Неверное количество аргументов!", "Необходим вызов: ./server [PORT]");
        exit(1);
    }

    int port = atoi(argv[1]);
//    rootDir = argv[2];
    int serverSocket = -1;
    initServerSocket(&serverSocket, port);

    pthread_t *workers;
    int quantityWorkers = 0;

    struct sockaddr_in connectInfo;
    struct Message msg;
    printf("Input (/help to help): \n");
    fflush(stdout);
    char buf[100];
    for (;;) {//ПРИДУМАТЬ КАК ЗАВЕРШАТЬ РАБОТУ СЕРВЕРА (ГЛОБАЛЬНЫЙ ФЛАГ?)



        if (safeSourceReadMsg(serverSocket, &connectInfo, &msg) < 0) { //тут мы получается ждем команду
            fprintf(stdout, "Проблемы с прослушиванием серверного сокета. Необходимо перезапустить сервер!\n");
            close(serverSocket); //ФИКСИТЬ ОШИБКУ КОГДА ПОЯВЯТЬСЯ ТАЙМАУТЫ
            break;
        }

        if (msg.type != CODE_CONNECT) {
            fprintf(stdout, "Получили какой-то не тот пакет в основном потоке. Нам нужен пакет с id = 1.\n");
            continue;
        }

//        pthread_mutex_lock(&mutex);
//        int exi = 0;
//        if (clientQuantity < 0) {
//            for (int i = 0; i < clientQuantity; i++) {
//                if (clients[i].port == connectInfo.sin_port) {
//                    exi = 1;
//                    printf("CLIENT FOUND\n");
//                }
//            }
//        }
//
//        if (!exi) {
//            printf("CLIENT NOT FOUND\n");
//            clients[clientQuantity].number = clientQuantity;
//            clients[clientQuantity].port = connectInfo.sin_port;
//            clients[clientQuantity].cond = 0;
//            clientQuantity++;
//        }
//        pthread_mutex_unlock(&mutex);

//        bzero(buf, 100);
//        fgets(buf, 100, stdin);
//        buf[strlen(buf) - 1] = '\0';
//
//        if (!strcmp("/quit", buf) || !strcmp("/q", buf)) {
//            fprintf(stdout, "Получена команда отключения, вырубаемся..\n");
//            break;
//        } else if (!strcmp("/lc", buf)) {
//            printf("Clients on-line:\n");
//            printf("N TIME LIC\n");
//
////        pthread_mutex_lock(&mutex);
//            for (int i = 0; i < clientQuantity; i++) {
//                clients[i].time -= time(NULL);
//                printf("%d %ld %s\n", clients[i].number, clients[i].time, clients[i].lic);
//            }
////        pthread_mutex_unlock(&mutex);
//
//            fflush(stdout);
//        }

        /*
        Каждый раз при получении команды создается новый поток для ее обработки. После обработки этой
        команды поток завершается.
        */
        //СДЕЛАТЬ ПОИСК ВОРКЕРОВ!!!!ОБЯЗАТЕЛЬНО!!!!
        //И ТУТ ОПАСНОЕ МЕСТО С ПАМЯТЬЮ МОЖЕТ НЕ УСПЕТЬ СОЗДАТЬСЯ ПОТОК И УЖЕ ПОМЕНЯТЬСЯ ПАМЯТЬ

        struct sockaddr_in tempInfo;
        bzero(&tempInfo, sizeof(struct sockaddr_in));
        memcpy(&tempInfo, &connectInfo, sizeof(struct sockaddr_in));

        workers = (pthread_t *) realloc(workers, sizeof(pthread_t) * (quantityWorkers + 1));
        if (pthread_create(&(workers[quantityWorkers]), NULL, asyncTask, (void *) &tempInfo)) {
            fprintf(stdout, "%s\n", "Не удалось создать поток для обработки задачи!");
            continue;
        }
        quantityWorkers++;
    }

    fprintf(stdout, "Ожидаем завершения работы воркеров.\n");
    for (int i = 0; i < quantityWorkers; i++) {
        pthread_join(workers[i], NULL);
    }

    close(serverSocket);
    free(workers);

    fprintf(stdout, "%s\n", "Сервер завершил работу.");
    return 0;
}


/**
Инициализация UDP сокета. Если произойдет какая-то ошибка, то программа будет
завершена вызовом функции exit().
Входные значения:
    int *serverSocket - ссылка на переменную сокета сервера;
    int port - порт, на котором сервер будет ожидать пакеты.
*/
void initServerSocket(int *serverSocket, int port) {
    struct sockaddr_in servaddr;
    fprintf(stdout, "Инициализация сервера...\n");

    /* Заполняем структуру для адреса сервера: семейство
    протоколов TCP/IP, сетевой интерфейс – любой, номер порта -
    port. Поскольку в структуре содержится дополнительное не
    нужное нам поле, которое должно быть нулевым, перед
    заполнением обнуляем ее всю */
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    *serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (*serverSocket < 0) {
        fprintf(stdout, "%s\n", "Не удалось создать сокет!");
        exit(1);
    }

    // int enable = 1;
    // if (setsockopt(serverSocket, IPPROTO_TCP, SO_REUSEADDR, &enable, sizeof(int)) < 0){
    //     fprintf(stderr, "%s\n", "setsockopt(SO_REUSEADDR) failed!");
    // }

    int resBind = bind(*serverSocket, (struct sockaddr *) &servaddr, sizeof(servaddr));
    if (resBind < 0) {
        fprintf(stdout, "%s\n", "Не удалось выполнить присваивание имени сокету!");
        close(serverSocket);
        exit(1);
    }

    fprintf(stdout, "%s\n", "Инициализация сервера прошла успешно.");
}

/*
Создает сокет на рандомном порту.
*/
void initServerSocketWithRandomPort(int *serverSocket) {
    struct sockaddr_in servaddr;
    fprintf(stdout, "Инициализация сокета на рандомном порту...\n");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    *serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (*serverSocket < 0) {
        fprintf(stdout, stderr, "%s\n", "Не удалось создать сокет на рандомном порту!");
        exit(1);
    }

    int resBind = bind(*serverSocket, (struct sockaddr *) &servaddr, sizeof(servaddr));
    if (resBind < 0) {
        fprintf(stdout, "%s\n", "Не удалось выполнить присваивание имени сокету на рандомном порту!");
        close(serverSocket);
        exit(1);
    }

    fprintf(stdout, "%s\n", "Инициализация сокета на рандомном порту прошла успешно.");
}

/**
Функция обрабокти клиента. Вызывается в новом потоке.
Входные значения:
    void* args - аргумент переданный в функцию при создании потока.
        В данном случае сюда приходит int *indexClient - номер клиента.
Возвращаемое значение:
    void* - так надо, чтоб вызывать функцию при создании потока.
*/
void *asyncTask(void *args) {
    struct sockaddr_in clientInfo = *((struct sockaddr_in *) args);

    fprintf(stdout, "Запущена задача клиента IP - %s  PORT - %d.\n", inet_ntoa(clientInfo.sin_addr),
            clientInfo.sin_port);

    int tempSocket = -1;
    initServerSocketWithRandomPort(&tempSocket);

    if (safeSendMsg(tempSocket, clientInfo, CODE_CONNECT, "CONNECT_OK", strlen("CONNECT_OK")) < 0) {
        fprintf(stdout, "Запущена задача клиента IP - %s  PORT - %d не выполнена!\n", inet_ntoa(clientInfo.sin_addr),
                clientInfo.sin_port);
        close(tempSocket);
//        return;
    }

    fprintf(stdout, "Соединение, должно быть, установилось.\n");

    struct Message msg;
    if (safeReadMsg(tempSocket, &clientInfo, &msg) < 0) {
        fprintf(stdout, "Не смогли считать команду от клиента в задаче!\n");
        close(tempSocket);
//        return;
    }

//    printf("11\n");
//    pthread_mutex_lock(&mutex);
//    int exi = 0;
//    if (clientQuantity < 0) {
//        for (int i = 0; i < clientQuantity; i++) {
//            if (clients[i].port == clientInfo.sin_port) {
//                exi = 1;
//            }
//        }
//    }
//
////    printf("12\n");
//
//    if (!exi) {
//        clients[clientQuantity].number = clientQuantity;
//        clients[clientQuantity].port = clientInfo.sin_port;
//        clients[clientQuantity].cond = 0;
//        if (pthread_create(&(clients[clientQuantity].timerThd), NULL, clientTimer, (void *) &clientQuantity)) {
//            printf("ERROR: Can't create timer thread for client!\n");
//            fflush(stdout);
//        } else printf("Created timer thread\n");
//        clientQuantity++;
//    }
//    pthread_mutex_unlock(&mutex);
//    printf("22\n");

    char errorString[SIZE_PACK_DATA] = {0};
    if (execClientCommand(tempSocket, &clientInfo, &msg, errorString) == -1) {
        fprintf(stdout, "Ошибка обработки команды клиента!\n");

        if (safeSendMsg(tempSocket, clientInfo, CODE_ERROR, &errorString, sizeof(errorString)) < 0) {
            fprintf(stdout, "Не смогли отправить сообщение об ошибке!\n");
            close(tempSocket);
//            return;
        }
    }

//    if (safeSendMsg(tempSocket, clientInfo, CODE_OK, "OK", 2) < 0) {
//        fprintf(stdout, "Не смогли отправить сообщение усешной обработки команды!\n");
//        close(tempSocket);
////        return;
//    }

    close(tempSocket);

    fprintf(stdout, "Команда обработана! Сокет закрыт, воркер завершил работу.\n");
}

int execClientCommand(const int socket, struct sockaddr_in *clientInfo, const struct Message *msg, char *errorString) {
    bzero(errorString, sizeof(errorString));

    if (parseCmd(msg, errorString) == -1) {
        fprintf(stdout, "Не удалось распарсить команду клиента: %s\nОписание ошибки: %s\n", msg->data, errorString);
        return -1;
    }

//    printf("31\n");

    if (validateCommand(*msg, errorString) == -1) {
        fprintf(stdout, "Команда клиента: %s - неккоретна!\nОписание ошибки: %s\n", msg->data, errorString);
        return -1;
    }

    fprintf(stdout, "Команда клиента: %s - корректна.\n", msg->data);


    if (!strcmp(msg->argv[0], "/park")) {
        fprintf(stdout, "Exec park %s\n", msg->data);
        return parking(socket, clientInfo, msg->argv[1], errorString);
    } else if (!strcmp(msg->argv[0], "/release")) {
        fprintf(stdout, "Exec release %s\n", msg->data);
        return release_client(socket, clientInfo, errorString);
    } else if (!strcmp(msg->argv[0], "/pay")) {
        fprintf(stdout, "Exec pay %s\n", msg->data);
//        return readFile(socket, clientInfo, msg->argv[1], errorString);
    } else if (!strcmp(msg->argv[0], "/quit")) {
        fprintf(stdout, "Exec quit %s\n", msg->data);
        return quit_client(socket, clientInfo, errorString);
    } else {
        fprintf(stdout, "Хоть мы все и проверили, но что-то с ней не так: %s\n", msg->data);
        return -1;
    }

    return 1;
}

int parseCmd(struct Message *msg, char *errorString) {
    bzero(errorString, sizeof(errorString));

    int countArg = 0;
    char *sep = " ";
    char *arg = strtok(msg->data, sep);

    fprintf(stdout, "Сари - %s\n", arg);

    if (arg == NULL) {
        sprintf(errorString, "Команда: %s - не поддается парсингу.\nВведите корректную команду. Используйте: help\n",
                msg->data);
        return -1;
    }

    while (arg != NULL && countArg <= MAX_QUANTITY_ARGS_CMD) {
        countArg++;
        strcpy(msg->argv[countArg - 1], arg);
        arg = strtok(NULL, sep);
    }

    if (countArg > MAX_QUANTITY_ARGS_CMD) {
        sprintf(errorString, "Слишком много аргументов в команде: %s - таких команд у нас нет. Используйте: help\n",
                msg->data);
        return -1;
    }

    msg->argc = countArg;

    return 1;
}

int validateCommand(const struct Message msg, char *errorString) {
    bzero(errorString, sizeof(errorString));

    int argc = msg.argc;
    char *firstArg = msg.argv[0];
    char *cmdLine = msg.data;

    if (!strcmp(firstArg, "/park")) {
        if (argc != 1) {
            sprintf(errorString, "Команда: %s - имеет лишние аргументы. Воспользуейтесь командой: help\n", msg.data);
            return -1;
        }
    } else if (!strcmp(firstArg, "/release")) {
        if (argc > 1) {
            sprintf(errorString, "Команда: %s - имеет много или мало аргументов. Воспользуейтесь командой: help\n",
                    msg.data);
            return -1;
        }
    } else if (!strcmp(firstArg, "/pay")) {
        if (argc != 1) {
            sprintf(errorString, "Команда: %s - имеет много или мало аргументов. Воспользуейтесь командой: help\n",
                    msg.data);
            return -1;
        }
    } else if (!strcmp(firstArg, "/quit")) {
        if (argc > 1) {
            sprintf(errorString, "Команда: %s - имеет много аргументов. Воспользуейтесь командой: help\n",
                    msg.data);
            return -1;
        }
    } else {
        sprintf(errorString, "Неизвстная команда: %s. Воспользуейтесь командой: help\n", cmdLine);
        return -1;
    }

    return 1;
}

int parking(const int socket, struct sockaddr_in *client, char *licName, char *errorString) {

    pthread_mutex_lock(&mutex);


    for (int i = 0; i < clientQuantity; i++) {
        if (!strcmp(clients[i].lic, licName) || clients[i].port == client->sin_port) {

            printf("CLIENT FOUND\n");
            if (safeSendMsg(socket, *client, CODE_PARK, "USER_ALREADY_EXISTS", strlen("USER_ALREADY_EXISTS")) < 0) {
                fprintf(stdout, "code load error");
                return -1;
            }
            return 1;
        }
    }


    printf("CLIENT NOT FOUND\n");
    clients[clientQuantity].number = clientQuantity;
    clients[clientQuantity].port = client->sin_port;
    clients[clientQuantity].cond = 0;
    strcpy(clients[clientQuantity].lic, licName);
    clients[clientQuantity].cond = 1;
    printf("Client %d cond = %d\n", clientQuantity, clients[clientQuantity].cond);
    time(&clients[clientQuantity].start);
    printf("Client %d start time = %ld\n", clientQuantity, clients[clientQuantity].start);
    clientQuantity++;

    pthread_mutex_unlock(&mutex);

//    for (int i = 0; i < clientQuantity; i++) {
//        if (clients[i].port == client->sin_port) {
//            strcpy(clients[i].lic, licName);
//            clients[i].cond = 1;
//            printf("Client %d cond = %d\n", i, clients[i].cond);
//            time(&clients[i].start);
//            printf("Client %d start time = %ld\n", i, clients[i].start);
//        }
//    }

    if (safeSendMsg(socket, *client, CODE_PARK, "PARK_OK", strlen("PARK_OK")) < 0) {
        fprintf(stdout, "code load error");
        return -1;
    }
    return 1;
}

int release_client(const int socket, struct sockaddr_in *client, char *errorString) {
    double total_time = 0;
    for (int i = 0; i < clientQuantity; i++) {
        if (clients[i].port == client->sin_port) {

            clients[i].cond = 2;
            printf("Client %d cond = %d\n", i, clients[i].cond);
            time(&clients[i].end);
            printf("Client %d end time = %ld\n", i, clients[i].end);
            total_time = (double) (clients[i].end - clients[i].start);
        }
    }

    char msg[100] = {0};
    sprintf(msg, "TIME: %.2f", total_time);

    if (safeSendMsg(socket, *client, CODE_RELEASE, msg, sizeof(msg)) < 0) {
        fprintf(stdout, "code load error");
        return -1;
    }
    return 1;
}

int quit_client(const int socket, struct sockaddr_in *client, char *errorString) {


    for (int i = 0; i < clientQuantity; i++) {
        if (clients[i].port == client->sin_port) {
            if (clients[i].cond != 3 && clients[i].cond != 0) {
                if (safeSendMsg(socket, *client, CODE_INFO, "YOU CAN`T QUIT YET",
                                strlen("YOU CAN`T QUIT YET")) < 0) {
                    fprintf(stdout, "code load error");
                    return -1;
                }
                return 1;
            }
        }
    }
    if (safeSendMsg(socket, *client, CODE_QUIT, "QUIT_OK", strlen("QUIT_OK")) < 0) {
        fprintf(stdout, "code load error");
        return -1;
    }
    return 1;
}





//int sendListFilesInDir(const int socket, struct sockaddr_in *clientInfo, const char *path, char *errorString) {
//    bzero(errorString, sizeof(errorString));
//
//    char fullPath[SIZE_MSG * 2] = { 0 };
//    catWithRootDir(fullPath, path);
//
//    DIR *dir = opendir(fullPath);
//    if (dir == NULL) {
//        fprintf(stdout,"Не смог открыть директорию - %s\n", path);
//        sprintf(errorString, "Не удалось получить список файлов из директории - %s. Возможно она не существует.\n", path);
//        return -1;
//    }
//
//    struct dirent *dirent;
//    while ((dirent = readdir(dir)) != NULL) {
//        if (dirent->d_name[0] == '.') {
//            continue;
//        }
//
//        if (safeSendMsg(socket, *clientInfo, CODE_INFO, dirent->d_name, strlen(dirent->d_name)) == -1) {
//            fprintf(stdout,"Проблема с отправкой имени файла из директории - %s\n", path);
//            sprintf(errorString, "Не получилось отправить навзание файла из директории - %s", path);
//            return -1;
//        }
//    }
//
//    if (closedir(dir) == -1) {
//        fprintf(stdout,"Беда! Не могу закрыть директорию - %s\n", path);
//        sprintf(errorString, "Проблемы с директорией! - %s", path);
//        return -1;
//    }
//
//    return 1;
//}

//int changeClientDir(const int socket, struct sockaddr_in *clientInfo, const char *path, char *errorString) {
//
//    fprintf(stdout,"Целевая директория - %s\n", path);
//
//    char fullPath[SIZE_MSG * 2] = { 0 };
//    catWithRootDir(fullPath, path);
//
//    fprintf(stdout,"Полный путь - %s\n", fullPath);
//
//    if(isWho(fullPath) != 2){
//        sprintf(errorString, "%s - это не каталог! Или такого каталога не существует.", path);
//        return -1;
//    }
//
//    if (safeSendMsg(socket, *clientInfo, CODE_QUIT, path, strlen(path)) < 0) {
//        fprintf(stdout,"Не смогли отправить путь.\n");
//    }
//
//    return 1;
//}

/**
Проверяет, что находится по данному пути файл или папка.
Вхоные значения:
    char *path - путь к каталогу.
Возвращаемое значение:
    1 если это файл, 2 если это папка или -1 если что-то не так.
*/
//int isWho(char *path){
//    struct stat statBuf;
//    if(stat(path, &statBuf) == -1){
//        return -1;
//    }
//    if(S_ISREG(statBuf.st_mode)){
//        return 1;
//    }else if(S_ISDIR(statBuf.st_mode)){
//        return 2;
//    }else if(S_ISLNK(statBuf.st_mode)){
//        return 1;
//    } else {
//        return -1;
//    }
//}


//Путь пользователя на сервере + имя файла
//int readFile(const int socket, struct sockaddr_in *clientInfo, const char *fileName, char *errorString) {
//
//    if (safeSendMsg(socket, *clientInfo, CODE_RELEASE, "Д", 1) < 0) {
//        fprintf(stdout,"code load error");
//        return -1;
//    }
//
//    char fullPath[SIZE_MSG * 2] = { 0 };
//    catWithRootDir(fullPath, fileName);
//
//    FILE *file = fopen(fullPath, "wb");
//    if (file == NULL) {
//        fprintf(stdout,"Не удалось открыть файл: %s - для записи!\n", fileName);
//        sprintf(errorString, "Не удалось загрузить файл - %s.", fileName);
//        return -1;
//    }
//
//    int err;
//    struct Message msg;
//    for(;;){
//        err = safeReadMsg(socket, clientInfo, &msg);
//        if(err == -1){
//            fprintf(stdout,"Не удалось принять кусок файла - %s. Данные не были сохранены.\n", fileName);
//            sprintf(errorString, "Не удалось загрузить файл - %s.", fileName);
//            fclose(file);
//            remove(fileName);
//            return -1;
//        }
//
//        if (msg.type == CODE_FILE) {
//            fprintf(stdout,"Пишу байт - %d\n", msg.length);
//            fwrite(msg.data, sizeof(char), msg.length, file);
//        } else if (msg.type == CODE_OK) {
//            fprintf(stdout,"Файл принят.\n");
//            break;
//        } else {
//            fprintf(stdout,"Пришло неправильное сообщение с кодом - %d.\n", msg.type);
//            sprintf(errorString, "Не удалось загрузить файл - %s.", fileName);
//            fclose(file);
//            remove(fileName);
//            return -1;
//        }
//    }
//
//    fclose(file);
//
//    return 1;
//}

//Путь пользователя на сервере + имя файла
//int sendFile(const int socket, struct sockaddr_in *clientInfo, const char *fileName, char *errorString) {
//
//    if (safeSendMsg(socket, *clientInfo, CODE_PAY, "Д", 1) < 0) {
//        fprintf(stdout,"code dload error");
//        return -1;
//    }
//
//    char fullPath[SIZE_MSG * 2] = { 0 };
//    catWithRootDir(fullPath, fileName);
//
//    fprintf(stdout,"Полный путь отправка - %s\n", fullPath);
//
//    if (isWho(fullPath) != 1) {
//        sprintf(errorString, "%s - это не файл!", fileName);
//        return -1;
//    }
//
//    //КАЖИСЬ НАДО ЕЩЕ ОТПРАВЛЯТЬ ЗАПРОС НА ТО ЧТ ОЯ ХОЧУ ОТПРАВИТЬ
//
//    FILE *file = fopen(fullPath, "rb");
//    if (file == NULL) {
//        fprintf(stdout,"Не удалось открыть файл: %s - для записи!\n", fileName);
//        sprintf(errorString, "Не удалось отправить файл - %s.", fileName);
//        return -1;
//    }
//
//    // FILE *fileTest = fopen("test", "wb");
//
//    char section[SIZE_MSG] = {'\0'};
//    int res = 0, err;
//    while ((res = fread(section, sizeof(char), sizeof(section), file)) != 0) {
//        // fwrite(section, sizeof(char), res, fileTest);
//        err = safeSendMsg(socket, *clientInfo, CODE_FILE, section, res);
//        fprintf(stderr, "res - %d\n", res);
//        if (err == -1) {
//            fprintf(stdout,"Не удалось отправить кусок файла - %s\n", fileName);
//            sprintf(errorString, "Не удалось отправить файл - %s\n", fileName);
//            fclose(file);
//            return -1;
//        }
//        bzero(section, sizeof(section));
//    }
//
//    fclose(file);
//    // fclose(fileTest);
//
//    return 1;
//}