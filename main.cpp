#define BOOST_SYSTEM_USE_UTF8
#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <thread>
#include <chrono>
#include <atomic>
#include <boost/system/error_code.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

const uint16_t STUN_BINDING_REQUEST = 0x0001;
constexpr uint32_t STUN_MAGIC_COOKIE = 0x2112A442u;

struct StunHeader {
    uint16_t type;
    uint16_t length;
    uint32_t cookie;
    std::array<uint8_t, 12> transaction_id;
};

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

std::tuple<std::string, uint16_t, uint16_t, boost::asio::ip::udp::socket> get_external_and_keepalive(
    boost::asio::io_context& io_context,
    const std::string& stun_server,
    uint16_t stun_port)
{
    boost::asio::ip::udp::socket socket(io_context);
    socket.open(boost::asio::ip::udp::v4());
    socket.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));

    uint16_t local_port = socket.local_endpoint().port();

    boost::asio::ip::udp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(stun_server, std::to_string(stun_port));
    boost::asio::ip::udp::endpoint stun_endpoint = *endpoints.begin();

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

    if (len < sizeof(StunHeader)) return {"", 0, 0, std::move(socket)};

    auto* resp_header = reinterpret_cast<StunHeader*>(recv_buf.data());
    if (ntohs(resp_header->type) != 0x0101) return {"", 0, 0, std::move(socket)};

    uint16_t msg_len = ntohs(resp_header->length);
    uint8_t* attrs = recv_buf.data() + sizeof(StunHeader);

    std::string ext_ip;
    uint16_t ext_port = 0;

    while (msg_len > 0) {
        uint16_t type = ntohs(*reinterpret_cast<uint16_t*>(attrs));
        uint16_t alen = ntohs(*reinterpret_cast<uint16_t*>(attrs + 2));
        uint8_t* val = attrs + 4;

        if (type == 0x0020 || type == 0x0001) {
            if (val[1] != 0x01) continue;
            ext_port = ntohs(*reinterpret_cast<uint16_t*>(val + 2));
            if (type == 0x0020) ext_port ^= (STUN_MAGIC_COOKIE >> 16);

            uint32_t ip = *reinterpret_cast<uint32_t*>(val + 4);
            if (type == 0x0020) ip ^= htonl(STUN_MAGIC_COOKIE);
            ext_ip = boost::asio::ip::address_v4(htonl(ip)).to_string();
            break;
        }

        attrs += 4 + ((alen + 3) & ~3);
        msg_len -= 4 + alen;
    }

    if (ext_ip.empty()) return {"", 0, 0, std::move(socket)};

    std::cout << "Ваш внешний адрес: " << ext_ip << ":" << ext_port << "\n\n";

    return {ext_ip, ext_port, local_port, std::move(socket)};
}

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    srand(static_cast<unsigned>(time(nullptr)));

    boost::asio::io_context io_context;

    std::vector<std::pair<std::string, uint16_t>> stuns = {
        {"stun.syncthing.net", 3478},
        {"stun.nextcloud.com", 3478},
        {"stun.cloudflare.com", 3478}
    };

    std::cout << "Выберите STUN:\n";
    for (size_t i = 0; i < stuns.size(); ++i)
        std::cout << i+1 << ") " << stuns[i].first << ":" << stuns[i].second << "\n";

    size_t ch;
    std::cin >> ch;
    if (ch < 1 || ch > stuns.size()) return 1;

    auto [host, port] = stuns[ch-1];

    auto [my_ip, my_port, local_port, socket] = get_external_and_keepalive(io_context, host, port);

    std::cout << "Попытка записать в буфер обмена..." << std::endl;

    //--------------------------

    if(copyStringToClipboard(my_ip + " " + std::to_string(my_port))){
        std::cout << "Успешно записано в буфер обмена!" << std::endl;
    }else{
        std::cout << "Не удалось записать в буфер обмена" << std::endl << std::endl;
    }

    //--------------------------

    if (my_port == 0) {
        std::cerr << "Не удалось получить внешний адрес\n";
        return 1;
    }

    std::atomic<bool> input_ready{false};
    std::string peer_ip;
    uint16_t peer_port = 0;


    std::thread input_thread([&] {
        std::cout << "Введите адрес друга (IP ПОРТ) и нажмите Enter:\n";
        std::cin >> peer_ip >> peer_port;
        input_ready = true;
    });

     std::cout << "Держим порт живым (запрос каждые 5 сек)...\n";

    boost::asio::ip::udp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(host, std::to_string(port));
    if (endpoints.empty()) {
        std::cerr << "Не удалось разрешить имя STUN-сервера\n";
        return 1;
    }
    boost::asio::ip::udp::endpoint stun_ep = *endpoints.begin();

    std::string punch = "KEEP";

    while (!input_ready) {
        socket.send_to(boost::asio::buffer(punch), stun_ep);
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    input_thread.join();

    boost::system::error_code ec;
    auto peer_addr = boost::asio::ip::make_address(peer_ip, ec);
    if (ec || !peer_addr.is_v4() || peer_port == 0 || peer_port > 65535) {
        std::cerr << "Некорректный адрес друга\n";
        return 1;
    }

    boost::asio::ip::udp::endpoint peer_endpoint(peer_addr, peer_port);

    std::cout << "\nНачинаем пробивать на " << peer_ip << ":" << peer_port << "...\n";

    std::atomic<bool> first_received{false};

    std::thread receiver([&] {
        boost::array<char, 2048> buf{};
        boost::asio::ip::udp::endpoint sender;
        while (!first_received) {
            boost::system::error_code e;
            size_t n = socket.receive_from(boost::asio::buffer(buf), sender, 0, e);
            if (!e) {
                std::cout << "\nПервый пакет получен от " 
                          << sender.address().to_string() << ":" << sender.port() << "\n";
                first_received = true;
            }
        }
    });

    int cnt = 0;
    while (!first_received && cnt < 300) {
        socket.send_to(boost::asio::buffer("PUNCH"), peer_endpoint);
        cnt++;
        std::cout << "№" << cnt << ".\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    if (!first_received) {
        std::cout << "Ответ не пришёл за 2 минуты...\n";
    }

    receiver.detach();

    std::cout << "\nПродолжаем работу.\n";

    std::cin.get();
    std::cin.get();

    return 0;
}