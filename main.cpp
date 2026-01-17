#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <windows.h>

using boost::asio::ip::udp;

std::atomic<bool> connected{false};

void setup_console() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

void receiver(udp::socket* sock) {
    char recv_buf[1024];
    udp::endpoint sender_endpoint;
    boost::system::error_code ec;

    while (true) {
        size_t len = sock->receive_from(boost::asio::buffer(recv_buf), sender_endpoint, 0, ec);
        if (!ec && len > 0) {
            std::string msg(recv_buf, len);
            
            if (!connected.load()) {
                if (msg == "K_PUNCH") {
                    connected.store(true);
                    std::cout << "\n[СИСТЕМА] Соединение установлено с " << sender_endpoint << "!" << std::endl;
                    std::cout << "Вы можете писать сообщения...\n" << std::endl;
                }
            } else {
                std::cout << "\n[НАПАРНИК]: " << msg << std::endl;
                std::cout << "[ВЫ]: " << std::flush;
            }
        }
    }
}

int main() {
    setup_console();

    std::string remote_ip;
    std::cout << "Введите IP напарника: ";
    std::cin >> remote_ip;

    unsigned short port;
    std::cout << "Введите порт: ";
    std::cin >> port;

    try {
        boost::asio::io_context io_context;
        udp::socket sock(io_context, udp::endpoint(udp::v4(), port));
        udp::endpoint target_endpoint(boost::asio::ip::make_address(remote_ip), port);

        std::thread recv_thread(receiver, &sock);
        recv_thread.detach();

        std::cout << "Пробив запущен. Ожидание ответа..." << std::endl;

        while (!connected.load()) {
            std::string punch = "K_PUNCH";
            sock.send_to(boost::asio::buffer(punch), target_endpoint);
            Sleep(1000);
        }

        std::string user_msg;
        std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
        
        while (true) {
            std::cout << "[ВЫ]: ";
            std::getline(std::cin, user_msg);
            if (!user_msg.empty()) {
                sock.send_to(boost::asio::buffer(user_msg), target_endpoint);
            }
        }

    } catch (std::exception& e) {
        std::cerr << "Критическая ошибка: " << e.what() << std::endl;
    }

    return 0;
}