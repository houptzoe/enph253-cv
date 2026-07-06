#include "mjpeg-stream.hpp"

#include <opencv2/imgcodecs.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr char kBoundary[] = "mars-cv-frame";

bool sendAll(int fd, const char* data, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        const ssize_t n = send(fd, data + sent, length - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool sendAll(int fd, const std::string& data)
{
    return sendAll(fd, data.data(), data.size());
}

bool sendAll(int fd, const std::vector<unsigned char>& data)
{
    return sendAll(fd, reinterpret_cast<const char*>(data.data()), data.size());
}

void discardRequestHeaders(int fd)
{
    char buffer[1024];
    std::string request;
    while (request.find("\r\n\r\n") == std::string::npos) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return;
        }
        request.append(buffer, buffer + n);
        if (request.size() > 8192) {
            return;
        }
    }
}
} // namespace

MjpegStreamServer::MjpegStreamServer(int port) : port_(port)
{
    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) {
        throw std::runtime_error("Failed to create stream socket");
    }

    int reuse = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(serverFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        close(serverFd_);
        serverFd_ = -1;
        throw std::runtime_error("Failed to bind stream port " + std::to_string(port_));
    }

    if (listen(serverFd_, 4) < 0) {
        close(serverFd_);
        serverFd_ = -1;
        throw std::runtime_error("Failed to listen on stream port " + std::to_string(port_));
    }

    running_ = true;
    acceptThread_ = std::thread(&MjpegStreamServer::acceptLoop, this);
}

MjpegStreamServer::~MjpegStreamServer()
{
    running_ = false;
    if (serverFd_ >= 0) {
        shutdown(serverFd_, SHUT_RDWR);
        close(serverFd_);
        serverFd_ = -1;
    }
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
}

void MjpegStreamServer::acceptLoop()
{
    while (running_) {
        const int clientFd = accept(serverFd_, nullptr, nullptr);
        if (clientFd < 0) {
            if (running_) {
                std::cerr << "Stream accept failed" << std::endl;
            }
            continue;
        }

        std::thread(&MjpegStreamServer::serveClient, this, clientFd).detach();
    }
}

void MjpegStreamServer::serveClient(int clientFd)
{
    discardRequestHeaders(clientFd);

    const std::string headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=" +
        std::string(kBoundary) +
        "\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Connection: close\r\n"
        "\r\n";

    if (!sendAll(clientFd, headers)) {
        close(clientFd);
        return;
    }

    int lastSentVersion = -1;
    while (running_) {
        std::vector<unsigned char> jpeg;
        int version = 0;
        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            version = frameVersion_;
            jpeg = jpeg_;
        }

        if (version == lastSentVersion || jpeg.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        lastSentVersion = version;

        const std::string partHeader =
            std::string("--") + kBoundary + "\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: " +
            std::to_string(jpeg.size()) + "\r\n\r\n";

        if (!sendAll(clientFd, partHeader) || !sendAll(clientFd, jpeg) || !sendAll(clientFd, "\r\n")) {
            break;
        }
    }

    close(clientFd);
}

void MjpegStreamServer::publish(const cv::Mat& bgrFrame)
{
    if (bgrFrame.empty()) {
        return;
    }

    std::vector<unsigned char> encoded;
    if (!cv::imencode(".jpg", bgrFrame, encoded, {cv::IMWRITE_JPEG_QUALITY, 80})) {
        return;
    }

    std::lock_guard<std::mutex> lock(frameMutex_);
    jpeg_ = std::move(encoded);
    ++frameVersion_;
}
