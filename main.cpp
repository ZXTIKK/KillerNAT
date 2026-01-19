#define BOOST_SYSTEM_USE_UTF8
#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <thread>
#include <chrono>
#include <tuple>
#include <atomic>
#include <boost/system/error_code.hpp>

const uint16_t STUN_BINDING_REQUEST = 0x0001;
const uint16_t STUN_BINDING_RESPONSE = 0x0101;
const uint16_t STUN_ATTR_MAPPED_ADDRESS = 0x0001;
const uint16_t STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020;
constexpr uint32_t STUN_MAGIC_COOKIE = 0x2112A442u;

struct StunHeader {
    uint16_t type;
    uint16_t length;
    uint32_t cookie;
    std::array<uint8_t, 12> transaction_id;
};

void setup_console() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

bool copyStringToClipboard(const std::string& utf8String) {
    if (utf8String.empty()) return false;
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &utf8String[0], (int)utf8String.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &utf8String[0], (int)utf8String.size(), &wstrTo[0], size_needed);
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();

    size_t sizeInBytes = (wstrTo.size() + 1) * sizeof(wchar_t);
    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, sizeInBytes);
    if (!hGlob) {
        CloseClipboard();
        return false;
    }

    memcpy(GlobalLock(hGlob), wstrTo.c_str(), sizeInBytes);
    GlobalUnlock(hGlob);

    if (!SetClipboardData(CF_UNICODETEXT, hGlob)) {
        GlobalFree(hGlob);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}


std::array<uint8_t, 12> generate_transaction_id() {
    std::array<uint8_t, 12> tid;
    for (auto& byte : tid) {
        byte = static_cast<uint8_t>(rand() % 256);
    }
    return tid;
}

std::tuple<std::string, uint16_t, uint16_t> get_external_address(
    boost::asio::io_context& io_context,
    const std::string& stun_server,
    uint16_t stun_port)
{
    try {
        boost::asio::ip::udp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(stun_server, std::to_string(stun_port));
        boost::asio::ip::udp::endpoint stun_endpoint = *endpoints.begin();

        boost::asio::ip::udp::socket socket(io_context);
        socket.open(boost::asio::ip::udp::v4());
        socket.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));

        uint16_t local_port = socket.local_endpoint().port();

        StunHeader header{};
        header.type = htons(STUN_BINDING_REQUEST);
        header.length = htons(0);
        header.cookie = htonl(STUN_MAGIC_COOKIE);
        header.transaction_id = generate_transaction_id();

        std::vector<uint8_t> request(sizeof(StunHeader));
        std::memcpy(request.data(), &header, sizeof(StunHeader));

        socket.send_to(boost::asio::buffer(request), stun_endpoint);

        boost::array<uint8_t, 1024> recv_buf{};
        boost::asio::ip::udp::endpoint sender_endpoint;
        size_t len = socket.receive_from(boost::asio::buffer(recv_buf), sender_endpoint);

        if (len < sizeof(StunHeader)) {
            std::cerr << "Некорректная длина ответа STUN\n";
            return {"", 0, 0};
        }

        auto* resp_header = reinterpret_cast<StunHeader*>(recv_buf.data());
        if (ntohs(resp_header->type) != STUN_BINDING_RESPONSE) {
            std::cerr << "Получен не Binding Response от STUN\n";
            return {"", 0, 0};
        }

        uint16_t msg_len = ntohs(resp_header->length);
        uint8_t* attrs = recv_buf.data() + sizeof(StunHeader);

        boost::asio::ip::address external_ip;
        uint16_t external_port = 0;

        while (msg_len > 0) {
            uint16_t attr_type = ntohs(*reinterpret_cast<uint16_t*>(attrs));
            uint16_t attr_len = ntohs(*reinterpret_cast<uint16_t*>(attrs + 2));
            uint8_t* attr_value = attrs + 4;

            if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS || attr_type == STUN_ATTR_MAPPED_ADDRESS) {
                if (attr_value[1] != 0x01) continue;

                external_port = ntohs(*reinterpret_cast<uint16_t*>(attr_value + 2));
                if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS) {
                    external_port ^= (STUN_MAGIC_COOKIE >> 16);
                }

                uint32_t ip_raw = *reinterpret_cast<uint32_t*>(attr_value + 4);
                if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS) {
                    ip_raw ^= htonl(STUN_MAGIC_COOKIE);
                }
                external_ip = boost::asio::ip::address_v4(htonl(ip_raw));
                break;
            }

            attrs += 4 + ((attr_len + 3) & ~3);
            msg_len -= 4 + attr_len;
        }

        if (external_ip.is_unspecified()) {
            std::cerr << "Не удалось получить внешний адрес\n";
            return {"", 0, 0};
        }

        std::cout << "Ваш внешний IP:   " << external_ip.to_string() << "\n";
        std::cout << "Ваш внешний порт: " << external_port << "\n";
        return {external_ip.to_string(), external_port, local_port};
        } catch (const std::exception& e) {
        std::cerr << "Ошибка STUN: " << e.what() << "\n";
        return {"", 0, 0};
    }
}

void punch_and_listen(boost::asio::io_context& io_context, uint16_t local_port,
                      const std::string& peer_ip, uint16_t peer_port)
{
    try {
        boost::asio::ip::udp::socket socket(io_context);
        socket.open(boost::asio::ip::udp::v4());
        socket.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), local_port));

        boost::system::error_code ec;
        auto addr = boost::asio::ip::make_address(peer_ip, ec);
        if (ec) {
            std::cerr << "Некорректный IP собеседника: " << ec.message() << "\n";
            return;
        }

        boost::asio::ip::udp::endpoint peer_endpoint(addr, peer_port);

        std::atomic<bool> received{false};

        std::thread receiver([&] {
            while (!received) {
                boost::array<char, 1024> buf{};
                boost::asio::ip::udp::endpoint sender;
                boost::system::error_code ec2;
                size_t len = socket.receive_from(boost::asio::buffer(buf), sender, 0, ec2);

                if (ec2) {
                    if (ec2 != boost::asio::error::operation_aborted) {
                        std::cerr << "Ошибка приёма: " << ec2.message() << "\n";
                    }
                    break;
                }

                std::string msg(buf.data(), len);
                std::cout << "\nПолучено: " << msg
                          << "  от " << sender.address().to_string()
                          << ":" << sender.port() << "\n";

                received = true;

                std::string response = "Привет! Соединение установлено!";
                socket.send_to(boost::asio::buffer(response), sender);
            }
        });

        std::cout << "Начинаем активное пробивание NAT на "
                  << peer_ip << ":" << peer_port << "...\n";

        std::string punch_msg = "PUNCH";
        int attempt = 0;
        const int max_attempts = 120;
        const auto interval = std::chrono::milliseconds(1000);

        while (!received && attempt < max_attempts) {
            socket.send_to(boost::asio::buffer(punch_msg), peer_endpoint);
            attempt++;
            std::cout << "Пакет пробивания #" << attempt << " отправлен\n";

            std::this_thread::sleep_for(interval);
        }

        if (!received) {
            std::cout << "\nПредупреждение: Ответ не получен после " << max_attempts
                      << " попыток. Возможно симметричный NAT, CGNAT или собеседник не запустил программу.\n";
        } else {
            std::cout << "\nУспех! Получен первый пакет от собеседника!\n";
        }

        std::cout << "\nТеперь режим чата (вводите сообщения, для выхода введите 'exit'):\n";

        receiver.detach();

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "exit" || line == "quit") break;
            if (line.empty()) continue;

            socket.send_to(boost::asio::buffer(line), peer_endpoint);
            std::cout << "[отправлено] " << line << "\n";
        }

        socket.close();

    } catch (const std::exception& e) {
        std::cerr << "Ошибка пробивания/прослушки: " << e.what() << "\n";
    }
}

int main()
{
    setup_console();
    srand(static_cast<unsigned>(time(nullptr)));

    boost::asio::io_context io_context;

    std::vector<std::pair<std::string, uint16_t>> stun_list = {
        {"stun.syncthing.net",      3478},
        {"stun.nextcloud.com",      3478},
        {"stun.cloudflare.com",     3478},
        {"stun.voip.blackberry.com",3478}
    };

    std::cout << "Выберите STUN-сервер:\n";
    for (size_t i = 0; i < stun_list.size(); ++i) {
        std::cout << i + 1 << ") " << stun_list[i].first << ":" << stun_list[i].second << "\n";
    }

    size_t choice = 0;
    std::cin >> choice;
    if (choice < 1 || choice > stun_list.size()) {
        std::cerr << "Неверный выбор\n";
        return 1;
    }

    auto [stun_host, stun_port] = stun_list[choice - 1];
    auto [my_ip, my_ext_port, my_local_port] =
        get_external_address(io_context, stun_host, stun_port);

    if (my_ext_port == 0) {
        std::cerr << "Не удалось получить внешний адрес\n";
        return 1;
    }

    std::cout << "\nВаш адрес для друга:  " << my_ip << ":" << my_ext_port << "\n\n\n" <<std::endl;
    std::cout << "Попытка записать в буфер обмена..." << std::endl;
    if(copyStringToClipboard(my_ip + " " + std::to_string(my_ext_port))){
        std::cout << "Успешно записано в буфер обмена!" << std::endl;
    }else{
        std::cout << "Не удалось записать в буфер обмена" << std::endl << std::endl;
        std::cout << my_ip << " " << my_ext_port << std::endl << std::endl << std::endl;
    }
    
    std::cout << "Введите адрес друга (IP ПОРТ): ";
    std::string peer_ip;
    uint16_t peer_port;

    bool valid = false;
    while (!valid) {
        std::cin >> peer_ip >> peer_port;

        boost::system::error_code ec;
        auto addr = boost::asio::ip::make_address(peer_ip, ec);
        if (ec || !addr.is_v4()) {
            std::cout << "Некорректный IPv4 адрес, попробуйте снова: ";
            continue;
        }
        if (peer_port == 0 || peer_port > 65535) {
            std::cout << "Порт должен быть от 1 до 65535, попробуйте снова: ";
            continue;
        }
        valid = true;
    }

    std::cout << "\nЗапуск попытки соединения...\n\n";

    punch_and_listen(io_context, my_local_port, peer_ip, peer_port);

    return 0;
}