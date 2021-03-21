//Author: Andrew Beshay
//Assignment for COMP3331 - CDHT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>

#define PRINTLINE printf("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__); //For debugging
#define PRINTLOGLINE    "%-8s      %ld         %4d      %6d      %4d\n"

//ANSI escape codes for unix terminals
//Colour coding makes easier visibility for responses, requests etc
#define DEFAULT     "\x1b[0m"
#define RED         "\x1b[31m"
#define GREEN       "\x1b[32m"
#define YELLOW      "\x1b[33m"
#define BLUE        "\x1b[34m"
#define PURPLE      "\x1b[35m"
#define CYAN        "\x1b[36m"

//Some nice constants for visibility
#define PORT        50000
#define PORTFILE    60000
#define BUFFER      2048
#define REQUEST     0x00
#define RESPONSE    0x01
#define LOOKING     0x02
#define FOUND       0x03
#define ANSWERED    0x04 

//Honestly random numbers and a bit messy
#define QUIT        0xC9
#define LEAVING     0xA0
#define INFORMED    0xB7
#define DEAD        0xD5

//Global variables, not the safest options, would prefer assigning ID to a const as well.
unsigned short ID; 
unsigned short FIRST;
unsigned short SECOND;
         float DROP;
unsigned int MSS;


int shuttingdown = 0;

char *states[] = {"Snd", "rcv", "Drop", "RTX", "RTX/Drop"};

//Keep track of all info about peers
typedef struct PEER_ {
    unsigned short id;
    unsigned short first;
    unsigned short second;
    int pingseq[2];
    signed int predfirst;
    signed int predsecond;
    unsigned short type;
    unsigned short quit;
    unsigned int Filename;
    unsigned short hash;
    unsigned short location;
    unsigned short requestingpeer;
    unsigned short respondingpeer;
} *peer;

int pingseq[2];

#define PEERSIZE struct PEER_

//Thread names
pthread_t notify_depature_thread, file_receive_thread, file_transfer_thread, dead_peer_thread;

// Function decelerations
void *udp_server(void *argv);
void *ping_client(void *argv);
void *input(void *argv);
void *tcp_server(void *argv);
void *notify_depature(void *argv); 
void *dead_peer(void *argv);
void *file_transfer(void *argv);
void *file_receive(void *argv);
static inline void CreateAddr(struct sockaddr_in *addr, short p); //Static inline to help performance, function was just made to clean up code
static inline void SendCheck(int sock, peer p, short id, short first, struct sockaddr_in addr);
void ColourLine(char *s);

int main(int argc, char **argv) {
    
    //Checking for correct usage
    if (argc < 4 || argc > 6) {
        printf("Usage: cdht [0-255] [0-255] [0-255] MMS Loss-rate\n");
        exit(EXIT_FAILURE);
    }

    DROP = atof(argv[5]);


    //Checking for valid ID's
    for (int i = 1; i < 4; i++) {
        unsigned int input = atoi(argv[i]);
        if ((0xFF | input) != 255) {
            printf("Error: Peers id must be between 0-255\n");
            exit(EXIT_FAILURE);
        }
    }
    
    peer GlobalPeer = malloc(sizeof(struct PEER_));
    memset(GlobalPeer, 0, sizeof(struct PEER_));
    GlobalPeer->id = atoi(argv[1]);
    GlobalPeer->first = atoi(argv[2]);
    GlobalPeer->second = atoi(argv[3]);
    GlobalPeer->pingseq[0] = 0;
    GlobalPeer->pingseq[1] = 0;
    GlobalPeer->predfirst = -1;
    GlobalPeer->predsecond = -2;
    GlobalPeer->quit = 0x00;
    // printf("Starting...");


    ID      = atoi(argv[1]); 
    FIRST   = atoi(argv[2]);
    SECOND  = atoi(argv[3]);
    MSS     = atoi(argv[4]);
    //Initialising threads
    pthread_t udp_server_thread, ping_client_thread, input_thread, tcp_server_thread;

    if(pthread_create(&udp_server_thread, NULL, udp_server, GlobalPeer)) {
        fprintf(stderr, "Error creating udp server thread\n");
        exit(EXIT_FAILURE);
    } else if(pthread_create(&ping_client_thread, NULL, ping_client, GlobalPeer)) {
        fprintf(stderr, "Error creating ping client thread\n");
        exit(EXIT_FAILURE);
    } else if(pthread_create(&input_thread, NULL, input, GlobalPeer)) {
        fprintf(stderr, "Error creating input thread\n");
        exit(EXIT_FAILURE);
    } else if(pthread_create(&tcp_server_thread, NULL, tcp_server, GlobalPeer)) {
        fprintf(stderr, "Error creating tcp server thread\n");
        exit(EXIT_FAILURE);
    }

    pthread_exit(NULL);
    return 0;
}


void *udp_server(void *argv) {
    
    peer currentpeer = (peer)(argv);
    int SOCK_UDP;
    struct sockaddr_in servaddr;
    struct timeval timeout;
 

    peer thispeer, recvpeer;
    thispeer = malloc(sizeof(struct PEER_));
    recvpeer = malloc(sizeof(struct PEER_));
    recvpeer->type = 0xFF;

    thispeer = currentpeer;
    thispeer->type = RESPONSE;

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    if ((SOCK_UDP = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}

    if (setsockopt(SOCK_UDP, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    CreateAddr(&servaddr, currentpeer->id);

    if (bind(SOCK_UDP, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Failed to bind");
        exit(EXIT_FAILURE);
    }
    
    while(1) {
        thispeer = currentpeer;
        thispeer->type = RESPONSE;
        if(recvfrom(SOCK_UDP, recvpeer, sizeof(struct PEER_), 0, NULL, NULL) > 0) {
            if(recvpeer->type == REQUEST) {
                if(recvpeer->first == currentpeer->id) {
                    currentpeer->predfirst = recvpeer->id;
                    //thispeer->pingseq[0]++;
                    recvpeer->pingseq[0] = currentpeer->pingseq[0];

                } else if(recvpeer->second == currentpeer->id) {
                    currentpeer->predsecond = recvpeer->id;
                    //thispeer->pingseq[1]++;
                    recvpeer->pingseq[1] = currentpeer->pingseq[1];
                }

                CreateAddr(&servaddr, recvpeer->id);
                sendto(SOCK_UDP, thispeer, sizeof(struct PEER_), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                ColourLine(YELLOW);
                printf("A ping request message was received from peer %d\n", recvpeer->id);
                ColourLine(DEFAULT);
            } else if (recvpeer->type == RESPONSE) {
                ColourLine(GREEN);
                printf("A ping response message was received from peer %d\n", recvpeer->id);
                ColourLine(DEFAULT);
                
                if(recvpeer->id == currentpeer->first) {
                    pingseq[0] = recvpeer->pingseq[0];
                } else if (recvpeer->id == currentpeer->second) {
                    pingseq[1] = recvpeer->pingseq[1];
                }

            }
        }
        if (currentpeer->pingseq[0] - pingseq[0] > 6) {
            ColourLine(RED);
            printf("Peer %d is no longer alive.\n", currentpeer->first);
            ColourLine(CYAN);
            currentpeer->first = currentpeer->second;
            currentpeer->pingseq[0] = currentpeer->pingseq[1];
            pingseq[0] = pingseq[1];
            printf("My first successor is now peer %d\n", currentpeer->first);
            if(pthread_create(&dead_peer_thread, NULL, dead_peer, currentpeer)) {
                fprintf(stderr, "Error creating udp server thread\n");
                exit(EXIT_FAILURE);
            } 
        } else if (currentpeer->pingseq[1] - pingseq[1] > 6) {
            ColourLine(RED);
            printf("Peer %d is no longer alive.\n", currentpeer->second);
            ColourLine(CYAN);
            printf("My first successor is now peer %d\n", currentpeer->first);
            
            pingseq[1] = currentpeer->pingseq[1];
            if(pthread_create(&dead_peer_thread, NULL, dead_peer, currentpeer)) {
                fprintf(stderr, "Error creating udp server thread\n");
                exit(EXIT_FAILURE);
            } 
        }
        

        if (shuttingdown) {
            break;
        }
    }
    close(SOCK_UDP);
    
    return NULL;
}

void *ping_client(void *argv) {
    int SOCK_UDP, SOCK_UDP2;
    struct sockaddr_in clientaddr;
    struct timeval timeout;
    peer currentpeer = (peer)(argv);
    peer ThisPeer;

    ThisPeer = malloc(sizeof(struct PEER_));
    ThisPeer->id = ID;
    ThisPeer->first = currentpeer->first;
    ThisPeer->second = currentpeer->second;
    ThisPeer->type = REQUEST;

    timeout.tv_sec = 0;
    timeout.tv_usec = 999999;

    if ((SOCK_UDP = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    if ((SOCK_UDP2 = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(SOCK_UDP, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(SOCK_UDP2, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    memset(&clientaddr, 0, sizeof(clientaddr));
    clientaddr.sin_family = AF_INET;
    clientaddr.sin_port = htons(PORT + ThisPeer->first);
    clientaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    connect(SOCK_UDP, (struct sockaddr *)&clientaddr, sizeof(clientaddr));

    memset(&clientaddr, 0, sizeof(clientaddr));
    clientaddr.sin_family = AF_INET;
    clientaddr.sin_port = htons(PORT + ThisPeer->second);
    clientaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    connect(SOCK_UDP, (struct sockaddr *)&clientaddr, sizeof(clientaddr));

    while (1) {
        // clientaddr.sin_port = htons((PORT + ThisPeer->first));
        sleep(2);
        ThisPeer->first = currentpeer->first;
        // printf("successor: %d\n", currentpeer->first);
        ThisPeer->pingseq[0] = currentpeer->pingseq[0];
        // ThisPeer->second = currentpeer->second;
        
        CreateAddr(&clientaddr, ThisPeer->first);
        sendto(SOCK_UDP, ThisPeer, sizeof(struct PEER_), 0, (struct sockaddr *)&clientaddr, sizeof(clientaddr));
        currentpeer->pingseq[0]++;
        sleep(3);

        ThisPeer->pingseq[1] = currentpeer->pingseq[1];
        // ThisPeer->first = currentpeer->first;
        ThisPeer->second = currentpeer->second;
        // printf("second: %d\n", currentpeer->second);
        CreateAddr(&clientaddr, ThisPeer->second);
        // clientaddr.sin_port = htons((PORT + ThisPeer->second));
        sendto(SOCK_UDP2, ThisPeer, sizeof(struct PEER_), 0, (struct sockaddr *)&clientaddr, sizeof(clientaddr));
        // sleep(2);
        currentpeer->pingseq[1]++;
        // sleep(2);
        if (shuttingdown) {
            break;
        }
    } 
    close(SOCK_UDP);
    while(1);

    return NULL;
}

void *input(void *argv) {
    peer thispeer = (peer)(argv);
    peer peerstuff = malloc(sizeof(struct PEER_));
    peerstuff->id = thispeer->id;
    unsigned int FILENAME = 0;
    size_t length = 13;
    ssize_t read;
    char *line = malloc(sizeof(length));
    //int SOCK_TCP1, SOCK_TCP2, SOCK_UDP;
    struct sockaddr_in clientaddr, serveraddr;

    while((read = getline(&line, &length, stdin)) != -1) {

        char *command = malloc(read*sizeof(char));
        char *filename_ascii = malloc(read*sizeof(char));
        sscanf(line, "%s %s",command,filename_ascii);
        FILENAME = atoi(filename_ascii);
        char *p = command;
        for( ; *p; ++p) *p = tolower(*p); //Convert string to lowercase, AUTHOR: J.F. Sebastian
        // printf("test: %s test: %d\n", filename_ascii, FILENAME);
        if(!strcmp(command, "request")) {
            thispeer->Filename = FILENAME;
            thispeer->hash = FILENAME % 256;
            printf("%d\n", thispeer->hash);
            thispeer->requestingpeer = thispeer->id;
            thispeer->type = LOOKING;

            int SOCK;

            if((SOCK = socket(AF_INET,SOCK_STREAM,0)) < 0) {
                perror("Socket creation failed");
                exit(EXIT_FAILURE);
            }

            CreateAddr(&clientaddr, thispeer->first);
            if(connect(SOCK, (struct sockaddr *)&clientaddr, sizeof(clientaddr)) < 0) {
                perror("Socket first connection failed");
                exit(EXIT_FAILURE);
            }
            send(SOCK, thispeer,sizeof(struct PEER_), 0);
            printf("File request message for %d has been sent to my successor.\n", thispeer->Filename);
            close(SOCK);
                
            
        } else if(!strcmp(command, "quit")) {

            ColourLine(YELLOW);
            printf("Peer gracefully quitting...\n");
            peerstuff = thispeer;

            if(pthread_create(&notify_depature_thread, NULL, notify_depature, peerstuff)) {
                fprintf(stderr, "Error creating tcp client thread\n");
                exit(EXIT_FAILURE);
            }
            pthread_join(notify_depature_thread, NULL);

            ColourLine(DEFAULT);
            exit(EXIT_SUCCESS);
            
        } else if(!strcmp(command, "info")) {
            ColourLine(CYAN);
            printf("Peer ID: %d\nPeer Successors: %d %d\nPeer Predecessors: %d %d\n", thispeer->id, 
            thispeer->first, thispeer->second, thispeer->predfirst, thispeer->predsecond);
            ColourLine(DEFAULT);
        }
        free(command);
    }

    return NULL;
}

void *tcp_server(void *argv) {
    peer currentpeer = (peer)(argv);
    int SOCK_TCP, RECV_TCP, TEMP_SOCK;
    struct sockaddr_in servaddr, clientaddr;
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    struct timeval timeout;

    peer thispeer, recvpeer;
    thispeer = malloc(sizeof(struct PEER_));
    recvpeer = malloc(sizeof(struct PEER_));
    recvpeer->type = 0xFF;

    thispeer = currentpeer;
    thispeer->type = RESPONSE;

    timeout.tv_sec = 2;
    timeout.tv_usec = 555555;

    if ((SOCK_TCP = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}

    if (setsockopt(SOCK_TCP, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    CreateAddr(&servaddr, currentpeer->id);

    if (bind(SOCK_TCP, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Failed to bind");
        exit(EXIT_FAILURE);
    }

    if (listen(SOCK_TCP, 1) < 0) {
        perror("Failed to listen");
        exit(EXIT_FAILURE);
    }

    while(1) {
        addr_size = sizeof(their_addr);
        RECV_TCP = accept(SOCK_TCP, (struct sockaddr*)&their_addr, &addr_size);

        while(recv(RECV_TCP, recvpeer, sizeof(struct PEER_), 0) > 0) {
            if(recvpeer->quit == LEAVING) {
                ColourLine(RED);
                printf("Peer %d will depart from the network\n", recvpeer->id);
                if(recvpeer->id == currentpeer->first) {
                    currentpeer->first = recvpeer->first;
                    currentpeer->second = recvpeer->second;
                } else {
                    currentpeer->second = recvpeer->first;
                }

                ColourLine(CYAN);
                printf("My first successor is now peer %d\n", currentpeer->first);
                printf("My second successor is now peer %d\n", currentpeer->second);
                ColourLine(DEFAULT);

                recvpeer->quit = QUIT;
                recvpeer->id = currentpeer->id;

                send(RECV_TCP, recvpeer, sizeof(struct PEER_), 0);
                close(RECV_TCP);
            } else if(recvpeer->quit == QUIT) {
                ColourLine(CYAN);
                printf("Peer %d told me to quit\n", recvpeer->id);
                ColourLine(DEFAULT);
            } else if(recvpeer->type == LOOKING) {
                if (recvpeer->hash == currentpeer->id) {
                    ColourLine(PURPLE);
                    printf("File %d is here.\n", recvpeer->Filename);
                    printf("A response message, destined for peer %d, has been sent.\n\n", recvpeer->requestingpeer);
                    ColourLine(YELLOW);
                    printf("We now start sending the file .....\n");
                    ColourLine(DEFAULT);
                    recvpeer->type = ANSWERED;
                    CreateAddr(&clientaddr, recvpeer->requestingpeer);
                    TEMP_SOCK = socket(AF_INET, SOCK_STREAM, 0);
                    if(connect(TEMP_SOCK, (struct sockaddr *)&clientaddr, sizeof(clientaddr))<0){
                        perror("Socket connection failed");
                        exit(EXIT_FAILURE);
                    }
                    send(TEMP_SOCK, recvpeer, sizeof(struct PEER_), 0);
                    close(TEMP_SOCK);
                } else if(recvpeer->hash > currentpeer->id && currentpeer->id > currentpeer->first) {
                    close(RECV_TCP);
                    recvpeer->type = FOUND;
                    SendCheck(TEMP_SOCK, recvpeer, currentpeer->id, currentpeer->first, clientaddr);
                } else if(recvpeer->hash > currentpeer->id && recvpeer->hash < currentpeer->first) {
                    recvpeer->type = FOUND;
                    recvpeer->respondingpeer = currentpeer->id;
                    close(RECV_TCP);
                    SendCheck(TEMP_SOCK, recvpeer, currentpeer->id, currentpeer->first, clientaddr);
                } else if(recvpeer->hash > currentpeer->id) {
                    close(RECV_TCP);
                    SendCheck(TEMP_SOCK, recvpeer, currentpeer->id, currentpeer->first, clientaddr);
                } else {
                    close(RECV_TCP);
                    SendCheck(TEMP_SOCK, recvpeer, currentpeer->id, currentpeer->first, clientaddr);
                }
            } else if(recvpeer->type == FOUND) {
                ColourLine(PURPLE);
                printf("File %d is here.\n", recvpeer->Filename);
                printf("A response message, destined for peer %d, has been sent.\n\n", recvpeer->requestingpeer);
                ColourLine(YELLOW);
                printf("We now start sending the file .....\n");
                ColourLine(DEFAULT);
                recvpeer->type = ANSWERED;
                recvpeer->respondingpeer = currentpeer->id;
                
                CreateAddr(&clientaddr, recvpeer->requestingpeer);
                TEMP_SOCK = socket(AF_INET, SOCK_STREAM, 0);
                if(connect(TEMP_SOCK, (struct sockaddr *)&clientaddr, sizeof(clientaddr))<0){
                    perror("Socket connection failed");
                    exit(EXIT_FAILURE);
                }
                send(TEMP_SOCK, recvpeer, sizeof(struct PEER_), 0);
                close(TEMP_SOCK);
                currentpeer->Filename = recvpeer->Filename;
                currentpeer->hash = recvpeer->hash;
                currentpeer->requestingpeer = recvpeer->requestingpeer;
                if(pthread_create(&file_transfer_thread, NULL, file_transfer, currentpeer)) {
                    fprintf(stderr, "Error creating udp server thread\n");
                    exit(EXIT_FAILURE);
                }
            } else if(recvpeer->type == ANSWERED) {
                currentpeer->respondingpeer = recvpeer->respondingpeer;
                if(pthread_create(&file_receive_thread, NULL, file_receive, currentpeer)) {
                    fprintf(stderr, "Error creating tcp server thread\n");
                    exit(EXIT_FAILURE);
                }
                recvpeer->type = 0xFF;
                ColourLine(PURPLE);
                printf("Received a response message from peer %d, which has the file %d\n", recvpeer->id, currentpeer->Filename);
                printf("We now start receiving the file .....\n");
                ColourLine(DEFAULT);

            } else if(recvpeer->type == DEAD) {
                recvpeer->first = currentpeer->first;
                recvpeer->second = currentpeer->second;
                recvpeer->type = QUIT;
                send(RECV_TCP, recvpeer, sizeof(struct PEER_), 0);
                close(RECV_TCP);
                recvpeer->type = 0xFF;
            }
        }

    }

    return NULL;
}

void *notify_depature(void *argv) {
    int SOCK_TCP;

    struct sockaddr_in clientaddr;
    peer currentpeer = (peer)(argv);
    peer recvpeer;
    int confirmation = 1;
    recvpeer = malloc(sizeof(struct PEER_));

    if ((SOCK_TCP = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}

    CreateAddr(&clientaddr, currentpeer->predfirst);

    if(connect(SOCK_TCP, (struct sockaddr *)&clientaddr, sizeof(clientaddr)) < 0) {
        perror("Socket first connection failed");
        exit(EXIT_FAILURE);
    }

    currentpeer->quit = LEAVING;

    send(SOCK_TCP, currentpeer, sizeof(struct PEER_), 0);

    while(confirmation) {
        if(recv(SOCK_TCP, recvpeer, sizeof(struct PEER_), 0) > 0) {
            if(recvpeer->quit == QUIT) {
                confirmation = 0;
            }
        }
    }

    close(SOCK_TCP);
    if ((SOCK_TCP = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}

    CreateAddr(&clientaddr, currentpeer->predsecond);

    if(connect(SOCK_TCP, (struct sockaddr *)&clientaddr, sizeof(clientaddr)) < 0) {
        perror("Socket second connection failed");
    }
    confirmation = 1;
    send(SOCK_TCP, currentpeer, sizeof(struct PEER_), 0);

    while(confirmation) {

        if(recv(SOCK_TCP, recvpeer, sizeof(struct PEER_), 0) > 0) {
            if(recvpeer->quit == QUIT) {
                ColourLine(GREEN);
                printf("Successfully Departed.\n");
                close(SOCK_TCP);
                ColourLine(DEFAULT);
                pthread_exit(NULL);
            }
        }
    }
    
    return NULL;
}

void *dead_peer(void *argv) {
    int SOCK_TCP;

    struct sockaddr_in clientaddr;
    peer currentpeer = (peer)(argv);
    peer recvpeer;

    int confirmation = 1;
    recvpeer = malloc(sizeof(struct PEER_));

    if ((SOCK_TCP = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}

    CreateAddr(&clientaddr, currentpeer->first);

    if(connect(SOCK_TCP, (struct sockaddr *)&clientaddr, sizeof(clientaddr)) < 0) {
        perror("Socket first connection failed");
        exit(EXIT_FAILURE);
    }

    currentpeer->type = DEAD;
    
    send(SOCK_TCP, currentpeer, sizeof(struct PEER_), 0);

    while(confirmation) {
        if(recv(SOCK_TCP, recvpeer, sizeof(struct PEER_), 0) > 0) {
            if(recvpeer->type == QUIT) {
                confirmation = 0;
            }
        }
    }
    
    if(recvpeer->first == currentpeer->second) {
        currentpeer->second = recvpeer->second;
    } else {
        currentpeer->second = recvpeer->first;
    }
    ColourLine(CYAN);
    printf("My second successor is now peer %d\n", currentpeer->second);
    ColourLine(DEFAULT);
    return NULL;
}

void *file_transfer(void *argv) {

    peer currentpeer = (peer)(argv);

    typedef struct PACKET_ {
        unsigned int seq;
        unsigned int ack;
        unsigned int size;
        unsigned char data[MSS];
    } Packet;

    Packet datagram;
    Packet rdatagram;

    int SOCK_UDP;
    struct sockaddr_in clientaddr, servaddr;
    socklen_t addr_size;
    struct timeval timeout;
    time_t ogtime, rawtime;
    
    
    char str[20];
    char *type = ".pdf";
   
    sprintf(str, "%d", currentpeer->Filename);
    strcat(str, type);

    //File setups
    FILE *file, *file2;
    file = fopen(str,"rb");
    file2 = fopen("responding_log.txt", "w");

    if (file == NULL) {
        printf("%s file failed to open\n", str);
        pthread_exit(NULL);
    }
 
    //Create sockets etc
    if ((SOCK_UDP = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    timeout.tv_sec = 1;
	timeout.tv_usec = 0; //1 second timeout

    if (setsockopt(SOCK_UDP, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    memset(&clientaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons((PORTFILE + currentpeer->requestingpeer));
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    connect(SOCK_UDP, (struct sockaddr *)&servaddr, sizeof(servaddr));

    addr_size = sizeof(servaddr);
    datagram.seq = 1;
    datagram.ack = 0;

    int ack = 0, done = 1;
    unsigned int oldack = 99999999;
    srand48(time(NULL));

    time(&ogtime);

    while(((datagram.size = fread(&(datagram.data), sizeof(char), MSS, file)) > 0) && (done)) {
        if(datagram.size == 0) {
            done = 0;
            ack = 1;
            break;
        } else {
            ack = 0;
        }
        
        while(ack != 1) {
            if(((float)rand()/(float)(RAND_MAX)) < DROP) {
                if(oldack == datagram.seq) {
                    fprintf(file2, PRINTLOGLINE, states[4], time(&rawtime) - ogtime, datagram.seq, datagram.size, datagram.ack);
                } else {
                    fprintf(file2, PRINTLOGLINE, states[2], time(&rawtime) - ogtime, datagram.seq, datagram.size, datagram.ack);
                }
            } else {
                sendto(SOCK_UDP, &datagram, sizeof(struct PACKET_), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                fprintf(file2, PRINTLOGLINE, states[0], time(&rawtime) - ogtime, datagram.seq, datagram.size, datagram.ack);
                oldack = datagram.seq;
            }

            if(recvfrom(SOCK_UDP, &rdatagram, sizeof(struct PACKET_), 0, (struct sockaddr *)&servaddr, &addr_size) > 0) {
                fprintf(file2, PRINTLOGLINE, states[1], time(&rawtime) - ogtime, rdatagram.seq, rdatagram.size, rdatagram.ack); 
                if(datagram.ack == rdatagram.seq) {
                    // datagram.seq = rdatagram.ack;
                    datagram.seq += datagram.size;
                    ack = 1;
                } else {
                    fprintf(file2, PRINTLOGLINE, states[3], time(&rawtime) - ogtime, rdatagram.seq, rdatagram.size, rdatagram.ack); 
                }
            }

        }
    }

    fclose(file);
    fclose(file2);

    printf("COMPLETED\n");
    return NULL;
}

void *file_receive(void *argv) {

    peer currentpeer = (peer)(argv);


    typedef struct PACKET_ {
        unsigned int seq;
        unsigned int ack;
        unsigned int size;
        unsigned char data[MSS];
    } Packet;

    Packet datagram;
    Packet rdatagram;

    int SOCK_UDP;
    struct sockaddr_in servaddr, clientaddr;
    socklen_t addr_size;
    time_t ogtime, rawtime;

    char str[20];
    char *type = "_done.pdf";

    sprintf(str, "%d", currentpeer->Filename);
    strcat(str, type);

    //File setups
    FILE *file, *file2;
    file = fopen(str, "wb");
    file2 = fopen("requesting_log.txt", "w");

    // Create sockets etc
    if ((SOCK_UDP = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons((PORTFILE + currentpeer->id));
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);


    if (bind(SOCK_UDP, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Failed to bind");
        exit(EXIT_FAILURE);
    }

    addr_size = sizeof(clientaddr);

    datagram.seq = 0;
    datagram.ack = 1;
    datagram.size = MSS;
    
    int n = 0, done = 0;
    time(&ogtime);

    while(!done) {
        if((n = recvfrom(SOCK_UDP, &rdatagram, sizeof(struct PACKET_), 0, (struct sockaddr *)&clientaddr, &addr_size)) > 0) {
            fprintf(file2, PRINTLOGLINE, states[1], time(&rawtime) - ogtime, rdatagram.seq, n, rdatagram.ack);

            if(datagram.seq == rdatagram.ack) {
                datagram.ack += rdatagram.size;
                datagram.size = rdatagram.size;
                sendto(SOCK_UDP, &datagram, sizeof(struct PACKET_), 0, (struct sockaddr *)&clientaddr, addr_size);
                if(datagram.size < MSS) {
                    done = 1;
                }
                n = fwrite(&rdatagram.data, sizeof(char), rdatagram.size, file);
                fprintf(file2, PRINTLOGLINE, states[0], time(&rawtime) - ogtime, datagram.seq, datagram.size, datagram.ack);
            } 
        } 

    }

    fclose(file);
    fclose(file2);
    close(SOCK_UDP);

    printf("COMPLETED\n");
    return NULL;
}

static inline void SendCheck(int sock, peer p, short id, short first, struct sockaddr_in addr) {
    ColourLine(CYAN);
    printf("File %d is not stored here\n", p->Filename);
    printf("File request message has been forwarded to my successor\n");
    ColourLine(DEFAULT);
    p->id = id;
    CreateAddr(&addr, first);
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(connect(sock, (struct sockaddr *)&addr, sizeof(addr))<0){
        perror("Socket connection failed");
        exit(EXIT_FAILURE);
    }
    send(sock, p, sizeof(struct PEER_), 0);
    close(sock);

}
static inline void CreateAddr(struct sockaddr_in *addr, short p) {
    memset(addr, 0, sizeof(struct sockaddr_in));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((PORT + p));
    addr->sin_addr.s_addr = htonl(INADDR_ANY);
}

void ColourLine(char *s) {
    printf("%s", s);
}
