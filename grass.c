#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>

/*** Defines ***/
#define GRASS_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)
#define die(str) write(STDOUT_FILENO,"\x1b[2J",4);write(STDOUT_FILENO,"\x1b[H",3);char _buf[80]; \
    snprintf(_buf,sizeof(_buf),"Call %s failed in...%s():%d\r\n",str,__func__,__LINE__);perror(_buf);printf("\r");exit(1)
#define ABUF_INIT {NULL, 0}

/*** Data ***/
struct editorConfig {
    unsigned int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};
struct editorConfig E;

/*** Terminal ***/
void errmess(const char* failedCall, int lineNum){ 
    write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen when unsuccessful call 

    char buf[80];
    snprintf(buf, sizeof(buf), "%s():%d\n", failedCall, lineNum);
    perror(buf);
    exit(1); 
}
void disableRawMode(){
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {die("tcsetattr");}
}
void enableRawMode(){
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {die("tcgetattr");}
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 10;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {die("tcsetattr");}
}
char editorReadKey(){
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if (nread == -1 && errno != EAGAIN) {die("read");}
    }
    return c;
}
int getCursorPosition(int* rows, int* cols){
    char buf[32];
    unsigned int i = 0;
    // func unfinished
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    while (i < sizeof(buf)-1){
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    
    if (buf[i] != '\x1b' || buf[1] != '[') return -1;
    if ((sscanf(&buf[2], "%d;%d", rows, cols) != 2)) return -1;

    return 0;
}
int getWindowSize(int*rows, int*cols){
    struct winsize ws;
    
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** Append Buffer ***/
struct abuf{
    char* b;
    int len;
};
void abAppend(struct abuf* ab, const char* s, int len){
    char* new = realloc(ab->b, ab->len + len);
    
    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}
void abFree(struct abuf* ab){
    free(ab->b);
}

/*** Output ***/
void editorDrawRows(struct abuf* ab){
    int y;    
    for (y=0; y<E.screenrows; y++){
        if (y == E.screenrows / 3){
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome),
                "Grass Editor -- version %s", GRASS_VERSION);
            if (welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            if (padding){
                abAppend(ab, "@" , 1);
                padding--;
            }
            while (padding--) abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcomelen);
        } else {
            abAppend(ab, "@", 1);
        }
        
        abAppend(ab, "\x1b[K", 3); // clear line after cursor
        if (y < E.screenrows - 1){
            abAppend(ab, "\r\n", 2);
        }
    }
}
void editorRefreshScreen(){
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len); // write out buffer 
    abFree(&ab);
}

/*** Input ***/
void editorMoveCursor(char key){
    switch(key){
        case 'w':
            E.cy--; break;
        case 'a':
            E.cx--; break;
        case 's':
            E.cy++; break;
        case 'd':
            E.cx++; break;
    }
}
void editorProcessKeypress(){
    char c = editorReadKey();    

    switch(c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen on safe exit
            exit(0);
            break;
        case 'w':
        case 'a':
        case 's':
        case 'd':
            editorMoveCursor(c);
            break;
    }
}

/*** Init ***/
void initEditor(){
    E.cx = 0;
    E.cy = 0;
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {die("getWindowSize");}
}
int main(){
    enableRawMode();
    initEditor();
    while (1){
        editorRefreshScreen();
        editorProcessKeypress();    
    }
    return 0;
}