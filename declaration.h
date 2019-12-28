#ifndef DECLARATION_H
#define DECLARATION_H

/**
508 байт данных в UDP/IPv4 гарантирует доставку пакета без промежуточной
фрагментации и принятие любым устройством
*/
#define SIZE_PACK 500
#define SIZE_PACK_DATA 480
#define SIZE_MSG 2000
#define SIZE_ARG 32
#define SIZE_MY_STR 500
#define MAX_QUANTITY_ARGS_CMD 3

#define CODE_CONNECT 100
#define CODE_CMD 101
#define CODE_PARK 200
#define CODE_RELEASE 201
#define CODE_PAY 202
#define CODE_INFO 300
#define CODE_QUIT 301
#define CODE_ERROR 400
#define CODE_OK 500
#define NO_ACK 0
#define ACK 777

struct Package {
    int ack;
    int id;
    int maxId;
    int code;
    int lengthData;
    char data[SIZE_PACK_DATA];
};

struct Message {
    int type;
    int length;
    char data[SIZE_MSG];
    int argc;
    char argv[MAX_QUANTITY_ARGS_CMD][SIZE_ARG];
};

#endif