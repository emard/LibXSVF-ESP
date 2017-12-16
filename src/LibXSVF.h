#include <FS.h>
#include <Arduino.h>

class LibXSVF
{
  public:
    uint8_t _tdo, _tdi, _tck, _tms; // pin numbers (not yet used)
    
    // constructor
    LibXSVF()
    {
    }

    // constructor that sets pinout (not yet used)
    LibXSVF(uint8_t tdo, uint8_t tdi, uint8_t tck, uint8_t tms)
#if 0
    : _tdo(tdo), _tdi(tdi), _tck(tck), _tms(tms)
#endif
    {
#if 1
      _tdo = tdo;
      _tdi = tdi;
      _tck = tck;
      _tms = tms;
#endif
    }

    // destructor
    ~LibXSVF()
    {
    }

    void begin(FS* fs); // provide a filesystem to work with
    int scan(); // function body is in LibXSVF.cpp
    uint32_t id(); // function body is in LibXSVF.cpp
    int program(String filename, int x); // program the device with some file
    int play_svf_packet(int index, uint8_t *buffer, int len, bool final); // program the device with some file
};

