#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <cstdint>
#include <map>
#include <thread>
#include <vector>
#include <atomic>
#include <iomanip>
#include <random>
#include <mutex>

#define PORT 8081
#define MESSAGE_LENGTH 1024

struct Client {
    int socket;
    std::atomic<bool> working{false};
    std::atomic<bool> done{false};
    int threadCount = 0;
    int matrixSize = 0;
    double calculationTime = 0.0;
    std::thread processingThread;
};
using ClientPtr = std::shared_ptr<Client>;
std::map<int, ClientPtr> clients;
std::mutex clientsMutex;

uint32_t to_big_endian(uint32_t val) { return htonl(val); }
uint32_t from_big_endian(uint32_t val) { return ntohl(val); }

int create_socket() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket creation failed\n";
        return -1;
    }
    std::cout << "Socket created. FD: " << sockfd << std::endl;
    return sockfd;
}

int bind_socket(int sockfd, int port) {
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr))) {
        std::cerr << "Bind failed\n";
        return -1;
    }
    std::cout << "Socket bound to port " << port << std::endl;
    return 0;
}

int accept_connection(int sockfd) {
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    int clientSocket = accept(sockfd, (sockaddr*)&clientAddr, &clientLen);
    if (clientSocket < 0) {
        std::cerr << "Accept failed\n";
        return -1;
    }
    std::cout << "Client connected. FD: " << clientSocket << std::endl;
    return clientSocket;
}

bool send_all(int sockfd, const char* data, int len) {
    int total = 0;
    while (total < len) {
        int sent = send(sockfd, data + total, len - total, 0);
        if (sent <= 0) return false;
        total += sent;
    }
    return true;
}

void send_data(int sockfd, const std::string& msg) {
    uint32_t len_be = to_big_endian(static_cast<uint32_t>(msg.size()));
    send_all(sockfd, reinterpret_cast<const char*>(&len_be), sizeof(len_be));
    send_all(sockfd, msg.c_str(), msg.size());
}

bool receive_all(int sockfd, char* buffer, int len) {
    int total = 0;
    while (total < len) {
        int received = recv(sockfd, buffer + total, len - total, 0);
        if (received <= 0) return false;
        total += received;
    }
    return true;
}

bool receive_data(int sockfd, std::string& msg) {
    uint32_t len_be;
    if (!receive_all(sockfd, reinterpret_cast<char*>(&len_be), sizeof(len_be)))
        return false;
    uint32_t len = from_big_endian(len_be);
    if (len >= MESSAGE_LENGTH) return false;

    char buffer[MESSAGE_LENGTH];
    if (!receive_all(sockfd, buffer, len)) return false;
    msg.assign(buffer, len);
    return true;
}

void matrixAdd(const std::vector<std::vector<int>>& A, const std::vector<std::vector<int>>& B, std::vector<std::vector<int>>& C, int startRow, int endRow) {
    for (int i = startRow; i < endRow; ++i)
        for (size_t j = 0; j < A[i].size(); ++j)
            C[i][j] = A[i][j] + B[i][j];
}

void processMatrices(Client& client) {
    client.working = true;
    client.done = false;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 100);

    int size = client.matrixSize;
    std::vector<std::vector<int>> A(size, std::vector<int>(size));
    std::vector<std::vector<int>> B(size, std::vector<int>(size));
    std::vector<std::vector<int>> C(size, std::vector<int>(size));

    for (int i = 0; i < size; ++i) {
        for (int j = 0; j < size; ++j) {
            A[i][j] = dis(gen);
            B[i][j] = dis(gen);
        }
    }

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    int rowsPerThread = size / client.threadCount;
    int extraRows = size % client.threadCount;
    int startRow = 0;

    for (int i = 0; i < client.threadCount; ++i) {
        int endRow = startRow + rowsPerThread + (i < extraRows ? 1 : 0);
        threads.emplace_back(matrixAdd, std::ref(A), std::ref(B), std::ref(C), startRow, endRow);
        startRow = endRow;
    }

    for (auto& t : threads) t.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    client.calculationTime = duration.count();

    client.working = false;
    client.done = true;
}

void handle_client(ClientPtr client) {
    int sockfd = client->socket;
    send_data(sockfd, "HELLO");

    while (true) {
        std::string cmd;
        if (!receive_data(sockfd, cmd)) {
            std::cerr << "Client FD " << sockfd << " disconnected.\n";
            break;
        }

        if (cmd == "HELLO") {
            send_data(sockfd, "HELLO from server");
        } else if (cmd == "SEND_DATA") {
            uint32_t matrixSize_be, threadCount_be;
            if (!receive_all(sockfd, reinterpret_cast<char*>(&matrixSize_be), sizeof(matrixSize_be)) ||
                !receive_all(sockfd, reinterpret_cast<char*>(&threadCount_be), sizeof(threadCount_be))) {
                std::cerr << "Failed to receive data\n";
                break;
            }
            client->matrixSize = from_big_endian(matrixSize_be);
            client->threadCount = from_big_endian(threadCount_be);
            send_data(sockfd, "DATA_ACCEPTED");
        } else if (cmd == "START_WORK") {
            if (client->matrixSize <= 0 || client->threadCount <= 0) {
                send_data(sockfd, "ERROR: Invalid data");
                continue;
            }
            if (client->processingThread.joinable())
                client->processingThread.join();
            client->processingThread = std::thread([client]() { processMatrices(*client); });
            send_data(sockfd, "WORK_STARTED");
        } else if (cmd == "GET_STATUS") {
            if (client->working)
                send_data(sockfd, "STATUS: WORKING");
            else if (client->done)
                send_data(sockfd, "STATUS: DONE");
            else
                send_data(sockfd, "STATUS: IDLE");
        } else if (cmd == "GET_RESULT") {
            if (!client->done) {
                send_data(sockfd, "RESULT_NOT_READY");
                continue;
            }
            send_data(sockfd, "RESULT_START");
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(6) << client->calculationTime;
            std::string time_str = oss.str();
            send_data(sockfd, time_str);
            send_data(sockfd, "RESULT_END");
        } else if (cmd == "Exit") {
            break;
        } else {
            send_data(sockfd, "UNKNOWN_COMMAND");
        }
    }

    if (client->processingThread.joinable())
        client->processingThread.join();
    close(sockfd);
    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        clients.erase(sockfd);
    }
    std::cout << "Client FD " << sockfd << " disconnected.\n";
}

int main() {
    int serverSocket = create_socket();
    if (serverSocket < 0) return 1;
    if (bind_socket(serverSocket, PORT) < 0) {
        close(serverSocket);
        return 1;
    }
    if (listen(serverSocket, 10) < 0) {
        std::cerr << "Listen failed\n";
        close(serverSocket);
        return 1;
    }
    std::cout << "Listening on port " << PORT << "...\n";

    while (true) {
        int clientSocket = accept_connection(serverSocket);
        if (clientSocket < 0) continue;
        auto client = std::make_shared<Client>();
        client->socket = clientSocket;
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            clients[clientSocket] = client;
        }
        std::thread([client]() { handle_client(client); }).detach();
    }

    close(serverSocket);
    return 0;
}