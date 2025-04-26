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
        print_color("Uwalniam mechaników");
        // zwolnienie mechaników
        for (int i = 0; i < N; ++i) {
            if (i != pid) {
                MPI_Send(&req, sizeof(req), MPI_BYTE, i, TAG_M_RELEASE, MPI_COMM_WORLD);
            }
        }
    }
    if(tag == 1) print_color("BŁAGAM O DOK");
    else print_color("BŁAGAM O MECHANIKA");
    for (int i = 0; i < N; ++i) {

        if (i != pid) {
            MPI_Send(&req, sizeof(req), MPI_BYTE, i, TAG_REQUEST, MPI_COMM_WORLD);
        }
    }
    lamport_clock++;
}

void send_reply(int dest, int tag, int occupied_mechanics = 0) {
    string zasob;
    if (tag == 2) zasob = "mechaników";
    else zasob = "doki";
    string msg = "Wysyłam odpowiedź do " + to_string(dest) + " o " + zasob;
    print_color(msg);
    lamport_clock++;
    Request_Reply rep = {lamport_clock, pid, occupied_mechanics, tag};
    MPI_Send(&rep, sizeof(rep), MPI_BYTE, dest, TAG_REPLY, MPI_COMM_WORLD);
}

int main(int argc, char** argv) {
    // od tego miejsce kod jest równoległy
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &N);
    MPI_Comm_rank(MPI_COMM_WORLD, &pid);
    print_color("Start programu");

    const int K = 3; // liczba dokow
    const int M = 5; // liczba mechanikow

    srand(time(NULL) + pid);
    int Z = 0;
    bool want_dock = false;
    bool in_dock = false;
    bool want_repair = false;
    bool in_repair = false;


    int replies_needed = N - K;
    int reply_count_dock = 0;
    int reply_count_mechanics = 0;

    int available_mechanics = M;

    int lamport_clock_requests = 0;

    int flag;

    vector<Request_Reply> queue;
    string msg;
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
                lamport_clock_requests = lamport_clock;
                send_request(1);
                send_request(2, Z);
            }
        }
        MPI_Status status;
        Request_Reply req;

        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
        if (!flag) {
            continue;// Nie ma wiadomości, kontynuuj
        }

        MPI_Recv(&req, sizeof(req), MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        update_clock(req.timestamp);
        if (status.MPI_TAG == TAG_REQUEST) {
            string zasob;
            if (req.tag == 2) zasob = "mechaników";
            else zasob = "doki";
            msg = "Dostałem zapytanie od " + to_string(req.pid) + " o " + zasob;
            print_color(msg);
            if (req.tag == 1) {
                // Doki
                msg = "\tReq.timestamp = " +to_string(req.timestamp) + "\n\tlamport_clock_req = " + to_string(lamport_clock_requests) + "\n\treq.pid = " + to_string(req.pid) + "\n\tpid = " + to_string(pid);
                print_color(msg);
                if (!in_dock && (!want_dock || (req.timestamp < lamport_clock_requests || (req.timestamp == lamport_clock_requests && req.pid < pid)))) {
                    send_reply(req.pid, 1); // zezwalamy na użycie doku
                } else {
                    queue.push_back(req); // dodajemy do kolejki
                }
            } else if (req.tag == 2) {
                // Mechanicy
                if (!in_dock && (!want_repair || (req.timestamp < lamport_clock_requests || (req.timestamp == lamport_clock_requests && req.pid < pid)))) {
                    send_reply(req.pid, 2, 0);
                    available_mechanics -= req.mechanics;
                } else {
                    send_reply(req.pid, 2, Z);
                    //queue.push_back(req); // dodajemy do kolejki
                }
            }
            else if (req.tag == 3) {
                // Zwolnienie mechaników
                available_mechanics += req.mechanics;
            }
        }

        else if (status.MPI_TAG == TAG_REPLY) {
            print_color("TAG_REPLY");
            update_clock(req.timestamp);
            if (req.pid == pid) {
                continue; // Odpowiedź od samego siebie, ignoruj
            }
            if (req.tag == 1) {
                // Odpowiedź dotycząca doków
                reply_count_dock++;
            }
            else if (req.tag == 2) {
                // Odpowiedź dotycząca mechaników
                reply_count_mechanics++;
            }
            msg = "\n\tReply count dock " + to_string(reply_count_dock) + "\n\t replies needed " + to_string(replies_needed) + "\n\t !in_dock " + to_string(!in_dock);
            print_color(msg);
            // Sprawdzanie, czy mamy wystarczająco doków
            if (reply_count_dock >= replies_needed && !in_dock) {
                in_dock = true;
                std::cout << "[" << pid << "] Zadokowano!" << std::endl;
                //in_dock = false;
                want_dock = false;
                reply_count_dock -= N;
            }
            if(reply_count_mechanics >= N - 1 && !in_repair) {
                if (available_mechanics >= Z) {
                    in_repair = true;
                    std::cout << "[" << pid << "] Rozpoczynam naprawe z " << Z << " mechanikami." << std::endl;
                    // Teraz naprawa
                    sleep(1);
                    // Koniec naprawy
                    in_repair = false;
                    in_dock = false;
                    want_repair = false;

                    send_request(3, Z); // Wysyłamy zapytanie o zwolnienie mechaników

                    //przechodzimy przez kolejke i odsyłamy wszystkim osobom których requesty mieliśmy zakolejkowane że już okej
                    for (auto it = queue.begin(); it != queue.end();) {
                        if (it->tag == 1) {
                            // Doki
                            send_reply(it->pid, 1);
                            it = queue.erase(it); // Usuwamy z kolejki po wysłaniu odpowiedzi
                        } else if (it->tag == 2) {
                            // Mechanicy
                            send_reply(it->pid, 2, 0);
                            it = queue.erase(it); // Usuwamy z kolejki po wysłaniu odpowiedzi
                        } else {
                            ++it; // Przechodzimy do następnego elementu
                        }
                    }
                } else {
                    send_request(2, Z); // Nie mamy wystarczająco mechaników, wysyłamy nowe żądanie
                }
            }
        }

    }

    MPI_Finalize();
    return 0;
}