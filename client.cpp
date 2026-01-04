#include <gtk/gtk.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <algorithm>

class AppWidgets;

struct PlayerState {
    char name[50];
    int hangman_state;
    char progress[256];
    char wrong_letters[20];
    char correct_letters[20];
    bool active;
    bool has_guessed;
    int guessed_count;
};

struct RoomsData {
    AppWidgets *widgets;
    int room_count;
    std::vector<std::string> room_names;
    std::vector<int> room_player_counts;
    std::vector<int> room_in_game;
};

struct JoinData {
    AppWidgets *widgets;
    int room_id;
};

struct PlayersData {
    AppWidgets *widgets;
    int player_count;
    std::vector<std::string> player_names;
};

struct GameStateData {
    AppWidgets *widgets;
    std::string game_state_line;
};

struct RankingData {
    AppWidgets *widgets;
    std::string ranking_text;
};

class AppWidgets {
public:
    GtkWidget *connection_window;
    GtkWidget *chat_window;
    GtkWidget *room_window;
    GtkWidget *game_window;
    
    GtkWidget *entry_ip;
    GtkWidget *entry_port;
    GtkWidget *entry_name;
    GtkWidget *chat_box;
    GtkWidget *entry_room;
    GtkWidget *chat_scroll;

    GtkWidget *create_room_btn;
    GtkWidget *disconnect_btn;
    GtkWidget *room_chat_send_btn;
    GtkWidget *game_chat_send_btn;
    GtkWidget *guess_btn;
    GtkWidget *connect_btn;
    
    GtkWidget *game_hangman_label;
    GtkWidget *game_word_label;
    GtkWidget *game_players_box;
    GtkWidget *game_entry_letter;
    GtkWidget *game_time_label;
    GtkWidget *game_wrong_letters_label;
    GtkWidget *game_chat_box;
    GtkWidget *game_chat_entry;

    GtkWidget *room_players_box;
    GtkWidget *room_chat_box;
    GtkWidget *room_chat_entry;
    GtkWidget *room_start_btn;
    GtkWidget *room_leave_btn;
    
    int sock;
    std::atomic<bool> running;
    std::atomic<bool> disconnecting;
    char player_name[50];
    int room_id;
    std::atomic<bool> in_game;
    
    PlayerState *players;
    int player_count;
    int word_length;
    int time_left;
    
    std::thread recv_thread;
    std::mutex data_mutex;
    std::mutex ranking_mutex;
    std::condition_variable cv;
    
    AppWidgets() : sock(-1), running(false), disconnecting(false),
                   room_id(-1), in_game(false), players(nullptr), 
                   player_count(0), word_length(0), time_left(0) {
        connection_window = nullptr;
        chat_window = nullptr;
        room_window = nullptr;
        game_window = nullptr;
        entry_ip = nullptr;
        entry_port = nullptr;
        entry_name = nullptr;
        chat_box = nullptr;
        entry_room = nullptr;
        chat_scroll = nullptr;
        create_room_btn = nullptr;
        disconnect_btn = nullptr;
        room_chat_send_btn = nullptr;
        game_chat_send_btn = nullptr;
        guess_btn = nullptr;
        connect_btn = nullptr;
        game_hangman_label = nullptr;
        game_word_label = nullptr;
        game_players_box = nullptr;
        game_entry_letter = nullptr;
        game_time_label = nullptr;
        game_wrong_letters_label = nullptr;
        game_chat_box = nullptr;
        game_chat_entry = nullptr;
        room_players_box = nullptr;
        room_chat_box = nullptr;
        room_chat_entry = nullptr;
        room_start_btn = nullptr;
        room_leave_btn = nullptr;
    }
    
    ~AppWidgets() {
        running = false;
        disconnecting = true;
        
        if (sock >= 0) {
            close(sock);
            sock = -1;
        }
        
        if (recv_thread.joinable()) {
            recv_thread.join();
        }
        
        delete[] players;
    }
};

static const char* hangman_stages[] = {
    "  ______\n"
    " |      |\n"
    " |\n"
    " |\n"
    " |\n"
    " |\n"
    "_|_\n",
    
    "  ______\n"
    " |      |\n"
    " |      O\n"
    " |\n"
    " |\n"
    " |\n"
    "_|_\n",
    
    "  ______\n"
    " |      |\n"
    " |      O\n"
    " |      |\n"
    " |\n"
    " |\n"
    "_|_\n",
    
    "  ______\n"
    " |      |\n"
    " |      O\n"
    " |     /|\n"
    " |\n"
    " |\n"
    "_|_\n",
    
    "  ______\n"
    " |      |\n"
    " |      O\n"
    " |     /|\\\n"
    " |\n"
    " |\n"
    "_|_\n",
    
    "  ______\n"
    " |      |\n"
    " |      O\n"
    " |     /|\\\n"
    " |     /\n"
    " |\n"
    "_|_\n",
    
    "  ______\n"
    " |      |\n"
    " |      O\n"
    " |     /|\\\n"
    " |     / \\\n"
    " |\n"
    "_|_\n"
};

static void send_message(int sock, const char *format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    send(sock, buffer, strlen(buffer), 0);
}

static void clear_container(GtkWidget *box) {
    if (!box || !GTK_IS_CONTAINER(box)) {
        return;
    }
    
    GList *children = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList *l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);
}

static void add_message_to_chat(GtkWidget *chat_box, const char *message, bool is_system) {
    if (!chat_box || !GTK_IS_BOX(chat_box)) {
        return;
    }
    
    GtkWidget *label = gtk_label_new(message);
    gtk_label_set_xalign(GTK_LABEL(label), is_system ? 0.5 : 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    
    if (is_system) {
        char *colored_message = g_markup_printf_escaped(
            "<span foreground=\"blue\">%s</span>", message);
        gtk_label_set_markup(GTK_LABEL(label), colored_message);
        g_free(colored_message);
    }
    
    gtk_box_pack_start(GTK_BOX(chat_box), label, FALSE, FALSE, 2);
    gtk_widget_show_all(chat_box);
}

static void join_room_clicked(GtkWidget *button, gpointer data);
static void create_room_clicked(GtkWidget *button, gpointer data);
static void disconnect_clicked(GtkWidget *button, gpointer data);
static void leave_room_clicked(GtkWidget *button, gpointer data);
static void start_game_clicked(GtkWidget *button, gpointer data);
static void guess_letter_clicked(GtkWidget *button, gpointer data);
static void send_chat_message(GtkWidget *button, gpointer data);
static void connect_clicked(GtkWidget *button, gpointer data);

static GtkWidget* create_game_window(AppWidgets *w);

static gboolean safe_hide_window(gpointer data) {
    GtkWidget *window = (GtkWidget*)data;
    if (window && GTK_IS_WIDGET(window) && gtk_widget_get_visible(window)) {
        gtk_widget_hide(window);
    }
    return FALSE;
}

static gboolean safe_show_window(gpointer data) {
    GtkWidget *window = (GtkWidget*)data;
    if (window && GTK_IS_WIDGET(window)) {
        gtk_widget_show_all(window);
    }
    return FALSE;
}

static gboolean switch_to_game_window_safe(gpointer data) {
    AppWidgets *w = static_cast<AppWidgets*>(data);
    
    std::lock_guard<std::mutex> lock(w->data_mutex);
    
    if (w->room_window && GTK_IS_WIDGET(w->room_window)) {
        if (gtk_widget_get_visible(w->room_window)) {
            gtk_widget_hide(w->room_window);
        }
    }
    
    if (!w->game_window) {
        w->game_window = create_game_window(w);
    }
    
    if (w->game_window && GTK_IS_WIDGET(w->game_window)) {
        gtk_widget_show_all(w->game_window);
    }
    
    w->in_game = true;
    
    return FALSE;
}

static gboolean switch_to_room_window_safe(gpointer data) {
    AppWidgets *w = (AppWidgets*)data;
    
    std::lock_guard<std::mutex> lock(w->data_mutex);
    
    if (w->game_window && GTK_IS_WIDGET(w->game_window)) {
        if (gtk_widget_get_visible(w->game_window)) {
            gtk_widget_hide(w->game_window);
        }
    }
    
    if (w->room_window && GTK_IS_WIDGET(w->room_window)) {
        gtk_widget_show_all(w->room_window);
    }
    
    w->in_game = false;
    
    return FALSE;
}

static void display_ranking_in_chat(AppWidgets *w, const char *ranking_text) {
    if (!w || !ranking_text || !w->room_chat_box) {
        return;
    }
    
    if (w->disconnecting) {
        return;
    }
    
    if (!GTK_IS_WIDGET(w->room_chat_box)) {
        return;
    }
    
    char *ranking_copy = strdup(ranking_text);
    if (!ranking_copy) {
        return;
    }
    
    char *line = strtok(ranking_copy, "|");
    while (line) {
        if (strlen(line) > 0) {
            add_message_to_chat(w->room_chat_box, line, true);
        }
        line = strtok(NULL, "|");
    }
    
    free(ranking_copy);
}

static gboolean handle_ranking_full(gpointer data) {
    RankingData *rd = (RankingData*)data;
    AppWidgets *w = rd->widgets;
    std::string ranking_text = rd->ranking_text;
    
    if (!w || ranking_text.empty()) {
        delete rd;
        return FALSE;
    }
    
    if (w->disconnecting) {
        delete rd;
        return FALSE;
    }
    
    display_ranking_in_chat(w, ranking_text.c_str());
    
    delete rd;
    return FALSE;
}

static gboolean update_game_state(gpointer data) {
    GameStateData *gsd = (GameStateData*)data;
    AppWidgets *w = gsd->widgets;
    std::string line = gsd->game_state_line;
    
    char *saveptr;
    char *line_cstr = const_cast<char*>(line.c_str());
    char *token = strtok_r(line_cstr, " ", &saveptr);
    
    if (!token || strcmp(token, "GAME") != 0) {
        delete gsd;
        return FALSE;
    }
    
    token = strtok_r(NULL, " ", &saveptr);
    if (!token) {
        delete gsd;
        return FALSE;
    }
    w->word_length = atoi(token);
    
    token = strtok_r(NULL, " ", &saveptr);
    if (!token) {
        delete gsd;
        return FALSE;
    }
    w->time_left = atoi(token);
    
    if (w->game_time_label && GTK_IS_LABEL(w->game_time_label)) {
        char time_text[50];
        snprintf(time_text, sizeof(time_text), "<span size='large'><b>Czas:</b> %ds</span>", w->time_left);
        gtk_label_set_markup(GTK_LABEL(w->game_time_label), time_text);
    }
    
    token = strtok_r(NULL, " ", &saveptr);
    if (!token) {
        delete gsd;
        return FALSE;
    }
    w->player_count = atoi(token);
    
    if (w->game_players_box && GTK_IS_BOX(w->game_players_box)) {
        clear_container(w->game_players_box);
    } else {
        delete gsd;
        return FALSE;
    }
    
    for (int i = 0; i < w->player_count; i++) {
        token = strtok_r(NULL, " ", &saveptr);
        if (!token) break;
        
        char *player_data = strdup(token);
        
        char *parts[7];
        int part_idx = 0;
        char *start = player_data;
        
        for (int j = 0; player_data[j] != '\0' && part_idx < 7; j++) {
            if (player_data[j] == ':') {
                player_data[j] = '\0';
                parts[part_idx++] = start;
                start = &player_data[j+1];
            }
        }
        if (part_idx < 7) {
            parts[part_idx++] = start;
        }
        
        if (part_idx < 7) {
            free(player_data);
            continue;
        }
        
        char *name = parts[0];
        char *hangman_str = parts[1];
        char *guessed_count_str = parts[2];
        char *wrong_letters = parts[3];
        char *active_str = parts[4];
        char *has_guessed_str = parts[5];
        char *progress_self = parts[6];
        
        int hangman_state = atoi(hangman_str);
        int guessed_count = atoi(guessed_count_str);
        bool active = atoi(active_str) != 0;
        bool has_guessed = atoi(has_guessed_str) != 0;
        
        if (strcmp(name, w->player_name) == 0) {
            char spaced_progress[256];
            int idx = 0;
            for (int j = 0; progress_self[j] != '\0' && idx < 254; j++) {
                spaced_progress[idx++] = progress_self[j];
                spaced_progress[idx++] = ' ';
            }
            if (idx > 0) spaced_progress[idx-1] = '\0';
            
            if (w->game_word_label && GTK_IS_LABEL(w->game_word_label)) {
                char word_text[256];
                snprintf(word_text, sizeof(word_text), "<span size='xx-large'><b>%s</b></span>", spaced_progress);
                gtk_label_set_markup(GTK_LABEL(w->game_word_label), word_text);
            }
            
            if (w->game_hangman_label && GTK_IS_LABEL(w->game_hangman_label) &&
                hangman_state >= 0 && hangman_state <= 6) {
                gtk_label_set_text(GTK_LABEL(w->game_hangman_label), hangman_stages[hangman_state]);
            }
            
            if (w->game_wrong_letters_label && GTK_IS_LABEL(w->game_wrong_letters_label)) {
                char wrong_text[100];
                snprintf(wrong_text, sizeof(wrong_text), "Błędne litery: %s", wrong_letters);
                gtk_label_set_text(GTK_LABEL(w->game_wrong_letters_label), wrong_text);
            }
            
            free(player_data);
            continue;
        }
        
        char player_info[256];
        const char *status = "";
        if (has_guessed) {
            status = "[zgadł]";
        } else if (!active) {
            status = "[odpadł]";
        }
        
        snprintf(player_info, sizeof(player_info), "%s: odgadnięte litery: %d/%d (błędów: %d) %s", 
                 name, guessed_count, w->word_length, hangman_state, status);
        
        GtkWidget *frame = gtk_frame_new(NULL);
        GtkWidget *label = gtk_label_new(player_info);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
        gtk_container_add(GTK_CONTAINER(frame), label);
        gtk_box_pack_start(GTK_BOX(w->game_players_box), frame, FALSE, FALSE, 2);
        
        free(player_data);
    }
    
    gtk_widget_show_all(w->game_players_box);
    
    if (w->game_window && GTK_IS_WIDGET(w->game_window) &&
        w->game_entry_letter && GTK_IS_WIDGET(w->game_entry_letter)) {
        gtk_widget_grab_focus(w->game_entry_letter);
    }
    
    delete gsd;
    return FALSE;
}

static gboolean update_rooms_list(gpointer data) {
    RoomsData *rd = (RoomsData*)data;
    AppWidgets *w = rd->widgets;
    
    if (!w->chat_box || !GTK_IS_BOX(w->chat_box)) {
        delete rd;
        return FALSE;
    }
    
    clear_container(w->chat_box);
    
    if (rd->room_count == 0) {
        GtkWidget *label = gtk_label_new("Brak dostępnych pokoi. Utwórz nowy!");
        gtk_box_pack_start(GTK_BOX(w->chat_box), label, FALSE, FALSE, 10);
    }
    
    for (int i = 0; i < rd->room_count; i++) {
        GtkWidget *frame = gtk_frame_new(NULL);
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
        gtk_container_add(GTK_CONTAINER(frame), vbox);
        
        char label_text[256];
        if (rd->room_in_game[i]) {
            snprintf(label_text, sizeof(label_text), "%s (%d/5) - Gra w toku", 
                    rd->room_names[i].c_str(), rd->room_player_counts[i]);
        } else {
            snprintf(label_text, sizeof(label_text), "%s (%d/5) - Oczekiwanie", 
                    rd->room_names[i].c_str(), rd->room_player_counts[i]);
        }
        
        GtkWidget *label = gtk_label_new(label_text);
        gtk_label_set_xalign(GTK_LABEL(label), 0.5);
        gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 2);
        
        if (!rd->room_in_game[i] && rd->room_player_counts[i] < 5) {
            GtkWidget *btn = gtk_button_new_with_label("Dołącz");
            JoinData *jd = new JoinData;
            jd->widgets = w;
            jd->room_id = i;
            g_signal_connect(btn, "clicked", G_CALLBACK(join_room_clicked), jd);
            gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 2);
        } else if (rd->room_in_game[i]) {
            GtkWidget *btn = gtk_button_new_with_label("(gra w toku)");
            gtk_widget_set_sensitive(btn, FALSE);
            gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 2);
        } else {
            GtkWidget *btn = gtk_button_new_with_label("Pokój pełny");
            gtk_widget_set_sensitive(btn, FALSE);
            gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 2);
        }
        
        gtk_box_pack_start(GTK_BOX(w->chat_box), frame, FALSE, FALSE, 5);
    }
    
    gtk_widget_show_all(w->chat_box);
    
    delete rd;
    return FALSE;
}

static gboolean update_room_players_list(gpointer data) {
    PlayersData *pd = (PlayersData*)data;
    AppWidgets *w = pd->widgets;
    
    if (!w->room_players_box || !GTK_IS_BOX(w->room_players_box)) {
        delete pd;
        return FALSE;
    }
    
    clear_container(w->room_players_box);
    
    for (int i = 0; i < pd->player_count; i++) {
        char player_text[100];
        snprintf(player_text, sizeof(player_text), "👤 %s", pd->player_names[i].c_str());
        
        GtkWidget *frame = gtk_frame_new(NULL);
        GtkWidget *label = gtk_label_new(player_text);
        gtk_label_set_xalign(GTK_LABEL(label), 0.5);
        gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
        gtk_container_add(GTK_CONTAINER(frame), label);
        gtk_box_pack_start(GTK_BOX(w->room_players_box), frame, FALSE, FALSE, 2);
    }
    
    gtk_widget_show_all(w->room_players_box);
    
    delete pd;
    return FALSE;
}

static void join_room_clicked(GtkWidget *button, gpointer data) {
    JoinData *jd = static_cast<JoinData*>(data);
    send_message(jd->widgets->sock, "JOIN %d\n", jd->room_id);
    delete jd;
}

static void create_room_clicked(GtkWidget *button, gpointer data) {
    AppWidgets *w = static_cast<AppWidgets*>(data);
    const char *name = gtk_entry_get_text(GTK_ENTRY(w->entry_room));
    if (!name || strlen(name) == 0) return;
    send_message(w->sock, "CREATE %s\n", name);
    gtk_entry_set_text(GTK_ENTRY(w->entry_room), "");
}

static void disconnect_clicked(GtkWidget *button, gpointer data) {
    AppWidgets *w = static_cast<AppWidgets*>(data);
    
    w->disconnecting = true;
    w->running = false;
    
    if (w->sock >= 0) {
        send_message(w->sock, "LEAVE\n");
    }
    
    if (w->recv_thread.joinable()) {
        w->recv_thread.join();
    }
    
    if (w->sock >= 0) {
        close(w->sock);
        w->sock = -1;
    }
    
    if (w->game_window && GTK_IS_WIDGET(w->game_window)) {
        g_idle_add(safe_hide_window, w->game_window);
        w->game_window = nullptr;
    }
    
    if (w->room_window && GTK_IS_WIDGET(w->room_window)) {
        g_idle_add(safe_hide_window, w->room_window);
        w->room_window = nullptr;
    }
    
    if (w->chat_window && GTK_IS_WIDGET(w->chat_window)) {
        g_idle_add(safe_hide_window, w->chat_window);
        w->chat_window = nullptr;
    }
    
    w->in_game = false;
    w->room_id = -1;
    w->disconnecting = false;
    
    if (w->connection_window && GTK_IS_WIDGET(w->connection_window)) {
        g_idle_add(safe_show_window, w->connection_window);
    }
}

static void leave_room_clicked(GtkWidget *button, gpointer data) {
    AppWidgets *w = static_cast<AppWidgets*>(data);
    
    w->disconnecting = true;
    send_message(w->sock, "LEAVE\n");
    
    if (w->game_window && GTK_IS_WIDGET(w->game_window)) {
        g_idle_add(safe_hide_window, w->game_window);
    }
    
    if (w->room_window && GTK_IS_WIDGET(w->room_window)) {
        g_idle_add(safe_hide_window, w->room_window);
    }
    
    if (w->chat_window && GTK_IS_WIDGET(w->chat_window)) {
        g_idle_add(safe_show_window, w->chat_window);
    }
    
    w->in_game = false;
    w->room_id = -1;
    w->disconnecting = false;
}

static void start_game_clicked(GtkWidget *button, gpointer data) {
    AppWidgets *w = static_cast<AppWidgets*>(data);
    send_message(w->sock, "START\n");
}

static void guess_letter_clicked(GtkWidget *button, gpointer data) {
    AppWidgets *w = static_cast<AppWidgets*>(data);
    const char *letter = gtk_entry_get_text(GTK_ENTRY(w->game_entry_letter));
    
    if (letter && strlen(letter) == 1 && isalpha(letter[0])) {
        send_message(w->sock, "GUESS %c\n", toupper(letter[0]));
        gtk_entry_set_text(GTK_ENTRY(w->game_entry_letter), "");

        if (w->game_entry_letter && GTK_IS_WIDGET(w->game_entry_letter)) {
            gtk_widget_grab_focus(w->game_entry_letter);
        }
    }
}

static void send_chat_message(GtkWidget *button, gpointer data) {
    AppWidgets *w = static_cast<AppWidgets*>(data);
    const char *message;
    
    if (w->in_game) {
        message = gtk_entry_get_text(GTK_ENTRY(w->game_chat_entry));
        if (message && strlen(message) > 0) {
            send_message(w->sock, "CHAT %s\n", message);
            gtk_entry_set_text(GTK_ENTRY(w->game_chat_entry), "");
        }
    } else {
        message = gtk_entry_get_text(GTK_ENTRY(w->room_chat_entry));
        if (message && strlen(message) > 0) {
            send_message(w->sock, "CHAT %s\n", message);
            gtk_entry_set_text(GTK_ENTRY(w->room_chat_entry), "");
        }
    }
}

static GtkWidget* create_connection_window(AppWidgets *w) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Wisielec na wyścigi - Połączenie");
    gtk_window_set_default_size(GTK_WINDOW(window), 300, 250);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    
    w->connection_window = window;
    
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), 20);
    gtk_container_add(GTK_CONTAINER(window), main_box);
    
    GtkWidget *header = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header), "<span size='x-large' weight='bold'>Wisielec na wyścigi</span>");
    gtk_box_pack_start(GTK_BOX(main_box), header, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 10);
    
    w->entry_ip = gtk_entry_new();
    w->entry_port = gtk_entry_new();
    w->entry_name = gtk_entry_new();
    
    gtk_entry_set_text(GTK_ENTRY(w->entry_ip), "127.0.0.1");
    gtk_entry_set_text(GTK_ENTRY(w->entry_port), "5000");
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->entry_name), "Twój nick");
    
    GtkWidget *ip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *ip_label = gtk_label_new("IP:");
    gtk_box_pack_start(GTK_BOX(ip_box), ip_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ip_box), w->entry_ip, TRUE, TRUE, 0);
    
    GtkWidget *port_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *port_label = gtk_label_new("Port:");
    gtk_box_pack_start(GTK_BOX(port_box), port_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(port_box), w->entry_port, TRUE, TRUE, 0);
    
    GtkWidget *name_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *name_label = gtk_label_new("Nick:");
    gtk_box_pack_start(GTK_BOX(name_box), name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(name_box), w->entry_name, TRUE, TRUE, 0);
    
    gtk_box_pack_start(GTK_BOX(main_box), ip_box, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(main_box), port_box, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(main_box), name_box, FALSE, FALSE, 5);
    
    GtkWidget *connect_btn = gtk_button_new_with_label("Połącz z serwerem");
    gtk_widget_set_size_request(connect_btn, 200, 40);
    gtk_box_pack_start(GTK_BOX(main_box), connect_btn, FALSE, FALSE, 20);
    
    w->connect_btn = connect_btn;
    
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    return window;
}

static GtkWidget* create_chat_window(AppWidgets *w) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Lobby - Lista pokoi");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 400);
    
    w->chat_window = window;
    
    GtkWidget *main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(main), 10);
    gtk_container_add(GTK_CONTAINER(window), main);
    
    GtkWidget *header_label = gtk_label_new(NULL);
    char header_text[100];
    snprintf(header_text, sizeof(header_text), 
             "<span size='large'>Witaj, <b>%s</b></span>", w->player_name);
    gtk_label_set_markup(GTK_LABEL(header_label), header_text);
    gtk_box_pack_start(GTK_BOX(main), header_label, FALSE, FALSE, 5);
    
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(main), scroll, TRUE, TRUE, 5);
    
    w->chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(w->chat_box), 5);
    gtk_container_add(GTK_CONTAINER(scroll), w->chat_box);
    
    GtkWidget *create_frame = gtk_frame_new("Utwórz nowy pokój");
    gtk_box_pack_start(GTK_BOX(main), create_frame, FALSE, FALSE, 5);
    
    GtkWidget *create_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(create_box), 10);
    gtk_container_add(GTK_CONTAINER(create_frame), create_box);
    
    w->entry_room = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->entry_room), "Nazwa pokoju");
    GtkWidget *create_btn = gtk_button_new_with_label("Utwórz");
    GtkWidget *disc_btn = gtk_button_new_with_label("Rozłącz");
    
    w->create_room_btn = create_btn;
    w->disconnect_btn = disc_btn;

    gtk_box_pack_start(GTK_BOX(create_box), w->entry_room, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(create_box), create_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(create_box), disc_btn, FALSE, FALSE, 5);
    
    return window;
}

static GtkWidget* create_room_window(AppWidgets *w) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Pokój");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 500);
    
    w->room_window = window;
    
    GtkWidget *main = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(main), 10);
    gtk_container_add(GTK_CONTAINER(window), main);
    
    GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(main), left_panel, FALSE, FALSE, 0);
    
    GtkWidget *players_frame = gtk_frame_new("Gracze w pokoju");
    gtk_box_pack_start(GTK_BOX(left_panel), players_frame, TRUE, TRUE, 0);
    
    GtkWidget *players_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(players_scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_set_border_width(GTK_CONTAINER(players_scroll), 5);
    gtk_container_add(GTK_CONTAINER(players_frame), players_scroll);
    
    w->room_players_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(players_scroll), w->room_players_box);
    
    w->room_start_btn = gtk_button_new_with_label("Rozpocznij grę");
    w->room_leave_btn = gtk_button_new_with_label("Opuść pokój");
    
    gtk_box_pack_start(GTK_BOX(left_panel), w->room_start_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(left_panel), w->room_leave_btn, FALSE, FALSE, 5);
    
    GtkWidget *right_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(main), right_panel, TRUE, TRUE, 0);
    
    GtkWidget *chat_frame = gtk_frame_new("Czat pokoju");
    gtk_box_pack_start(GTK_BOX(right_panel), chat_frame, TRUE, TRUE, 0);
    
    GtkWidget *chat_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(chat_frame), chat_scroll);
    
    w->room_chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(w->room_chat_box), 5);
    gtk_container_add(GTK_CONTAINER(chat_scroll), w->room_chat_box);
    
    GtkWidget *chat_input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(right_panel), chat_input_box, FALSE, FALSE, 0);
    
    w->room_chat_entry = gtk_entry_new();
    GtkWidget *chat_send_btn = gtk_button_new_with_label("Wyślij");
    
    w->room_chat_send_btn = chat_send_btn;
    
    gtk_box_pack_start(GTK_BOX(chat_input_box), w->room_chat_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(chat_input_box), chat_send_btn, FALSE, FALSE, 0);
    
    return window;
}

static GtkWidget* create_game_window(AppWidgets *w) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Wisielec - Gra");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    
    w->game_window = window;
    
    GtkWidget *main = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(main), 10);
    gtk_container_add(GTK_CONTAINER(window), main);
    
    GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_pack_start(GTK_BOX(main), left_panel, TRUE, TRUE, 0);
    
    GtkWidget *top_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_pack_start(GTK_BOX(left_panel), top_box, FALSE, FALSE, 0);
    
    w->game_time_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(w->game_time_label), 
                        "<span size='large'><b>Czas:</b> 120s</span>");
    
    w->game_word_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(w->game_word_label), 
                        "<span size='xx-large'><b>_ _ _ _ _</b></span>");
    
    gtk_box_pack_start(GTK_BOX(top_box), w->game_time_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top_box), w->game_word_label, TRUE, TRUE, 0);
    
    GtkWidget *hangman_frame = gtk_frame_new("Twój wisielec");
    gtk_container_set_border_width(GTK_CONTAINER(hangman_frame), 20);
    gtk_box_pack_start(GTK_BOX(left_panel), hangman_frame, TRUE, TRUE, 0);

    w->game_hangman_label = gtk_label_new(hangman_stages[0]);
    gtk_label_set_xalign(GTK_LABEL(w->game_hangman_label), 0.5);
    gtk_container_add(GTK_CONTAINER(hangman_frame), w->game_hangman_label);
    
    w->game_wrong_letters_label = gtk_label_new("Błędne litery: ");
    gtk_box_pack_start(GTK_BOX(left_panel), w->game_wrong_letters_label, FALSE, FALSE, 5);
    
    GtkWidget *guess_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(left_panel), guess_box, FALSE, FALSE, 0);
    
    w->game_entry_letter = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(w->game_entry_letter), 1);
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->game_entry_letter), "Wpisz literę");
    GtkWidget *guess_btn = gtk_button_new_with_label("Zgaduj");
    
    w->guess_btn = guess_btn;
        
    gtk_box_pack_start(GTK_BOX(guess_box), w->game_entry_letter, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(guess_box), guess_btn, FALSE, FALSE, 0);
    
    GtkWidget *right_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_pack_start(GTK_BOX(main), right_panel, FALSE, FALSE, 0);
    
    GtkWidget *players_frame = gtk_frame_new("Gracze");
    gtk_box_pack_start(GTK_BOX(right_panel), players_frame, TRUE, TRUE, 0);
    
    GtkWidget *players_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(players_scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_set_border_width(GTK_CONTAINER(players_scroll), 5);
    gtk_container_add(GTK_CONTAINER(players_frame), players_scroll);
    
    w->game_players_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(players_scroll), w->game_players_box);
    
    GtkWidget *chat_frame = gtk_frame_new("Czat gry");
    gtk_box_pack_start(GTK_BOX(right_panel), chat_frame, TRUE, TRUE, 0);
    
    GtkWidget *chat_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(chat_frame), chat_scroll);
    
    w->game_chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(w->game_chat_box), 5);
    gtk_container_add(GTK_CONTAINER(chat_scroll), w->game_chat_box);
    
    GtkWidget *chat_input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(right_panel), chat_input_box, FALSE, FALSE, 0);
    
    w->game_chat_entry = gtk_entry_new();
    GtkWidget *game_chat_send_btn = gtk_button_new_with_label("Wyślij");
    
    w->game_chat_send_btn = game_chat_send_btn;
    
    gtk_box_pack_start(GTK_BOX(chat_input_box), w->game_chat_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(chat_input_box), game_chat_send_btn, FALSE, FALSE, 0);
    
    if (w->guess_btn && GTK_IS_BUTTON(w->guess_btn)) {
        g_signal_connect(w->guess_btn, "clicked", G_CALLBACK(guess_letter_clicked), w);
    }
    
    if (w->game_chat_send_btn && GTK_IS_BUTTON(w->game_chat_send_btn)) {
        g_signal_connect(w->game_chat_send_btn, "clicked", G_CALLBACK(send_chat_message), w);
    }
    
    if (w->game_entry_letter && GTK_IS_ENTRY(w->game_entry_letter)) {
        g_signal_connect(w->game_entry_letter, "activate", G_CALLBACK(guess_letter_clicked), w);
    }
    
    if (w->game_chat_entry && GTK_IS_ENTRY(w->game_chat_entry)) {
        g_signal_connect(w->game_chat_entry, "activate", G_CALLBACK(send_chat_message), w);
    }
    
    g_signal_connect(window, "show", G_CALLBACK(gtk_widget_grab_focus), w->game_entry_letter);
    
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_widget_destroyed), &w->game_window);
    
    return window;
}

static gboolean show_nickname_error(gpointer data) {
    AppWidgets *w = static_cast<AppWidgets*>(data);
    
    if (w->running) {
        w->running = false;
        if (w->recv_thread.joinable()) {
            w->recv_thread.join();
        }
        if (w->sock >= 0) {
            close(w->sock);
            w->sock = -1;
        }
    }
    
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(w->connection_window),
                                              GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_ERROR,
                                              GTK_BUTTONS_OK,
                                              "Nazwa użytkownika '%s' jest już zajęta. Wybierz inną.", w->player_name);
    gtk_window_set_title(GTK_WINDOW(dialog), "Błąd");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (w->entry_name) {
        gtk_entry_set_text(GTK_ENTRY(w->entry_name), "");
        gtk_widget_grab_focus(w->entry_name);
    }
    
    if (w->connection_window) {
        g_idle_add(safe_show_window, w->connection_window);
    }
    
    if (w->game_window && GTK_IS_WIDGET(w->game_window)) {
        if (gtk_widget_get_visible(w->game_window)) {
            g_idle_add(safe_hide_window, w->game_window);
        }
        w->game_window = nullptr;
    }
    
    if (w->room_window && GTK_IS_WIDGET(w->room_window)) {
        if (gtk_widget_get_visible(w->room_window)) {
            g_idle_add(safe_hide_window, w->room_window);
        }
        w->room_window = nullptr;
    }
    
    if (w->chat_window && GTK_IS_WIDGET(w->chat_window)) {
        if (gtk_widget_get_visible(w->chat_window)) {
            g_idle_add(safe_hide_window, w->chat_window);
        }
        w->chat_window = nullptr;
    }
    
    w->in_game = false;
    w->room_id = -1;
    
    if (w->connect_btn && GTK_IS_WIDGET(w->connect_btn)) {
        gtk_widget_set_sensitive(w->connect_btn, TRUE);
    }
    
    return FALSE;
}

static gboolean create_and_show_lobby(gpointer data) {
    AppWidgets *w = static_cast<AppWidgets*>(data);
    
    if (w->connection_window && GTK_IS_WIDGET(w->connection_window)) {
        g_idle_add(safe_hide_window, w->connection_window);
    }
    
    if (!w->chat_window) {
        w->chat_window = create_chat_window(w);
        
        if (w->create_room_btn && GTK_IS_BUTTON(w->create_room_btn)) {
            g_signal_connect(w->create_room_btn, "clicked", G_CALLBACK(create_room_clicked), w);
        }
        if (w->disconnect_btn && GTK_IS_BUTTON(w->disconnect_btn)) {
            g_signal_connect(w->disconnect_btn, "clicked", G_CALLBACK(disconnect_clicked), w);
        }
    }
    
    if (w->chat_window && GTK_IS_WIDGET(w->chat_window)) {
        g_idle_add(safe_show_window, w->chat_window);
    }
    
    send_message(w->sock, "REFRESH\n");
    
    return FALSE;
}

static gboolean show_error(gpointer data) {
    char *msg = (char*)data;
    
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                              GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_ERROR,
                                              GTK_BUTTONS_OK,
                                              "%s", msg);
    gtk_window_set_title(GTK_WINDOW(dialog), "Błąd");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    free(msg);
    return FALSE;
}

static void connect_clicked(GtkWidget *button, gpointer data) {
    AppWidgets *w = static_cast<AppWidgets*>(data);
    
    const char *ip = gtk_entry_get_text(GTK_ENTRY(w->entry_ip));
    const char *port_str = gtk_entry_get_text(GTK_ENTRY(w->entry_port));
    const char *name = gtk_entry_get_text(GTK_ENTRY(w->entry_name));
    
    if (!name || strlen(name) == 0) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(w->connection_window),
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_ERROR,
                                                  GTK_BUTTONS_OK,
                                                  "Podaj nick!");
        gtk_window_set_title(GTK_WINDOW(dialog), "Błąd");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(w->connection_window),
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_ERROR,
                                                  GTK_BUTTONS_OK,
                                                  "Nie można utworzyć socketu!");
        gtk_window_set_title(GTK_WINDOW(dialog), "Błąd");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(port_str));
    
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(w->connection_window),
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_ERROR,
                                                  GTK_BUTTONS_OK,
                                                  "Nieprawidłowy adres IP!");
        gtk_window_set_title(GTK_WINDOW(dialog), "Błąd");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        close(sock);
        return;
    }
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(w->connection_window),
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_ERROR,
                                                  GTK_BUTTONS_OK,
                                                  "Nie można połączyć z serwerem!");
        gtk_window_set_title(GTK_WINDOW(dialog), "Błąd");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        close(sock);
        if (button && GTK_IS_BUTTON(button)) {
            gtk_widget_set_sensitive(button, TRUE);
        }
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(w->data_mutex);
        w->sock = sock;
        w->running = true;
        w->disconnecting = false;
        strncpy(w->player_name, name, sizeof(w->player_name) - 1);
        w->player_name[sizeof(w->player_name) - 1] = '\0';
    }
    
    send_message(w->sock, "NAME %s\n", name);
    
    if (button && GTK_IS_BUTTON(button)) {
        gtk_widget_set_sensitive(button, FALSE);
    }
    
    try {
        w->recv_thread = std::thread([w]() {
            char buffer[4096];
            char *line;
            
            while (w->running) {
                int n = recv(w->sock, buffer, sizeof(buffer) - 1, 0);
                if (n <= 0) {
                    w->running = false;
                    g_idle_add((GSourceFunc)show_error, strdup("Rozłączono z serwerem"));
                    break;
                }
                
                buffer[n] = 0;
                
                line = strtok(buffer, "\n");
                
                while (line) {
                    if (strncmp(line, "WELCOME", 7) == 0) {
                        line = strtok(NULL, "\n");
                        continue;
                    }
                    
                    if (strncmp(line, "ROOMS", 5) == 0) {
                        if (!w->chat_window) {
                            line = strtok(NULL, "\n");
                            continue;
                        }
                        
                        char *saveptr;
                        char *token = strtok_r(line + 6, " ", &saveptr);
                        if (!token) {
                            line = strtok(NULL, "\n");
                            continue;
                        }
                        
                        int room_count = atoi(token);
                        
                        RoomsData *rd = new RoomsData;
                        rd->widgets = w;
                        rd->room_count = room_count;
                        
                        for (int i = 0; i < room_count; i++) {
                            token = strtok_r(NULL, " ", &saveptr);
                            if (!token) break;
                            
                            char *room_info = strdup(token);
                            char *parts[3];
                            int part_idx = 0;
                            char *start = room_info;
                            
                            for (int j = 0; room_info[j] != '\0' && part_idx < 3; j++) {
                                if (room_info[j] == ':') {
                                    room_info[j] = '\0';
                                    parts[part_idx++] = start;
                                    start = &room_info[j+1];
                                }
                            }
                            
                            if (part_idx < 2) {
                                free(room_info);
                                continue;
                            }
                            
                            parts[part_idx] = start;
                            
                            rd->room_names.push_back(std::string(parts[0]));
                            rd->room_player_counts.push_back(atoi(parts[1]));
                            
                            if (part_idx >= 2) {
                                rd->room_in_game.push_back(atoi(parts[2]));
                            } else {
                                rd->room_in_game.push_back(0);
                            }
                            
                            free(room_info);
                        }
                        
                        g_idle_add(update_rooms_list, rd);
                    }
                    else if (strncmp(line, "ROOM_PLAYERS", 12) == 0) {
                        if (!w->room_window) {
                            line = strtok(NULL, "\n");
                            continue;
                        }
                        
                        char *players_list = line + 13;
                        char *temp = strdup(players_list);
                        
                        int count = 0;
                        char *token = strtok(temp, " ");
                        while (token) {
                            count++;
                            token = strtok(NULL, " ");
                        }
                        free(temp);
                        
                        PlayersData *pd = new PlayersData;
                        pd->widgets = w;
                        pd->player_count = count;
                        
                        temp = strdup(players_list);
                        token = strtok(temp, " ");
                        for (int i = 0; i < count && token; i++) {
                            pd->player_names.push_back(std::string(token));
                            token = strtok(NULL, " ");
                        }
                        free(temp);
                        
                        g_idle_add(update_room_players_list, pd);
                    }
                    else if (strncmp(line, "JOINED", 6) == 0) {
                        int room_id = atoi(line + 7);
                        
                        {
                            std::lock_guard<std::mutex> lock(w->data_mutex);
                            w->room_id = room_id;
                            w->in_game = false;
                        }
                        
                        if (!w->room_window) {
                            w->room_window = create_room_window(w);
                            
                            if (w->room_start_btn && GTK_IS_BUTTON(w->room_start_btn)) {
                                g_signal_connect(w->room_start_btn, "clicked", G_CALLBACK(start_game_clicked), w);
                            }
                            if (w->room_leave_btn && GTK_IS_BUTTON(w->room_leave_btn)) {
                                g_signal_connect(w->room_leave_btn, "clicked", G_CALLBACK(leave_room_clicked), w);
                            }
                            if (w->room_chat_send_btn && GTK_IS_BUTTON(w->room_chat_send_btn)) {
                                g_signal_connect(w->room_chat_send_btn, "clicked", G_CALLBACK(send_chat_message), w);
                            }
                        }
                        
                        if (w->chat_window) {
                            g_idle_add(safe_hide_window, w->chat_window);
                        }
                        g_idle_add(safe_show_window, w->room_window);
                    }
                    else if (strncmp(line, "GAME", 4) == 0) {
                        g_idle_add(switch_to_game_window_safe, w);
                        
                        GameStateData *gsd = new GameStateData;
                        gsd->widgets = w;
                        gsd->game_state_line = line;
                        
                        g_timeout_add(200, update_game_state, gsd);
                    }
                    else if (strncmp(line, "ROOM_LOBBY", 10) == 0) {
                        bool disconnecting = w->disconnecting.load();
                        
                        if (disconnecting) {
                            break;
                        }
                        
                        g_timeout_add(100, switch_to_room_window_safe, w);
                    }
                    else if (strncmp(line, "RANKING_FULL", 12) == 0) {
                        bool disconnecting = w->disconnecting.load();
                        
                        if (disconnecting) {
                            break;
                        }
                        
                        if (strlen(line) > 13) {
                            RankingData *rd = new RankingData;
                            rd->widgets = w;
                            rd->ranking_text = line + 13;
                            
                            g_idle_add((GSourceFunc)handle_ranking_full, rd);
                        }
                    }
                    else if (strncmp(line, "CHAT", 4) == 0) {
                        GtkWidget *chat_box = NULL;
                        
                        {
                            std::lock_guard<std::mutex> lock(w->data_mutex);
                            chat_box = w->in_game ? w->game_chat_box : w->room_chat_box;
                        }
                        
                        if (chat_box && GTK_IS_BOX(chat_box)) {
                            const char *chat_text = line + 5;
                            add_message_to_chat(chat_box, chat_text, false);
                        }
                    }
                    else if (strncmp(line, "ERROR", 5) == 0) {
                        char *error_msg = strdup(line + 6);
                        
                        if (strstr(error_msg, "Nickname already taken")) {
                            g_idle_add((GSourceFunc)show_nickname_error, w);
                        } else {
                            g_idle_add(show_error, error_msg);
                        }
                    }
                    else if (strncmp(line, "OK", 2) == 0) {
                        if (strstr(line, "Nickname set to")) {
                            g_idle_add(create_and_show_lobby, w);
                        } else {
                            GtkWidget *chat_box = nullptr;
                            
                            {
                                std::lock_guard<std::mutex> lock(w->data_mutex);
                                chat_box = w->in_game ? w->game_chat_box : w->room_chat_box;
                            }
                            
                            if (chat_box && GTK_IS_BOX(chat_box)) {
                                add_message_to_chat(chat_box, line + 3, true);
                            }
                        }
                    }
                    else if (strncmp(line, "ROOM_CREATED", 12) == 0) {
                        int room_id = atoi(line + 13);
                        
                        if (w->chat_box && GTK_IS_BOX(w->chat_box)) {
                            char msg[100];
                            snprintf(msg, sizeof(msg), "Pokój utworzony (ID: %d). Kliknij 'Dołącz' aby wejść.", room_id);
                            add_message_to_chat(w->chat_box, msg, true);
                        }
                        
                        send_message(w->sock, "REFRESH\n");
                    }
                    
                    line = strtok(NULL, "\n");
                }
            }
        });
        
        w->recv_thread.detach();
        
    } catch (const std::exception& e) {
        if (button && GTK_IS_BUTTON(button)) {
            g_idle_add((GSourceFunc)[](gpointer data) -> gboolean {
                AppWidgets *w = static_cast<AppWidgets*>(data);
                if (w->connect_btn && GTK_IS_WIDGET(w->connect_btn)) {
                    gtk_widget_set_sensitive(w->connect_btn, TRUE);
                }
                return FALSE;
            }, w);
        }
        
        close(sock);
        
        {
            std::lock_guard<std::mutex> lock(w->data_mutex);
            w->sock = -1;
            w->running = false;
        }
        
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(w->connection_window),
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_ERROR,
                                                  GTK_BUTTONS_OK,
                                                  "Nie można utworzyć wątku odbierającego!");
        gtk_window_set_title(GTK_WINDOW(dialog), "Błąd");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    
    AppWidgets *w = new AppWidgets();
    
    GtkWidget *win = create_connection_window(w);
    
    if (w->connect_btn) {
        g_signal_connect(w->connect_btn, "clicked", G_CALLBACK(connect_clicked), w);
    }
    
    gtk_widget_show_all(win);
    gtk_main();
    
    delete w;
    
    return 0;
}