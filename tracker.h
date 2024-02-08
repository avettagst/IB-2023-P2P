#pragma once
#include "protocol.h"


class ClientData{
    private:
        string name;
        struct sockaddr_in address;
        ClientID_t id;
        uint16_t peerPort;
        thread ServThread; //service thread
        int socket; //client socket
    
    public:
        ClientData(int socket_, const struct sockaddr_in address_, std::function<void (ClientData*)> fn)
            :address(address_), ServThread(fn, this) , socket(socket_)
        {
        }

        ~ClientData(){
            ServThread.join();
        }

        void setName(const char* name_){
            name = name_;
        }

        void setPeerPort(const uint16_t port){
            peerPort = port;
        }

        const uint16_t getPeerPort() const{
            return peerPort;
        }

        const string getName() const{
            return name;
        }

        void setID(ClientID_t id_){
            id = id_;
        }

        ClientID_t getID(){
            return id;
        }


        int getSocket() const{
            return socket;
        }

        const sockaddr_in getAddress() const{
            return address;
        }
};

class TrackerFile{
    //quizá habría que mejorar la privacidad de esta clase
    private:
        string name;
        FileID_t id;
        uint64_t size;

    
    public:
        TrackerFile(string name_, FileID_t id_, uint64_t size_)
        :name(name_), id(id_), size(size_)
        {}

        string getFilename(){
            return name;
        }

        FileID_t getFileID(){
            return id;
        }

        const uint64_t getFilesize() const{
            return size;
        }
};



class Tracker{

    private:
        struct sockaddr_in ServerAddr;
        socklen_t AddrLen=sizeof(ServerAddr);
        vector<ClientData*> connectedClients;
        ClientID_t ClientID; //para asignar a los clientes que se conecten
        FileID_t FileID; //para asignar a los files publicados
        vector<TrackerFile> PublishedFiles;
        int ss; //server socket
        Multimap mm;

        //Sincronización
        mutex conection_mtx; //sincroniza ID de clientes
        mutex publish_mtx; //sincroniza ID de files

    void printAddress(sockaddr_in address){
        string ipAddress = inet_ntoa(address.sin_addr);
        uint16_t port = ntohs(address.sin_port);

        cout << "IP Address: " << ipAddress << std::endl;
        cout << "Port: " << port << std::endl;
    }


    public:
        Tracker(uint16_t port_)
            :ClientID(1), FileID(1), mm() //Multimapa vacío, id comienza en 0
        {   
            //Dirección del server
            ServerAddr.sin_family = AF_INET;
            ServerAddr.sin_addr.s_addr = INADDR_ANY;
            ServerAddr.sin_port = htons(port_);

            int val = 1;
            setsockopt(ss, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
            
            //bindeo el socket a la dirección del Server
            ss  = socket (PF_INET, SOCK_STREAM, 0);
            if(ss == -1){
                perror("Error creating listen socket on tracker: ");
            }
            bind(ss, (struct sockaddr *)&ServerAddr, AddrLen);
            showServerAddress();
            
            //pongo ss a escuchar y ejecuto acceptClients
            listen(ss , 5); //backlog (5): no importa
            acceptClients();
        }


        //Debe estar activa siempre
        //Acepta conexión y dispara thread de comunicación con ese cliente
        void acceptClients(){
            while(true){
                struct sockaddr_in clientAddr;
                socklen_t AddrLen = sizeof(clientAddr);
                int cs = accept(ss, (sockaddr*)&clientAddr, &AddrLen);
                if (cs >= 0){
                    cout << "\n\nClient connected with address\n";
                    printAddress(clientAddr);
                    ClientData* nc = new ClientData(cs, clientAddr, std::bind(&Tracker::clientService, this, placeholders::_1)); 
                    connectedClients.emplace_back(nc);
                }
                else{
                    perror("Error accepting clients: ");
                }
            }
        }

        //Thread que atiende al cliente (largado por acceptClients)
        void clientService(ClientData* client){
            Msg m;
            cout << "\n>>>clientService thread\n";
            while(1){
                m.receiveFrom(client->getSocket());
                trackerDispatch(m, client);
            }
        }

        //Recibe mensajes del cliente (publish/request) y larga la funcion correspondiente
        void trackerDispatch(const Msg &m, ClientData* client){
            switch(m.getType()){
                case (uint8_t)MsgType::CON:
                    cout << "\n<-- CON\n";
                    recConnect(m.getClientname(), m.getPeerPort(), client);
                    break;
                
                case (uint8_t)MsgType::REQ:
                    cout << "\n<-- REQ: " << m.getFilename() << endl;
                    recRequest(m.getFilename(), client);
                    break;

                case (uint8_t)MsgType::PUB:
                    cout << "\n<-- PUB: " << m.getFilename() << endl;
                    cout << "Tamaño del archivo publicado: " << m.getSize() << endl;
                    recPublish(m.getFilename(), m.getBitmap(), m.getSize(), client);
                    break;

                case (uint8_t)MsgType::ADDRESS:
                    cout << "\n<-- ADD\n";
                    cout << "Dirección del cliente con ID = " << m.getClientID() << endl;
                    recAddressRequest(m.getClientID(), client);
                    break;

                case (uint8_t)MsgType::DIS:
                    recDisconnect();
                    break;
            }
        }

        //El tracker le asigna un id al cliente, lo almacena en su base de datos (nombre, dirección, id) y le responde
        void recConnect(const char* clientname, const uint16_t peerPort, ClientData* client){

            unique_lock<mutex> con_lck{conection_mtx}; //Tomo el recurso
            client->setID(ClientID); //Completo la información del cliente
            ClientID++; //Actualizo el ClientID del tracker
            con_lck.unlock(); //Libero el recurso
            client->setName(clientname);
            client->setPeerPort(peerPort);
            
        
            cout << "Nombre: " << client->getName() << endl;
            cout << "ID: " << client->getID() << endl;

            //Le respondo
            Msg response;
            response.setConnectResponse(client->getID());
            response.writeTo(client->getSocket());  
            cout << "--> CON_RSP\n";          
        }

        void recRequest(const char* name, ClientData* client){
            //primero obtener el ID del file a partir de su nombre y despues ejecutar findFile
            FileID_t requestedID = 0;
            Msg response;
            size_t N; //number of seeders
            uint64_t filesize = 0;

            

            cout << "\nReviso archivos ya publicados:\n";
            for (auto& file : PublishedFiles){
                cout << endl << file.getFilename() << " - ID: " << file.getFileID();
                if(strncmp(file.getFilename().c_str(), name, NAME_MAXLEN) == 0){
                    cout << " -- COINCIDE\n\n";
                    requestedID = file.getFileID();
                    const vector<Seeder> requestedSeeders = mm.requestedSeeders(requestedID);
                    N = requestedSeeders.size();
                    filesize = file.getFilesize();
                    
                    response.setRequestResponse(requestedSeeders, requestedID, N, filesize);
                    response.writeTo(client->getSocket());
                    cout << "--> REQ_RSP\n";
                    return; 
                }
            }

            //si no hubo coincidencias...
            vector<Seeder> nullVector;
            cout << "\n X - NO HUBO COINCIDENCIAS\n\n";
            response.setRequestResponse(nullVector, requestedID, 0, filesize);
            response.writeTo(client->getSocket());
            cout << "--> REQ_RSP\n";          
        }


        void recPublish(const string &name, const Bitmap bm, const uint64_t size_,  ClientData* client){

            //Tomo el recurso apenas recibo el mensaje. Lo libero una vez resuelta la cuestión
            unique_lock<mutex> pub_lck{publish_mtx};
            if(PublishedFiles.size() == 0){
                cout << name << " - primer file publicado" << endl;
                
                FileID_t newFileID = FileID;
                FileID++;
                //lo agrego al vector de archivos publicados
                TrackerFile newPublishedFile(name, newFileID, size_);
                PublishedFiles.emplace_back(newPublishedFile);    
                //lo agrego al multimapa
                Seeder sdr(client->getID(), bm);
                mm.addFile(newFileID, sdr);
                publish_mtx.unlock(); //libero el recurso
                

                //y respondo con el ID correspondiente
                Msg resp;
                resp.setPublishResponse(newFileID);
                resp.writeTo(client->getSocket());
                cout << "--> PUB_RSP. FileID: " << newFileID << endl;
                return;
            }        

            for(auto& file: PublishedFiles){
                if(strncmp(name.c_str(), file.getFilename().c_str(), NAME_MAXLEN) == 0){
                    cout << name << " - anteriormente publicado" << endl;
                    //si ya fue publicado...
                    //actualizo multimapa
                    Seeder sdr(client->getID(), bm);
                    mm.refreshSeeders(file.getFileID(), sdr);
                    publish_mtx.unlock(); //libero el recurso
    
                    //respondo con el ID correspondiente
                    Msg resp;
                    resp.setPublishResponse(file.getFileID());
                    resp.writeTo(client->getSocket());
                    cout << "--> PUB_RSP. FileID: " << file.getFileID() << endl;

                    return; //ya terminé, salgo de la función
                }
            }


            //si llegue acá, es porque aún no fue publicado...
            cout << name << " - aún no fue publicado" << endl;
            //actualizo FileID y se lo asigno a dicho File
            FileID_t newFileID = FileID;
            FileID++;
        
            //lo agrego al vector de archivos publicados
            TrackerFile newPublishedFile(name, newFileID, size_);
            PublishedFiles.emplace_back(newPublishedFile);
            
            //lo agrego al multimapa
            Seeder sdr(client->getID(), bm);
            mm.addFile(newFileID, sdr);
            publish_mtx.unlock(); //libero el recurso

            //y respondo con el ID correspondiente
            Msg resp;
            resp.setPublishResponse(newFileID);
            resp.writeTo(client->getSocket());
            cout << "--> PUB_RSP. FileID: " << newFileID << endl;
                
        }

        void recAddressRequest(ClientID_t id, ClientData* client){
            Msg r;
            
            for(auto& c : connectedClients){
                if(c->getID() == id){
                    r.setAddressResponse(c->getAddress(), c->getPeerPort());
                    r.writeTo(client->getSocket());
                    cout << "--> ADD_RSP\n";

                }
            }
        }

        void recDisconnect(){

        }



        // void recAccept(int cs){

        // }

        //Imprimo la dirección del Server en consola
        void showServerAddress(){
            string ipAddress = inet_ntoa(ServerAddr.sin_addr);
            uint16_t port = ntohs(ServerAddr.sin_port);

            cout << "IP Address: " << ipAddress << std::endl;
            cout << "Port: " << port << std::endl;
        
        }

};