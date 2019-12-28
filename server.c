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
    int time;
    time_t start, end;
    int cond; // cond == 1 -> is parked; 0 -> new client, 2 -> leave request, 3 -> payed off, able to quit
} clients[5];

struct payLog {
    int client;
    int payment;
    int change;
} *pLog;

pthread_t commander;
pthread_t *workers;
int quantityWorkers = 0;
int serverSocket = -1;

int clientQuantity = 0;
int operations = 0;

void initServerSocket(int *serverSocket, int port);

void initServerSocketWithRandomPort(int *serverSocket);

void *asyncTask(void *args);

int execClientCommand(const int socket, struct sockaddr_in *clientInfo, struct Message *msg, char *errorString);

int parseCmd(struct Message *msg, char *errorString);

int validateCommand(const struct Message msg, char *errorString);

int parking(const int socket, struct sockaddr_in *client, char *licName, char *errorString);

int quit_client(const int socket, struct sockaddr_in *client, char *errorString);

int release_client(const int socket, struct sockaddr_in *client, char *errorString);

int payment(const int socket, struct sockaddr_in *client, int amount, char *errorString);

int kickClient(const int socket, struct sockaddr_in *client);

void *commando(void *args);


int main(int argc, char **argv) {

    if (argc != 2) {
        fprintf(stdout, "%s\n%s\n", "Неверное количество аргументов!", "Необходим вызов: ./server [PORT]");
        exit(1);
    }

    int port = atoi(argv[1]);

    initServerSocket(&serverSocket, port);


    struct sockaddr_in connectInfo;
    struct Message msg;
    printf("Input (/help to help): \n");
    fflush(stdout);
    if (pthread_create(&commander, NULL, commando, NULL)) {
        fprintf(stdout, "%s\n", "Не удалось создать поток для обработки серверной консоли!");
        exit(-1);
    }
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


void *commando(void *args) {
    char buf[100];

    int total_time;

    for (;;) {

        bzero(buf, 100);
        fgets(buf, 100, stdin);
        buf[strlen(buf) - 1] = '\0';

        if (!strcmp("/quit", buf) || !strcmp("/q", buf)) {
            fprintf(stdout, "Получена команда отключения, вырубаемся..\n");
            fprintf(stdout, "Ожидаем завершения работы воркеров.\n");
            for (int i = 0; i < quantityWorkers; i++) {
                pthread_join(workers[i], NULL);
            }

            close(serverSocket);
            free(workers);

            fprintf(stdout, "%s\n", "Сервер завершил работу.");
            exit(1);
        } else if (!strcmp("/lc", buf)) {
            printf("Clients on-line:\n");
            printf("N TIME LIC\n");

            for (int i = 0; i < clientQuantity; i++) {
                if (clients[i].port != 0) {
                    if (clients[i].cond < 2) {
                        time(&clients[i].end);
                        total_time = (int) (clients[i].end - clients[i].start);
                        clients[i].time = total_time;
                    }

                    printf("%d  %d   %s\n", clients[i].number, clients[i].time, clients[i].lic);
                }

            }

            fflush(stdout);
        } else {
            char *sep = " ";
            char *str = strtok(buf, sep);
            if (str == NULL) {
                printf("Illegal format!\n");
                fflush(stdout);
                continue;
            }
            if (!strcmp("/kick", str)) {
                str = strtok(NULL, sep);
                if (str != NULL) {
                    int kickNum = atoi(str);

                    if (str[0] != '0' && kickNum == 0) {
                        printf("Illegal format! Use /kick NUMBER.\n");
                        fflush(stdout);
                        continue;
                    }
                    printf("Ща попробуем кикнуть %d-го клиента\n", kickNum);
                }
            }
        }
    }

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

int execClientCommand(const int socket, struct sockaddr_in *clientInfo, struct Message *msg, char *errorString) {
    bzero(errorString, sizeof(errorString));

    if (parseCmd(msg, errorString) == -1) {
        fprintf(stdout, "Не удалось распарсить команду клиента: %s\nОписание ошибки: %s\n", msg->data, errorString);
        return -1;
    }

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
        return payment(socket, clientInfo, atoi(msg->argv[1]), errorString);
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

    for (int i = 0; i <= msg->argc; i++) {
        printf("PARSER: %s\n", msg->argv[i]);
    }

    return 1;
}

int validateCommand(const struct Message msg, char *errorString) {
    bzero(errorString, sizeof(errorString));

    int argc = msg.argc;
    char *firstArg = msg.argv[0];
    char *cmdLine = msg.data;

    if (!strcmp(firstArg, "/park")) {
        if (argc != 2) {
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
        if (argc != 2) {
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

//    pthread_mutex_lock(&mutex);


    for (int i = 0; i < clientQuantity; i++) {
        if (!strcmp(clients[i].lic, licName) && clients[i].port == client->sin_port) {
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
    clients[clientQuantity].time = 0;
    strcpy(clients[clientQuantity].lic, licName);
    clients[clientQuantity].cond = 1;
    printf("Client %d cond = %d\n", clientQuantity, clients[clientQuantity].cond);
    time(&clients[clientQuantity].start);
    printf("Client %d lic = %s\n", clientQuantity, clients[clientQuantity].lic);

    char msg[SIZE_MSG] = {0};

    snprintf(msg, sizeof(msg), "PARK OK\nYOUR LIC: %s", clients[clientQuantity].lic);

    if (safeSendMsg(socket, *client, CODE_PARK, msg, sizeof(msg)) < 0) {
        fprintf(stdout, "code load error");
        return -1;
    }

    clientQuantity++;

//    pthread_mutex_unlock(&mutex);


    return 1;
}

int release_client(const int socket, struct sockaddr_in *client, char *errorString) {
    int total_time = 0;
    int index = 0;
    for (int i = 0; i < clientQuantity; i++) {
        if (clients[i].port == client->sin_port) {

            clients[i].cond = 2;
            printf("Client %d cond = %d\n", i, clients[i].cond);
            time(&clients[i].end);
            printf("Client %d end time = %ld\n", i, clients[i].end);
            total_time = (int) (clients[i].end - clients[i].start);
            clients[i].time = total_time;
            clients[i].debt = total_time * 2;
            index = i;
        }
    }

    char msg[100] = {0};
    sprintf(msg, "YOUR TIME: %d", total_time);


    if (safeSendMsg(socket, *client, CODE_RELEASE, msg, sizeof(msg)) < 0) {
        fprintf(stdout, "code load error");
        return -1;
    }
    sprintf(msg, "YOUR DEBT: %d$", clients[index].debt);
    if (safeSendMsg(socket, *client, CODE_RELEASE, msg, sizeof(msg)) < 0) {
        fprintf(stdout, "code load error");
        return -1;
    }
    return 1;
}

int payment(const int socket, struct sockaddr_in *client, int amount, char *errorString) {
    char msg[100] = {0};
    int cash;

    printf("RECEIVED PAYMENT %d$\n", amount);
    for (int i = 0; i < clientQuantity; i++) {
        if (clients[i].port == client->sin_port) {

            clients[i].debt -= amount;
            if (clients[i].debt == 0) {
                printf("Client %d payed off, his soul is free..\n", i);
                clients[i].cond = 3;
                sprintf(msg, "You have payed your debt.");

            } else if (clients[i].debt < 0) {
                cash = -clients[i].debt;
                printf("Client %d payed well, but %d$ more than he had to.\n", i, cash);
                clients[i].cond = 3;
                sprintf(msg, "You have payed your debt.\nIn fact, you paid %d$ more", cash);
            } else {
                printf("Client %d has to pay %d$ more.\n", i, clients[i].debt);
                sprintf(msg, "You have to pay: %d$", clients[i].debt);
            }

        }
    }

    if (safeSendMsg(socket, *client, CODE_PAY, msg, sizeof(msg)) < 0) {
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

            } else {
                if (safeSendMsg(socket, *client, CODE_QUIT, "QUIT_OK", strlen("QUIT_OK")) < 0) {
                    fprintf(stdout, "code load error");
                    return -1;
                }
                clients[i].port = 0;
            }
        }
    }

    return 1;

}

int kickClient(const int socket, struct sockaddr_in *client) {

    for (int i = 0; i < clientQuantity; i++) {
        if (clients[i].port == client->sin_port) {
            if (safeSendMsg(socket, *client, CODE_QUIT, "FORCE_QUIT", strlen("QUIT_OK")) < 0) {
                fprintf(stdout, "code load error");
                return -1;
            }
            clients[i].port = 0;

            break;
        }
    }
    return 1;
}
