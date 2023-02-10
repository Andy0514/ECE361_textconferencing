//
// Created by MacBook Pro on 2023-01-22.
//

#ifndef ECE361_TEXTCONFERENCING_CLIENT_H
#define ECE361_TEXTCONFERENCING_CLIENT_H

#endif //ECE361_TEXTCONFERENCING_CLIENT_H

enum CLIENT_ACTION_TYPE {
    CLIENT_LOGIN,
    CLIENT_LOGOUT,
    JOINSESSION,
    LEAVESESSION,
    CREATESESSION,
    LIST,
    QUIT,
    CLIENT_REGISTER,
    TEXT
};

char* get_user_input(enum CLIENT_ACTION_TYPE* action);

// return sockfd, or -1 for failure - try again
int handle_login(char* cmd, char* client_id);

void handle_register(char* cmd);

void handle_logout(int sockfd, char* client_id);

void handle_join_session(char* session_name, int sockfd, char* client_id);

void handle_leave_session(int sockfd, char* client_id);

void handle_create_session(char* session_name, int sockfd, char* client_id);

void handle_list(int sockfd, char* client_id);

void handle_send_text (int sockfd, char* msg, char* client_id);

void send_message_to_server(int sockfd, struct message* msg);

void send_string_to_server(int sockfd, const char* msg_str);