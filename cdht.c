#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>



int main(int argc, char **argv) {

    if (argc < 4 || argc > 6) {
        printf("Usage: cdht [0-255] [0-255] [0-255] MMS Loss-rate\n");
        exit(EXIT_FAILURE);
    }

    double DropRate = atof(argv[5]);

    for (int i = 1; i < 4; i++) {
        unsigned int input = atoi(argv[i]);
        if ((0xFF | input) != 255) {
            printf("Error: Arguments each must be between 0-255\n");
            exit(EXIT_FAILURE);
        }
    }

    
    printf("%d %d %d %d %0.1f\n", atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), DropRate);
    



    return 0;
}