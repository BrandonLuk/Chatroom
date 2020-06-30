/*
    Preset messages to be used by the server/client.
*/

#pragma once


// Server-wide user joining message
// The input strings should be: <color> <username> <color reset>
static const char user_join_notice[] = "%s%s%s has joined.\n";

// Server-wide user leaving message
// The input strings should be: <color> <username> <color reset>
static char user_leave_notice[] = "%s%s%s has left.\n";

/*
    Notices for adding a client.
*/
static const char server_is_full_notice[] = "Sorry, the server is currently full.";
static const int server_is_full_notice_nbytes = sizeof(server_is_full_notice);

static const char name_request_msg[] = "Enter desired username:";
static const int name_request_msg_nbytes = sizeof(name_request_msg);

static const char color_request_msg[] = "Welcome, %s! Choose a display color. Your options are "
                                        "\x1B[32mGREEN\x1B[0m, "
                                        "\x1B[33mYELLOW\x1B[0m, "
                                        "\x1B[34mBLUE\x1B[0m, "
                                        "\x1B[35mMAGENTA\x1B[0m, "
                                        "\x1B[36mCYAN\x1B[0m, "
                                        "\x1B[37mWHITE\x1B[0m: ";

static const char server_join_msg[] = "You have joined the server.";
static const int server_join_msg_nbytes = sizeof(server_join_msg);


static const char retry_color_dialog[] = "That's not a recognized color! Try again: ";