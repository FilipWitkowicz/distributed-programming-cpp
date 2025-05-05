#include <mpi.h>
#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <string>

using namespace std;




const int TAG_REQUEST = 1;
const int TAG_REPLY = 2;
const int TAG_M_RELEASE = 3;

struct Request_Reply {
    int timestamp;
    int pid;
    int mechanics; // dla requesta ile mechaników chce, dla replya ile ma zajętych
    int tag; // 1 = dock, 2 = mechanic, 3 = release
};


int lamport_clock = 0;
int N, pid; // N - liczba statkow
vector<int> pid_to_inform_about_release; 

void print_color(const std::string& message) {
    // Tablica kolorów ANSI
    const char* colors[] = {
        "\x1B[31m", // Czerwony
        "\x1B[32m", // Zielony
        "\x1B[33m", // Żółty
        "\x1B[34m", // Niebieski
        "\x1B[35m", // Fioletowy
        "\x1B[36m", // Cyjan
        "\x1B[37m", // Biały
    };

    // Wybór koloru na podstawie pid
    const char* color = colors[pid % 7]; // Modulo, aby nie wyjść poza tablicę

    // Wypisanie wiadomości w kolorze
    std::cout << color << "[" << pid << "] " << message << "\033[0m" << std::endl; // \033[0m resetuje kolor
}

void update_clock(int received_ts) {
    lamport_clock = std::max(lamport_clock, received_ts) + 1;
}

void send_request(int tag, int needed_mechanics = 0) {
    Request_Reply req = {lamport_clock, pid, needed_mechanics, tag};
    if(tag == 3) {
        print_color("Uwalniam mechaników " + to_string(needed_mechanics));
        // zwolnienie mechaników
        int pid_to_send;;
        for (int i = 0; i < pid_to_inform_about_release.size(); ++i) {
            pid_to_send = pid_to_inform_about_release[i];
            print_color("Uwalniam mechaników " + to_string(needed_mechanics) + " do " + to_string(pid_to_send));
            MPI_Send(&req, sizeof(req), MPI_BYTE, pid_to_send, TAG_REQUEST, MPI_COMM_WORLD);
        }
        lamport_clock++;
        return;
    }
    if(tag == 1) print_color("BŁAGAM O DOK");
    else if(tag == 2) print_color("BŁAGAM O MECHANIKÓW " + to_string(needed_mechanics));
    for (int i = 0; i < N; ++i) {
        
        if (i != pid) {
            MPI_Send(&req, sizeof(req), MPI_BYTE, i, TAG_REQUEST, MPI_COMM_WORLD);
        }
    }
    lamport_clock++;
}

void send_reply(int dest, int tag, int occupied_mechanics = 0) {
    string msg;
    if (tag == 2) msg = "REPLY: do " + to_string(dest) + " ja zajmuje " + to_string(occupied_mechanics) + " mechaników";
    else if(tag == 1) msg = "REPLY: do " + to_string(dest) + " o dok i zezwalam na użycie doku";
    print_color(msg);
    lamport_clock++;
    Request_Reply rep = {lamport_clock, pid, occupied_mechanics, tag};
    MPI_Send(&rep, sizeof(rep), MPI_BYTE, dest, TAG_REPLY, MPI_COMM_WORLD);
}

void process_queue(vector<Request_Reply>& queue) {
    for (auto it = queue.begin(); it != queue.end();) {
        if (it->tag == 1) {
            // Obsługa doków
            send_reply(it->pid, 1);
            it = queue.erase(it); // Usuwamy z kolejki
        } else if (it->tag == 2) {
            // Obsługa mechaników
            send_reply(it->pid, 2, 0);
            it = queue.erase(it); // Usuwamy z kolejki
        } else {
            ++it; // Przechodzimy do następnego elementu
        }
    }
}

void handle_request(const Request_Reply& req, MPI_Status& status, vector<Request_Reply>& queue, int& available_mechanics, int& LC_last_request_dock, int& LC_last_request_mechanics, bool& want_dock, bool& want_repair, bool& in_dock, int Z) {
    string msg;
    if (req.tag == 1) {
        // Obsługa żądań o dok
        msg = "Otrzymany DOK REQUEST\n\tPID\tTIMESTAMP\tWant dock?\nHIM\t" + to_string(req.pid) + "\t" + to_string(req.timestamp) + "\t" + "\nME\t" + to_string(pid) + "\t" + to_string(LC_last_request_dock) + "\t\t" + to_string(want_dock);
        print_color(msg);
        if (!in_dock && (!want_dock || (req.timestamp < LC_last_request_dock || (req.timestamp == LC_last_request_dock && req.pid < pid)))) {
            send_reply(req.pid, 1); // Zezwalamy na użycie doku
        } else {
            queue.push_back(req); // Dodajemy do kolejki
        }
    } else if (req.tag == 2) {
        // Obsługa żądań o mechaników
        msg = "Otrzymany MECHANIC REQUEST\n\tPID\tTIMESTAMP\tWant repair?\nHIM\t" + to_string(req.pid) + "\t" + to_string(req.timestamp) + "\t" + "\nME\t" + to_string(pid) + "\t" + to_string(LC_last_request_mechanics) + "\t\t" + to_string(want_repair);
        print_color(msg);
        if (!in_dock && (!want_repair || (req.timestamp < LC_last_request_mechanics || (req.timestamp == LC_last_request_mechanics && req.pid < pid)))) {
            send_reply(req.pid, 2, 0);
            available_mechanics -= req.mechanics;
            msg = "Ava M = " + to_string(available_mechanics);
            print_color(msg);
        } else {
            send_reply(req.pid, 2, Z);
        }
    } else if (req.tag == 3) {
        // Zwolnienie mechaników
        available_mechanics += req.mechanics;
        msg = "Zwolniono mechaników: " + to_string(req.mechanics) + "\nAva M = " + to_string(available_mechanics);
        print_color(msg);
    }
}

void handle_reply(const Request_Reply& req, int& reply_count_dock, int& reply_count_mechanics, vector<int>& pid_to_inform_about_release, int replies_needed, bool& in_dock, bool& want_dock, bool& in_repair, int& available_mechanics, int Z, vector<Request_Reply>& queue) {
    if (req.pid == pid) return; // Ignoruj odpowiedź od samego siebie

    if (req.tag == 1) {
        // Odpowiedź dotycząca doków
        reply_count_dock++;
    } else if (req.tag == 2) {
        // Odpowiedź dotycząca mechaników
        reply_count_mechanics++;
        if (req.mechanics == 0) {
            pid_to_inform_about_release.push_back(req.pid);
            print_color("Dodano do release: " + to_string(req.pid));
        }
    }

    string msg = "\n\tReply count dock " + to_string(reply_count_dock) +
                 "\n\t replies needed " + to_string(replies_needed) +
                 "\n\t !in_dock " + to_string(!in_dock) +
                 "\n\treply_count_mechanics " + to_string(reply_count_mechanics) +
                 "\n\t available mechanics " + to_string(available_mechanics) +
                 "\n\t Z " + to_string(Z);
    print_color(msg);

    // Sprawdzanie, czy mamy wystarczająco doków
    if (reply_count_dock >= replies_needed && !in_dock) {
        in_dock = true;
        print_color("[" + to_string(pid) + "] Zadokowano!");
        want_dock = false;
        reply_count_dock -= (N - 1);
    }

    // Sprawdzanie, czy mamy wystarczająco mechaników
    if (in_dock && reply_count_mechanics >= N - 1 && !in_repair) {
        if (available_mechanics >= Z) {
            in_repair = true;
            print_color("[" + to_string(pid) + "] Rozpoczynam naprawę z " + to_string(Z) + " mechanikami.");
            sleep(1); // Symulacja naprawy
            in_repair = false;
            in_dock = false;
            reply_count_mechanics = 0;
            send_request(3, Z); // Zwolnienie mechaników
            pid_to_inform_about_release.clear(); // Czyścimy listę PID-ów
            process_queue(queue);
        } else {
            print_color("Nie mamy wystarczająco mechaników, czekam na zwolnienie " + to_string(Z) + " mechaników od innych statków.");
            send_request(2, Z); // Wysyłamy nowe żądanie
        }
    }
}


int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &N);
    MPI_Comm_rank(MPI_COMM_WORLD, &pid);
    print_color("Start programu");

    const int K = 1; // Liczba doków
    const int M = 5; // Liczba mechaników

    srand(time(NULL) + pid);
    int Z = 0;
    bool want_dock = false, in_dock = false, want_repair = false, in_repair = false;
    int replies_needed = N - K, reply_count_dock = 0, reply_count_mechanics = 0;
    int available_mechanics = M, LC_last_request_dock = 0, LC_last_request_mechanics = 0;
    vector<Request_Reply> queue;
    vector<int> pid_to_inform_about_release;

    while (true) {
        // Jeżeli jesteśmy w pełni sprawni to losujemy czy idziemy na wojnę
        if(!in_dock && !want_dock && !in_repair && !want_repair) {
            // losujemy czy idziemy na wojne
            // a jak idziemy na wojne to losujemy ile naprawic
            if (rand() % 2 == 0) {
                //bylismy na wojnie (krew, ból, smierc, łzy ta linijka jest bardzo naładowana emocjonalnie). Wojna była dużo brutalniejsza niż ktokolwiek zakładał. Misie były równie bezwzględne co słodkie. Wiele osób straciło ręce i nogi. Niektórzy stracili życie, a niektórzy przeżyli - ciężko powiedzieć, która z tych opcji była lepsza. Straty w ludziach były przez federacje przewidziane, jednak statki wróciły bardziej zniszczone niż zakładano. Obecnie najważniejsze dla federacji jest jedno - naprawić jak najszybciej zniszczone statki aby móc wysłać kolejne oddziały na wojnę.
                print_color("Powrót z wojny, misie są bezlitosne");
                want_dock = true;//wiec teraz chcemy do doku
                Z = 1 + rand() % M; // liczba potrzebnych mechanikow (1-M)
                want_repair = true; // chcemy naprawic statek
                LC_last_request_dock = lamport_clock;
                send_request(1);
                LC_last_request_mechanics = lamport_clock;
                send_request(2, Z);
            }
        }

        MPI_Status status;
        Request_Reply req;
        int flag;

        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
        if (!flag) continue;

        MPI_Recv(&req, sizeof(req), MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        update_clock(req.timestamp);

        if (status.MPI_TAG == TAG_REQUEST) {
            handle_request(req, status, queue, available_mechanics, LC_last_request_dock, LC_last_request_mechanics, want_dock, want_repair, in_dock, Z);
        } else if (status.MPI_TAG == TAG_REPLY) {
            handle_reply(req, reply_count_dock, reply_count_mechanics, pid_to_inform_about_release, replies_needed, in_dock, want_dock, in_repair, available_mechanics, Z, queue);
        }
    }

    MPI_Finalize();
    return 0;
}