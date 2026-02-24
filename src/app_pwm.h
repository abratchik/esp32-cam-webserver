#ifndef APP_PWM_H
#define APP_PWM_H

#include "esp32-hal-ledc.h"
#include "app_component.h"

#define NUM_PWM 16
#define NUM_TIMERS 4
#define NUM_CHANNELS_PER_TIMER (NUM_PWM/NUM_TIMERS)
#define PWM_BASE_INDEX 0
#define RESET_ALL_PWM                   0
#define PWM_DEFAULT_FREQ                50
#define PWM_DEFAULT_RESOLUTION_BITS     10
#define MIN_PULSE_WIDTH                544     // the shortest pulse sent to a servo  
#define MAX_PULSE_WIDTH               2400     // the longest pulse sent to a servo 
#define REFRESH_USEC                 20000

#define USABLE_ESP32_PWM (NUM_PWM-PWM_BASE_INDEX)

#include <cstdint>
#include <vector>

#include "Arduino.h"

#include <esp_log.h>

const char PWM_TAG[] PROGMEM = "pwm";
const char PWM_PIN[] PROGMEM = "pin";
const char PWM_FREQ[] PROGMEM = "frequency";
const char PWM_RESOLUTION_BITS[] PROGMEM = "resolution";
const char PWM_DEFAULT_DUTY[] PROGMEM = "default";

class ESP32PWM {

    public:
        // setup
        ESP32PWM(double freq, uint8_t resolution_bits);

        void detachPin(int pin) {ledcDetachPin(pin);};
        void attachPin(uint8_t pin) { setPin(pin); ledcAttachPin(pin, getChannel());}

        // write raw duty cycle
        void write(uint32_t duty) {myDuty = duty; ledcWrite(getChannel(), duty);};
        // Write a duty cycle to the PWM using a unit vector from 0.0-1.0
        void writeScaled(double duty) { write(mapf(duty, 0.0, 1.0, 0, (double) ((1 << _resolutionBits) - 1)));};
        // reset PWM to default
        void reset() {write(default_duty);};

        // Return microseconds to timer ticks for the current frequency and resolution
        int usToTicks(int usec);

        // Read/write pwm data
        uint32_t getDuty() {return ledcRead(getChannel());};
        double getDutyScaled() {
            return mapf((double) myDuty, 0, (double) ((1 << _resolutionBits) - 1), 0.0, 1.0);
        };
        uint32_t getDefaultDuty() {return default_duty;}; 
        void setDefaultDuty(uint32_t val) {default_duty = val;};


        // Getters & Setters
        int getPin() {return pin;};
        int getChannel() {return _pwmChannel;};
        bool attached() {return _attachedState;};
        double getFreq() {return _myFreq;};
        int getTimer() {return timerNum;}
        uint8_t getResolutionBits() {return _resolutionBits;};

        void setChannel(int channel) {_pwmChannel = channel;};
        

    protected:
        void setPin(int p) { pin = p; _attachedState = true;};
        static double mapf(double x, double in_min, double in_max, double out_min, double out_max);

    private:

        int timerNum = -1;
        uint32_t myDuty = 0;

        uint32_t default_duty = 0;

        int _pwmChannel = 0;                         
        bool _attachedState = false;
        int pin;
        uint8_t _resolutionBits;
        double _myFreq;

};


class CLAppPWM : public CLAppComponent {
    public:
        CLAppPWM() {
            setTag("pwm");
        };

        int loadFromJson(JsonObject jctx, bool full_set = true);
        int saveToJson(JsonObject jctx, bool full_set = true);

        ESP32PWM* get(uint8_t index) { if(index < pwms.size()) return pwms[index]; else return nullptr; };

        /**
         * @brief attaches a new PWM/servo and returns its pointer in case of success, or NULL otherwise
         * 
         * @param pin 
         * @param freq
         * @param resolution_bits
         * @return ESP32PWM*  
         */
        ESP32PWM* attach(uint8_t pin, double freq = PWM_DEFAULT_FREQ, uint8_t resolution_bits = PWM_DEFAULT_RESOLUTION_BITS);
        
        /**
         * @brief writes an angle value to PWM/Servo.
         * 
         * @param pin 
         * @param value 
         * @param min_v
         * @param max_v
         * @return int 
         */
        int write(uint8_t pin, int value, int min_v = MIN_PULSE_WIDTH, int max_v = MAX_PULSE_WIDTH);

        /**
         * @brief Set all PWM to its default value. If the default was not defined, it will be reset to 0
         * 
         * @param pin 
         */
        void reset(uint8_t pin = RESET_ALL_PWM);

        // Returns true if the pin is supported for PWM/Servo use, false otherwise
        static bool isPinSupported(int pin);
    
    protected:

        void configure(ESP32PWM* pwm, double freq, uint8_t resolution_bits);

        int allocateChannel(double freq);

        // Finds a suitable timer for the requested frequency and allocates it. 
        // Returns the timer number allocated, or -1 if no timer is available for 
        // the requested frequency.
        int allocateTimer(double freq);

        void deallocate(ESP32PWM* pwm);


        int timerAndIndexToChannel(int timer, int index);

        bool isChannelUsed(int channel); 

    private:
        std::vector<ESP32PWM*> pwms;

        // Number of currently allocated PWM channels per timer. 
        // This is used to track the number of channels allocated to each timer, 
        // which is needed to determine whether a new channel can be allocated to a timer or not.
        int timerCount[NUM_TIMERS] = { 0, 0, 0, 0 };

        // Frequencies currently allocated to each timer. 
        // If a timer is not allocated, the value is 0
        double timerFreqSet[NUM_TIMERS] = { 0, 0, 0, 0 };

};  

extern CLAppPWM AppPwm;

#endif /* APP_PWM_H */
