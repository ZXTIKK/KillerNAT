#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <algorithm>
#include <limits>
#include <windows.h>

void setup_console();
bool is_valid_ip(const std::string& ip_str);
unsigned short parse_port(const std::string& port_str);

int main() {
    setup_console();

    std::string remote_ip;
    while (true) {
        std::cout << "Введите IP: ";
        std::cin >> remote_ip;
        if (is_valid_ip(remote_ip)) break;
        std::cerr << "Ошибка: Неверный формат IPv4." << std::endl;
    }

    std::string port_input;
    unsigned short remote_port = 0;
    while (true) {
        std::cout << "Введите порт: ";
        std::cin >> port_input;
        remote_port = parse_port(port_input);
        if (remote_port != 0) break;
        std::cerr << "Ошибка: Порт должен быть в диапазоне 1024-65535." << std::endl;
    }

    std::cout << "\nЦель: " << remote_ip << ":" << remote_port << std::endl;
    std::cout << "Нажмите Enter для начала...";
    
    std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
    std::cin.get(); 

    try {
        using boost::asio::ip::udp;
        boost::asio::io_context io_context;

        udp::socket sock(io_context);
        sock.open(udp::v4());
        sock.bind(udp::endpoint(udp::v4(), remote_port));
        sock.non_blocking(true);

        udp::endpoint target_endpoint(boost::asio::ip::make_address(remote_ip), remote_port);
        udp::endpoint sender_endpoint;
        
        char recv_buf[1024];
        std::string message = "K_PUNCH";

        std::cout << "Работаем на порту " << remote_port << ". Ожидание пробива..." << std::endl;

        while (true) {
            sock.send_to(boost::asio::buffer(message), target_endpoint);
            std::cout << "[SENT] -> " << remote_ip << ":" << remote_port << std::endl;

            boost::system::error_code ec;
            size_t len = sock.receive_from(boost::asio::buffer(recv_buf), sender_endpoint, 0, ec);

            if (!ec) {
                std::cout << "\n[!!!] NAT ПРОБИТ! [!!!]" << std::endl;
                std::cout << "Ответ от: " << sender_endpoint.address() << ":" << sender_endpoint.port() << std::endl;
                std::cout << "Данные: " << std::string(recv_buf, len) << std::endl;
                break;
            }

            Sleep(500); 
        }
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    std::cout << "Нажмите Enter для выхода...";
    std::cin.get();
    return 0;
}

void setup_console() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

bool is_valid_ip(const std::string& ip_str) {
    if (std::count(ip_str.begin(), ip_str.end(), '.') != 3) return false;
    boost::system::error_code ec;
    auto addr = boost::asio::ip::make_address(ip_str, ec);
    return !ec && addr.is_v4();
}

unsigned short parse_port(const std::string& port_str) {
    try {
        int p = std::stoi(port_str);
        if (p >= 1024 && p <= 65535) return static_cast<unsigned short>(p);
    } catch (...) {}
    return 0;
}