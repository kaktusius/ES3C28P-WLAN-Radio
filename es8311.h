#pragma once
#include <Arduino.h>
#include <Wire.h>

#define ES8311_SAMPLE_RATE48     48000
#define ES8311_BITS_PER_SAMPLE16 16

struct _coeff_div {
    uint32_t mclk;        
    uint32_t rate;        
    uint8_t pre_div;      
    uint8_t pre_multi;    
    uint8_t adc_div;      
    uint8_t dac_div;      
    uint8_t fs_mode;      
    uint8_t lrck_h;       
    uint8_t lrck_l;
    uint8_t bclk_div;     
    uint8_t adc_osr;      
    uint8_t dac_osr;
};

class ES8311 {
private:
    TwoWire *m_TwoWireInstance = NULL;
    uint32_t _mclk_hz = 44100 * 256; 
    int8_t m_addr = -1;
public:
    ES8311();
    ~ES8311();
    bool begin(TwoWire* twi, int8_t address = 0x18);
    bool setVolume(uint8_t volume);
    uint8_t getVolume();
    bool setSampleRate(uint32_t sample_rate);
    bool setBitsPerSample(uint8_t bps);
    bool enableMicrophone(bool enable);
    bool setMicrophoneGain(uint8_t gain);
    uint8_t getMicrophoneGain();
    void read_all();
protected:
    int get_coeff(uint32_t mclk, uint32_t rate);
    bool WriteReg(uint8_t reg, uint8_t val);
    uint8_t ReadReg(uint8_t reg);
};