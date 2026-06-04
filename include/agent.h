#ifndef AGENT_H
#define AGENT_H

void agent_init(void);
char* agent_send_message(const char* user_text, int chat_id);
extern int is_cli_mode;

#endif // AGENT_H
