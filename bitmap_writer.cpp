#include <iostream>

#include "file.h"
#include "bitmap.h"

using namespace std;

static void proc(const char *fname);

int main(int argc, char *argv[])
{
    if( argc < 2 ) {
        cerr << "ERROR: usage " << argv[0] << " file\n";
        return 1;
    }

    for( int i = 1; i < argc; ++i )
        proc(argv[i]);

    return 0;
}

void proc(const char *fname)
{
    File f(fname);

    auto num_blocks = f.getNumBlocks();
    Bitmap bm(num_blocks);

    for( size_t i = 0; i < num_blocks; ++i )
        bm.set(i, true);
    
    string bm_fname = Bitmap::fname_from_file(fname);
    bm.serializar(bm_fname.c_str());

    cout << "\nProc file " << fname << endl;
    cout << "Bitmap fname: " << bm_fname << endl;
    cout << "FileSize: " << f.getSize() << endl;
    cout << "BlockSize: " << f.getBlockSize() << endl;
    cout << "NumBlocks: " << num_blocks << endl;
}



