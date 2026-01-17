#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <windows.h>

int main() {
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
    try {
        boost::asio::io_context io_context;
        
        std::cout << "Boost.Asio успешно инициализирован!" << std::endl;

    } catch (std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}