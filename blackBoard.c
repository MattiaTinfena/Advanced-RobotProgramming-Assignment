#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>  
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/file.h>
#include <fcntl.h>
#include <errno.h>
#include "auxfunc.h"
#include <signal.h>

// process to whom that asked or received
#define askwr 1
#define askrd 0
#define recwr 3
#define recrd 2

#define nfds 19

#define HEIGHT 30
#define WIDTH 80

#define HMARGIN 5
#define WMARGIN 5
#define BMARGIN 2

#define PERIODBB 10000  // [us]

#define NUM_OBSTACLES 10
#define NUM_TARGET 5

// dronex;droney;target[x1],...,target[xn];target[y1],...,target[yn];
typedef struct {
    int x;
    int y;
} Drone_bb;

typedef struct {
    float x;
    float y;
} Force;

typedef struct {
    float x[NUM_TARGET];
    float y[NUM_TARGET];
    int value[NUM_TARGET];
} Targets;

typedef struct
{
    float x[NUM_OBSTACLES];
    float y[NUM_OBSTACLES];
} Obstacles;


int pid;
float reset_period = 10; // [s]
float reset = 0;

int pTarget = 0;
int pObst = 0;
int pDrone = 1;
int pInput = 1;

int colObst;
int rowObst;
int colTarget;
int rowTarget;
int val;

char ack [2] = "A\0";

// int nh = 1000, nw = 1000;

void sig_handler(int signo) {
    if (signo == SIGUSR1) {
        handler(BLACKBOARD,100);
    }
}

// void resizeHandler(int sig){
//     getmaxyx(stdscr, nh, nw);  /* get the new screen size */
//     endwin();
//     initscr();
//     start_color();
//     curs_set(0);
//     noecho();
//     win = newwin(nh-3, nw-3, 0, 0); 
//     }

void drawDrone(WINDOW * win, int row, int col){
    wattron(win, A_BOLD); // Attiva il grassetto
    wattron(win, COLOR_PAIR(1));   
    mvwprintw(win, row - 1, col, "|");     
    mvwprintw(win, row, col + 1, "--");
    mvwprintw(win, row, col, "+");
    mvwprintw(win, row + 1, col, "|");     
    mvwprintw(win, row , col -2, "--");
    wattroff(win, COLOR_PAIR(1)); 
    wattroff(win, A_BOLD); // Attiva il grassetto 
}

void drawObstacle(WINDOW * win, int row, int col){
    wattron(win, A_BOLD); // Attiva il grassetto
    wattron(win, COLOR_PAIR(2));   
    mvwprintw(win, row, col, "0");
    wattroff(win, COLOR_PAIR(2)); 
    wattroff(win, A_BOLD); // Attiva il grassetto 
}

void drawTarget(WINDOW * win, int row, int col, int val) {
    wattron(win, A_BOLD); // Attiva il grassetto
    wattron(win, COLOR_PAIR(3));  
    char val_str[2];
    sprintf(val_str, "%d", val); // Converte il valore in stringa
    mvwprintw(win, row, col, "%s", val_str); // Usa un formato esplicito
    wattroff(win, COLOR_PAIR(3)); 
    wattroff(win, A_BOLD); // Disattiva il grassetto
}

int randomSelect(int n) {
    unsigned int random_number;
    int random_fd = open("/dev/urandom", O_RDONLY);
    
    if (random_fd == -1) {
        perror("Error opening /dev/urandom");
        return -1;  // Indicate failure
    }

    if (read(random_fd, &random_number, sizeof(random_number)) == -1) {
        perror("Error reading from /dev/urandom");
        close(random_fd);
        return -1;  // Indicate failure
    }
    
    close(random_fd);  // Close file after successful read
    
    return random_number % n;
}

void setPermissions(){
    if (reset >= reset_period){
        reset = 0;
        pTarget = 1;
        pObst = 1;

        //For testing purposes

        colObst = randomSelect(WIDTH - (2*BMARGIN));
        rowObst = randomSelect(HEIGHT - (2*BMARGIN));
        colTarget = randomSelect(WIDTH - (2*BMARGIN));
        rowTarget = randomSelect(HEIGHT - (2*BMARGIN));
        val = randomSelect(10);
    }
    else{
        pTarget = 0;
        pObst = 0;
    }
}

int main(int argc, char *argv[]) {

    // Log file opening
    FILE *file = fopen("outputbb.txt", "w");
    if (file == NULL) {
        perror("Errore nell'apertura del file");
        exit(1);
    }

    int fds[4][4] = {0};

    if (argc < 5) {
        fprintf(stderr, "Uso: %s <fd_str[0]> <fd_str[1]> <fd_str[2]> <fd_str[3]>\n", argv[0]);
        exit(1);
    }

    for (int i = 0; i < 4; i++) {
        char *fd_str = argv[i + 1];

        int index = 0;

        // Tokenization each value and discard ","
        char *token = strtok(fd_str, ",");
        token = strtok(NULL, ",");

        // FDs ectraction
        while (token != NULL && index < 4) {
            fds[i][index] = atoi(token);
            index++;
            token = strtok(NULL, ",");
        }
    }

    // //FDs print
    //     for (int i = 0; i < 4; i++) {
    //     fprintf(file, "Descrittori di file estratti da fd_str[%d]:\n", i);
    //     for (int j = 0; j < 4; j++) {
    //         fprintf(file, "fds[%d]: %d\n", j, fds[i][j]);
    //     }
    // }

    pid = (int)getpid();
    char dataWrite [80] ;
    snprintf(dataWrite, sizeof(dataWrite), "b%d,", pid);

    if(writeSecure("log.txt", dataWrite,1,'a') == -1){
        perror("Error in writing in log.txt");
        exit(1);
    }

    // closing the unused fds to avoid deadlock
    close(fds[DRONE][askwr]);
    close(fds[DRONE][recrd]);
    close(fds[INPUT][askwr]);
    close(fds[INPUT][recrd]);
    close(fds[OBSTACLE][askwr]);
    close(fds[OBSTACLE][recrd]);
    close(fds[TARGET][askwr]);
    close(fds[TARGET][recrd]);

    // Reading buffer
    char data[80];
    ssize_t bytesRead;
    fd_set readfds;
    struct timeval tv;

    //Setting select timeout
    tv.tv_sec = 0;
    tv.tv_usec = 1000;
    
    signal(SIGUSR1, sig_handler);
    

    initscr();
    start_color();
    curs_set(0);
    noecho();
    cbreak();
    //getmaxyx(stdscr, nh, nw);
    WINDOW * win = newwin(HEIGHT, WIDTH, 5, 5); 
    

    // Definizione delle coppie di colori
    init_pair(1, COLOR_BLUE, COLOR_BLACK);     // Testo blu su sfondo nero
    init_pair(2, COLOR_RED , COLOR_BLACK);  // Testo arancione su sfondo nero
    init_pair(3, COLOR_GREEN, COLOR_BLACK);    // Testo verde su sfondo nero
    
    int colDrone = 20;
    int rowDrone = 20;

    colObst = randomSelect(WIDTH - (2*BMARGIN));
    rowObst = randomSelect(HEIGHT - (2*BMARGIN));
    colTarget = randomSelect(WIDTH - (2*BMARGIN));
    rowTarget = randomSelect(HEIGHT - (2*BMARGIN));
    val = randomSelect(10);
    //Drone_bb drone;
    //Force force_o, force_t;
    char drone_str[80];
    char forceO_str[80];
    char forceT_str[80];
  
    while (1) {
        
        reset += PERIODBB/1000000;
        setPermissions();

        // Update the main window
        werase(win);
        box(win, 0, 0);
        drawDrone(win, rowDrone + HMARGIN + BMARGIN,colDrone + WMARGIN + BMARGIN);
        drawObstacle(win, rowObst + HMARGIN + BMARGIN,colObst + WMARGIN + BMARGIN);
        drawTarget(win, rowTarget + HMARGIN + BMARGIN,colTarget + WMARGIN + BMARGIN, val + 1);
        wrefresh(win);
        
        //FDs setting for select
        FD_ZERO(&readfds);
        FD_SET(fds[DRONE][askrd], &readfds);
        FD_SET(fds[INPUT][askrd], &readfds);
        FD_SET(fds[OBSTACLE][askrd], &readfds);
        FD_SET(fds[TARGET][askrd], &readfds); 

        fprintf(file, "Sending drone position to [OB]\n");
        fflush(file);
        if (write(fds[OBSTACLE][recwr], &drone_str, sizeof(drone_str)) == -1) {
            fprintf(file,"[BB] Error sending drone position to [OBSTACLE]\n");
            fflush(file);
            exit(EXIT_FAILURE);
        }

        int sel = select(nfds, &readfds, NULL, NULL, &tv);
        
        if (sel == -1) {
            perror("Select error");
            break;
        } 

        // receiving force from obstacle.c and target.c and sending to drone
        fprintf(file, "Reading force_o \n");
        fflush(file);
        if (read(fds[OBSTACLE][askrd], &forceO_str, sizeof(forceO_str)) == -1){
            fprintf(file,"[BB] Error reading force_o\n");
            fflush(file);
            exit(EXIT_FAILURE);
        }
        fprintf(file, "Reading force_t \n");
        fflush(file);
        if (read(fds[TARGET][askrd], &forceT_str, sizeof(forceT_str)) == -1){
            fprintf(file,"[BB] Error reading force_t\n");
            fflush(file);
            exit(EXIT_FAILURE);
        }

        fprintf(file, "Sending force_o \n");
        fflush(file);
        if (write(fds[DRONE][recwr], &forceO_str, sizeof(forceO_str)) == -1) {
            fprintf(file,"[BB] error sending force_o to [DRONE]\n");
            fflush(file);
            exit(EXIT_FAILURE);
        }
        fprintf(file, "Reading force_t \n");
        fflush(file);
        if (write(fds[DRONE][recwr], &forceT_str, sizeof(forceT_str)) == -1) {
            fprintf(file,"[BB] error sending force_t to [DRONE]\n");
            fflush(file);
            exit(EXIT_FAILURE);
        }

        if(ready > 0){
            unsigned int rand = randomSelect(ready);
            int selected = fdsQueue[rand];

            if (selected == fds[DRONE][askrd]){
                fprintf(file, "selected drone\n");
                fflush(file);   
                //manda ack di lettura dato al drone
                
            } else if (selected == fds[INPUT][askrd]){
                fprintf(file, "selected input\n");
                fflush(file);
                char inp [12]; 
                read(fds[INPUT][askrd], inp, 12);
                char* extractedMsg = strchr(inp, ';'); // Trova il primo ';'
                if (extractedMsg != NULL) {
                    extractedMsg++; // Salta il ';'
                } else {
                    extractedMsg = inp; // Se ';' non trovato, usa l'intero messaggio
                }

                if (strcmp(extractedMsg, moves[0]) == 0) {
                    rowDrone--;
                    colDrone--;
                } else if (strcmp(extractedMsg, moves[1]) == 0) {
                    rowDrone--;
                } else if (strcmp(extractedMsg, moves[2]) == 0) {
                    rowDrone--;
                    colDrone++;
                } else if (strcmp(extractedMsg, moves[3]) == 0) {
                    colDrone--;
                } else if (strcmp(extractedMsg, moves[4]) == 0) {
                    ;
                } else if (strcmp(extractedMsg, moves[5]) == 0) {
                    colDrone++;
                } else if (strcmp(extractedMsg, moves[6]) == 0) {
                    rowDrone++;
                    colDrone--;
                } else if (strcmp(extractedMsg, moves[7]) == 0) {
                    rowDrone++;
                } else if (strcmp(extractedMsg, moves[8]) == 0) {
                    rowDrone++;
                    colDrone++;
                } else {
                    printf("Unknown command: %s\n", extractedMsg);
                }

                write(fds[INPUT][recwr],ack,strlen(ack) + 1);
                //legge nuovo input
                //lo manada direttamente al drone
                //aspetta la nuova drone position             
            } else if (selected == fds[OBSTACLE][askrd] || selected == fds[TARGET][askrd]){
                fprintf(file, "selected obstacle or drone\n");
                fflush(file);
                //if pObst == 1 || pTarget == 1
                    //send drone position to target
                    //read the target
                    //send drone position and target to obstacle
                    //read obstacle
            }else{
                fprintf(file, "Problems\n");
                fflush(file);
            }
        }
        usleep(PERIODBB);
    }

    return 0;
}