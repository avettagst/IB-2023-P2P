#pragma once
#include <iostream>
#include <arpa/inet.h>
#include <cstdint>
#include <thread>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <vector>
#include <string>
#include <cstring>
#include <assert.h>
#include <unistd.h>
#include <functional>
#include <mutex>
#include <deque>
#include <condition_variable>
//#include "message.h"
#include "file.h"
#include "bitmap.h"

using namespace std;
using ClientID_t = uint64_t;


const uint16_t NAME_MAXLEN = 200; //cantidad máxima de caracteres para nombres de clientes/files
const uint16_t MAX_SEEDERS = 32; //cantidad máxima de Seeders para un mismo file
const uint16_t MAX_FILES = 100; //cantidad máxima de files en un cliente

extern const size_t BlockLUT[LUT_SIZE][2];

static const size_t MAX_PAYLOAD_SZ8 = 64 << 10; //64 KBytes

enum class MsgType
{
    //Para inicializar Msg
    INV, // Invalid

    //Del cliente al tracker
    REQ, //Request
    PUB, //Publish
    CON, //Connect
    DIS, //Disconnect
    ADDRESS, //Address

    //Del tracker al cliente
    REQ_RSP, //Request response
    PUB_RSP, //Publish response
    CON_RSP, //Connect response
    DIS_RSP,  //Disconnect response
    ADDRESS_RSP, //Address response

    //Entre clientes
    TRANSF_REQ, //Transfer request
    TRANSF_RSP  //Transfer response
};






// Uso Bitmap
struct Seeder
{
    private:
        ClientID_t id;
        Bitmap bm;

    public:
        Seeder(ClientID_t id_, const Bitmap& bm_)
            : id(id_), bm(bm_) 
        {
        }

        const ClientID_t getID() const{
            return id;
        }

        const Bitmap getBitmap() const{
            return bm;
        }

        const uint64_t getBitmapSize() const {
            uint64_t sz = sizeof(uint64_t) + sizeof(uint8_t)*bm.getNumBlocks();
            return sz;
        }

        void refreshBitmap(Bitmap new_bm){
            bm = new_bm;
        }

}__attribute__((__packed__));

struct MM_elem
{ // Multimap element

        FileID_t id;
        vector<Seeder> seeders;

        MM_elem(const FileID_t id_, const Seeder &sdr)
            :id(id_)
        {
            seeders.push_back(sdr);
        }

};

class Multimap
{   
    public:
        vector<MM_elem> elements;

    
        Multimap()
        {            
        }

        //Para responder a un Request
        const vector<Seeder> requestedSeeders(FileID_t fileID){
            for (const auto& elem : elements){
                if(fileID == elem.id){
                    return elem.seeders;
                    break;
                }
            }

            vector<Seeder> nullVector;

            return nullVector; //No tengo el file en multimap
        }


        //Para un file ya publicado, agrego un seeder nuevo o actualizo el bitmap de un seeder ya registrado
        void refreshSeeders(FileID_t fileID, Seeder refSdr){
            for (auto& elem : elements){
                if(fileID == elem.id){
                    //ya encontré el file, recorro sus seeders
                    for(auto& sdr : elem.seeders){
                        if(sdr.getID() == refSdr.getID()){
                            //ya estaba registrado el Seeder, actualizo el bitmap
                            sdr.refreshBitmap(refSdr.getBitmap());
                            break;
                        }
                    }
                    //si no está registrado como Seeder, lo agrego
                    elem.seeders.emplace_back(refSdr); 
                    break;
                }
            }
        }

        //Agrego un file y su primer seeder al multimap
        void addFile(FileID_t fileID, Seeder sdr){
            MM_elem new_elem(fileID, sdr);
            elements.emplace_back(new_elem);
        }
};

void printAddress(sockaddr_in address){
    string ipAddress = inet_ntoa(address.sin_addr);
    uint16_t port = ntohs(address.sin_port);

    cout << "IP Address: " << ipAddress << std::endl;
    cout << "Port: " << port << std::endl << endl;
}




class Msg 
{
    private:
        struct{
            uint8_t type;  //"nunca usar un enum en una estructura de un protocolo"
            size_t size;   //tamaño en bytes del mensaje (incluyendo al header)
        }header __attribute__((__packed__));

        union {
            struct{
                uint16_t peersPort;
                size_t len;
                char clientname[NAME_MAXLEN];    
            } con;

            struct{
                ClientID_t clientID;
            } con_rsp __attribute__((__packed__));

            struct {
                char fname[NAME_MAXLEN];
            } req;

            struct{
                size_t NSdrs;  //Número de seeders con el file
                uint64_t size;
                FileID_t fileID;
                char fname[NAME_MAXLEN];
                Seeder seeders[MAX_SEEDERS];
                
            } req_rsp __attribute__((__packed__));

            struct {
                char fname[NAME_MAXLEN];
                uint64_t size;
                Bitmap bm;
            } pub __attribute__((__packed__));

            struct {
                FileID_t fileID;
            } pub_rsp __attribute__((__packed__));

            struct {
                ClientID_t PeerID;
            } address __attribute__((__packed__));

            struct {
                sockaddr_in PeerAddress;
            } address_rsp __attribute__((__packed__));

            //disconnect: va sin payload, con el MsgType ya es suficiente
            //¿disconnect response debería llevar payload?

            struct{
                FileID_t fileID;
                uint64_t block;
            } transf_req __attribute__((__packed__));
            
            struct{
                uint8_t data[0];
                //lo que sea un bloque del file, no sé cómo diseñarlo aún
            } transf_rsp /*__attribute__((__packed__))*/;

            uint8_t buff[MAX_PAYLOAD_SZ8];
        } payload;

        //Calculo el tamaño del mensaje (según el tipo) y lo guardo en el header
        void commitSize(size_t payloadSz8 = 0){
            size_t sz = sizeof(header);

            switch(header.type){
                case (uint8_t)MsgType::INV:
                    break;

                case (uint8_t)MsgType::CON:
                    sz += sizeof(payload.pub);
                    break;

                case (uint8_t)MsgType::CON_RSP:
                    sz += sizeof(payload.con_rsp);
                    break;

                case (uint8_t)MsgType::REQ:
                    sz += sizeof(payload.req);
                    break;

                case (uint8_t)MsgType::REQ_RSP:
                    sz += sizeof(payload.req_rsp.fileID) + sizeof(payload.req_rsp.size) + sizeof(payload.req_rsp.NSdrs) + sizeof(payload.req.fname);
                    sz += payload.req_rsp.NSdrs*(sizeof(uint64_t)+sizeof(Bitmap));
                    break;

                case (uint8_t)MsgType::PUB:
                    sz += sizeof(payload.pub);
                    break;
                
                case (uint8_t)MsgType::PUB_RSP:
                    sz += sizeof(payload.pub_rsp);
                    break;

                case (uint8_t)MsgType::ADDRESS:
                    sz += sizeof(payload.address);
                    break;

                case (uint8_t)MsgType::ADDRESS_RSP:
                    sz += sizeof(payload.address_rsp);
                    break;

                case (uint8_t)MsgType::DIS:
                    break;

                case (uint8_t)MsgType::DIS_RSP:
                    break;

                case (uint8_t)MsgType::TRANSF_REQ:
                    sz += sizeof(payload.transf_req);
                    break;

                case (uint8_t)MsgType::TRANSF_RSP:
                    sz += payloadSz8;
                    //falta diseñar cómo serán los bloques de los files
                    break;                
            }

            header.size = sz;
        }


        //Me aseguro de escribir correctamente todo en el socket
        void writeComplete(int socket, const char* buf, size_t len){
            size_t pending = len;
            while(pending){
                int n = write(socket, buf, pending);
                if(n == -1 && errno == EINTR) //seguí participando
                    continue;
                if(n == 0)  //ya no leo nada
                    break;

                pending -= n;
                buf += n;
            }
        }

        //Me aseguro de leer correctamente lo que llega desde el socket
        void readComplete(int socket, char* buf, size_t len){
            size_t pending = len;
            while(pending){
                int n = read(socket, buf, pending);
                if(n == -1 && errno == EINTR) //seguí participando
                    continue;
                if(n == 0)  //ya no leo nada
                    break;

                pending -= n;
                buf += n;
            }
        }

    public:
        Msg()
        : header{(uint8_t)MsgType::INV, 0}, payload{}
        {
        }

        void setConnect(const string &name, const uint16_t peerPort){
            header.type = (uint8_t)MsgType::CON;
            payload.con.peersPort = peerPort;
            strncpy(payload.con.clientname, name.c_str(), NAME_MAXLEN); //copio name (string) como char[], copio hasta NAME_MAXLEN bytes
            commitSize();
        }

        void setConnectResponse(ClientID_t id){
            header.type = (uint8_t)MsgType::CON_RSP;
            payload.con_rsp.clientID = id;
            commitSize();
        }

        void setRequest(const string &name){
            header.type = (uint8_t)MsgType::REQ;
            strncpy(payload.req.fname, name.c_str(), NAME_MAXLEN);
            commitSize();
        }

        void setRequestResponse(const vector<Seeder> seeders_, FileID_t id_, size_t N, uint64_t filesize){
            header.type = (uint8_t)MsgType::REQ_RSP;
            payload.req_rsp.fileID = id_;
            payload.req_rsp.NSdrs = N;
            payload.req_rsp.size = filesize;
            for (size_t i = 0; i < N; ++i) {
                payload.req_rsp.seeders[i] = seeders_[i];
            }
            commitSize();
        }

        void setPublish(const string &name, Bitmap bm, uint64_t size_){
            header.type = (uint8_t)MsgType::PUB;
            strncpy(payload.pub.fname, name.c_str(), NAME_MAXLEN);
            payload.pub.bm = bm;
            payload.pub.size = size_;
            commitSize();
        }

        void setPublishResponse(FileID_t id){
            header.type = (uint8_t)MsgType::PUB_RSP;
            payload.pub_rsp.fileID = id;
            commitSize();
        }

        void setAddressRequest(ClientID_t id){
            header.type = (uint8_t)MsgType::ADDRESS;
            payload.address.PeerID = id;
            commitSize();
        }

        void setAddressResponse(sockaddr_in address, const uint16_t port){
            header.type = (uint8_t)MsgType::ADDRESS_RSP;
            sockaddr_in listenAddress = address;
            listenAddress.sin_port = htons(port);
            payload.address_rsp.PeerAddress = listenAddress;
            commitSize();
        }

        void setDisconnect(){
            header.type = (uint8_t)MsgType::DIS;
            //chequear que onda el payload
            commitSize();
        }

        void setDisconnectResponse(){
            header.type = (uint8_t)MsgType::DIS_RSP;
            //chequear que onda el payload
            commitSize();
        }

        void setTransferRequest(FileID_t id, uint64_t block){
            header.type = (uint8_t)MsgType::TRANSF_REQ;
            payload.transf_req.fileID = id;
            payload.transf_req.block = block;
            commitSize();
        }

        void setTransferResponse(uint8_t *data, uint64_t dataSz8){
            assert(dataSz8 <= MAX_PAYLOAD_SZ8);
            header.type = (uint8_t)MsgType::TRANSF_RSP;
            memcpy(payload.transf_rsp.data, data, dataSz8);
            commitSize(dataSz8);
        }


        //Recibiendo como parámetro el socket de comunicación, leo mensajes y los almaceno en la instancia Msg que ejecutó el método
        void receiveFrom(int socket){
            readComplete(socket, (char*)&header, sizeof(header));   //leo el header (cuyo tamaño ya conozco)
            // cout << "leí header\n";
            //una vez leído el header, puedo saber el tamaño del mensaje y leer el payload
            readComplete(socket, (char*)&payload, header.size-sizeof(header)); //leo el payload
            // cout << "leí payload\n";
        }

        //Recibiendo como parámetro el socket de comunicación, escribo la instancia Msg desde la cual se ejecutó el método
        void writeTo(int socket){

            writeComplete(socket, (char*)&header, header.size); //con header le marco la posición del buffer pero escribo todo el Msg (size)
        }

        uint8_t getType() const{
            return header.type;
        }

        //para Type = CON
        const char* getClientname() const{
            if(header.type == (uint8_t)MsgType::CON)
                return payload.con.clientname;
            else{
                perror("getClientname: para mensajes tipo CON");
                return nullptr;
            }


            
        }

        //para Type = CON
        const uint16_t getPeerPort() const{
            if(header.type == (uint8_t)MsgType::CON)
                return payload.con.peersPort;
            else{
                perror("getPeerPort: para mensajes tipo CON");
                return 0;
            }


        }

        //para Type = TRANSF_RSP
        const uint8_t* getData() const{
            return payload.transf_rsp.data;
        }
 
        //para Type = CON_RSP/ADDRESS
        const ClientID_t getClientID() const{
            switch(header.type){
                case (uint8_t)MsgType::CON_RSP:
                    return payload.con_rsp.clientID;
                case (uint8_t)MsgType::ADDRESS:
                    return payload.con_rsp.clientID;
                default:
                    perror("getClientID: para mensajes tipo CON_RESP / ADDRESS");
                    return 0;
            } 
        }

        //para Type = PUB/REQ/REQ_RSP
        const char* getFilename() const{
            switch(header.type){
                case (uint8_t)MsgType::PUB:
                    return payload.pub.fname;
                case (uint8_t)MsgType::REQ:
                    return payload.req.fname;
                case (uint8_t)MsgType::REQ_RSP:
                    return payload.req_rsp.fname;
                default:
                    perror("getFilename: par mensajes tipo PUB/REQ/REQ_RSP");
                    return nullptr;
            }
        }

        //para Type = PUB_RSP/REQ_RSP
        const FileID_t getFileID() const{                
            switch(header.type){
                case (uint8_t)MsgType::PUB_RSP:
                    return payload.pub_rsp.fileID;
                case (uint8_t)MsgType::REQ_RSP:
                    return payload.req_rsp.fileID;
                case (uint8_t)MsgType::TRANSF_REQ:
                    return payload.transf_req.fileID;
                default:
                    return 0; //tipo de mensaje equivocado
            }
        }

        const size_t getMsgSize() const{
            return header.size;
        }

        const uint64_t getBlock() const{
            return payload.transf_req.block;
        }

        const Bitmap getBitmap() const{
            return payload.pub.bm;
            //chequear tipo de mensaje y ver qué retorno si le pifian
        }

        //para Type = REQ_RSP
        const size_t getNumberOfSeeders() const{
            if(header.type == (uint8_t)MsgType::REQ_RSP)
                return payload.req_rsp.NSdrs;

            return -1; //tipo de mensaje equivocado
        }

        const vector<Seeder> getSeeders() const{
            if(header.type != (uint8_t)MsgType::REQ_RSP){
                perror("getSeeders: para mensajes tipo REQ_RSP");
                vector<Seeder> nullv;
                return  nullv;
            }

            vector<Seeder> sdr_vec(&payload.req_rsp.seeders[0], &payload.req_rsp.seeders[payload.req_rsp.NSdrs]);
            return sdr_vec;
            //chequear tipo de mensaje y ver qué retorno si le pifian
        }

        const sockaddr_in getAddress() const{
            return payload.address_rsp.PeerAddress;
        }

        const uint64_t getSize() const{
            switch(header.type){
                case (uint8_t)MsgType::PUB:
                    return payload.pub.size;
                case (uint8_t)MsgType::REQ_RSP:
                    // cout << "En la respuesta: " << payload.req_rsp.size; 
                    return payload.req_rsp.size;
                default:
                    perror("getSize: para mensajes tipo PUB / REQ_RSP");
                    return 0;
            }

            return payload.req_rsp.size;
        }
};