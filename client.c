#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUFSIZE 64
#define BUFLEN 4096
#define MSG "Any Message \n"

void reaper(int);

/*------------------------------------------------------------------------
 * main - UDP client for TIME service that prints the resulting time
 *------------------------------------------------------------------------
 */
typedef struct PDU {
    char type;
    char data[100];
} PDU;

typedef struct DPDU {
    char type;
    char data[1460];
} DPDU;

char peerName[10];
char fileName[10];
char peerAddress[80];
int i, j;

int main(int argc, char **argv) {
    char *host = "localhost";
    int port = 3000;
    char now[100];          /* 32-bit integer to hold time	*/
    struct hostent *phe;    /* pointer to host information entry	*/
    struct sockaddr_in sin; /* an Internet endpoint address		*/
    int s, n, type, x;      /* socket descriptor and socket type	*/
    char *bp;
    char rbuf[BUFLEN];
    char userInput;

    switch (argc) {
        case 1:
            break;
        case 2:
            host = argv[1];
        case 3:
            host = argv[1];
            port = atoi(argv[2]);
            break;
        case 4:
            host = argv[1];
            port = atoi(argv[2]);
            strcpy(peerAddress, argv[3]);
            break;
        default:
            fprintf(stderr, "usage: UDPtime [host [port]]\n");
            exit(1);
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);

    /* Map host name to IP address, allowing for dotted decimal */
    if (phe = gethostbyname(host)) {
        memcpy(&sin.sin_addr, phe->h_addr, phe->h_length);
    } else if ((sin.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE)
        fprintf(stderr, "Can't get host entry \n");

    /* Allocate a socket */
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        fprintf(stderr, "Can't create socket \n");

    /* Connect the socket */
    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
        fprintf(stderr, "Can't connect to %s %s \n", host, "Time");

    /* Ask for peer name */
    getPeerName();

    while (1) {
        printf("\nPlease choose one of the following options: \n");
        printf("(1) - Register file \n");
        printf("(2) - Unregister file \n");
        printf("(3) - List all downloadable files \n");
        printf("(4) - Download file \n");
        printf("(Q) - Quit \n");

        printf("Your input ==> ");
        scanf("%c%*c", &userInput);

        switch (userInput) {
            case '1':
                printf("\n\nEnter file name below: \n");
                i = read(0, fileName, 10);
                fileName[i - 1] = '\0';

                registerFile(s, fileName);
                break;
            case '2':
                deregisterFile(s);
                break;
            case '3':
                listContent(s);
                break;
            case '4':
                downloadContent(s);
                break;
            case 'q':
            case 'Q':
                if (exitPeer(s) == 0) return 0;
                break;
            default:
                fprintf(stderr, "Invalid input, please try again! \n");

                printf("\n\n");
        }
    }
}

// Gets peer name
void getPeerName(void) {
    int p1 = 0;
    memset(peerName, 0, sizeof(peerName));
    printf("Please enter your peer name below: \n");
    p1 = read(0, peerName, 10);
    peerName[p1 - 1] = '\0';
    printf("\n\n");
}

// Register a file
int registerFile(int s, char *fileNameTemp) {
    PDU register_pdu, receive_pdu;
    struct sockaddr_in reg_addr;
    int p_sock, alen;
    int k = 0;

    struct sockaddr_in clientAddr;
    int clientLen, d_sock;

    char downloadFileName[10];
    strcpy(downloadFileName, fileNameTemp);

    printf("fileNameTemp: %s\n", fileNameTemp);

    // opens port for TCP connections
    p_sock = socket(AF_INET, SOCK_STREAM, 0);

    // not sure
    bzero((char *)&reg_addr, sizeof(struct sockaddr_in));

    reg_addr.sin_family = AF_INET;
    reg_addr.sin_port = htons(0);
    reg_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(p_sock, (struct sockaddr *)&reg_addr, sizeof(reg_addr));

    alen = sizeof(struct sockaddr_in);
    getsockname(p_sock, (struct sockaddr *)&reg_addr, &alen);

    // create the PDU sent to the index server to register file
    register_pdu.type = 'R';
    char portCharArr[10];
    snprintf(portCharArr, 10, "%d", ntohs(reg_addr.sin_port));

    strncpy(&register_pdu.data, &peerName, 10);
    strncpy(&register_pdu.data[10], &downloadFileName, 10);
    strncpy(&register_pdu.data[20], &peerAddress, 10);
    strncpy(&register_pdu.data[30], &portCharArr, 10);

    printf("address: %s, port: %s\n", peerAddress, portCharArr);

    receive_pdu.type = NULL;
    k = 0;
    while ((write(s, &register_pdu, 101) > 0) && (k = read(s, &receive_pdu, sizeof(receive_pdu))) && receive_pdu.type != 'A') {
        printf("Error, peer name is already taken!\n");
        getPeerName();

        strncpy(&register_pdu.data, &peerName, 10);

        // wait for reply
        printf("File sent with new name: %s\n", peerName);
    }

    /* queue up to 5 connect requests  */
    listen(p_sock, 5);
    (void)signal(SIGCHLD, reaper);

    // forks to child and parent
    switch (fork()) {
        case 0: /* child */
            downloadRequestListener(p_sock, downloadFileName);
            return;
        default: /* parent */
            (void)close(p_sock);
            return;
        case -1:
            fprintf(stderr, "Error forking child process when registering!\n");
    }
}

// listens to download requests for that specific file
void downloadRequestListener(int sd, char *downloadFileName) {
    struct sockaddr_in clientAddr;
    int clientLen, new_sd;

    DPDU send_pdu, recv_pdu;
    FILE *fp;

    while (1) {
        clientLen = sizeof(clientAddr);
        new_sd = accept(sd, (struct sockaddr *)&clientAddr, &clientAddr);

        if (new_sd < 0) {
            fprintf(stderr, "Can't accept client \n");
            exit(1);
        }

        switch (fork()) {
            case 0: /* child */
                (void)close(sd);
                exit(processDownloadRequest(new_sd, downloadFileName));
            default: /* parent */
                (void)close(new_sd);
                break;
            case -1:
                fprintf(stderr, "fork: error\n");
        }
    }
}

// process the download request
int processDownloadRequest(int sd, char *downloadFileName) {
    DPDU send_pdu, recv_pdu;
    int k;
    int breakWhile = 1;
    FILE *fp;

    fp = fopen(downloadFileName, "r");
    if (fp == NULL) {
        fprintf(stderr, "error opening file\n");
        return 1;
    }

    while (breakWhile) {
        send_pdu.type = 'C';

        for (k = 0; k < 1460; k++) {
            send_pdu.data[k] = fgetc(fp);
            if (send_pdu.data[k] == EOF) {
                breakWhile = 0;
                break;
            }
        }

        write(sd, &send_pdu, sizeof(send_pdu));
    }

    return 0;
}

void listContent(int s) {
    PDU list_pdu, receive_pdu;
    int k = 0;
    int l = 0;

    // create the PDU sent to the index server to register file
    list_pdu.type = 'O';

    // sends 'O' type pdu to server
    write(s, &list_pdu, 101);

    receive_pdu.type = NULL;
    printf("\nPeer Name || Content Name || Number of Downloads\n\n");

    while ((k = read(s, &receive_pdu, sizeof(receive_pdu))) && receive_pdu.type != 'A') {
        char nodePeerName[10];
        char nodeContentName[10];
        char nodeNumDownloads[10];

        strncpy(nodePeerName, &receive_pdu.data, 10);
        strncpy(nodeContentName, &receive_pdu.data[10], 10);
        strncpy(nodeNumDownloads, &receive_pdu.data[20], 10);

        // print content nodes
        printf("%d. %s  ||  %s  ||  %s\n", ++l, nodePeerName, nodeContentName, nodeNumDownloads);
    }

    printf("\n-----------------\n\n");
}

// De-register file
int deregisterFile(int s) {
    PDU deregister_pdu, receive_pdu;
    int k = 0;

    printf("\n\nEnter file name below: \n");
    k = read(0, fileName, 10);
    fileName[k - 1] = '\0';

    deregister_pdu.type = 'T';
    strncpy(&deregister_pdu.data, &peerName, 10);
    strncpy(&deregister_pdu.data[10], &fileName, 10);

    receive_pdu.type = NULL;
    k = 0;

    if ((write(s, &deregister_pdu, 101) > 0) && (k = read(s, &receive_pdu, sizeof(receive_pdu))) && receive_pdu.type == 'E') {
        printf("Error, file name does not exist\n");
        return 1;
    }

    printf("File successfully deleted\n");
    return 0;
}

// deregister all content from this peer and
int exitPeer(int s) {
    PDU deregister_pdu;

    char userInputQuit[10];
    int k = 0;

    printf("\n\nAre you sure you want to quit? This will deregister all your files in the network (Y/N): \n");
    k = read(0, userInputQuit, 10);
    userInputQuit[k - 1] = '\0';

    if (userInputQuit[0] == 'N' || userInputQuit[0] == 'n')
        return 1;

    deregister_pdu.type = 'T';
    strncpy(&deregister_pdu.data, &peerName, 10);

    deregister_pdu.data[10] = '*';
    deregister_pdu.data[11] = '\0';

    write(s, &deregister_pdu, 101);
    return 0;
}

int downloadContent(int s) {
    PDU receive_pdu, download_pdu;

    char downloadPeerName[10];
    char downloadContentName[10];

    char address[10];
    char port[10];
    char *host = (char *)malloc(10);
    int portNum;

    int sd;
    struct hostent *hp;
    struct sockaddr_in server;

    int k = 0;
    int x = 0;

    // create the PDU sent to the index server to register file
    download_pdu.type = 'S';

    printf("\n\nEnter peer name below: \n");
    k = read(0, downloadPeerName, 10);
    downloadPeerName[k - 1] = '\0';

    printf("\n\nEnter file name below: \n");
    k = read(0, downloadContentName, 10);
    downloadContentName[k - 1] = '\0';

    // sends 'S' type pdu to server
    strncpy(&download_pdu.data, &downloadPeerName, 10);
    strncpy(&download_pdu.data[10], &downloadContentName, 10);

    receive_pdu.type = NULL;
    k = 0;

    if ((write(s, &download_pdu, 101) > 0) && (k = read(s, &receive_pdu, sizeof(receive_pdu))) && receive_pdu.type == 'E') {
        printf("Error, file and/or peer does not exist\n");
        return 1;
    }

    /* Create a stream socket	*/
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "Can't creat a socket\n");
        exit(1);
    }

    // parses out peer/content name
    strncpy(&address, &receive_pdu.data, 10);
    strncpy(&port, &receive_pdu.data[10], 10);

    strcpy(host, address);
    portNum = atoi(port);

    // format the sockaddr_in struct
    bzero((char *)&server, sizeof(struct sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_port = htons(portNum);
    server.sin_addr.s_addr = htonl(INADDR_ANY);

    if (hp = gethostbyname(host))
        bcopy(hp->h_addr, (char *)&server.sin_addr, hp->h_length);
    else if (inet_aton(host, (struct in_addr *)&server.sin_addr)) {
        fprintf(stderr, "Can't get server's address\n");
        return 1;
    }

    printf("\nStarting download..........\n\n");
    printf("address: %s, port: %s, portNum: %d\n", host, port, portNum);

    /* Connecting to the server */
    if (connect(sd, (struct sockaddr *)&server, sizeof(server)) == -1) {
        fprintf(stderr, "Can't connect \n");
        return 1;
    }

    // send download request pdu
    DPDU recv_pdu;
    FILE *fp;

    // create file
    fp = fopen(downloadContentName, "w");
    if (fp == NULL) {
        fprintf(stderr, "error opening file to create\n");
        return 1;
    }

    while ((k = read(sd, &recv_pdu, sizeof(recv_pdu))) > 0) {
        for (x = 0; x < 1460; x++) {
            if (recv_pdu.data[x] == EOF)
                break;
            fputc(recv_pdu.data[x], fp);
        }
    }

    fclose(fp);

    printf("registering downloaded file now \n");

    // register the file
    registerFile(s, downloadContentName);
}

/*	reaper		*/
void reaper(int sig) {
    int status;
    while (wait3(&status, WNOHANG, (struct rusage *)0) >= 0)
        ;
}