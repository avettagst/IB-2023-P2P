#include "protocol.h"
#include "client.h"



int main(){
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    uint16_t serverPort = 5001;
    uint16_t peerPort = 5052;
    serverAddress.sin_port = htons(serverPort);
    cout << "ServerAddress is:\n";
    printAddress(serverAddress);
    Client me("Bob", "c2", peerPort);
    me.run_request(serverAddress, "chikiado.jpeg", peerPort);


    return 0;
}