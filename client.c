// Code is adapted from Beej's Guide
#include "packet.h"
#include "client.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <math.h>
#include <fcntl.h>
#include <pthread.h>

 
// strategy: use pthread as a receiving thread to continue to listen to
// a socket and print its inputs. Use the main thread to poll user and
// send messages to the server

// variables used to control execution flow
int request_thread_exit = 0;

/*
 * Responsible for receiving messages from the socket, and printing message.
 * Use non-blocking IO, and upon leaving session, this thread terminates.
 */
void* receive_messages(void* fd) {
    int sockfd = *((int*) fd);
    char buf[MAX_STR_LEN];

    // use fd_set to listen for active message
    fd_set active_fd;
    FD_SET(sockfd, &active_fd);

    while (1) {
        fd_set fd_copy = active_fd;
        if (select(sockfd + 1, &fd_copy, NULL, NULL, NULL) == -1) {
            printf("Select error\n");
            close(sockfd);
            *(int*)fd = -1;
            return NULL;
        }

        if (FD_ISSET(sockfd, &fd_copy)) {
            // sockfd can be read from
            int num_read = recv(sockfd, buf, MAX_STR_LEN, 0);
            if (num_read == 0) {
                printf("Server disconnected!\n");
                close(sockfd);
                *(int*)fd = -1;
                return NULL;
            }

            if (num_read == -1) {
                // nothing from the socket, check if the thread has been terminated
                if (request_thread_exit == 1) {
                    return NULL;
                }
                continue;
            } else {
                // Received something from the server, so display the message. However, different
                // messages could be displayed, depending on server response type. Note that we
                // don't expect any login messages to be displayed here!
                buf[num_read] = '\0';
                struct message *msg = str_to_message(buf);

                switch (msg->type) {
                    case JN_ACK:
                        printf("Joined the session %s successfully\n", msg->data);
                        break;
                    case JN_NAK:
                        printf("Cannot join the session %s\n", msg->data);
                        break;
                    case NS_ACK:
                        printf("Created and joined the new session.\n");
                        break;
                    case NS_NAK:
                        printf("Could not create and/or join the new session.\n");
                        break;
                    case QU_ACK:
                        printf("Query result: \n%s\n", msg->data);
                        break;
                    case MESSAGE:
                        printf("Session message from %s: %s\n", msg->source, msg->data);
                        break;
                    case DM_MSG:
                        printf("Direct message from %s: %s\n", msg->source, msg->data);
                        break;
                    case DM_NAK:
                        printf("Could not send direct message: %s\n", msg->data);
                        break;
                    default:
                        printf("Received known / unexpected packet!!!\n");
                        break;
                }
                if (msg->type != LO_ACK && msg->type != LO_NAK) {
                    free(msg);
                } // the login acknowledgments are freed elsewhere
            }
        }
    }
}

int main(int argc, const char** argv) {

    if (argc != 1) {
        printf("Error - the client doesn't accept any arguments\n");
        exit(1);
    }

    int sockfd = -1;
    pthread_t receive_thread;
    char client_id[MAX_NAME];

    while (1) {
        enum CLIENT_ACTION_TYPE curr_action;
        char* buf = get_user_input(&curr_action);

        switch (curr_action) {
            case CLIENT_REGISTER:
                if (sockfd == -1) {
                    handle_register(buf);
                } else {
                    printf("Please log out first if you want to register a separate account\n");
                }
                break;
            case CLIENT_LOGIN:
                if (sockfd == -1) {
                    sockfd = handle_login(buf, client_id);
                    if (sockfd != -1) {
                        // login succeeded, create a new thread that listens on sockfd
                        pthread_create(&receive_thread, NULL, receive_messages, (void *) &sockfd);
                    } else {
                        sockfd = -1;
                        strncpy(client_id, "", MAX_NAME);
                    }
                } else {
                    printf("Error - you've logged in already!\n");
                }
                break;
            case CLIENT_LOGOUT:
                if (sockfd != -1) {
                    handle_logout(sockfd, client_id);
                    request_thread_exit = 1;
                    pthread_join(receive_thread, NULL);
                    close(sockfd);
                    sockfd = -1;
                } else {
                    printf("Error - you're not logged in\n");
                }
                break;
            case JOINSESSION:
                if (sockfd != -1) {
                    handle_join_session(buf, sockfd, client_id);
                } else {
                    printf("Please login first\n");
                }
                break;
            case LEAVESESSION:
                if (sockfd != -1) {
                    handle_leave_session(sockfd, client_id);
                } else {
                    printf("Please login first\n");
                }
                break;
            case CREATESESSION:
                if (sockfd != -1) {
                    handle_create_session(buf, sockfd, client_id);
                } else {
                    printf("Please login first\n");
                }
                break;
            case LIST:
                if (sockfd != -1) {
                    handle_list(sockfd, client_id);
                } else {
                    printf("Please login first\n");
                }
                break;
            case TEXT:
                if (sockfd != -1) {
                    handle_send_text(sockfd, buf, client_id);
                } else {
                    printf("Please login first\n");
                }
                break;
            case DM:
                if (sockfd != -1) {
                    handle_send_dm(sockfd, buf, client_id);
                } else {
                    printf("Please login first\n");
                }
                break;
            case QUIT:
                if (sockfd != -1) {
                    handle_logout(sockfd, client_id);
                    request_thread_exit = 1;
                    pthread_join(receive_thread, NULL);
                    close(sockfd);
                    sockfd = -1;
                }
                return 0;
            default:
                printf("Error - the previous action failed. Please try again.\n");
                break;
        }

        if (buf != NULL) {
            free(buf);
        }
    }
}

/*
 * Returns the corresponding action in "action", and
 * returns the text (if any) in a char ptr
 */
char* get_user_input(enum CLIENT_ACTION_TYPE* action) {

    char buf[MAX_STR_LEN];
    if (fgets(buf, MAX_STR_LEN-1, stdin) == NULL) {
        printf("Input error");
        return NULL;
    }

    // Remove the trailing newline, make sure the buffer is null-terminating
    buf[strlen(buf) - 1] = '\0';


    if (buf[0] == '/') {
        // get the first word
        char first_word[MAX_STR_LEN];

        char* delim = strtok(buf, " ");
        if (delim == NULL) {
            // There are no spaces in the command, the entire thing is the first word
            strcpy(first_word, buf);
        } else {
            strcpy(first_word, delim);
        }
        
        // this is likely a command
        if (strcmp(first_word, "/login") == 0) {
            char* the_rest = malloc(MAX_STR_LEN * sizeof(char));
            delim = strtok(NULL, "\0");
            if (delim != NULL) {
                strcpy(the_rest, delim);
            } else {
                // This is an error, but will handle it later
                strcpy(the_rest, "");
            }
            *action = CLIENT_LOGIN;
            return the_rest;
        } else if (strcmp(first_word, "/logout") == 0) {
            *action = CLIENT_LOGOUT;
            return NULL;
        } else if (strcmp(first_word, "/joinsession") == 0) {
            *action = JOINSESSION;
            char* the_rest = malloc(MAX_STR_LEN * sizeof(char));
            delim = strtok(NULL, "\0");
            if (delim != NULL) {
                strcpy(the_rest, delim);
            } else {
                // This is an error, but will handle it later
                strcpy(the_rest, "");
            }
            return the_rest;
        } else if (strcmp(first_word, "/leavesession") == 0) {
            *action = LEAVESESSION;
            return NULL;
        } else if (strcmp(first_word, "/createsession") == 0) {
            *action = CREATESESSION;
            char* the_rest = malloc(MAX_STR_LEN * sizeof(char));
            delim = strtok(NULL, "\0");
            if (delim != NULL) {
                strcpy(the_rest, delim);
            } else {
                // This is an error, but will handle it later
                strcpy(the_rest, "");
            }
            return the_rest;
        } else if (strcmp(first_word, "/list") == 0) {
            *action = LIST;
            return NULL;
        } else if (strcmp(first_word, "/quit") == 0) {
            *action = QUIT;
            return NULL;
        } else if (strcmp(first_word, "/register") == 0) {
            *action = CLIENT_REGISTER;
            char* the_rest = malloc(MAX_STR_LEN * sizeof(char));
            delim = strtok(NULL, "\0");
            if (delim != NULL) {
                strcpy(the_rest, delim);
            } else {
                strcpy(the_rest, "");
            }
            return the_rest;
        } else if (strcmp(first_word, "/dm") == 0) {
            *action = DM;
            char* the_rest = malloc(MAX_STR_LEN * sizeof(char));
            delim = strtok(NULL, "\0");
            if (delim != NULL) {
                strcpy(the_rest, delim);
            } else {
                strcpy(the_rest, "");
            }
            return the_rest;
        }
    }

    // If the code reaches here, the entire sentence is a message
    char* message = malloc(MAX_STR_LEN * sizeof(char));
    strcpy(message, buf);
    *action = TEXT;
    return message;
}


int handle_login(char* cmd, char* client_id) {
    // both incluing \0
#define IP_LENGTH 50
#define PORT_LENGTH 6

    int sockfd;
    char password[MAX_PASSWD];
    char server_ip[IP_LENGTH];
    char server_port[PORT_LENGTH];
    char* error_msg = "4 arguments are required: <client ID> <password> <server-IP> <server-port>\n";

    // get client ID
    char* delim = strtok(cmd, " ");
    if (delim == NULL) {
        printf("%s\n", error_msg);
        return -1;
    }

    strncpy(client_id, delim, MAX_NAME);
    if (client_id[MAX_NAME - 1] != '\0') {
        printf("client ID is too long, try again.\n");
        return -1;
    }


    // get password
    delim = strtok(NULL, " ");
    if (delim == NULL) {
        printf("%s\n", error_msg);
        return -1;
    }

    strncpy(password, delim, MAX_PASSWD);
    if (password[MAX_PASSWD - 1] != '\0') {
        printf("password is too long, try again\n");
        return -1;
    }


    //get server IP address;
    delim = strtok(NULL, " ");
    if (delim == NULL) {
        printf("%s\n", error_msg);
        return -1;
    }

    strncpy(server_ip, delim, IP_LENGTH);
    if (server_ip[IP_LENGTH - 1] != '\0') {
        printf("IP address is probably wrong, try again\n");
        return -1;
    }


    // get server port address;
    delim = strtok(NULL, " \n");
    if (delim == NULL) {
        printf("%s\n", error_msg);
        return -1;
    }

    strncpy(server_port, delim, PORT_LENGTH);
    if (server_port[PORT_LENGTH - 1] != '\0') {
        printf("IP port is probably wrong, try again\n");
        return -1;
    }

    struct addrinfo hints, *server_info;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if ((getaddrinfo(server_ip, server_port, &hints, &server_info)) != 0) {
        printf("Unable to reach the server you specified\n");
        return -1;
    }

    struct addrinfo* connectionPtr;
    // loop through all the results and connect to the first we can
    for(connectionPtr = server_info; connectionPtr != NULL; connectionPtr = connectionPtr->ai_next) {
        if ((sockfd = socket(connectionPtr->ai_family,
                             connectionPtr->ai_socktype,
                             connectionPtr->ai_protocol)) == -1) {
            printf("Socket error\n");
            continue;
        }

        if (connect(sockfd, connectionPtr->ai_addr, connectionPtr->ai_addrlen) == -1) {
            close(sockfd);
            printf("Cannot connect\n");
            continue;
        }
        break;
    }

    if (connectionPtr == NULL) {
        printf("The client failed to connect.\n");
        return -1;
    }

    freeaddrinfo(server_info);

    // Send login info to the server
    struct message login_message;
    login_message.type = LOGIN;
    login_message.size = strlen(password) + 1;
    strcpy(login_message.source, client_id);
    strcpy(login_message.data, password);
    send_message_to_server(sockfd, &login_message);

    // wait for server to confirm or deny login
    char buf[MAX_STR_LEN];
    int num_read = recv(sockfd, buf, MAX_STR_LEN, 0);
    buf[num_read] = '\0';

    struct message *msg = str_to_message(buf);
    if (msg->type == LO_ACK) {
        printf("You're now logged in as: %s\n", client_id);
        free(msg);
        return sockfd;
    } else {
        printf("Login failed: %s\n", msg->data);
        free(msg);
        return -1;
    }
}



void handle_register (char* cmd) {
    // both incluing \0
#define IP_LENGTH 50
#define PORT_LENGTH 6

    int sockfd;
    char client_id[MAX_NAME];
    char password[MAX_PASSWD];
    char server_ip[IP_LENGTH];
    char server_port[PORT_LENGTH];
    char* error_msg = "4 arguments are required: <new client ID> <password> <server IP> <server port>\n";

    // get client ID
    char* delim = strtok(cmd, " ");
    if (delim == NULL) {
        printf("%s\n", error_msg);
        return;
    }

    strncpy(client_id, delim, MAX_NAME);
    if (client_id[MAX_NAME - 1] != '\0') {
        printf("client ID is too long, try again.\n");
        return;
    }


    // get password
    delim = strtok(NULL, " ");
    if (delim == NULL) {
        printf("%s\n", error_msg);
        return;
    }

    strncpy(password, delim, MAX_PASSWD);
    if (password[MAX_PASSWD - 1] != '\0') {
        printf("password is too long, try again\n");
        return;
    }

    // Do format checking on the password - it can't contain spaces
    for (int i = 0; i < MAX_PASSWD; i++) {
        if (password[i] == ' ' || password[i] == '\t' || password[i] == '\r' || password[i] == '\n' || password[i] == '\f') {
            printf("Password contains invalid character, try again\n");
            return;
        }
    }


    //get server IP address;
    delim = strtok(NULL, " ");
    if (delim == NULL) {
        printf("%s\n", error_msg);
        return;
    }

    strncpy(server_ip, delim, IP_LENGTH);
    if (server_ip[IP_LENGTH - 1] != '\0') {
        printf("IP address is probably wrong, try again\n");
        return;
    }


    // get server port address;
    delim = strtok(NULL, " \n");
    if (delim == NULL) {
        printf("%s\n", error_msg);
        return;
    }

    strncpy(server_port, delim, PORT_LENGTH);
    if (server_port[PORT_LENGTH - 1] != '\0') {
        printf("IP port is probably wrong, try again\n");
        return;
    }

    struct addrinfo hints, *server_info;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if ((getaddrinfo(server_ip, server_port, &hints, &server_info)) != 0) {
        printf("Unable to reach the server you specified\n");
        return;
    }

    struct addrinfo* connectionPtr;
    // loop through all the results and connect to the first we can
    for(connectionPtr = server_info; connectionPtr != NULL; connectionPtr = connectionPtr->ai_next) {
        if ((sockfd = socket(connectionPtr->ai_family,
                             connectionPtr->ai_socktype,
                             connectionPtr->ai_protocol)) == -1) {
            printf("Socket error\n");
            continue;
        }

        if (connect(sockfd, connectionPtr->ai_addr, connectionPtr->ai_addrlen) == -1) {
            close(sockfd);
            printf("Cannot connect\n");
            continue;
        }
        break;
    }

    if (connectionPtr == NULL) {
        printf("The client failed to connect.\n");
        return;
    }

    freeaddrinfo(server_info);

    // Send login info to the server
    struct message login_message;
    login_message.type = REGISTER;
    login_message.size = strlen(password) + 1;
    strcpy(login_message.source, client_id);
    strcpy(login_message.data, password);
    send_message_to_server(sockfd, &login_message);

    // wait for server to confirm or deny login
    char buf[MAX_STR_LEN];
    int num_read = recv(sockfd, buf, MAX_STR_LEN, 0);
    buf[num_read] = '\0';

    struct message *msg = str_to_message(buf);
    if (msg->type == REG_ACK) {
        printf("You've now registered as: %s. Please login again.\n", client_id);
        free(msg);
    } else {
        printf("Registration failed: %s\n", msg->data);
        free(msg);
    }
}




void handle_join_session(char* session_name, int sockfd, char* client_id) {
    if (strtok(session_name, " ") == NULL) {
        // session name can't be NULL
        printf("Session name format error\n");
        return;
    }

    struct message join_message;
    join_message.type = JOIN;
    join_message.size = strlen(session_name) + 1;
    if (join_message.size >= MAX_SESSION_ID) {
        printf("Error - session ID is too long, must be %d characters or below\n", MAX_SESSION_ID - 1);
        return;
    }

    strcpy(join_message.source, client_id);
    strcpy(join_message.data, session_name);

    send_message_to_server(sockfd, &join_message);
}

void handle_logout(int sockfd, char* client_id) {
    struct message logout_message;
    logout_message.type = EXIT;
    logout_message.size = 1;
    logout_message.data[0] = '\0';
    strcpy(logout_message.source, client_id);
    send_message_to_server(sockfd, &logout_message);
}

void handle_leave_session(int sockfd, char* client_id) {
    struct message leave_session_message;
    leave_session_message.type = LEAVE_SESS;
    leave_session_message.size = 1;
    leave_session_message.data[0] = '\0';
    strcpy(leave_session_message.source, client_id);
    send_message_to_server(sockfd, &leave_session_message);
}

void handle_create_session(char* session_name, int sockfd, char* client_id) {
    if (strtok(session_name, " ") == NULL) {
        // session name can't be NULL
        printf("Session name format error\n");
        return;
    }
    struct message create_session_message;
    create_session_message.type = NEW_SESS;
    create_session_message.size = strlen(session_name) + 1;
    if (create_session_message.size >= MAX_SESSION_ID) {
        printf("Error - session ID is too long. must be %d characters or below\n", MAX_SESSION_ID - 1);
        return;
    }

    strcpy(create_session_message.source, client_id);
    strcpy(create_session_message.data, session_name);
    send_message_to_server(sockfd, &create_session_message);
}

void handle_list(int sockfd, char* client_id) {
    struct message list_message;
    list_message.type = QUERY;
    list_message.size = 1;
    list_message.data[0] = '\0';
    strcpy(list_message.source, client_id);
    send_message_to_server(sockfd, &list_message);
}

void handle_send_text (int sockfd, char* msg, char* client_id) {
    struct message text_message;
    text_message.type = MESSAGE;
    text_message.size = strlen(msg) + 1;
    strcpy(text_message.data, msg);
    strcpy(text_message.source, client_id);
    send_message_to_server(sockfd, &text_message);
}

void handle_send_dm (int sockfd, char* cmd, char* client_id) {

    struct message text_message;
    text_message.type = DM_REQ;
    strcpy(text_message.source, client_id);
    
    char receiver[MAX_NAME];
    char msg [MAX_DATA];
    int available_message_size = MAX_DATA - MAX_NAME - 2;
    char* error_msg = "2 arguments are requires: <receiver client ID> <message>";

    // get receiver ID
    char* delim = strtok(cmd, " ");
    if (delim == NULL) {
        printf("%s\n", error_msg);
        return;
    }

    strncpy(receiver, delim, MAX_NAME);
    if (receiver[MAX_NAME - 1] != '\0') {
        printf("Receiver ID is too long, try again.\n");
        return;
    }


    // get message
    delim = strtok(NULL, "\n");
    if (delim == NULL) {
        printf("%s\n", error_msg);
        return;
    }

    strncpy(msg, delim, available_message_size);
    if (msg[available_message_size - 1] != '\0') {
        printf("Message is too long and will be truncated.\n");
        msg[available_message_size-1] = '\0';
    }

    sprintf(text_message.data, "%s %s", receiver, msg);
    text_message.size = strlen(text_message.data) + 1;
    send_message_to_server(sockfd, &text_message);
}

void send_string_to_server(int sockfd, const char* msg_str) {
    // Send TCP message to client
    if (send(sockfd, msg_str, strlen(msg_str), 0) == -1) {
        printf("%s\n", msg_str);
        printf("Error sending message: %d\n", errno);
        exit(1);
    }
}

void send_message_to_server(int sockfd, struct message* msg) {
    const char* msg_str = message_to_str(msg);
    send_string_to_server(sockfd, msg_str);
    free(msg_str);
}

