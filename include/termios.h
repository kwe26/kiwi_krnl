/* Minimal termios: Kiwi's console is either cooked (ICANON|ECHO) or raw. */
#ifndef _TERMIOS_H
#define _TERMIOS_H
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
#define NCCS 8
struct termios {
    tcflag_t c_iflag, c_oflag, c_cflag, c_lflag;
    cc_t c_cc[NCCS];
};
#define ICANON 0x0002
#define ECHO   0x0008
#define ISIG   0x0001
#define ICRNL  0x0100
#define IXON   0x0400
#define VMIN   6
#define VTIME  5
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2
int  tcgetattr(int fd, struct termios *t);
int  tcsetattr(int fd, int when, const struct termios *t);
void cfmakeraw(struct termios *t);
#endif
