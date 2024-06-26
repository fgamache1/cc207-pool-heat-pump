#include "hpci.h"

namespace esphome
{
    namespace hpci
    {
        // uint8_t HeatPumpController::frame[HP_FRAME_LEN];
        // hpInfo HeatPumpController::hpData;

        void HeatPumpController::setup()
        {
            this->high_freq_.start();
            settings::ctrlSettings defaultSettings = {
                29,                  // uint8_t targetTemp;
                40,                  // uint8_t defrostAutoEnableTime;
                7,                   // uint8_t defrostEnableTemp;
                13,                  // uint8_t defrostDisableTemp;
                8,                   // uint8_t defrostMaxDuration;
                2,                   // uint8_t restartOffsetTemp;
                0,                   // uint8_t compressorStopMarginTemp;
                118,                 // uint8_t thermalProtection;
                40,                  // uint8_t maximumTemp;
                15,                  // uint8_t stopWhenReachedDelay;
                true,                // bool specialCtrlMode;
                false,               // bool on;
                settings::HEAT,      // actionEnum action;
                true,                // bool autoRestart;
                settings::HEAT_ONLY, // modeEnum opMode;
            };
            this->hpSettings = defaultSettings;
            this->sendControl(this->hpSettings);
            swi::setWireDirection(swi::RECEIVING);
            esphome::ESP_LOGD(__FILE__, "Succesful setup !");
        }

        void HeatPumpController::sendControl(settings::ctrlSettings settings)
        {
            uint8_t frame[HP_FRAME_LEN];
            frame[0] = 0x33; // header
            frame[1] = 0x18; // header
            frame[2] = settings.targetTemp;
            frame[3] = settings.defrostAutoEnableTime;
            frame[4] = settings.defrostEnableTemp;
            frame[5] = settings.defrostDisableTemp;
            frame[6] = settings.defrostMaxDuration * 20;
            // MODE 1
            frame[7] = 0x00; // Clear any current flag
            frame[7] |= (settings.specialCtrlMode ? 0x80 : 0x00);
            frame[7] |= (settings.on ? 0x40 : 0x00);
            frame[7] |= ((settings.action == settings::HEAT) ? 0x20 : 0x00);
            frame[7] |= (settings.autoRestart ? 0x08 : 0x00);
            frame[7] |= ((int)settings.opMode & 0x03) << 1;
            frame[8] = 0x1D; // MODE 2 (NOT A HYBRID PUMP)
            frame[9] = settings.restartOffsetTemp;
            frame[10] = settings.compressorStopMarginTemp;
            frame[11] = settings.thermalProtection;
            frame[12] = settings.maximumTemp;
            frame[13] = settings.stopWhenReachedDelay;
            frame[14] = 0x00; // SCHEDULE SETTING OFF
            frame[15] = this->computeChecksum(frame, HP_FRAME_LEN);
            swi::sendFrame(frame, HP_FRAME_LEN);
        }

        bool HeatPumpController::decode(uint8_t frame[])
        {
            if (frame[0] == 0x4B)
            {
                this->hpData.targetTemp = frame[2];
                this->hpData.defrostAutoEnableTime = frame[3];
                this->hpData.defrostEnableTemp = frame[4];
                this->hpData.defrostDisableTemp = frame[5];
                this->hpData.defrostMaxDuration = frame[6] / 20;
                this->hpData.on = frame[7] & 0x40;
                this->hpData.autoRestart = frame[7] & 0x08;
                this->hpData.opMode = static_cast<settings::modeEnum>(frame[7] & 0x6);
                this->hpData.action = static_cast<settings::actionEnum>(frame[7] & 0x20);
                this->hpData.specialCtrlMode = frame[7] & 0x80;
                this->hpData.restartOffsetTemp = frame[9];
                this->hpData.compressorStopMarginTemp = frame[10];
                this->hpData.thermalProtection = frame[11];
                this->hpData.maximumTemp = frame[12];
                this->hpData.stopWhenReachedDelay = frame[13];
                // this->hpData.targetTemp = frame[14];
                return true;
            }
            else if (frame[0] == 0xBB)
            {
                this->hpData.waterTempIn = frame[1];
                this->hpData.waterTempOut = frame[2];
                this->hpData.coilTemp = frame[3];
                this->hpData.airOutletTemp = frame[4];
                this->hpData.outdoorAirTemp = frame[5];
                this->hpData.errorCode = static_cast<settings::hpErrorEnum>(frame[7]);
                this->hpData.timeSinceFan = frame[10];
                this->hpData.timeSincePump = frame[11];
                this->hpData.maximumTemp = frame[12];
                this->hpData.stopWhenReachedDelay = frame[13];
                return true;
            }
            esphome::ESP_LOGW(__FILE__, "UNKNOWN MESSAGE !");
            return false;
        }

        void HeatPumpController::loop()
        {

            if (swi::readFrame())
            {

                if (this->frameIsValid(swi::read_frame, swi::frameCnt))
                {
                    esphome::ESP_LOGD(__FILE__, "Got a frame");
                    this->decode(swi::read_frame);
                    esphome::ESP_LOGD(__FILE__, "PAC %s, temp target: %d", (this->hpData.on ? "ON" : "OFF"), this->hpData.targetTemp);
                    esphome::ESP_LOGD(__FILE__, "Water temp IN %d, Water temp OUT: %d", this->hpData.waterTempIn, this->hpData.waterTempOut);
                }
                else
                {
                    esphome::ESP_LOGW(__FILE__, "Invalid or corrupt frame");
                }
            }
        }

        void HeatPumpController::setOn(bool value)
        {
            this->hpSettings.on = value;
            this->sendControl(this->hpSettings);
        }
        void HeatPumpController::setTargetTemp(uint16_t value)
        {
            this->hpSettings.targetTemp = value;
            this->sendControl(this->hpSettings);
        }
        float HeatPumpController::getTargetTemp()
        {
            return (float)this->hpData.targetTemp;
        }
        float HeatPumpController::getWaterInTemp()
        {

            return (float)this->hpData.waterTempIn;
        }

        float HeatPumpController::getWaterOutTemp()
        {
            return this->hpData.waterTempOut;
        }

        float HeatPumpController::getOutdoorTemp()
        {
            return (float)this->hpData.outdoorAirTemp;
        }

        bool HeatPumpController::getOn()
        {
            return this->hpData.on;
        }

        bool HeatPumpController::getRunning()
        {
            return (this->hpData.timeSinceFan > 0);
        }

        uint16_t HeatPumpController::getErrorCode()
        {
            return this->hpData.errorCode;
        }

        bool HeatPumpController::frameIsValid(uint8_t frame[], uint8_t size)
        {
            if (size == HP_FRAME_LEN)
            {
                return this->checksumIsValid(frame, size);
            }
            else
            {
                return false;
            }
            return false;
        }

        bool HeatPumpController::checksumIsValid(uint8_t frame[], uint8_t size)
        {
            unsigned char computed_checksum = this->computeChecksum(frame, size);
            unsigned char checksum = frame[size - 1];
            return computed_checksum == checksum;
        }

        uint8_t HeatPumpController::computeChecksum(uint8_t frame[], uint8_t size)
        {
            unsigned int total = 0;
            for (byte i = 1; i < size - 1; i++)
            {
                total += frame[i];
            }
            byte checksum = total % 256;
            return checksum;
        }

    }
}
