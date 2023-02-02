#ifndef ECE361_TEXTCONFERENCING_PACKET_H
#define ECE361_TEXTCONFERENCING_PACKET_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

// These all include the null-terminator, so the actual size
// is one-less
#define MAX_NAME 20
#define MAX_PASSWD 20
#define MAX_DATA 1000
#define MAX_STR_LEN MAX_NAME+MAX_PASSWD+MAX_DATA
#define MAX_SESSION_ID 20
#define SESSION_CAP 20


enum MSG_TYPE {
    LOGIN,
    LO_ACK,
    LO_NAK,
    EXIT,
    JOIN,
    JN_ACK,
    JN_NAK,
    LEAVE_SESS,
    NEW_SESS,
    NS_ACK,
    NS_NAK, // Doesn't have to be implemented, but I implemented it anyway
    MESSAGE,
    QUERY,
    QU_ACK
};

struct message {
    unsigned int type;
    unsigned int size; // includes the \0
    char source[MAX_NAME];
    char data[MAX_DATA];
};

/*
 * When storing a message in string format, use " " as separator. When the user enters
 * an ID, have to make sure it doesn't contain the " " character
 */
const char* message_to_str (struct message* msg) {
    char* buffer = malloc(msg->size + 100);
    // a '\0' will be added to the very end of buffer
    sprintf(buffer, "%d %d %s %s", msg->type, msg->size, msg->source, msg->data);
    return buffer;
}

struct message* str_to_message (const char* input) {
    struct message* result = malloc(sizeof(struct message));
    printf("%s\n", input);

    char* delim = strtok(input, " ");
    if (delim == NULL) {
        printf("Message string formatting error1: %s\n", input);
        exit(1);
    }
    result->type = atoi(delim);

    delim = strtok(NULL, " ");
    if (delim == NULL) {
        printf("Message string formatting error2: %s\n", input);
        exit(1);
    }
    result->size = atoi(delim);

    delim = strtok(NULL, " ");
    if (delim == NULL) {
        printf("Message string formatting error3: %s\n", input);
        exit(1);
    }
    strcpy(result->source, delim);

    if (result->size == 1) {
        strcpy(result->data, "");
    } else {
        delim = strtok(NULL, "\0");
        if (delim == NULL) {
            printf("Message string formatting error\n");
            exit(1);
        }
        strcpy(result->data, delim);
    }

    assert(result->data[result->size - 1] == '\0');
    return result;
}



#endif //ECE361_TEXTCONFERENCING_PACKET_H
