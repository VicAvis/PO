#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>  // inet_pton, sockaddr_in, htons, ntohl, htonl
#include <unistd.h>     // close, send, recv
#include <cstdint>

#define PORT 8080
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 1024


int main()
{
    int my_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (my_socket < 0){
        std::cerr << "Error creating socket.\n";
        return 1;
    }
    std::cout << "Client socket created. FD: " << my_socket << std::endl;

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr) <= 0){
        std::cerr << "Invalid address.\n";
        close(my_socket);
        return 1;
    }
     std::cout << "Server address configured for " << SERVER_IP << ":" << PORT << std::endl;

    if (connect(my_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0){
        std::cerr << "Connection failed.\n";
        close(my_socket);
        return 1;
    }
    std::cout << "Connected to server successfully!" << std::endl;


    // receiving the length of welcome message
    uint32_t len_be = 0; // sizeof(len_be) = uint32_t, size is 4 bytes, first we read the message's length and write it to len_be
    int received = recv(my_socket, (char*)&len_be, sizeof(len_be), 0);
    if (received < 0){
        std::cerr << "Failed to receive message length.\n";
        close(my_socket);
        return 1;
    }
    // from big endian
    uint32_t len = ntohl(len_be);
     std::cout << "Received welcome message length: " << len << std::endl;

// checking welcome message
    char buffer[BUFFER_SIZE];
    if (len >= BUFFER_SIZE) {
        std::cerr << "Welcome message too big for buffer.\n";
        close(my_socket);
        return 1;
    }     // better have receive_all check
    received = recv(my_socket, buffer, len, 0); // we read exactly 4 bytes!!
    if (received <= 0) {
        std::cerr << "Failed to receive message.\n";
        close(my_socket);
        return 1;
    }
    buffer[len] = '\0'; // not sure
    std::cout << "Server says: " << buffer << std::endl;

    while (true)
    {
        std::string input;
        std::cout << "Enter command (Time, Date, Exit): ";

        if (!std::getline(std::cin, input)) {
             std::cerr << "Error reading input.\n";
             break;
        }
        if (input.empty()) continue;

        uint32_t input_len_host = static_cast<uint32_t>(input.size()); // length of the message from user input
        uint32_t input_len_net = htonl(input_len_host); // convert to Big Endian to send to network

        // we send four bytes that represent the length of the message// send_all better
        ssize_t send_result = send(my_socket, (char*)&input_len_net, sizeof(input_len_net), 0);
        if (send_result < 0){
            std::cerr << "Failed to send command length.\n";
            break;
        }
        // sending the message itself
        send_result = send(my_socket, input.c_str(), input.size(), 0);
        if (send_result < 0){
            std::cerr << "Failed to send command message.\n";
            break;
        }
        std::cout << "Sent command: " << input << std::endl;

        if (input == "Exit")
        {
            std::cout << "Exit command sent. Closing connection.\n";
            break;
        }

        uint32_t reply_len_be = 0; // receiving the length of the answer
        received = recv(my_socket, (char*)&reply_len_be, sizeof(reply_len_be), 0);
        if (received < 0){
            std::cerr << "Failed to receive reply length.\n";
            break;
        }

        // converting received from Big Endian
        uint32_t reply_len = ntohl(reply_len_be);
        std::cout << "Received reply length: " << reply_len << std::endl;

        // checking the length of the reply and we will receive message back
        if (reply_len >= BUFFER_SIZE){
            std::cerr << "Server reply too big for buffer.";
            break;
        }
        received = recv(my_socket, buffer, reply_len, 0); // reading exactly  reply_len number of bytes
             if (received < 0){
                 std::cerr << "Failed to receive server reply.\n";
                 break;
             }
             buffer[reply_len] = '\0';
             std::cout << "Server reply: " << buffer << std::endl; // reply
        }

    std::cout << "Closing client socket FD: " << my_socket << std::endl;
    close(my_socket);

    return 0;
}