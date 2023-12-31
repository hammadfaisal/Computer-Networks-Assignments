#include <bits/stdc++.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netdb.h>
#include <sys/select.h>
#include <unistd.h>
#include <chrono>
#include <iostream>
#include "md5.h"

#define MYPORT "4950"
#define MAXREQUESTSIZE 1448
#define BUFFERSIZE 1500
using namespace std;

long long start_time;
int dupcount = 0;
int swapcount = 0;


void clear_buffer(int sockfd) {
    char buf[BUFFERSIZE];
    struct sockaddr_storage their_addr;
    socklen_t addr_len;
    addr_len = sizeof their_addr;
    while(true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        int ret = select(sockfd+1, &readfds, NULL, NULL, &tv);
        if (ret == -1) {
            cout << "select error" << endl;
            return;
        } else if (ret == 0) {
            cout << "timeout" << endl;
            return;
        } else {
            if (FD_ISSET(sockfd, &readfds)) {
                if (recvfrom(sockfd, buf, BUFFERSIZE-1, 0,
                             (struct sockaddr *)&their_addr, &addr_len) == -1) {
                    cout << "recvfrom error" << endl;
                    return;
                }
            }
        }
    }
}   



void varBlockSize(vector<char> &data, int file_size, int sockfd, struct addrinfo* p, double rate = 50) {
    int numbytes;
    char buf[BUFFERSIZE];
    set<int> q,q2;
    for (int i =0; i < (file_size+MAXREQUESTSIZE-1)/MAXREQUESTSIZE; i++) {
        q.insert(i);
    }
    double timeout = 1000/rate;
    vector<long long> sent_time((file_size+MAXREQUESTSIZE-1)/MAXREQUESTSIZE, 0);
    double req_window_frac = 1;
    double rtt = timeout;
    double rtt_var = 0;
    while(q.size()) {
        int req_window = (int) (req_window_frac);
        double cur_timeout = max(timeout,rtt + 4*rtt_var);
        long long next_time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count() + cur_timeout;
        for(int i =0; i<req_window;i++) {
            if (q.size()==0)
                break;
        int offset = *q.begin();
        q.erase(q.begin());
        q2.insert(offset);
        offset*=MAXREQUESTSIZE;
        string req = "Offset: "+to_string(offset)+"\nNumBytes: "+to_string(min(MAXREQUESTSIZE, file_size-offset))+"\n\n";
        if ((numbytes = sendto(sockfd, req.c_str(), req.length(), 0,
                                p->ai_addr, p->ai_addrlen)) == -1) {
            cout << "sendto error" << endl;
            return;
        }
        long long int curr_time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
        sent_time[offset/MAXREQUESTSIZE] = curr_time;
        }
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = max((next_time - chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count())*1000,0LL);
        int ret = select(sockfd+1, &readfds, NULL, NULL, &tv);
        int recv_count = 0;
        double avg_rtt = 0;
        while (select(sockfd+1, &readfds, NULL, NULL, &tv) > 0) {
            if (FD_ISSET(sockfd, &readfds)) {
                if ((numbytes = recvfrom(sockfd, buf, BUFFERSIZE-1, 0,
                                            p->ai_addr, &p->ai_addrlen)) == -1) {
                    cout << "recvfrom error" << endl;
                    return;
                }
                if ("Offset" !=  string(buf, 6)) {
                    continue;
                }
                buf[numbytes] = '\0';
                cout << "received " << numbytes << " bytes" << endl;
                // cout << "received data:\n" << buf;
                int j =0;
                // find the offset
                int recv_offset = 0;
                while(j<numbytes && buf[j]!=':') {
                    j++;
                }
                // cout <<"Here"<<endl;
                j+=2;
                while(j<numbytes && buf[j]!='\n') {
                    recv_offset = recv_offset * 10 + buf[j] - '0';
                    j++;
                }
                auto it = q2.find(recv_offset/MAXREQUESTSIZE);
                auto it2 = q.find(recv_offset/MAXREQUESTSIZE);
                if (it == q2.end() && it2 == q.end()) {
                    dupcount++;
                    continue;
                }
                recv_count++;
                if (it != q2.end()) {
                    q2.erase(it);
                }
                if (it2 != q.end()) {
                    q.erase(it2);
                }
                long long curr_rtt = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count() - sent_time[recv_offset/MAXREQUESTSIZE];
                avg_rtt = (avg_rtt*(recv_count-1) + curr_rtt)/recv_count;
                while(j<numbytes && (buf[j-1]!='\n' || buf[j]!='\n')) {
                    j++;
                }
                j++;
                // cout <<"Here2"<<endl;
                int initial_j = j;
                while(j<numbytes) {
                    data[recv_offset+j-initial_j] = buf[j];
                    j++;
                }
                int last_byte = recv_offset+numbytes-initial_j-1;
                
            }
            tv.tv_usec = max((next_time - chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count())*1000,0LL);
        }
        rtt_var = 0.75*rtt_var + 0.25*abs(rtt-avg_rtt);
        rtt = 0.875*rtt + 0.125*avg_rtt;
        if (recv_count>=req_window) {
            req_window_frac+=min(req_window_frac,(cur_timeout)/(4*(double)req_window*req_window));
        } else if (recv_count<req_window-1) {
            req_window_frac=1;
        }
        if (q.size()==0) {
            swap(q,q2);
            swapcount++;
        }
    }
}


int main(int argc, char* argv[]) {
    if (argc < 3) {
        cout << "Usage: " << argv[0] << " <IP> <port>" << endl;
        return 0;
    }
    start_time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    char buf[BUFFERSIZE];
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) {
        cout << "getaddrinfo: " << gai_strerror(rv) << endl;
        return 0;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family,
                             p->ai_socktype,
                             p->ai_protocol)) == -1) {
            cout << "socket error" << endl;
            continue;
        }
        break;
    }

    if (p == NULL) {
        cout << "failed to create socket" << endl;
        return 0;
    }
    //clear socket receive buffer
    clear_buffer(sockfd);
    string initial = "SendSize\nReset\n\n";
    int file_size = 0;
    while(true) {
        if ((numbytes = sendto(sockfd, initial.c_str(), initial.length(), 0,
                              p->ai_addr, p->ai_addrlen)) == -1) {
            cout << "sendto error" << endl;
            return 0;
        }
        cout << "sent " << numbytes << " bytes" << endl;
        // the server will send back the file size. use select since the first packet may be lost
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 20000;
        int ret = select(sockfd+1, &readfds, NULL, NULL, &tv);
        if (ret == -1) {
            cout << "select error" << endl;
            return 0;
        } else if (ret == 0) {
            cout << "timeout" << endl;
            continue;
        } else {
            if (FD_ISSET(sockfd, &readfds)) {
                if ((numbytes = recvfrom(sockfd, buf, BUFFERSIZE-1, 0,
                                         p->ai_addr, &p->ai_addrlen)) == -1) {
                    cout << "recvfrom error" << endl;
                    return 0;
                }
                for (int i = 0; i < numbytes; i++) {
                    if (buf[i]>='0' && buf[i]<='9')
                    file_size = file_size * 10 + buf[i] - '0';
                }
                cout << "file size: " << file_size << endl;
                break;
            }
        }
    }
    vector<char> data(file_size, 0);

    varBlockSize(data, file_size, sockfd, p);
    
    const string s(data.begin(), data.end());
    string md5hash =  md5(s);

    //submit
    string submit = "Submit: 2021CS10559@infiniteloop\nMD5: "+md5hash+"\n\n";
    while(true) {
        if ((numbytes = sendto(sockfd, submit.c_str(), submit.length(), 0,
                                p->ai_addr, p->ai_addrlen)) == -1) {
            cout << "sendto error" << endl;
            return 0;
        }
        cout << "sent " << numbytes << " bytes to " << argv[1] << endl;

        // the server sends back requested data
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 20000;
        int ret = select(sockfd+1, &readfds, NULL, NULL, &tv);
        if (ret == -1) {
            cout << "select error" << endl;
            return 0;
        } else if (ret == 0) {
            cout << "timeout" << endl;
            continue;
        } else {
            tv.tv_usec = 0;
            while (select(sockfd+1, &readfds, NULL, NULL, &tv) > 0) {
                if (FD_ISSET(sockfd, &readfds)) {
                    if ((numbytes = recvfrom(sockfd, buf, BUFFERSIZE-1, 0,
                                                p->ai_addr, &p->ai_addrlen)) == -1) {
                        cout << "recvfrom error" << endl;
                        return 0;
                    }
                    buf[numbytes] = '\0';
                    if ("Result" == string(buf, 6)) {
                        cout << "received " << numbytes << " bytes from " << argv[1] << endl;
                        cout << "received data:\n" << buf;
                        cout << "duplicate packets received: " << dupcount << endl;
                        cout << "swap count: " << swapcount << endl;
                        return 0;
                    }
                }
            }
        }
    }
}