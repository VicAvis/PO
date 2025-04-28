#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdint>
#include <vector>
#include <limits>

#define SERVER_IP "127.0.0.1"
#define PORT 8081
#define MESSAGE_LENGTH 1024

uint32_t to_big_endian(uint32_t val) { return htonl(val); }
uint32_t from_big_endian(uint32_t val) { return ntohl(val); }

bool send_all(int sockfd, const char* data, size_t len) {
    size_t total = 0;
    while (total < len) {
        int sent = send(sockfd, data + total, len - total, 0);
        if (sent <= 0) return false;
        total += sent;
    }
    return true;
}

bool receive_all(int sockfd, char* buffer, size_t len) {
    size_t total = 0;
    while (total < len) {
        int received = recv(sockfd, buffer + total, len - total, 0);
        if (received <= 0) return false;
        total += received;
    }
    return true;
}

void send_command(int sockfd, const std::string& command) {
    uint32_t len_be = to_big_endian(static_cast<uint32_t>(command.size()));
    send_all(sockfd, reinterpret_cast<const char*>(&len_be), sizeof(len_be));
    send_all(sockfd, command.c_str(), command.size());
}

std::string receive_response(int sockfd) {
    uint32_t len_be;
    if (!receive_all(sockfd, reinterpret_cast<char*>(&len_be), sizeof(len_be))) {
        return "DISCONNECTED";
    }
    uint32_t len = from_big_endian(len_be);

    if (len >= MESSAGE_LENGTH) {
        return "ERROR: Message too long";
    }

    char buffer[MESSAGE_LENGTH];
    if (!receive_all(sockfd, buffer, len)) {
        return "ERROR: Receive failed";
    }
    return std::string(buffer, len);
}

void print_help() {
    std::cout << "\n Commands:\n"
              << "  HELP          - Show this help\n"
              << "  SEND_DATA     - Send matrix parameters\n"
              << "  START_CALC    - Start calculations\n"
              << "  GET_STATUS    - Check calculation status\n"
              << "  GET_RESULT    - Get result matrix\n"
              << "  EXIT          - Exit program\n";
}

int get_positive_input(const std::string& prompt) {
    int value;
    while (true) {
        std::cout << prompt;
        std::cin >> value;
        if (std::cin.fail() || value <= 0) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Invalid input. Please enter a positive number.\n";
        } else {
            break;
        }
    }
    return value;
}

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address\n";
        close(sockfd);
        return 1;
    }

    if (connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection failed\n";
        close(sockfd);
        return 1;
    }

    std::string response = receive_response(sockfd);
    std::cout << "Server: " << response << std::endl;

    bool data_sent = false;
    bool is_connected = true;
    print_help();

    while (is_connected) {
        std::cout << "\nPlease enter command: ";
        std::string command;
        std::cin >> command;

        if (command == "HELP") {
            print_help();
        }
        else if (command == "SEND_DATA") {
            int matrixSize = get_positive_input("Enter matrix size: ");
            int threadCount = get_positive_input("Enter thread count: ");

            send_command(sockfd, "SEND_DATA");

            uint32_t matrixSize_be = to_big_endian(matrixSize);
            uint32_t threadCount_be = to_big_endian(threadCount);

            send_all(sockfd, reinterpret_cast<const char*>(&matrixSize_be), sizeof(matrixSize_be));
            send_all(sockfd, reinterpret_cast<const char*>(&threadCount_be), sizeof(threadCount_be));

            response = receive_response(sockfd);
            std::cout << "Server: " << response << std::endl;
            if (response == "DATA_ACCEPTED") data_sent = true;
        }
        else if (command == "START_CALC") {
            if (!data_sent) {
                std::cout << "Error: Send data first!\n";
                continue;
            }
            send_command(sockfd, "START_WORK");
            response = receive_response(sockfd);
            std::cout << "Server: " << response << std::endl;
        }
        else if (command == "GET_STATUS") {
            send_command(sockfd, "GET_STATUS");
            response = receive_response(sockfd);
            std::cout << "Status: " << response << std::endl;
        }
        else if (command == "GET_RESULT") {
            send_command(sockfd, "GET_RESULT");
            std::string response = receive_response(sockfd);

            if (response == "RESULT_START") {
                response = receive_response(sockfd);
                try {
                    double time_ms = std::stod(response);
                    std::cout << "Calculation time: " << time_ms << " ms\n";
                } catch (const std::exception& e) {
                    std::cout << "Error parsing time: " << e.what() << std::endl;
                }

                response = receive_response(sockfd);
                if (response == "RESULT_END") {
                    std::cout << "Result received successfully!\n";
                } else {
                    std::cout << "Error: " << response << std::endl;
                }
            } else {
                std::cout << "Error: " << response << std::endl;
            }
        }
        else if (command == "EXIT") {
            send_command(sockfd, "Exit");
            is_connected = false;
        }
        else {
            std::cout << "Unknown command. Type 'HELP'.\n";
        }

        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

    close(sockfd);
    std::cout << "Disconnected from server\n";
    return 0;
}