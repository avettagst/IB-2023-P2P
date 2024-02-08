#pragma once

#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <ostream>

#include "bitmap.h"

using FileID_t = uint64_t;
const uint8_t LUT_SIZE = 9;
extern const size_t BlockLUT[LUT_SIZE][2];


class File
{
    private:
        FileID_t id;
        std::string name;
        uint64_t size;
        uint64_t blk_num;
        uint64_t blk_size;
        
        void setBlockSize(){
            if(size <= BlockLUT[0][0]){ //primer entrada
                blk_size = BlockLUT[0][1];
                return;
            }

            for (int i = 1; i < LUT_SIZE-1; i++){
                if(size > BlockLUT[i][0] && size <= BlockLUT[i+1][0]){
                    blk_size = BlockLUT[i+1][1];
                    return;
                }
            }
            
            //última entrada
            blk_size = BlockLUT[LUT_SIZE-1][1];
        }

        void setBlockNumber(){
            blk_num = (size + blk_size - 1)/blk_size; 
        }


    public:
        // Constructor (para archivos ya en el cliente)
        File(const std::string &nm)
            :id(0), name(nm)
        {
            struct stat st;
            int ret = stat(name.c_str(), &st);
            assert(ret == 0);
            size = st.st_size;
            std::cout << "\n\nSize del archivo construido: " << size << std::endl;
            setBlockSize();
            std::cout << "Blocksize del archivo construido: " << blk_size << std::endl;
            setBlockNumber();
            std::cout << "Number of blocks del archivo construido: " << blk_num << std::endl;
        }

        // Constructor (para archivos a descargar de la red)
        File(const FileID_t id_, const std::string &nm)
        :id(id_), name(nm)
        {
        }


        const std::string &getName() const{
            return name;
        }

        const uint64_t getSize() const{
            return size;
        }

        const FileID_t getID() const{
            return id;
        }

        const uint64_t getBlockSize() const{
            return blk_size;
        }

        void setID(FileID_t id_){
            id = id_;
        }

        uint64_t getNumBlocks() const {
            return blk_num;
        }
};


