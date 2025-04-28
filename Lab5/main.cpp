#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#define PORT 8081
#define ROOT_DIR "static"

using namespace std;

string readFile(const string& path) {
    ifstream file(path, ios::binary);
    if (!file.is_open()) return "";
    ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void sendResponse(int client, const string& status, const string& content) {
    ostringstream response;
    response << "HTTP/1.1 " << status << "\r\n";
    response << "Content-Length: " << content.size() << "\r\n";
    response << "Content-Type: text/html\r\n\r\n";
    response << content;
    send(client, response.str().c_str(), response.str().length(), 0);
}

void handleClient(int clientSocket) {
    char buffer[4096];
    int received = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        close(clientSocket);
        return;
    }
    buffer[received] = '\0';

    istringstream request(buffer);
    string method, path, version;
    request >> method >> path >> version;

    if (method != "GET") {
        string body = "Method is not \"GET\"";
        sendResponse(clientSocket, body, body);
        close(clientSocket);
        return;
    }

    if (path == "/") path = "/index.html";

    string filePath = string(ROOT_DIR) + path;
    string content = readFile(filePath);

    if (content.empty()) {
        string body = "<html><body><h1>404 Not Found</h1></body></html>";
        sendResponse(clientSocket, "404 Not Found", body);
    } else {
        sendResponse(clientSocket, "200 OK", content);
    }

    close(clientSocket);
}

int main() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        cerr << "Socket creation failed." << endl;
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (::bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr))) {
        cerr << "Bind failed." << endl;
        close(serverSocket);
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == -1) {
        cerr << "Listen failed." << endl;
        close(serverSocket);
        return 1;
    }

    cout << "Server started at http://localhost:" << PORT << endl;

    while (true) {
        int clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == -1) {
            cerr << "Accept failed." << endl;
            continue;
        }
        thread(handleClient, clientSocket).detach();
    }

    close(serverSocket);
    return 0;
}