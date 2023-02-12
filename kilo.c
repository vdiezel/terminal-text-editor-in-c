/*** includes ***/
/* clone of the kilo text editor written in C */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** data ***/
struct termios orig_termios;

void die(const char *s) {
  perror(s);
  exit(1);
}

/*** terminal ***/

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) ==-1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = orig_termios;

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

/*** init ***/

int main() {
  enableRawMode();

  while(1) {
    char c = '\0';
    /* when read times out on cygwin, -1 is returned with errno EAGAIN */
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
    if (iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }
    if (c == 'q') break;
  }

  return 0;
}
