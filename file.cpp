#include "file.h"

//{FileSize, BlockSize}
//Recorro la tabla buscando la primer cota superior para el tamaño
//de mi archivo y, con eso, defino el BlockSize a utilizar
const size_t BlockLUT[LUT_SIZE][2] = {
    {4096, 256},
    {8192, 512},
    {16384, 1024},
    {32768, 2048},
    {4194304, 4096},
    {16777216, 8192},
    {67108864, 16384},
    {268435456, 32768},
    {0, 65536}
};