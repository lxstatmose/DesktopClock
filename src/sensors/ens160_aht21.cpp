#include "ens160_aht21.h"

#include "pin_config.h"

#include <Arduino.h>
#include <Wire.h>

#include <ScioSense_ENS16x.h>
#include <PTSolns_AHTx.h>

/* ENS160+AHT21 на одной шине I2C (SDA=1, SCL=2), как и прежний BMP280.
 * AHT21 даёт T/RH, ENS160 — eCO2/TVOC/AQI. T/RH из AHT21 дополнительно
 * подаются в ENS160 как компенсация (иначе показания газа плывут). */

static ENS160       s_ens;
static PTSolns_AHTx s_aht(AHT21);

static bool s_ensOk  = false;
static bool s_ahtOk  = false;

/* Кэш последних значений ENS160: новые данные приходят раз в секунду,
 * а опрос — раз в 30 с, но чтобы не показывать нули при редкой гонке,
 * храним последние прочитанные значения. */
static float    s_eco2 = 0.0f;
static float    s_etvoc = 0.0f;
static uint8_t  s_aqi = 0;
static uint8_t  s_status = 0;
static uint32_t s_rs[4] = {0, 0, 0, 0};
static uint8_t  s_fw[3] = {0, 0, 0};

/* Проверка, отвечает ли устройство по адресу. endTransmission() со STOP
 * честно возвращает код ошибки и НЕ пишет в лог — в отличие от варианта
 * с sendStop=false, который шину не трогает и всегда возвращает 0. */
static bool i2c_present(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

/* Прямое чтение 16-битного PART_ID (регистр 0x00) — чтобы опознать чип
 * до вызова init(): тот делает reset() в регистр 0x10, что для чужого
 * устройства на шине было бы нежелательно. */
static int read_part_id(uint8_t addr) {
    if (!i2c_present(addr)) return -1;             /* NACK — устройства нет */
    Wire.beginTransmission(addr);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom((int)addr, 2) != 2) return -1;
    const int lo = Wire.read();
    const int hi = Wire.read();
    return (hi << 8) | lo;
}

static void scan_i2c() {
    Serial.println("[air] сканирую I2C шину...");
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        if (i2c_present(addr)) {
            Serial.printf("[air]   найден девайс на 0x%02X\n", addr);
            found++;
        }
    }
    if (found == 0)
        Serial.println("[air]   устройств не найдено — проверь питание/провода");
}

/* ENS160 адрес зависит от модуля: ScioSense даташит — 0x52 (и 0x53 при
 * ADDR→VDDIO), часть дешёвых модулей — 0x59. Опознаём по PART_ID == 0x160.
 * Перебирать всю шину нельзя: requestFrom на пустом адресе заваливает
 * консоль ошибками Wire, поэтому проверяем только известные адреса. */
static const uint8_t ENS160_ADDRS[] = {0x52, 0x53, 0x59};

static uint8_t find_ens160() {
    for (size_t i = 0; i < sizeof(ENS160_ADDRS); i++) {
        const uint8_t addr = ENS160_ADDRS[i];
        if (addr == AHT21_I2C_ADDR) continue;      /* это AHT21, не путать */
        const int id = read_part_id(addr);
        if (id == 0x160) {
            Serial.printf("[air] ENS160 найден на 0x%02X (PART_ID=0x%03X)\n",
                          addr, id);
            return addr;
        }
    }
    return 0;
}

bool sensor_begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);

    Serial.printf("[air] I2C init: SDA=%d, SCL=%d\n", PIN_I2C_SDA, PIN_I2C_SCL);
    scan_i2c();

    /* ---- AHT21: температура + влажность ---- */
    s_ahtOk = s_aht.begin(Wire, AHT21_I2C_ADDR, 100000);
    Serial.printf("[air] AHT21 0x%02X: %s\n", AHT21_I2C_ADDR,
                  s_ahtOk ? "готов" : "НЕ найден");

    /* ---- ENS160: качество воздуха ---- */
    const uint8_t ensAddr = find_ens160();
    if (ensAddr != 0) {
        s_ens.begin(&Wire, ensAddr);
        s_ensOk = s_ens.init();     /* reset + чтение PART_ID/версии прошивки */
        if (s_ensOk) {
            const uint8_t *fw = s_ens.getFirmwareVersion();
            Serial.printf("[air] ENS160 0x%02X: готов, fw %u.%u.%u\n",
                          ensAddr, fw[0], fw[1], fw[2]);
            s_ens.startStandardMeasure();
        } else {
            Serial.println("[air] ENS160: init() провалился");
        }
    } else {
        Serial.println("[air] ENS160 не найден на шине");
    }

    return s_ahtOk || s_ensOk;
}

bool sensor_read(HomeData &out) {
    out.aht21Ok  = s_ahtOk;
    out.ens160Ok = s_ensOk;
    out.valid    = false;

    bool got = false;

    /* ---- AHT21: T/RH (блокирующее чтение ~100 мс) ----
     * Значения кэшируются: если разовое чтение сорвалось, на экране
     * останутся прошлые цифры, а не нули. */
    static float s_tempC = NAN, s_hum = NAN;
    float tC = NAN, rh = NAN;
    if (s_ahtOk) {
        const AHTxStatus st = s_aht.readTemperatureHumidity(tC, rh, 150);
        if (st == AHTX_OK && !isnan(tC) && !isnan(rh)) {
            s_tempC = tC;
            s_hum   = rh;
        } else {
            Serial.printf("[air] AHT21 read: err=%d\n", (int)st);
        }
        tC = s_tempC;
        rh = s_hum;
    }

    if (!isnan(s_tempC)) {
        out.tempC    = s_tempC;
        out.humidity = s_hum;
        got = true;
    }

    /* ---- ENS160: компенсация от AHT21, затем чтение ---- */
    if (s_ensOk) {
        /* Без компенсации газовый канал считает по внутреннему термощупу,
         * что даёт заметную ошибку eCO2/TVOC. */
        if (!isnan(tC) && !isnan(rh))
            s_ens.writeCompensation(Ens16x_CalcTempInFromCelsius(tC),
                                    Ens16x_CalcRhIn(rh));

        /* Неблокирующее: wait() ждал бы ~1 с, а опрос и так раз в 30 с —
         * новые данные к этому моменту уже готовы. */
        if (s_ens.update() == RESULT_OK) {
            if (s_ens.hasNewData()) {
                s_eco2  = (float)s_ens.getEco2();
                s_etvoc = (float)s_ens.getTvoc();
                s_aqi   = s_ens.getAirQualityIndex_UBA();
            }
            if (s_ens.hasNewGeneralPurposeData()) {
                s_rs[0] = s_ens.getRs0();
                s_rs[1] = s_ens.getRs1();
                s_rs[2] = s_ens.getRs2();
                s_rs[3] = s_ens.getRs3();
            }
            /* Биты 3:2 статуса: 0 норма, 1 прогрев, 2 первый запуск,
             * 3 невалидный вывод. Показываем на экране, чтобы не пугаться. */
            s_status = (uint8_t)((s_ens.getDeviceStatus() >> 2) & 0x03);

            const uint8_t *fw = s_ens.getFirmwareVersion();
            s_fw[0] = fw[0];
            s_fw[1] = fw[1];
            s_fw[2] = fw[2];

            /* Считаем успешным, если были новые данные или статус уже
             * читался раньше (значит датчик отвечает). */
            if (s_ens.hasNewData() || s_status != 0 || s_eco2 > 0.0f)
                got = true;
        }

        out.eco2     = s_eco2;
        out.etvoc    = s_etvoc;
        out.aqi      = s_aqi;
        out.ensStatus = s_status;
        out.rs[0] = s_rs[0];
        out.rs[1] = s_rs[1];
        out.rs[2] = s_rs[2];
        out.rs[3] = s_rs[3];
        out.fwVer[0] = s_fw[0];
        out.fwVer[1] = s_fw[1];
        out.fwVer[2] = s_fw[2];
    }

    out.valid = got;

    if (got) {
        Serial.printf("[air] T=%.1f H=%.0f eCO2=%.0f TVOC=%.0f AQI=%u st=%u"
                      " Rs=%lu/%lu/%lu/%lu fw=%u.%u.%u\n",
                      (double)out.tempC, (double)out.humidity,
                      (double)out.eco2, (double)out.etvoc,
                      out.aqi, out.ensStatus,
                      (unsigned long)out.rs[0], (unsigned long)out.rs[1],
                      (unsigned long)out.rs[2], (unsigned long)out.rs[3],
                      out.fwVer[0], out.fwVer[1], out.fwVer[2]);
    }
    return got;
}