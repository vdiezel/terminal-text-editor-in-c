/*** includes ***/
/* clone of the kilo text editor written in C */
/* using mostly VT100 escape sequences */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
struct editorConfig {
  int cx, cy;
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

/*** terminal ***/

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;

  /*
    BRKINT: flag to fore SIGINT for break conditions
    ICRNL: flag to translate carriage returns from 13 '\r' to 10 '\n'
    INPCK: enables parity checking
    ISTRIP: set the 8th bit of each input byte to be stripped (should be already turned off)
    IXON: flag for ctrl+s (stop data transmision), ctrl+q turns it on again
  */
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

  /*
    OPOST: flag for output processing features
  */
  raw.c_oflag &= ~(OPOST);

  /*
    CS8: bitmask - sets character size of 8 bits per byte
  */
  raw.c_cflag |= (CS8);

  /*  this is how we can flip a bitflag in c
      ECHO: 00000000000000000000000000001000
      ~: 11111111111111111111111111110111
      &= keeps all c_flags that are set and unset the ECHO flag
  */
  /*
    ECHO: Flag of echoing of typed characters
    ICANON: Flag for canonical mode (waiting for enter press)
    ISIG: Flag of SIGINT for Ctrl+c and SIGSTP for Ctrl+z
    IEXTEN: Flag to set ctrl+v for the terminal to wait for more input
  */
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  return c;
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }

  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  /* using escape sequence to have a more stable method
     of getting the window size
  */

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** append buffer ***/

struct abuf {
  char *b;
  int len;
};

#define ABUG_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab -> b, ab -> len + len);

  if (new == NULL) return;
  memcpy(&new[ab -> len], s, len);
  ab -> b = new;
  ab -> len += len;
}

void abFree(struct abuf *ab) {
  free(ab -> b);
}

/*** outputs ***/

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    if (y == E.screenrows / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
      if (welcomelen > E.screencols) welcomelen = E.screencols;
      int padding = (E.screencols - welcomelen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }
      while(padding--) abAppend(ab, " ", 1);
      abAppend(ab, welcome, welcomelen);
    } else {
      abAppend(ab, "~", 1);
    }

    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  struct abuf ab = ABUG_INIT;

  // hide cursor
  abAppend(&ab, "\x1b[?25l", 6);

  /* reposition the cursor */
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d:%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** inputs ***/

void editorMoveCursor(char key) {
  switch (key) {
    case 'a':
      E.cx--;
      break;
    case 'd':
      E.cx++;
      break;
    case 'w':
      E.cy--;
      break;
    case 's':
      E.cy++;
      break;
  }
}

void editorProcessKeypress() {
  char c = editorReadKey();

  switch (c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    case 'w':
    case 's':
    case 'a':
    case 'd':
      editorMoveCursor(c);
      break;
  }
}

/*** init ***/

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
  enableRawMode();
  initEditor();

  while(1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
