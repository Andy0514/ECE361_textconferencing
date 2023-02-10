// Code is adapted from Beej's Guide
#include "packet.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <math.h>

#define BACKLOG 20

struct CLIENT_INFO_NODE* client_info_head = NULL;
struct SESSION_INFO_NODE* session_info_head = NULL;

#define LOGIN_FILE "login.txt"

// get sockaddr, IPv4 or IPv6
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, const char** argv) {

    // process command line input
    if (argc != 2) {
        printf("Error - please run this command as 'server <TCP port to listen on>'\n");
        exit(1);
    }

    // read login information
    client_info_head = read_login();
    if (client_info_head == NULL) {
        printf("Error - no client login information is found\n");
        exit(1);
    }

    int sockfd; // listen on sock_fd
    struct addrinfo hints, *servinfo;
    struct sockaddr_storage client_addr; // connector's address information socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
        printf("Error: getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    struct addrinfo* curr = servinfo;
    for (; curr != NULL; curr = curr->ai_next) {
        if ((sockfd = socket(curr->ai_family, curr->ai_socktype, curr->ai_protocol)) == -1) {
            printf("server: socket\n");
            continue;
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            printf("setsockopt\n");
            exit(1);
        }
        if (bind(sockfd, curr->ai_addr, curr->ai_addrlen) == -1) {
            close(sockfd);
            printf("server: bind\n");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if(curr==NULL){
        printf("server: failed to bind\n");
        exit(1);
    }
    if (listen(sockfd, BACKLOG) == -1) {
        printf("listen\n");
        exit(1);
    }
    printf("Server: Listening for connection on port %s\n", argv[1]);

    int highest_fd = sockfd;
    fd_set active_fd;
    FD_ZERO(&active_fd);
    FD_SET(sockfd, &active_fd);

    while (1) {
        fd_set fd_copy = active_fd;

        if (select(highest_fd + 1, &fd_copy, NULL, NULL, NULL) == -1) {
            printf("Select error\n");
            exit(1);
        }

        // fd_copy will only be left with the fd's that can be read right now
        for (int i = 0; i <= highest_fd; i++) {
            if (FD_ISSET(i, &fd_copy)) {

                if (i == sockfd) {
                    // This is the socket that listens for incoming connections.
                    // Establish a new connection here.
                    socklen_t sin_size = sizeof(struct sockaddr_storage);
                    int new_fd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size);
                    if (new_fd == -1) {
                        printf("Accept connection error\n");
                        continue;
                    }

                    inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr *)&client_addr), s, sizeof s);
                    printf("server: got connection from %s\n", s);

                    // add the new fd to the file set
                    FD_SET(new_fd, &active_fd);
                    highest_fd = (highest_fd > new_fd) ? highest_fd : new_fd;
                } else {
                    // handle regular communication with clients that have already connected
                    char buf[MAX_STR_LEN];
                    int num_read = recv(i, buf, MAX_STR_LEN, 0);
                    if (num_read == -1) {
                        printf("Error when server reads from socket. %d\n", errno);
                        exit(1);
                    }

                    if (num_read == 0 || (num_read == -1 && errno == 54)) {
                        // Client disconnected
                        struct CLIENT_INFO_NODE* curr = client_info_head;
                        while (curr != NULL) {
                            if (curr->sockfd == i) {
                                struct SESSION_INFO_NODE* session = curr->active_session;
                                if (session) {
                                    remove_user_from_session(session, curr);
                                }
                                curr->active_session = NULL;
                                curr->sockfd = -1;
                                break;
                            }
                            curr = curr->next;
                        }
                        assert(curr != NULL);
                        printf("Client %s disconnected\n", curr->username);
                        close(i);
                        FD_CLR(i, &active_fd);
                        continue;
                    }

                    buf[num_read] = '\0';
                    struct message * msg = str_to_message(buf);
                    int result;
                    switch (msg->type) {
                        case REGISTER:
                            // Register doesn't involve logging in, so the FD set is cleared rightaway.
                            // User has to establish a separate connection to log in.
                            handle_register_user(msg, i);
                            FD_CLR(i, &active_fd);
                            break;
                        case LOGIN:
                            result = handle_login(msg, i);
                            if (result == -1) {
                                FD_CLR(i, &active_fd);
                            }
                            break;
                        case EXIT:
                            handle_exit(msg);
                            FD_CLR(i, &active_fd);
                            break;
                        case JOIN:
                            handle_join_session(msg, i);
                            break;
                        case LEAVE_SESS:
                            handle_leave_session(msg, i);
                            break;
                        case NEW_SESS:
                            handle_new_session(msg, i);
                            break;
                        case MESSAGE:
                            handle_send_message(msg, i);
                            break;
                        case QUERY:
                            handle_query(msg, i);
                            break;
                        default:
                            printf("No packet type has been matched");
                            exit(1);

                    }
                    free(msg);
                }
            }
        }
    }
}



struct CLIENT_INFO_NODE* read_login() {
    // Reads the login information from a text file, login.txt
    FILE* fp = fopen(LOGIN_FILE, "r");
    if (fp == NULL) {
        printf("Error: Can't read the login file\n");
        exit(1);
    }

    char *line;
    size_t len = 0;
    char delim[] = " \t\r\n\v\f";

    struct CLIENT_INFO_NODE* head = NULL;
    struct CLIENT_INFO_NODE* curr;

    while (getline(&line, &len, fp) != -1) {
        if (!head) {
            head = malloc(sizeof(struct CLIENT_INFO_NODE));
            curr = head;
        } else {
            curr->next = malloc(sizeof(struct CLIENT_INFO_NODE));
            curr = curr -> next;
        }
        char* ptr = strtok(line, delim);
        strcpy(curr->username, ptr);

        ptr = strtok(NULL, delim);
        strcpy(curr->password, ptr);

        curr->next = NULL;
        curr->active_session = NULL;
        curr->sockfd = -1;
    }
    free(line);
    fclose(fp);
    return head;
}

struct CLIENT_INFO_NODE* get_client_info (const char* username) {
    struct CLIENT_INFO_NODE* curr = client_info_head;
    while (curr != NULL) {
        if (strcmp(username, curr->username) == 0) {
            return curr;
        }
        curr = curr -> next;
    }
    return NULL;
}



struct SESSION_INFO_NODE* get_session_info (const char* session_id) {
    struct SESSION_INFO_NODE* curr = session_info_head;
    while (curr != NULL) {
        if (strcmp(session_id, curr->session_id) == 0) {
            return curr;
        }
        curr = curr -> next;
    }
    return NULL;
}


void send_message_to_client(int sockfd, struct message* msg) {
    // Send TCP message to client
    const char* msg_str = message_to_str(msg);
    printf("Sending message: %s\n", msg_str);
    send_string_to_client(sockfd, msg_str);
    free(msg_str);
}

void send_string_to_client(int sockfd, const char* msg_str) {
    // Send TCP message to client
    if (send(sockfd, msg_str, strlen(msg_str), 0) == -1) {
        printf("Error sending message: %d\n", errno);
        exit(1);
    }
}


int handle_login(struct message* msg, int sockfd) {
    // msg is the login message
    // Must check the username and password against the known database.
    // If login is successful, a positive fd will be set in matching_username->sockfd.
    // This also sends a response to the client
    struct message new_msg;
    strcpy(new_msg.source, "SERVER");
    new_msg.type = LO_NAK;

    struct CLIENT_INFO_NODE* matching_username = get_client_info(msg->source);
    if (matching_username) {
        if (strcmp(msg->data, matching_username->password) == 0) {

            if (matching_username->sockfd != -1) {
                strcpy(new_msg.data, "You have already logged in elsewhere\n");
            } else {
                // successful log in
                matching_username->sockfd = sockfd;
                new_msg.type = LO_ACK;
                strcpy(new_msg.data, "");
            }
        } else {
            strcpy(new_msg.data, "invalid password");
        }
    } else {
        strcpy(new_msg.data, "username not found");
    }

    new_msg.size = strlen(new_msg.data) + 1;

    send_message_to_client(sockfd, &new_msg);
    return (new_msg.type == LO_ACK ? 0 : -1);
}

void handle_exit(struct message* msg) {
    struct CLIENT_INFO_NODE* matching_username = get_client_info(msg->source);
    if (matching_username) {

        // if currently in a session, leave this session
        if (matching_username->active_session != NULL) {
            struct SESSION_INFO_NODE* session = matching_username->active_session;
            remove_user_from_session(session, matching_username);
            matching_username->active_session = NULL;
        }

        close(matching_username->sockfd);
        matching_username->sockfd = -1;
    }
}

void handle_join_session(struct message* msg, int sockfd) {
    struct CLIENT_INFO_NODE* matching_username = get_client_info(msg->source);
    struct message new_msg;
    strcpy(new_msg.source, "SERVER");
    new_msg.type = JN_NAK;
    char error_msg[MAX_STR_LEN];

    // join a session that has already been created, and not yet at capacity
    if (matching_username) {
        if (matching_username->sockfd == sockfd && matching_username->active_session == NULL) {
            struct SESSION_INFO_NODE* matching_session = get_session_info(msg->data);

            if (matching_session) {
                int added = 0;
                for (int i = 0; i < SESSION_CAP; i++) {
                    if (matching_session->clients[i] == NULL) {
                        matching_session->clients[i] = matching_username;
                        added = 1;
                        matching_session->num_connected_client++;
                        matching_username->active_session = matching_session;
                        printf("%s %p %d\n", matching_username->username, matching_username->active_session, matching_username->sockfd);
                        break;
                    }
                }

                if (!added) {
                    // the session is full
                    sprintf(error_msg, "%s - the session is full!", msg->data);
                    strcpy(new_msg.data, error_msg);
                } else {
                    new_msg.type = JN_ACK;
                    strcpy(new_msg.data, msg->data);
                }
            } else {
                sprintf(error_msg, "%s - you entered an invalid session ID", msg->data);
                strcpy(new_msg.data, error_msg);
            }

        } else if (matching_username->sockfd != sockfd) {
            // user hasn't logged in yet (at least on this client)
            sprintf(error_msg, "%s - you need to log in first", msg->data);
            strcpy(new_msg.data, error_msg);
        } else if (matching_username->active_session != NULL) {
            // the user is already in a session
            sprintf(error_msg, "%s - you're already in a session. Leave the session first.", msg->data);
            strcpy(new_msg.data, error_msg);
        }
    } else {
        // The user is not authenticated...
        sprintf(error_msg, "%s - client ID unrecognized.", msg->data);
        strcpy(new_msg.data, error_msg);
    }

    new_msg.size = strlen(new_msg.data) + 1;
    send_message_to_client(sockfd, &new_msg);
}


void handle_leave_session(struct message* msg, int sockfd) {
    struct CLIENT_INFO_NODE* matching_username = get_client_info(msg->source);

    // leave the current session
    if (matching_username) {
        if (matching_username->sockfd == sockfd && matching_username->active_session != NULL) {
            struct SESSION_INFO_NODE *matching_session = matching_username->active_session;
            remove_user_from_session(matching_session, matching_username);
            matching_username->active_session = NULL;
        }
    }
    // No need to send any reply, even if it results in error
}

// Helps with deleting a user from a session, and clearing the session too if it's now empty
void remove_user_from_session(struct SESSION_INFO_NODE* session, struct CLIENT_INFO_NODE* client) {
    int found = 0;
    for (int i = 0; i < SESSION_CAP; i++) {
        if (session->clients[i] == client) {
            session->clients[i] = NULL;
            found = 1;
            session->num_connected_client--;

            if (session->num_connected_client == 0) {
                struct SESSION_INFO_NODE* p = session->prev;
                struct SESSION_INFO_NODE* n = session->next;

                // No more clients in this session, erase it
                if (p && n) {
                    p->next = n;
                    n->prev = p;
                } else if (p) {
                    p->next = NULL;
                } else {
                    if (n) n->prev = NULL;
                    session_info_head = n;
                }
            } else {
                printf("There are still %d users in session\n", session->num_connected_client);
            }
            break;
        }
    }
    assert(found);
}

// Create and join a session
void handle_new_session(struct message* msg, int sockfd) {
    struct CLIENT_INFO_NODE* matching_username = get_client_info(msg->source);
    struct message new_msg;
    strcpy(new_msg.source, "SERVER");
    new_msg.type = NS_NAK;
    char error_msg[MAX_STR_LEN];

    if (matching_username) {
        if (matching_username->sockfd == sockfd && matching_username->active_session == NULL) {

            // see if a session already exists with this session ID
            struct SESSION_INFO_NODE* matching_session = get_session_info(msg->data);
            if (matching_session == NULL) {
                // Add a session node to the head of the linked list
                struct SESSION_INFO_NODE *new_session = malloc(sizeof(struct SESSION_INFO_NODE));
                if (session_info_head) {
                    session_info_head->prev = new_session;
                }
                new_session->next = session_info_head;
                new_session->prev = NULL;
                session_info_head = new_session;

                assert(msg->size <= MAX_SESSION_ID);
                strncpy(new_session->session_id, msg->data, msg->size);
                new_session->num_connected_client = 1;
                for (int client = 0; client < SESSION_CAP; client++) {
                    if (client == 0) {
                        new_session->clients[client] = matching_username;
                        matching_username->active_session = new_session;
                    } else {
                        new_session->clients[client] = NULL;
                    }
                }
                new_msg.type = NS_ACK;
                strcpy(new_msg.data, msg->data);
            } else {
                // a session already exists with this name
                sprintf(error_msg, "%s - a session already exists with this name", msg->data);
                strcpy(new_msg.data, error_msg);
            }
        } else if (matching_username->active_session != NULL) {
            // User is in another session
            sprintf(error_msg, "%s - you need to exit the current session first", msg->data);
            strcpy(new_msg.data, error_msg);
        } else {
            // user hasn't logged in yet (at least on this client)
            sprintf(error_msg, "%s - you need to log in first", msg->data);
            strcpy(new_msg.data, error_msg);
        }

    } else {
        // The user is not authenticated...
        sprintf(error_msg, "%s - client ID unrecognized", msg->data);
        strcpy(new_msg.data, error_msg);
    }

    new_msg.size = strlen(new_msg.data) + 1;
    send_message_to_client(sockfd, &new_msg);
}


void handle_send_message(struct message* msg, int sockfd) {
    struct CLIENT_INFO_NODE* matching_username = get_client_info(msg->source);
    if (matching_username && matching_username->sockfd == sockfd) {
        struct SESSION_INFO_NODE* session = matching_username->active_session;

        if (session) {
            const char* str = message_to_str(msg);
            for (int i = 0; i < SESSION_CAP; i++) {
                if (session->clients[i] != NULL && session->clients[i] != matching_username) {
                    send_string_to_client(session->clients[i]->sockfd, str);
                }
            }
            free(str);
        }
    }
}


void handle_query(struct message* msg, int sockfd) {
    // Sends the list of users, and their active sessions back as reply.
    // Can only send the first 40 users back

    struct message new_msg;
    strcpy(new_msg.source, "SERVER");
    new_msg.type = QU_ACK;
    new_msg.data[0] = '\0';
    struct CLIENT_INFO_NODE* curr = client_info_head;
    int curr_length = 0;
    while (curr != NULL) {
        if (curr->sockfd != -1) {
            strcat(new_msg.data, curr->username);
            strcat(new_msg.data, ": ");
            curr_length += strlen(curr->username) + 2;
            if (curr->active_session == NULL) {
                strcat(new_msg.data, "no session\n");
                curr_length += 11;
            } else {
                strcat(new_msg.data, curr->active_session->session_id);
                curr_length += (strlen(curr->active_session->session_id) + 1);
                new_msg.data[curr_length - 1] = '\n';
                new_msg.data[curr_length] = '\0'; // This allows the next strcat to find the right place to concatenate
            }
        }
        curr = curr->next;

        if (curr_length > MAX_DATA - MAX_NAME - MAX_SESSION_ID - 20) {
            // It won't fit, so don't put any more data in
            strcat(new_msg.data, "...\n");
            curr_length += 4;
            break;
        }
    }
    curr_length++;
    new_msg.data[curr_length - 1] = '\0';
    new_msg.size = curr_length;
    send_message_to_client(sockfd, &new_msg);
}


// Check the user information and put it into the login file for persistent storage.
// Assume that the username and password are all valid (they're checked by the client).
// The user isn't automatically logged-in by this - they have to login separately.
void handle_register_user(struct message* msg, int sockfd) {
    struct message new_msg;
    strcpy(new_msg.source, "SERVER");
    new_msg.type = REG_NAK;

    struct CLIENT_INFO_NODE* existing_username = get_client_info(msg->source);
    if (existing_username != NULL) {
        strcpy(new_msg.data, "The username has already been registered.");
    } else {
        FILE* fp = fopen(LOGIN_FILE, "a");
        if (fp == NULL) {
            strcpy(new_msg.data, "Server cannot write to the login file.");
        } else {
            fprintf(fp, "%s %s\n", msg->source, msg->data);
            strcpy(new_msg.data, "");
            new_msg.type = REG_ACK;
            fclose(fp);
            printf("Registration successful for user %s\n", msg->source);

            // Refresh the login information
            client_info_head = read_login();
        }
    }
    new_msg.size = strlen(new_msg.data) + 1;
    send_message_to_client(sockfd, &new_msg);
}

