#ifndef JTAG_H
#define JTAG_H
#include <LibXSVF.h> // for the jtag
#include "storage.h"

extern LibXSVF jtag;

void program_file(fs::FS &storage, String filename, int detach);

#endif
