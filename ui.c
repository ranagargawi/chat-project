#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "ui.h"

/* ── ANSI helpers ── */
#define ESC "\033["
#define RESET    "\033[0m"
#define BOLD     "\033[1m"
#define DIM      "\033[2m"
#define C_YELLOW "\033[33m"
#define C_CYAN   "\033[36m"
#define C_MAGENTA "\033[35m"
#define C_GREEN  "\033[32m"

/* ── Terminal state ── */
static struct termios orig_term;
static int term_rows = 24;
static int term_cols = 80;

/* Layout (1-based rows):
   1          : header
   2          : top separator
   3..rows-2  : message scroll region
   rows-1     : bottom separator
   rows       : input line               */
static int msg_bot;   /* = term_rows - 2  (last row of scroll region) */
static int input_row; /* = term_rows                                   */

/* Current input buffer, kept so message functions can repaint after writing */
static char cur_input[512] = {0};

/* ── Cursor helpers ── */

static void cursor_to(int row, int col)
{
    printf(ESC "%d;%dH", row, col);
}

static void cursor_to_input_end(void)
{
    cursor_to(input_row, 3 + (int)strlen(cur_input));
}

/* ── Init / cleanup ── */

void ui_init(const char *my_username)
{
    /* Get terminal dimensions */
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 6) {
        term_rows = ws.ws_row;
        term_cols = ws.ws_col;
    }
    msg_bot   = term_rows - 2;
    input_row = term_rows;

    /* Raw mode: char-by-char, no echo */
    tcgetattr(STDIN_FILENO, &orig_term);
    struct termios raw = orig_term;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    /* Clear screen */
    printf("\033[2J");

    /* Set scroll region: rows 3..msg_bot */
    printf(ESC "3;%dr", msg_bot);

    /* Header row */
    cursor_to(1, 1);
    printf(BOLD C_CYAN " Chat — %s" RESET, my_username);
    cursor_to(1, term_cols - 19);
    printf(DIM "/help for commands" RESET);

    /* Top separator */
    cursor_to(2, 1);
    for (int i = 0; i < term_cols; i++) printf("─");

    /* Bottom separator */
    cursor_to(term_rows - 1, 1);
    for (int i = 0; i < term_cols; i++) printf("─");

    /* Input prompt */
    cursor_to(input_row, 1);
    printf("> ");

    fflush(stdout);
}

void ui_cleanup(void)
{
    /* Reset scroll region, move to bottom, restore terminal */
    printf(ESC "r");                      /* reset scroll region to full screen */
    cursor_to(term_rows, 1);
    printf("\n" RESET);
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_term);
}

/* ── Message display ── */

static void get_timestamp(char *buf, int sz)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(buf, sz, "%02d:%02d", t->tm_hour, t->tm_min);
}

/* Scroll the message area up one line and print `line` at the bottom.
   Always restores cursor to the end of the input line. */
static void append_msg(const char *line)
{
    cursor_to(msg_bot, 1);    /* go to bottom of scroll region          */
    printf("\n\r");            /* LF → scrolls region up; CR → col 1    */
    printf(ESC "K");           /* clear this (now blank) line             */
    printf("%s" RESET, line);
    cursor_to_input_end();
    fflush(stdout);
}

void ui_msg_system(const char *text)
{
    char ts[8], line[600];
    get_timestamp(ts, sizeof ts);
    snprintf(line, sizeof line,
             DIM "[%s]" RESET " " C_GREEN "* %s" RESET, ts, text);
    append_msg(line);
}

void ui_msg_direct(const char *sender, const char *body)
{
    char ts[8], line[600];
    get_timestamp(ts, sizeof ts);
    snprintf(line, sizeof line,
             DIM "[%s]" RESET " "
             C_MAGENTA "DM " RESET
             BOLD "%s" RESET ": %s",
             ts, sender, body);
    append_msg(line);
}

void ui_msg_group(const char *group, const char *sender, const char *body)
{
    char ts[8], line[600];
    get_timestamp(ts, sizeof ts);
    snprintf(line, sizeof line,
             DIM "[%s]" RESET " "
             C_CYAN "#%s " RESET
             BOLD "%s" RESET ": %s",
             ts, group, sender, body);
    append_msg(line);
}

/* ── Input line ── */

void ui_set_input(const char *buf)
{
    strncpy(cur_input, buf ? buf : "", sizeof cur_input - 1);
    cursor_to(input_row, 1);
    printf(ESC "K");   /* clear to end of line */
    printf("> %s", cur_input);
    fflush(stdout);
}

/* ── Character read ── */

int ui_readch(void)
{
    unsigned char c = 0;
    if (read(STDIN_FILENO, &c, 1) <= 0) return -1;
    return (int)c;
}