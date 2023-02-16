#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

typedef struct PDU {
    char type;
    char data[100];
} PDU;

typedef struct FileNode {
    char peerName[10];
    char contentName[10];
    char address[10];
    char port[10];
    int numDownloads;
    struct FileNode *next;
} FileNode;

/*------------------------------------------------------------------------
 * main - Iterative UDP server for TIME service
 *------------------------------------------------------------------------
 */
int main(int argc, char *argv[]) {
    struct sockaddr_in fsin; /* the from address of a client	*/
    PDU buf;
    PDU spdu;
    int m;
    int size, n, bytes_to_read, count;
    char name[100];

    char *pts;
    int sock;               /* server socket		*/
    time_t now;             /* current time			*/
    int alen;               /* from-address length		*/
    struct sockaddr_in sin; /* an Internet endpoint address         */
    int s, type;            /* socket descriptor and socket type    */
    int port = 3000;

    switch (argc) {
        case 1:
            break;
        case 2:
            port = atoi(argv[1]);
            break;
        default:
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            exit(1);
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    /* Allocate a socket */
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        fprintf(stderr, "can't creat socket\n");

    /* Bind the socket */
    if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
        fprintf(stderr, "can't bind to %d port\n", port);
    listen(s, 5);
    alen = sizeof(fsin);

    // create a null head as a linked list store files in our index server
    FileNode *head = NULL;

    head = (FileNode *)malloc(sizeof(FileNode));
    head->next = NULL;

    while (1) {
        m = recvfrom(s, &buf, sizeof(buf), 0, (struct sockaddr *)&fsin, &alen);

        switch (buf.type) {
            case 'R':
                if (nodeExists(head, buf)) {
                    spdu.type = 'E';
                    spdu.data[0] = '\0';
                    (void)sendto(s, &spdu, sizeof(spdu), 0, (struct sockaddr *)&fsin, sizeof(fsin));
                } else {
                    registerFile(head, buf);

                    spdu.type = 'A';
                    spdu.data[0] = '\0';
                    (void)sendto(s, &spdu, sizeof(spdu), 0, (struct sockaddr *)&fsin, sizeof(fsin));
                }

                break;

            case 'O':
                sendContentList(head->next, s, fsin);
                break;

            case 'T':
                unregisterFile(head, buf, s, fsin);
                break;

            case 'S':
                sendFileAddress(head->next, buf, s, fsin);
                break;

            default:
                fprintf(stderr, "Error, incorrect pdu type %c!\n", buf.type);
        }
    }
}

// registers a file
int registerFile(FileNode *head, PDU receivePdu) {
    // get to the tail node
    while (head->next != NULL) {
        head = head->next;
    }

    // add to the end
    head->next = (FileNode *)malloc(sizeof(FileNode));
    head->next->next = NULL;

    FileNode *node = head->next;
    node->numDownloads = 0;

    // adds data to node
    strncpy(&node->peerName, &receivePdu.data, 10);
    strncpy(&node->contentName, &receivePdu.data[10], 10);
    strncpy(&node->address, &receivePdu.data[20], 10);
    strncpy(&node->port, &receivePdu.data[30], 10);

    printf("address: %s, port: %s\n", node->address, node->port);
}

// traverse through linkedlist
int nodeExists(FileNode *node, PDU receivePdu) {
    // parses out the pdu
    char peerName[10];
    char contentName[10];

    strncpy(&peerName, &receivePdu.data, 10);
    strncpy(&contentName, &receivePdu.data[10], 10);

    while (node != NULL) {
        if (strcmp(peerName, node->peerName) == 0 && strcmp(contentName, node->contentName) == 0)
            return 1;

        node = node->next;
    }

    return 0;
}

// sends entire content list
void sendContentList(FileNode *node, int s, struct sockaddr_in fsin) {
    PDU nodePdu;
    nodePdu.type = 'O';

    while (node != NULL) {
        char numDownloadsCharArr[10];
        sprintf(numDownloadsCharArr, "%d", node->numDownloads);

        strncpy(&nodePdu.data, &node->peerName, 10);
        strncpy(&nodePdu.data[10], &node->contentName, 10);
        strncpy(&nodePdu.data[20], &numDownloadsCharArr, 10);

        (void)sendto(s, &nodePdu, sizeof(nodePdu), 0, (struct sockaddr *)&fsin, sizeof(fsin));

        node = node->next;
    }

    nodePdu.type = 'A';
    (void)sendto(s, &nodePdu, sizeof(nodePdu), 0, (struct sockaddr *)&fsin, sizeof(fsin));
}

// unregisters all from peer
void unregisterAll(FileNode *node, PDU receivePdu) {
    FileNode *deleteNode = NULL;
    char peerName[10];

    strncpy(&peerName, &receivePdu.data, 10);

    while (node != NULL && node->next != NULL) {
        if (strcmp(peerName, node->next->peerName) == 0) {
            deleteNode = node->next;
            node->next = node->next->next;
            free(deleteNode);
        } else {
            node = node->next;
        }
    }
}

// unregisters file from peer
void unregisterFile(FileNode *node, PDU receivePdu, int s, struct sockaddr_in fsin) {
    int hasDeleted = 0;

    // parses out the pdu
    char peerName[10];
    char contentName[10];

    PDU sendPdu;
    FileNode *deleteNode = NULL;

    strncpy(&peerName, &receivePdu.data, 10);
    strncpy(&contentName, &receivePdu.data[10], 10);

    // handles case of unregister all from that peername
    if (contentName[0] == '*' && contentName[1] == '\0') {
        printf("unregistering all files\n");
        unregisterAll(node, receivePdu);
        return;
    }

    while (node->next != NULL) {
        if (strcmp(peerName, node->next->peerName) == 0 && strcmp(contentName, node->next->contentName) == 0) {
            deleteNode = node->next;
            node->next = node->next->next;
            free(deleteNode);

            hasDeleted = 1;
            break;
        }

        node = node->next;
    }

    if (hasDeleted)
        sendPdu.type = 'A';
    else
        sendPdu.type = 'E';

    (void)sendto(s, &sendPdu, sizeof(sendPdu), 0, (struct sockaddr *)&fsin, sizeof(fsin));
}

// sends over the file address
void sendFileAddress(FileNode *node, PDU receivePdu, int s, struct sockaddr_in fsin) {
    int hasFound = 0;
    PDU sendPdu;

    // parses out the pdu
    char peerName[10];
    char contentName[10];

    strncpy(&peerName, &receivePdu.data, 10);
    strncpy(&contentName, &receivePdu.data[10], 10);

    while (node != NULL) {
        if (strcmp(peerName, node->peerName) == 0 && strcmp(contentName, node->contentName) == 0) {
            sendPdu.type = 'S';

            // adds values to pdu
            strncpy(&sendPdu.data, &node->address, 10);
            strncpy(&sendPdu.data[10], &node->port, 10);

            hasFound = 1;
            node->numDownloads++;

            (void)sendto(s, &sendPdu, sizeof(sendPdu), 0, (struct sockaddr *)&fsin, sizeof(fsin));
            break;
        }

        node = node->next;
    }

    if (hasFound == 0) {
        sendPdu.type = 'E';
        (void)sendto(s, &sendPdu, sizeof(sendPdu), 0, (struct sockaddr *)&fsin, sizeof(fsin));
    }
}
