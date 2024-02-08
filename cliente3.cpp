#include "protocol.h"
#include "client.h"



int main(){
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    uint16_t serverPort = 5001;
    uint16_t peerPort = 5053;
    serverAddress.sin_port = htons(serverPort);
    cout << "ServerAddress is:\n";
    printAddress(serverAddress);
    Client me("Charly", "c3", peerPort);
    me.run_request(serverAddress, "HarryPotter2.mp4", peerPort);
    //me.run(serverAddress, peerPort);


    return 0;
}