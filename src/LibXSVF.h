#include <Arduino.h>

class LibXSVF
{
  public:
    uint8_t _tdo, _tdi, _tck, _tms; // pin numbers
    
    // constructor
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
    
    ~LibXSVF()
    {
    }
    
    void test(); // function body is in LibXSVF.cpp
};

