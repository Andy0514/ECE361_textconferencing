//
// Created by MacBook Pro on 2023-01-20.
//

#ifndef ECE361_TEXTCONFERENCING_SERVER_H
#define ECE361_TEXTCONFERENCING_SERVER_H

#include "packet.h"

struct SESSION_INFO_NODE;
struct CLIENT_INFO_NODE;

struct CLIENT_INFO_NODE {
    char username [MAX_NAME];
    char password [MAX_PASSWD];
    struct SESSION_INFO_NODE* active_session;
    int sockfd;
    struct CLIENT_INFO_NODE* next;
};

struct SESSION_INFO_NODE {
    char session_id[MAX_SESSION_ID];
    struct CLIENT_INFO_NODE* clients [SESSION_CAP];
    int num_connected_client;
    struct SESSION_INFO_NODE* prev;
    struct SESSION_INFO_NODE* next;
};

struct CLIENT_INFO_NODE* read_login();

// returns the CLIENT_INFO* node corresponding to the username
struct CLIENT_INFO_NODE* get_client_info (const char* username);

struct SESSION_INFO_NODE* get_session_info (const char* session_id);

struct SESSION_INFO_NODE* create_new_session (struct SESSION_INFO_NODE* head, char* session_id, struct CLIENT_INFO_NODE* client);

struct SESSION_INFO_NODE* remove_from_session(struct CLIENT_INFO_NODE* client);

void send_message_to_client(int sockfd, struct message* msg);

void send_string_to_client(int sockfd, const char* msg_str);

int handle_login (struct message* msg, int sockfd);

void handle_exit(struct message* msg);

void handle_join_session(struct message* msg, int sockfd);

void handle_leave_session(struct message* msg, int sockfd);

void handle_new_session(struct message* msg, int sockfd);

void handle_send_message(struct message* msg, int sockfd);

void handle_query(struct message* msg, int sockfd);

void remove_user_from_session(struct SESSION_INFO_NODE* session, struct CLIENT_INFO_NODE* client);
#endif //ECE361_TEXTCONFERENCING_SERVER_H
