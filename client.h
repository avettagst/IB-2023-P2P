#pragma once

#include <fstream>
#include <list>

#include "protocol.h"

using namespace std;


void readCompleteBlock(int fd, uint8_t* buf, size_t len){
    size_t pending = len;
    while(pending){
        int n = read(fd, buf, pending);
        if(n == -1 && errno == EINTR) //seguí participando
            continue;
        if(n == 0)  //ya no leo nada
            break;

        pending -= n;
        buf += n;
    }
}




//Qué tiene cada cliente: el archivo y su bitmap correspondiente
class ClientFile{
    //quizá habría que mejorar la privacidad de esta clase
    private:
        File file;
        Bitmap bm;

    
    public:
        ClientFile(const FileID_t id_, std::string &filename, const uint64_t nb)
        :file(id_, filename), bm(nb)
        {

        }


        ClientFile(const std::string &filename)
        :file(filename), bm(file.getNumBlocks())
        {
            bm.deserializar(Bitmap::fname_from_file(filename.c_str()).c_str());
        }


        const string getFilename() const{
            return file.getName();
        }

        const FileID_t getFileID() const{
            return file.getID();
        }

        const uint64_t getBlockSize() const{
            return file.getBlockSize();
        }

        Bitmap getBitmapconst(){
            return bm;
        }

        const uint64_t getFilesize() const{
            return file.getSize();
        }

        void setBitmap(Bitmap new_bm){
            bm = new_bm;
        }

        void setFileID(FileID_t id_){
            file.setID(id_);
        }

        Bitmap &getBitmap(){
            return bm;
        }


};



class PeerData{
    private:
        ClientID_t id;
        struct sockaddr_in address;
        int socket; //client socket
        Bitmap bm;
        thread PeerThread;
        ClientFile downloading;
        struct entry{
            FileID_t fileID;
            uint64_t block;
            entry(FileID_t id = 0, uint64_t b = 0)
            :fileID(id), block(b){}
        };
        deque<entry> queue;
        bool running = true;
        condition_variable BR_cv; //Block Request
        mutex BR_mtx;
        
    public:

        ~PeerData(){
            PeerThread.join();
        }

        //Mando TRANSF_REQ a los seeders que tienen el bloque que quiero
        void PeerTransfer(){
            while(running){
                unique_lock<mutex> lck{BR_mtx};
                while(queue.size() == 0){
                     BR_cv.wait(lck);
                }
                entry req = queue.front();  //tomo el primer pedido de la fila
                queue.pop_front();          //luego lo elimino

                Msg request;
                request.setTransferRequest(req.fileID, req.block);
                request.writeTo(socket);
                cout << "--> TRANSF_REQ\n";

                Msg response;
                response.receiveFrom(socket);
                cout << "<-- TRANSF_RSP\n";
                int fd = open (downloading.getFilename().c_str(), O_WRONLY | O_CREAT, 0644);
                if(fd==-1){
                    perror("Error abriendo archivo");
                    return;
                }
                lseek(fd, downloading.getBlockSize()*req.block, SEEK_SET);
                write(fd, response.getData(), downloading.getBlockSize());
                downloading.getBitmap().set(req.block, true);
                //almacenar datos del file y actualizar MI bitmap                
            }
        }

        void putRequest(FileID_t fileID, size_t block){
            unique_lock<mutex> lck{BR_mtx};
            entry new_req(fileID, block);
            queue.push_back(new_req);
        }


        PeerData(ClientID_t id_, sockaddr_in address_, int socket_, const Bitmap &bm_, ClientFile &downloadingFile)
            :id(id_), address(address_), socket(socket_), bm(bm_), downloading(downloadingFile)
        {
            PeerThread = thread(&PeerData::PeerTransfer, this);
        }

        const bool HasBlock(size_t i) const{
            return bm.get(i);
        }

        const int getPeerSocket() const{
            return socket;
        }




};





class Client{
    private:
        string Name;
        ClientID_t id;
        vector<ClientFile> Files;
        struct sockaddr_in listenAddress;
        int cs; //connected socket (al tracker)
        int ls; //listen socket
        vector<thread> DownloadTh;
        vector<thread> UploadTh;
        thread AcceptThread;
        bool running;

        uint64_t DownloadBlockSize(uint64_t size){
            uint64_t blk_size;

            if(size <= BlockLUT[0][0]){ //primer entrada
                blk_size = BlockLUT[0][1];
                return blk_size;
            }

            for (int i = 1; i < LUT_SIZE-1; i++){
                if(size > BlockLUT[i][0] && size <= BlockLUT[i+1][0]){
                    blk_size = BlockLUT[i][1];
                    return blk_size;
                }
            }
            
            //última entrada
            blk_size = BlockLUT[LUT_SIZE-1][1];
            return blk_size;
        }

        uint64_t DownloadBlockNumber(uint64_t size, uint64_t blk_size){
            return (size + blk_size - 1)/blk_size;
        }

        const sockaddr_in getAddress(ClientID_t id){
            Msg m;
            m.setAddressRequest(id);
            m.writeTo(cs);
            cout << "--> ADD\n";

            Msg r;
            r.receiveFrom(cs);
            cout << "<-- ADD_RSP\n";
            return r.getAddress();
            
        }

        void download(const Msg &m){
            cout << "\n>>>Download thread\n";
            list<PeerData> peers;
            FileID_t fileID = m.getFileID();
            string filename = m.getFilename();
            vector<Seeder> seeders = m.getSeeders();
            size_t NSdrs = m.getNumberOfSeeders();
            uint64_t size = m.getSize();

            uint64_t blk_sz = DownloadBlockSize(size);
            cout << "Block size: " << blk_sz << endl;
            uint64_t blk_num = DownloadBlockNumber(size, blk_sz);
            cout << "Number of blocks: " << blk_num << endl;

            //Instancio el file a descargar
            ClientFile downloading(fileID, filename, blk_num);
            bool yalotengo = false;
            //Me fijo si ya tengo (parcialmente) el file requerido
            for(auto& f : Files){
                if((strncmp(filename.c_str(), f.getFilename().c_str(), NAME_MAXLEN) == 0)){
                    //si ya lo tengo, mantengo esa información
                    downloading = f;
                    yalotengo = true;
                    break;
                }
            }

            if(!yalotengo){
                Files.push_back(downloading);
            }
            
            if(NSdrs > 0){
                for(auto& s : seeders){
                    ClientID_t id_ = s.getID();
                    cout << "\nSeederID: " << id_ << endl;
                    sockaddr_in address_ = getAddress(id_);
                    cout << "Dirección del seeder:\n";
                    printAddress(address_);
                    Bitmap bm_ = s.getBitmap();
                    int ds = socket (PF_INET, SOCK_STREAM, 0);
                    if(connect(ds, (struct sockaddr *) &address_, sizeof(address_)) < 0){
                        perror("Error connecting to seeder (peer)\n");
                    }
                    else{
                        cout << "Conexión exitosa con el Peer de ID = " << id_ << endl << endl;
                    }

                    for(auto& f : Files){
                        if((strncmp(filename.c_str(), f.getFilename().c_str(), NAME_MAXLEN) == 0)){
                            peers.emplace_back(id_, address_, ds, ref(bm_), ref(f));
                        }
                    }
                }
            }
            else{
                cout << "No hay Seeders para el archivo pedido\n";
                return;
            }
            




            


            for (int i = 0; i < (int)blk_num; i++){
                if(downloading.getBitmap().get(i) == 0){ //me fijo solo los bloques que todavía no tengo
                    for (auto& p : peers){
                        if(p.HasBlock((size_t)i)){  //si el peer lo tiene, se lo pido
                            p.putRequest(fileID, (size_t)i);
                            continue;
                        }
                    }
                }
            }

        }

        //Manejo la descarga de cada bloque recibido de un archivo descargado.
        void BlockTransfer(FileID_t fileID, size_t block, int PeerSocket){
            
            Msg req;
            req.setTransferRequest(fileID, block);
            req.writeTo(PeerSocket);
            
            Msg resp;
            resp.receiveFrom(PeerSocket);
            //almacenar data recibida. no sé cómo
        }


        void upload(Msg m, int socket){
            bool sharing = true;
            Msg req; //transfer request
            Msg resp;
            FileID_t id;
            uint64_t block;
            uint64_t blk_sz;
            string filename;

            //Con el primer mensaje, me fijo qué archivo me piden y qué tamaño de bloque tiene
            id = req.getFileID();

            for(auto& f : Files){
                if(f.getFileID() == id){
                    blk_sz = f.getBlockSize();
                    filename = f.getFilename();

                }
            }

            block = req.getBlock();
            uint8_t data[blk_sz];
            int fd = open (filename.c_str(), O_RDONLY);
            if(fd==-1){
                perror("Error abriendo archivo");
                return;
            }
            lseek(fd, blk_sz*block, SEEK_SET);
            readCompleteBlock(fd, data, blk_sz);
            Msg response;
            response.setTransferResponse(data, blk_sz);
            response.writeTo(socket);
            cout << "--> TRANSF_RSP";

            //Después, sigo recibiendo pedidos del mismo archivo
            while(sharing){
                req.receiveFrom(socket);
                if(req.getType()!=(uint8_t)MsgType::TRANSF_REQ || req.getFileID() != id){
                    perror("Error: solo TRANSF_REQ de un único fileID");
                    return;
                }
                block =req.getBlock();
                lseek(fd, blk_sz*block, SEEK_SET);
                readCompleteBlock(fd, data, blk_sz);
                Msg response;
                response.setTransferResponse(data, blk_sz);
                response.writeTo(socket);
                cout << "--> TRANSF_RSP";
            }
        }

        //Agarro los files (y bitmaps) del directorio y los guardo en el vector Files
        void leerFiles(const string &dir_name) {
            int ret = chdir(dir_name.c_str());
            assert(ret == 0);

            ifstream config("config.txt");
            if( ! config )
                throw runtime_error("ERROR: No puedo abrir config.txt");

            string line;
            while( getline(config, line) ) {
                cout << "\n\nEncontré: <" << line << ">";
                // File f(line)
                // Files.(line);
                // Bitmap bm;
                Files.emplace_back(line);
            }

            
            
        }

    public:
        Client(const string name_, const string &dir_name, const uint16_t AttentionPort)
            :Name(name_)
        {
            cout << "Comienzo a contruir cliente: " << Name << endl;


            leerFiles(dir_name);


            cs = socket(PF_INET, SOCK_STREAM, 0); //connected socket (al tracker)
            if(ls == -1){
                perror("Error creating connected socket on client: ");
            }         

            listenAddress.sin_family = AF_INET;
            listenAddress.sin_addr.s_addr = INADDR_ANY;
            listenAddress.sin_port = htons(AttentionPort);
            cout << "\nPeer attention address of this client:\n";
            printAddress(listenAddress);

            

            //ls = listen socket --> Abro socket para escuchar peers
            ls = socket (PF_INET, SOCK_STREAM, 0); //listen socket
            if(ls == -1){
                perror("Error creating listen socket on client: ");
            }
            int val = 1;
            setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
            
            int ret = bind(ls, (struct sockaddr *)&listenAddress, sizeof(listenAddress));
            if(ret == -1){
                perror("Error binding listen socket on client: ");
            }
            listen(ls , 5);
        }

        void run(const sockaddr_in serverAddress, uint16_t peerPort) {

            //Largo thread especialmente para escuchar a los peers que vengan a buscar files
            AcceptThread = thread(&Client::acceptPeers, this, ls);


            //cs = connected socket --> Me conecto al tracker (serverAddress)
            cs = doConnect(serverAddress, peerPort);

            // Publico todos los files inicializados
            Msg pub, resp;
            for(auto& file: Files){
                pub.setPublish(file.getFilename(), file.getBitmap(), file.getFilesize());
                pub.writeTo(cs);
                cout << "--> PUB: " << file.getFilename() << endl;
                resp.receiveFrom(cs);
                file.setFileID(resp.getFileID());
                cout << "<-- PUB_RSP. FileID: " << resp.getFileID() << endl << endl;
            }

            std::this_thread::sleep_for(std::chrono::seconds(5));

            


            // protocolo con el tracker
            while( 1 ) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            // stop peer threads
            // join peer threads
        }

        void run_request(const sockaddr_in serverAddress, string requestedfile, const uint16_t peerPort) {

            //Largo thread especialmente para escuchar a los peers que vengan a buscar files
            AcceptThread = thread(&Client::acceptPeers, this, ls);


            //cs = connected socket --> Me conecto al tracker (serverAddress)
            cs = doConnect(serverAddress, peerPort);

            // Publico todos los files inicializados
            Msg pub, resp;
            for(auto& file: Files){
                pub.setPublish(file.getFilename(), file.getBitmap(), file.getFilesize());
                pub.writeTo(cs);
                cout << "--> PUB: " << file.getFilename() << endl;
                resp.receiveFrom(cs);
                file.setFileID(resp.getFileID());
                cout << "<-- PUB_RSP. FileID: " << resp.getFileID() << endl << endl;
            }

            std::this_thread::sleep_for(std::chrono::seconds(5));         

            doRequest(requestedfile, cs);

            // protocolo con el tracker
            while( 1 ) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            // stop peer threads
            // join peer threads
        }

        ~Client(){
            for (auto& downth: DownloadTh){
                downth.join();
            }

            for (auto& upth: UploadTh){
                upth.join();
            }

            AcceptThread.join();
        }

        //Hilo que se encarga exclusivamente de recibir conexiones de Peers
        void acceptPeers(int ls){
            cout << "\n>>>acceptPeers thread\n\n"; 
            sockaddr_in PeerAddr;
            socklen_t PeerAddrLen = sizeof(PeerAddr);

            //Dejo el listen socket a la espera de nuevos Peers todo el tiempo
            while(1){
                //ps --> peer socket
                int ps = accept(ls, (struct sockaddr *)&PeerAddr, &PeerAddrLen);
                if(ps < 0){
                    perror("Error accepting peer: ");
                    return;
                }
                else{
                    cout << "Peer conectado. Address:\n";
                    printAddress(PeerAddr); 
                }
                
                //Largo un thread de atención para el nuevo Peer conectado
                UploadTh.emplace_back(&Client::peerAttention, this, ps);

            }
        }

        //Un Peer se conecta conmigo para pedir Transfer Requests --> le dedico un hilo (Upload) que lo atienda
        void peerAttention(int ps){
            cout << "\n>>>peerAttention thread\n\n\n";
            Msg req;
            req.receiveFrom(ps);
            cout << "<--TRANSF_REQ\n";

            upload(req, ps);
        }


        int doConnect(const sockaddr_in serverAddress, const uint16_t peerPort){
            
            //Me conecto al tracker y me quedo con el 'connected socket'
            if(connect(cs, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) < 0){
                perror("Error connecting the server");
            }
    	    else{
                cout << "Cliente conectado al tracker";                
                //Mando un mensaje tipo CON al tracker con mi nombre
                Msg con;
                con.setConnect(Name, peerPort);
                con.writeTo(cs);
                cout << "\n--> CON\n";
                Msg resp;
                resp.receiveFrom(cs);
                clientDispatch(resp);
            }

            return cs; //Después del dispatch, guardo el connected socket
        }


        // void doPublish(const string &name, Bitmap bm, int cs){
        //     Msg m;
        //     m.setPublish(name, bm);
        //     m.writeTo(cs);
        //     cout << "\nMandé PUB: " << string(name) << endl;

        //     Msg resp;
        //     resp.receiveFrom(cs);
        //     clientDispatch(resp, name);
        //     cout << "Recibí respuesta\n";
        // }

        void doRequest(const string &name, int cs){
            Msg m;
            m.setRequest(name);
            m.writeTo(cs);
            cout << "--> REQ: " << name << endl;

            Msg resp;
            resp.receiveFrom(cs);
            clientDispatch(resp);
        }

        void doDisconnect(int cs){
            Msg m;
            m.setDisconnect();

            Msg resp;
            resp.receiveFrom(cs);
            clientDispatch(resp);
            cout << "Sale de clientDispatch\n";
        }

        void clientDispatch(const Msg &m, const string &name = ""){ //name solo sirve para PUB_RSP
            switch(m.getType()){
                case (uint8_t)MsgType::CON_RSP:
                    recConnectResponse(m.getClientID());
                    break;
                
                case (uint8_t)MsgType::REQ_RSP:
                    if(m.getFileID() > 0){
                        cout << "<-- REQ_RSP\n";
                        cout << "FileID = " << m.getFileID() << endl;
                        cout << "Cantidad de seeders: " << m.getNumberOfSeeders() << endl;
                        cout << "Size = " << m.getSize() << endl;
                        DownloadTh.emplace_back(&Client::download, this, m);
                    }
                    else{
                        cout << "<-- REQ_RSP\n";
                        cout << "El archivo pedido no se encuentra publicado\n";
                    }
                    
                    break;

                case (uint8_t)MsgType::PUB_RSP:
                    recPublishResponse(m.getFileID(), name);
                    break;

                case (uint8_t)MsgType::DIS_RSP:
                    //ver como hacemos con el disconnect
                    //recDisconnectResponse();
                    break;

                default:
                    break;

                               
            }
        }

        void recConnectResponse(ClientID_t id_){
            cout << "<-- CON_RSP. ClientID: " << id_ << endl <<endl;
            id = id_;
        }

        void recPublishResponse(FileID_t id_, const string &name_){
            for (auto& f : Files){
                if((strncmp(name_.c_str(), f.getFilename().c_str(), NAME_MAXLEN) == 0)){
                    f.setFileID(id_);
                    cout << "FileID: " << id_ << endl;
                    break;
                }
            }
        }
};