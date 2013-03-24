#include <Lib/Incitatus.h>
#include <Lib/libc/stdio.h>
#include <Lib/libc/string.h>

static void parseInput(char* entry);
static int add(int x, int y);
static int sub(int x, int y);
static int multp(int x, int y);
static int div(int x, int y);

int main(void) {

    char c = 0;
    char entry[64];
    int i = 0;

    while(1) {

        puts("Input: ");

        while((c = getch()) != '\n' && c != 'e') {

            /* Handle backspace */
            if(c == '\b') {

                i--;
                entry[i] = '\0';

            } else {

                entry[i] = c;
                i++;

            }

        }

        if(c == 'e')
            break;

        puts("Answer: ");
        entry[i] = '\0';
        i = 0;
        parseInput(entry);
        putc('\n');

    }

    exit(0);
}

static void parseInput(char* entry) {

    char* left = strtok(entry, " ");
    char* op = strtok(NULL, " ");
    char* right = strtok(NULL, " ");

    int x = atoi(left);
    int y = atoi(right);

    if(left == NULL || op == NULL || right == NULL)
        puts("Invalid input!\n");


    switch(op[0]) {

        case '+':
            printf("%d", add(x, y));
            break;

        case '-':
            printf("%d", sub(x, y));
            break;

        case '*':
            printf("%d", multp(x, y));
            break;

        case '/':
            printf("%d", div(x, y));
            break;

    }

}

static int add(int x, int y) {

    return x + y;

}

static int sub(int x, int y) {

    return x - y;

}

static int multp(int x, int y) {

    return x * y;

}

static int div(int x, int y) {

    return x / y;

}