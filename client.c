#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "declaration.h"
#include "dexchange.h"


struct sockaddr_in serverInfo;
char preDir[SIZE_MSG];

void execCommand(const int sock, struct sockaddr_in *serverInfo);


int main(int argc, char **argv) {

    if (argc != 3) {
        fprintf(stdout, "%s\n%s\n", "Неверное количество аргументов!", "Необходим вызов: ./client [IP] [PORT]");
        exit(1);
    }
    char *ip = (char *) argv[1];
    int port = atoi(argv[2]);

    bzero(&serverInfo, sizeof(serverInfo));
    serverInfo.sin_family = AF_INET;
    if (inet_aton(ip, (struct in_addr *) serverInfo.sin_addr.s_addr) == 0) {
        fprintf(stderr, "0\n");
        exit(1);
    }
    serverInfo.sin_port = htons(port);

    fprintf(stdout, "Создаю сокет...\n");

    int sock = -1;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        fprintf(stdout, "Не удалось создать сокет! Приложение не запущено.\n");
        exit(1);
    }

    struct sockaddr_in myInfo;
    bzero(&myInfo, sizeof(myInfo));
    myInfo.sin_family = AF_INET;
    myInfo.sin_port = htons(0);
    myInfo.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (const struct sockaddr *) &myInfo, sizeof(myInfo)) < 0) {
        fprintf(stdout, "Не удалось настроить адрес сокета! Приложение не запущено.\n");
        close(sock);
        exit(1);
    }

    bzero(preDir, sizeof(preDir));
    strcpy(preDir, "$>");

    struct sockaddr_in tempServerInfo;
    struct Message msg;
    char inputBuf[SIZE_MSG];
    char input_copy[SIZE_MSG];
    for (;;) {


        bzero(inputBuf, sizeof(inputBuf));
        fgets(inputBuf, sizeof(inputBuf), stdin);
        inputBuf[strlen(inputBuf) - 1] = '\0';

        // fprintf(stderr, "stdin - %d\n", strlen(inputBuf));

        if (!strcmp("/quitterino", inputBuf)) {
            break;
        }

        strcpy(input_copy, inputBuf);
        char *sep = " ";
        char *arg = strtok(input_copy, sep);

        if (arg == NULL) {
            fprintf(stdout, "Дичь! Попробуйте еще раз.\n");
            continue;
        }

        char tempStr[SIZE_MSG] = {0};


        if (safeSendMsg(sock, serverInfo, CODE_CONNECT, "CONNECT", strlen("CONNECT")) < 0) {
            fprintf(stdout, "Проблемы с отправкой команды на сервер - 1! Попробуйте еще раз.\n");
            continue;
        }

        if (safeReadMsg(sock, &tempServerInfo, &msg) < 0) {
            fprintf(stdout, "Проблемы с отправкой команды на сервер - 2! Попробуйте еще раз.\n");
            continue;
        }

        if (strlen(tempStr) == 0) {
            if (safeSendMsg(sock, tempServerInfo, CODE_CMD, inputBuf, strlen(inputBuf)) < 0) {
                fprintf(stdout, "Проблемы с отправкой команды на сервер - 3! Попробуйте еще раз.\n");
                continue;
            }
        } else {
            if (safeSendMsg(sock, tempServerInfo, CODE_CMD, tempStr, strlen(tempStr)) < 0) {
                fprintf(stdout, "Проблемы с отправкой команды на сервер - 3! Попробуйте еще раз.\n");
                continue;
            }
        }

        execCommand(sock, &tempServerInfo);
    }

    close(sock);
    fprintf(stdout, "Приложение завершило свою работу. До скорой встречи!\n");

    return 0;
}


void execCommand(const int sock, struct sockaddr_in *serverInfo) {
    struct Message msg;
    int code = -1;

    int wrongQuantity = 10;
    for (;;) {
        if (safeReadMsg(sock, serverInfo, &msg) < 0) {
            fprintf(stdout, "Проблемы с получением ответа от сервера! Попробуйте еще раз.\n");
            return;
        }

        code = msg.type;

        if (code == CODE_QUIT) {
            fprintf(stdout, "%s\n", msg.data);
            close(sock);
            fprintf(stdout, "Приложение завершило свою работу. До скорой встречи!\n");
            exit(0);
        } else if (code == CODE_ERROR) {
            fprintf(stdout, "Возникла ошибка!\n%s\n", msg.data);
        } else if (code == CODE_INFO) {
            fprintf(stdout, "%s\n", msg.data);
        } else if (code == CODE_PARK) {
            fprintf(stdout, "%s\n", msg.data);
            break;
        } else if (code == CODE_RELEASE) {
            fprintf(stdout, "%s\n", msg.data);
            if (safeReadMsg(sock, serverInfo, &msg) < 0) {
                fprintf(stdout, "Проблемы с получением ответа от сервера! Попробуйте еще раз.\n");
                return;
            }
            fprintf(stdout, "%s\n", msg.data);
            break;
        } else if (code == CODE_PAY) {
            fprintf(stdout, "%s\n", msg.data);
            break;
        } else if (code == CODE_OK) {
            break;
        } else {
            fprintf(stdout, "Пришло что-то не то! Попробуйте еще раз.");

            if (--wrongQuantity == 0) {
                fprintf(stdout,
                        "Получено много неверных пакетов. Предположительно что-то не так с сетью. Сообщение не получено.\n");
                break;
            }
        }
    }

//    return;
}
