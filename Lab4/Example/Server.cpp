#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h> //  sockaddr_in, INADDR_ANY, htonl, htons, ntohl
#include <arpa/inet.h>
#include <unistd.h>     //  close, read, write, send/recv
#include <chrono>
#include <ctime>
#include <cstdint>      // uint32_t

#define PORT 8080
#define MESSAGE_LENGTH 1024

uint32_t to_big_endian(uint32_t val){
    return htonl(val); // htonl = Host To Network Long
}

uint32_t from_big_endian(uint32_t val){
    return ntohl(val);
}

int create_socket(){
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        std::cerr << "Socket creation failed\n";
        return -1;
    }
    std::cout << "Socket created successfully. FD: " << sockfd << std::endl;
    return sockfd;
}

int bind_socket(int sockfd, int port){
    sockaddr_in serverAddr = {}; //  IPv4
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY instructs listening socket to bind to all available interfaces
    serverAddr.sin_port = htons(port);     //  port number to Host To Network Short


    if(bind(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0){  // binding socket sockfd with address serverAddr.
        std::cerr << "Bind failed\n";
        return -1;
    }
    std::cout << "Socket bound to port " << port << std::endl;
    return 0;
}

int accept_connection(int sockfd){
    sockaddr_in clientAddr{}; // ip
    socklen_t clientLen = sizeof(clientAddr);

    int clientSocket = accept(sockfd, (sockaddr*)&clientAddr, &clientLen); // pops the first request in the waiting queue
    // and creates a new socket to deal with that client
    if(clientSocket < 0){
        std::cerr << "Accept failed\n";
        return -1;
    }
    std::cout << "Client connected. New socket FD: " << clientSocket << std::endl;
    return clientSocket;
}

bool send_all(int sockfd, const char* data, int len){
    int totalSent = 0;
    while(totalSent < len){
        int sent = send(sockfd, data + totalSent, len - totalSent, 0);
        if (sent <= 0) {
             return false;
        }
        totalSent += sent;
    }
    return true;
}

void send_data(int sockfd, const std::string& message){
    uint32_t len = static_cast<uint32_t>(message.size()); // how long is the message?
    uint32_t len_be = to_big_endian(len); // converting to Big Endian

    if(!send_all(sockfd, reinterpret_cast<const char*>(&len_be), sizeof(len_be))){
        std::cerr << "Failed to send length\n";
        return;
    }
    if(!send_all(sockfd, message.c_str(), message.size())){
        std::cerr << "Failed to send message\n";
    }
     // std::cout << "Sent (" << message.size() << " bytes): " << message << std::endl;
}

bool receive_all(int sockfd, char* buffer, int len){ // socket descriptor, where to write data, how many bytes to read
    int totalReceived = 0;
    while(totalReceived < len){
        int received = recv(sockfd, buffer + totalReceived, len - totalReceived, 0);
        if (received < 0) {
            return false;
        }
        totalReceived += received; // making sure to wait till 4 bytes are read
    }
    return true;
}

bool receive_data(int sockfd, std::string& message){ // Змінено сигнатуру
    uint32_t len_be; // length

    // receiving length of message
    if(!receive_all(sockfd, reinterpret_cast<char*>(&len_be), sizeof(len_be))) {
        return false;
    }
    uint32_t len = from_big_endian(len_be);  // converting

    if(len >= MESSAGE_LENGTH){ // checking length
        std::cerr << "Received message length." << std::endl;
        return false;
    }

    char buffer[MESSAGE_LENGTH];// reading the message itself
    if(!receive_all(sockfd, buffer, len)) {
        return false;
    }

    message.assign(buffer, len); // copying data from local buffer to std::string
    return true;
}

std::string get_current_time(){
    auto now_p = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now_p);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&now_t));
    return std::string("Time is: ") + buf;
}

std::string get_current_date(){
    auto now_p = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now_p);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&now_t));
    return std::string("Date is: ") + buf;
}

void handle_client(int clientSocket){
    std::cout << "Handling client on socket FD: " << clientSocket << std::endl;
    send_data(clientSocket, "Welcome to the server!");

    while(true){
        std::string request;
        // receiving message from client
        if(!receive_data(clientSocket, request)){
            std::cerr << "Client disconnected or error. \n";
            break;
        }

        std::cout << "Received from FD " << clientSocket << ": " << request << "\n";

        if(request == "Time"){
            send_data(clientSocket, get_current_time());
        }
        else if (request == "Date"){
            send_data(clientSocket, get_current_date());
        }
        else if (request == "Exit"){
            std::cout << "Client on FD " << clientSocket << " requested Exit.\n";
            break;
        }
        else{
            send_data(clientSocket, "Invalid request");
        }
    }

    std::cout << "Closing client connection on FD: " << clientSocket << std::endl;
    close(clientSocket);
}

int main(){
    int serverSocket = create_socket(); // listening socket
    if (serverSocket < 0) return 1;

    if(bind_socket(serverSocket, PORT) < 0){
        close(serverSocket);
        return 1;
    }

    if (listen(serverSocket, 5) < 0){ // queue will be 5
        std::cerr << "Listen failed\n";
        close(serverSocket);
        return 1;
    }
    std::cout << "Server is listening on port " << PORT << "...\n";

    while(true){
        int clientSocket = accept_connection(serverSocket);
        if (clientSocket < 0) {
            std::cerr << "Failed to accept connection, continuing...\n";
            continue;
        }
        // only one client at a time
        handle_client(clientSocket);
    }

    std::cout << "Closing server socket...\n";
    close(serverSocket);
    return 0;
}