#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <ctime>
#include <map>
#include <sstream>
#include <random>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <queue>
#include <iomanip>
#include <fcntl.h>
#include <errno.h>


enum class GameState {
    WAITING,
    PLAYING,
    FINISHED
};

struct Client {
    int fd;
    int room_id;
    std::string name;
    time_t join_time;
    bool ready_for_next;
};

struct PlayerState {
    int fd;
    std::string name;
    int hangman_stage;
    std::vector<bool> guessed_letters;
    bool guessed_word;
    time_t game_start;
    time_t finish_time;
    bool active;
    std::vector<char> wrong_letters;
    std::vector<char> correct_letters;
};

struct Room {
    std::string name;
    std::vector<int> client_fds;
    GameState state;
    std::string secret_word;
    std::vector<PlayerState> players;
    time_t game_start;
    int time_limit;
    int current_round;
    std::map<int, time_t> join_times;
    std::thread* game_thread;
    std::atomic<bool> game_running;

    Room()
        : state(GameState::WAITING),
          time_limit(120),
          current_round(0),
          game_thread(nullptr),
          game_running(false) {}

    Room(const Room&) = delete;
    Room& operator=(const Room&) = delete;

    Room(Room&& other) noexcept
        : name(std::move(other.name)),
          client_fds(std::move(other.client_fds)),
          state(other.state),
          secret_word(std::move(other.secret_word)),
          players(std::move(other.players)),
          game_start(other.game_start),
          time_limit(other.time_limit),
          current_round(other.current_round),
          join_times(std::move(other.join_times)),
          game_thread(other.game_thread),
          game_running(other.game_running.load()) {
        other.game_thread = nullptr;
        other.game_running = false;
    }

    Room& operator=(Room&& other) noexcept {
        if (this != &other) {
            if (game_thread && game_running) {
                game_running = false;
                game_thread->join();
                delete game_thread;
            }

            name = std::move(other.name);
            client_fds = std::move(other.client_fds);
            state = other.state;
            secret_word = std::move(other.secret_word);
            players = std::move(other.players);
            game_start = other.game_start;
            time_limit = other.time_limit;
            current_round = other.current_round;
            join_times = std::move(other.join_times);
            game_thread = other.game_thread;
            game_running = other.game_running.load();

            other.game_thread = nullptr;
            other.game_running = false;
        }
        return *this;
    }

    ~Room() {
        if (game_thread && game_running) {
            game_running = false;
            game_thread->join();
            delete game_thread;
        }
    }
};


std::vector<Client> clients;
std::vector<Room*> rooms;

std::mutex clients_mutex;
std::mutex rooms_mutex;

std::vector<std::string> word_list = {
    "PROGRAMOWANIE", "KOMPUTER", "INTERNET", "SERWER", "KLIENT",
    "ALGORYTM", "SZYFR", "HASLO", "GRACZ",
    "WISIELEC", "RYWALIZACJA", "SIEC", "ROZGRYWKA", "TURNIEJ",
    "ZWYCIESTWO", "PORAZKA", "WYTRWALOSC", "INTELIGENCJA", "LOGIKA",
    "STRATEGIA", "KOMUNIKACJA", "INFORMACJA", "TECHNOLOGIA", "ROZWOJ"
};


Client* get_client(int fd) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto& c : clients) {
        if (c.fd == fd)
            return &c;
    }
    return nullptr;
}

Room* get_room(int room_id) {
    std::lock_guard<std::mutex> lock(rooms_mutex);
    if (room_id >= 0 && room_id < (int)rooms.size())
        return rooms[room_id];
    return nullptr;
}

void remove_client_from_rooms_unlocked(int fd) {
    for (auto room : rooms) {
        room->client_fds.erase(
            std::remove(room->client_fds.begin(), room->client_fds.end(), fd),
            room->client_fds.end()
        );

        room->players.erase(
            std::remove_if(room->players.begin(), room->players.end(),
                [fd](const PlayerState& p) { return p.fd == fd; }),
            room->players.end()
        );

        room->join_times.erase(fd);
    }
}

void remove_client_from_rooms(int fd) {
    std::lock_guard<std::mutex> lock(rooms_mutex);
    remove_client_from_rooms_unlocked(fd);
}

bool nickname_taken(const std::string& name) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (const auto& c : clients) {
        if (c.name == name)
            return true;
    }
    return false;
}

std::string generate_word() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, word_list.size() - 1);
    return word_list[dist(gen)];
}

void init_game(Room* room) {
    std::lock_guard<std::mutex> lock(rooms_mutex);

    room->state = GameState::PLAYING;
    room->current_round++;
    room->secret_word = generate_word();
    room->game_start = time(nullptr);

    room->players.clear();

    for (int fd : room->client_fds) {
        Client* c = get_client(fd);
        if (!c)
            continue;

        PlayerState p;
        p.fd = fd;
        p.name = c->name;
        p.hangman_stage = 0;
        p.guessed_letters.assign(room->secret_word.size(), false);
        p.guessed_word = false;
        p.game_start = room->game_start;
        p.finish_time = 0;
        p.active = true;

        room->players.push_back(p);
    }

    std::cout << "Rozpoczęto grę w pokoju '" << room->name
              << "' z hasłem: " << room->secret_word << std::endl;
}

void process_guess(Room* room, PlayerState& player, char letter) {
    letter = toupper(letter);

    if (std::find(player.correct_letters.begin(), player.correct_letters.end(), letter) != player.correct_letters.end() ||
        std::find(player.wrong_letters.begin(), player.wrong_letters.end(), letter) != player.wrong_letters.end())
        return;

    bool found = false;

    for (size_t i = 0; i < room->secret_word.size(); ++i) {
        if (room->secret_word[i] == letter) {
            player.guessed_letters[i] = true;
            found = true;
        }
    }

    if (found) {
        player.correct_letters.push_back(letter);

        bool complete = true;
        for (bool g : player.guessed_letters) {
            if (!g) {
                complete = false;
                break;
            }
        }

        if (complete) {
            player.guessed_word = true;
            player.finish_time = time(nullptr);
            player.active = false;

            std::cout << "Gracz " << player.name
                      << " odgadł hasło w pokoju '"
                      << room->name << "'" << std::endl;
        }
    } else {
        player.wrong_letters.push_back(letter);
        player.hangman_stage++;

        if (player.hangman_stage >= 6) {
            player.active = false;
            std::cout << "Gracz " << player.name
                      << " odpadł w pokoju '"
                      << room->name << "'" << std::endl;
        }
    }
}

bool is_game_finished(Room* room) {
    time_t now = time(nullptr);

    if (now - room->game_start > room->time_limit) {
        std::cout << "Czas gry wyczerpany w pokoju '"
                  << room->name << "'" << std::endl;
        return true;
    }

    bool all_done = true;
    int active_count = 0;

    for (const auto& p : room->players) {
        if (!p.guessed_word && p.active) {
            all_done = false;
            active_count++;
        }
    }

    if (all_done || active_count <= 1) {
        std::cout << "Gra zakończona w pokoju '"
                  << room->name
                  << "', aktywnych graczy: "
                  << active_count << std::endl;
        return true;
    }

    return false;
}

std::string build_ranking(Room* room) {
    std::vector<PlayerState> ranked = room->players;

    std::sort(ranked.begin(), ranked.end(),
        [](const PlayerState& a, const PlayerState& b) {
            if (a.guessed_word && !b.guessed_word) return true;
            if (!a.guessed_word && b.guessed_word) return false;

            if (a.guessed_word && b.guessed_word) {
                return (a.finish_time - a.game_start) <
                       (b.finish_time - b.game_start);
            }

            return a.hangman_stage < b.hangman_stage;
        });

    std::ostringstream oss;
    oss << " RANKING - KONIEC GRY \n";
    oss << "═══════════════════════════\n\n";

    int position = 1;

    for (size_t i = 0; i < ranked.size(); ++i) {
        const auto& p = ranked[i];

        if (i > 0 && p.guessed_word && ranked[i - 1].guessed_word) {
            double prev = difftime(ranked[i - 1].finish_time, ranked[i - 1].game_start);
            double curr = difftime(p.finish_time, p.game_start);
            if (fabs(prev - curr) >= 0.01)
                position = i + 1;
        } else {
            position = i + 1;
        }

        oss << position << ". " << p.name << "\n";

        if (p.guessed_word) {
            double t = difftime(p.finish_time, p.game_start);
            oss << "      Czas: " << std::fixed << std::setprecision(2)
                << t << "s | Błędów: "
                << p.hangman_stage << "\n";
        } else {
            oss << "     DNF";
            if (p.hangman_stage >= 6) {
                oss << " (odpadł po " << p.hangman_stage << " błędach)\n";
            } else {
                oss << " (nie ukończył w czasie)";
                if (p.hangman_stage > 0)
                    oss << " | Błędów: " << p.hangman_stage;
                oss << "\n";
            }
        }

        oss << "\n";
    }

    oss << "═══════════════════════════\n";
    oss << "Właściciel może rozpocząć nową grę";

    return oss.str();
}

std::string player_progress(const PlayerState& p, const std::string& word) {
    std::string out;
    for (size_t i = 0; i < word.size(); ++i) {
        if (p.guessed_letters[i])
            out += word[i];
        else
            out += '_';
    }
    return out;
}

void send_game_state(Room* room) {
    std::ostringstream oss;

    time_t now = time(nullptr);
    int time_left = room->time_limit - (now - room->game_start);
    if (time_left < 0)
        time_left = 0;

    oss << "GAME "
        << room->secret_word.length() << " "
        << time_left << " "
        << room->players.size();

    for (const auto& p : room->players) {
        std::string progress = player_progress(p, room->secret_word);

        int guessed = 0;
        for (bool g : p.guessed_letters)
            if (g) guessed++;

        oss << " " << p.name
            << ":" << p.hangman_stage
            << ":" << guessed
            << ":";

        for (char c : p.wrong_letters)
            oss << c;

        oss << ":"
            << (p.active ? "1" : "0")
            << ":" << (p.guessed_word ? "1" : "0")
            << ":" << progress;
    }

    oss << "\n";
    std::string msg = oss.str();

    std::cout << "DEBUG: Wysyłam stan gry do pokoju '"
              << room->name << "': " << msg;

    for (int fd : room->client_fds) {
        send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
    }
}

void game_loop(Room* room) {
    room->game_running = true;

    while (room->game_running && room->state == GameState::PLAYING) {
        send_game_state(room);

        if (is_game_finished(room)) {
            room->state = GameState::FINISHED;

            time_t now = time(nullptr);
            for (auto& p : room->players) {
                if (p.guessed_word && p.finish_time == 0)
                    p.finish_time = now;
            }

            std::string ranking = build_ranking(room);
            std::string flat = ranking;

            size_t pos = 0;
            while ((pos = flat.find('\n', pos)) != std::string::npos) {
                flat.replace(pos, 1, "|");
                pos++;
            }

            room->state = GameState::WAITING;
            room->current_round = 0;
            room->players.clear();

            std::string lobby = "ROOM_LOBBY\n";
            for (int fd : room->client_fds)
                send(fd, lobby.c_str(), lobby.size(), MSG_NOSIGNAL);

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            std::string msg = "RANKING_FULL " + flat + "\n";
            for (int fd : room->client_fds)
                send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);

            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    room->game_running = false;
}

void send_room_players(Room* room) {
    std::ostringstream oss;
    oss << "ROOM_PLAYERS";

    for (int fd : room->client_fds) {
        Client* c = get_client(fd);
        if (c)
            oss << " " << c->name;
    }

    oss << "\n";
    std::string msg = oss.str();

    for (int fd : room->client_fds)
        send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
}

void broadcast_rooms() {
    std::lock_guard<std::mutex> lock(rooms_mutex);

    for (const auto& c : clients) {
        std::ostringstream oss;
        oss << "ROOMS " << rooms.size();

        for (const auto& room : rooms) {
            oss << " " << room->name
                << ":" << room->client_fds.size()
                << ":" << (room->state == GameState::PLAYING ? "1" : "0");
        }

        oss << "\n";
        std::string msg = oss.str();
        send(c.fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
    }
}

void handle_name(int fd, const std::string& name) {
    Client* c = get_client(fd);
    if (!c)
        return;

    if (nickname_taken(name)) {
        std::string msg = "ERROR Nickname already taken: " + name + "\n";
        send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
        return;
    }

    c->name = name;
    c->join_time = time(nullptr);

    std::string msg = "OK Nickname set to " + name + "\n";
    send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
}

void handle_create(int fd, const std::string& room_name) {
    Client* c = get_client(fd);
    if (!c || c->name.empty()) {
        std::string msg = "ERROR Najpierw ustaw nick\n";
        send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
        return;
    }

    std::lock_guard<std::mutex> lock(rooms_mutex);

    for (const auto& r : rooms) {
        if (r->name == room_name) {
            std::string msg = "ERROR Pokój o takiej nazwie istnieje\n";
            send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
            return;
        }
    }

    Room* room = new Room();
    room->name = room_name;
    rooms.push_back(room);
}

void handle_join(int fd, int room_id) {
    Client* c = get_client(fd);
    if (!c || c->name.empty()) {
        std::string msg = "ERROR Najpierw ustaw nick\n";
        send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
        return;
    }

    Room* room = get_room(room_id);
    if (!room) {
        std::string msg = "ERROR Pokój nie znaleziony\n";
        send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
        return;
    }

    if (room->client_fds.size() >= 5) {
        std::string msg = "ERROR Pokój jest pełen (max 5 graczy)\n";
        send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
        return;
    }

    if (room->state == GameState::PLAYING) {
        std::string msg = "WAITING Gra w trakcie, dołaczysz w nastepnej rundzie\n";
        send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(rooms_mutex);
        remove_client_from_rooms_unlocked(fd);
        room->client_fds.push_back(fd);
        room->join_times[fd] = time(nullptr);
    }

    c->room_id = room_id;

    std::string msg = "JOINED " + std::to_string(room_id) + "\n";
    send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);

    send_room_players(room);
    broadcast_rooms();
}

void handle_leave(int fd) {
    Client* c = get_client(fd);
    if (!c)
        return;

    int old_room = c->room_id;
    Room* room = get_room(old_room);

    if (room && room->state == GameState::PLAYING) {
        for (auto& p : room->players) {
            if (p.fd == fd) {
                p.active = false;
                break;
            }
        }
    }

    remove_client_from_rooms(fd);
    c->room_id = -1;

    std::string msg = "LEFT\n";
    send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);

    Room* updated = get_room(old_room);
    if (updated && !updated->client_fds.empty())
        send_room_players(updated);

    broadcast_rooms();
}

void handle_start(int fd) {
    Client* c = get_client(fd);
    if (!c || c->room_id == -1)
        return;

    Room* room = get_room(c->room_id);
    if (!room || room->state != GameState::WAITING)
        return;

    if (room->client_fds.size() < 2) {
        std::string msg = "ERROR Potrzeba conajmniej 2 graczy\n";
        send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
        return;
    }

    int owner_fd = -1;
    time_t oldest = time(nullptr) + 1;

    for (int pfd : room->client_fds) {
        auto it = room->join_times.find(pfd);
        if (it != room->join_times.end() && it->second < oldest) {
            oldest = it->second;
            owner_fd = pfd;
        }
    }

    if (owner_fd != fd) {
        Client* owner = get_client(owner_fd);
        std::string msg = owner
            ? "ERROR Tylko " + owner->name + " może rozpoczać grę\n"
            : "ERROR Tylko gracz będący najdłużej w pokoju może rozpocząć grę\n";
        send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
        return;
    }

    init_game(room);

    if (room->game_thread)
        delete room->game_thread;

    room->game_thread = new std::thread(game_loop, room);
    room->game_thread->detach();
}

void handle_guess(int fd, char letter) {
    Client* c = get_client(fd);
    if (!c || c->room_id == -1)
        return;

    Room* room = get_room(c->room_id);
    if (!room || room->state != GameState::PLAYING)
        return;

    for (auto& p : room->players) {
        if (p.fd == fd && p.active && !p.guessed_word) {
            process_guess(room, p, letter);
            break;
        }
    }
}

void handle_ready(int fd) {
    Client* c = get_client(fd);
    if (!c)
        return;

    c->ready_for_next = true;

    Room* room = get_room(c->room_id);
    if (!room || room->state != GameState::FINISHED)
        return;

    for (int fd2 : room->client_fds) {
        Client* p = get_client(fd2);
        if (p && !p->ready_for_next)
            return;
    }

    for (int fd2 : room->client_fds) {
        Client* p = get_client(fd2);
        if (p)
            p->ready_for_next = false;
    }

    init_game(room);
    room->game_thread = new std::thread(game_loop, room);
    room->game_thread->detach();
}

void process_client_data(int fd, const std::string& data) {
    std::istringstream iss(data);
    std::string line;

    while (std::getline(iss, line)) {
        if (line.empty())
            continue;

        std::istringstream ls(line);
        std::string cmd;
        ls >> cmd;

        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

        if (cmd == "NAME") {
            std::string name;
            ls >> name;
            handle_name(fd, name);
        }
        else if (cmd == "CREATE") {
            std::string room;
            std::getline(ls >> std::ws, room);
            handle_create(fd, room);
        }
        else if (cmd == "JOIN") {
            int id;
            if (ls >> id)
                handle_join(fd, id);
        }
        else if (cmd == "LEAVE") {
            handle_leave(fd);
        }
        else if (cmd == "START") {
            handle_start(fd);
        }
        else if (cmd == "GUESS") {
            char c;
            if (ls >> c && isalpha(c))
                handle_guess(fd, c);
        }
        else if (cmd == "READY") {
            handle_ready(fd);
        }
        else if (cmd == "CHAT") {
            std::string msg;
            std::getline(ls >> std::ws, msg);

            Client* c = get_client(fd);
            if (c && c->room_id != -1) {
                Room* room = get_room(c->room_id);
                if (room) {
                    std::string out = "CHAT " + c->name + ": " + msg + "\n";
                    for (int pfd : room->client_fds)
                        send(pfd, out.c_str(), out.size(), MSG_NOSIGNAL);
                }
            }
        }
        else if (cmd == "REFRESH") {
            broadcast_rooms();
        }
        else {
            std::string err = "ERROR Unknown command: " + cmd + "\n";
            send(fd, err.c_str(), err.size(), MSG_NOSIGNAL);
        }
    }
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5000);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(listen_fd, 10) < 0) {
        perror("listen");
        return 1;
    }

    int flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);

    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        return 1;
    }

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    epoll_event events[10];

    std::cout << "Serwer nasłuchuje na porcie 5000..." << std::endl;
    //std::cout << "Dostępne komendy: NAME, CREATE, JOIN, LEAVE, START, GUESS, READY" << std::endl;

    while (true) {
        int n = epoll_wait(epfd, events, 10, 100);

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == listen_fd) {
                while (true) {
                    int cfd = accept(listen_fd, nullptr, nullptr);
                    if (cfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        perror("accept");
                        break;
                    }

                    int cflags = fcntl(cfd, F_GETFL, 0);
                    fcntl(cfd, F_SETFL, cflags | O_NONBLOCK);

                    {
                        std::lock_guard<std::mutex> lock(clients_mutex);
                        clients.push_back({cfd, -1, "", time(nullptr), false});
                    }

                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = cfd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);

                    std::cout << "Nowy klient: fd=" << cfd << std::endl;

                    std::string welcome =
                        "WELCOME Please set your nickname with: NAME <nickname>\n";
                    send(cfd, welcome.c_str(), welcome.size(), MSG_NOSIGNAL);
                }
                continue;
            }

            char buffer[4096];
            std::string data;

            while (true) {
                int len = recv(fd, buffer, sizeof(buffer) - 1, 0);

                if (len == 0) {
                    std::cout << "Klient rozłączony: fd=" << fd << std::endl;

                    remove_client_from_rooms(fd);

                    {
                        std::lock_guard<std::mutex> lock(clients_mutex);
                        clients.erase(
                            std::remove_if(clients.begin(), clients.end(),
                                [fd](const Client& c) {
                                    if (c.fd == fd) {
                                        close(c.fd);
                                        return true;
                                    }
                                    return false;
                                }),
                            clients.end()
                        );
                    }

                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    broadcast_rooms();
                    break;
                }

                if (len < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;

                    remove_client_from_rooms(fd);

                    {
                        std::lock_guard<std::mutex> lock(clients_mutex);
                        clients.erase(
                            std::remove_if(clients.begin(), clients.end(),
                                [fd](const Client& c) {
                                    if (c.fd == fd) {
                                        close(c.fd);
                                        return true;
                                    }
                                    return false;
                                }),
                            clients.end()
                        );
                    }

                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    broadcast_rooms();
                    break;
                }

                buffer[len] = 0;
                data += buffer;
            }

            if (!data.empty())
                process_client_data(fd, data);
        }

        static int tick = 0;
        if (++tick % 10 == 0) {
            broadcast_rooms();

            std::lock_guard<std::mutex> lock(rooms_mutex);
            for (auto room : rooms) {
                if (room->state == GameState::PLAYING && room->game_running)
                    send_game_state(room);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (auto r : rooms)
        delete r;

    close(listen_fd);
    close(epfd);
    return 0;
}
