#pragma once

#include <stdint.h>
#include <string.h>
#include <cassert>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

const uint16_t MAX_BLK_SZ8 = 8192; //máxima cantidad de bloques que pueden componer al archivo

class Bitmap
{
    private:
        uint8_t blocks[MAX_BLK_SZ8]; // cada bit representa un bloque
        uint64_t num_blks;

    public:

        Bitmap(uint64_t num_blks_)
        :num_blks(num_blks_)
        {
            memset(blocks, 0, MAX_BLK_SZ8);
        }

        Bitmap(const Bitmap &) = default;

        const uint64_t getNumBlocks() const{
            return num_blks;
        }

        void set(size_t i, bool value)
        {
            assert(i < num_blks);
            size_t blk = i / 8; // bloque donde se encuentra el índice
            size_t bit = i % 8; // bit dentro del bloque
            if (value)
            {
                blocks[blk] |= (1 << bit); // 1<<bit es un entero que solo tiene 1 en la posicion bit
            }                              //|= hace operación OR entre el valor en blocks[blk] y 1<<bit y lo almacena en blocks[idx]
                                        // es decir, si estaba bajo, levanta el bit que quería levantar
            else
            {
                blocks[blk] &= ~(1 << bit); // complementario de lo que está arriba (niega 1<<bit y hace AND)
            }
        }

        const bool get(size_t i) const{
            //assert(i < num_blks);
            size_t blk = i / 8; // bloque donde se encuentra el índice
            size_t bit = i % 8; // bit dentro del bloque

            return (blocks[blk] >> bit) & 1; // pongo el bit que me interesa como el menos significativo y hago un AND con 00000001
        }

        static std::string fname_from_file(const char *fname) {
            return std::string(fname) + ".bitmap";
        }

        void serializar(const char *filename) {
            int fd = open(filename, O_WRONLY | O_CREAT, 0644);
            assert(fd>0);

            ssize_t ret = write(fd, &num_blks, sizeof(num_blks));
            assert((size_t) ret == sizeof(num_blks));

            size_t nbytes = (num_blks + 7) >> 3;
            ret = write(fd, blocks, nbytes);
            assert((size_t) ret == nbytes);

            close(fd);
        }

        void deserializar(const char *filename) {
            int fd = open(filename, O_RDONLY);
            assert(fd>0);

            uint64_t nb;
            ssize_t ret = read(fd, &nb, sizeof(nb));
            assert((size_t) ret == sizeof(num_blks));

            size_t nbytes = (nb + 7) >> 3;
            ret = read(fd, blocks, nbytes);
            assert((size_t) ret == nbytes);
            close(fd);

            num_blks = nb;
        }
}__attribute__((__packed__));
