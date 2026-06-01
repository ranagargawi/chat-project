#ifndef UI_H
#define UI_H

void ui_init   (const char *my_username);
void ui_cleanup(void);

void ui_msg_system(const char *text);
void ui_msg_direct(const char *sender, const char *body);
void ui_msg_group (const char *group, const char *sender, const char *body);

/* Redraw the input line with the current buffer */
void ui_set_input(const char *buf);

/* Read one character from stdin (call only after select() says stdin ready).
   Returns the raw byte (127 for backspace, '\n' for enter, printable chars). */
int  ui_readch(void);

#endif /* UI_H */