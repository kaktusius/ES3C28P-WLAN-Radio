#include "es8311.h"

static const struct _coeff_div coeff_div[] = {
    /* 44.1k (Wichtig für deinen Radio-Webstream) */
    {11289600, 44100, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800, 44100, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400, 44100, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200, 44100, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    /* 48k */
    {12288000, 48000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 48000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 48000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 48000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 48000, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10}
};

ES8311::ES8311() {}
ES8311::~ES8311() {}

int ES8311::get_coeff(uint32_t mclk, uint32_t rate) {
    for (int i = 0; i < (sizeof(coeff_div) / sizeof(coeff_div[0])); i++) {
        if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk) { return i; }
    }
    return -1;
}

bool ES8311::begin(TwoWire* twi, int8_t address) {
    m_addr = address;
    m_TwoWireInstance = twi;
    bool ok = true;
    uint8_t reg = 0;

    m_TwoWireInstance->beginTransmission(m_addr);
    if (m_TwoWireInstance->endTransmission() != 0) {
        Serial.println("[ERR] ES8311 per I2C nicht gefunden!");
        return false;
    }

    // Logik-Korrektur: Jedes Register muss wahr (true) zurückgeben
    ok &= WriteReg(0x00, 0x1F); // Reset
    vTaskDelay(pdMS_TO_TICKS(20));
    ok &= WriteReg(0x00, 0x00); // Release reset
    ok &= WriteReg(0x00, 0x80); // Power on
    ok &= WriteReg(0x01, 0x3F); // Enable all clocks

    reg = ReadReg(0x06);
    reg &= ~0x20;               // SCLK (BCLK) pin nicht invertieren
    ok &= WriteReg(0x06, reg); 

    ok &= setSampleRate(44100); 
    ok &= setBitsPerSample(16); 

    ok &= WriteReg(0x0D, 0x01); // Analog-Sektion starten
    ok &= WriteReg(0x0E, 0x02); // PGA & ADC aktivieren
    ok &= WriteReg(0x12, 0x00); // DAC einschalten
    ok &= WriteReg(0x13, 0x10); // HP-Drive aktivieren
    ok &= WriteReg(0x1C, 0x6A); // ADC EQ Bypass
    ok &= WriteReg(0x37, 0x08); // DAC EQ Bypass

    return ok;
}

bool ES8311::setVolume(uint8_t volume) {
    if (volume > 100) volume = 100;
    int reg32 = (volume == 0) ? 0 : ((volume * 256 / 100) - 1);
    return WriteReg(0x32, reg32);
}

uint8_t ES8311::getVolume() {
    uint8_t reg32 = ReadReg(0x32);
    return (reg32 == 0) ? 0 : (((reg32 * 100) / 256) + 1);
}

bool ES8311::setSampleRate(uint32_t sample_rate) {
    uint8_t reg = 0;
    bool ok = true;
    _mclk_hz = sample_rate * 256; 
    if (sample_rate > 64000) _mclk_hz /= 2;
    int coeff = get_coeff(_mclk_hz, sample_rate);
    if (coeff < 0) return false;
    
    const struct _coeff_div* const selected_coeff = &coeff_div[coeff];
    reg = ReadReg(0x02);
    reg |= (selected_coeff->pre_div - 1) << 5;
    reg |= selected_coeff->pre_multi << 3;
    ok &= WriteReg(0x02, reg); 
    
    const uint8_t reg03 = (selected_coeff->fs_mode << 6) | selected_coeff->adc_osr;
    ok &= WriteReg(0x03, reg03);                   
    ok &= WriteReg(0x04, selected_coeff->dac_osr); 
    
    const uint8_t reg05 = ((selected_coeff->adc_div - 1) << 4) | (selected_coeff->dac_div - 1);
    ok &= WriteReg(0x05, reg05); 
    
    reg = ReadReg(0x06);
    reg &= 0xE0;
    if (selected_coeff->bclk_div < 19) {
        reg |= (selected_coeff->bclk_div - 1) << 0;
    } else {
        reg |= (selected_coeff->bclk_div) << 0;
    }
    ok &= WriteReg(0x06, reg); 
    
    reg = ReadReg(0x07);
    reg &= 0xC0;
    reg |= selected_coeff->lrck_h << 0;
    ok &= WriteReg(0x07, reg);                    
    ok &= WriteReg(0x08, selected_coeff->lrck_l); 
    return ok;
}

bool ES8311::setBitsPerSample(uint8_t bps) {
    uint8_t reg09 = ReadReg(0x09) & ~0x1C;
    uint8_t reg0A = ReadReg(0x0A) & ~0x1C;
    switch (bps) {
        case 16: reg09 |= (3 << 2); reg0A |= (3 << 2); break;
        case 24: reg09 |= (0 << 2); reg0A |= (0 << 2); break;
        case 32: reg09 |= (4 << 2); reg0A |= (4 << 2); break;
        default: return false;
    }
    return WriteReg(0x09, reg09) && WriteReg(0x0A, reg0A);
}

bool ES8311::enableMicrophone(bool enable) {
    uint8_t reg = 0x1A; if (enable) reg |= (1 << 6);
    return WriteReg(0x17, 0xC8) && WriteReg(0x14, reg);
}

bool ES8311::setMicrophoneGain(uint8_t gain) {
    if (gain > 7) gain = 7;
    return WriteReg(0x16, gain);
}

uint8_t ES8311::getMicrophoneGain() { return ReadReg(0x16) & 0x07; }

bool ES8311::WriteReg(uint8_t reg, uint8_t val) {
    m_TwoWireInstance->beginTransmission(m_addr);
    m_TwoWireInstance->write(reg);
    m_TwoWireInstance->write(val);
    return m_TwoWireInstance->endTransmission() == 0;
}

uint8_t ES8311::ReadReg(uint8_t reg) {
    m_TwoWireInstance->beginTransmission(m_addr);
    m_TwoWireInstance->write(reg);
    m_TwoWireInstance->endTransmission(false);
    m_TwoWireInstance->requestFrom((uint16_t)m_addr, (uint8_t)1, (static_cast<bool>(true)));
    return m_TwoWireInstance->available() ? m_TwoWireInstance->read() : 0;
}

void ES8311::read_all() {
    for (uint8_t i = 0; i < 0x4A; i++) { Serial.printf("0x%02X: 0x%02X\n", i, ReadReg(i)); }
}