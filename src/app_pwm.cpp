
#include "app_pwm.h"

double ESP32PWM::mapf(double x, double in_min, double in_max, double out_min, double out_max) {
	if(x>in_max)
		return out_max;
	if(x<in_min)
		return out_min;
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
};

int ESP32PWM::usToTicks(int usec)
{
    return (int)((double)usec / ((double)REFRESH_USEC / (double)(pow(2, _resolutionBits)))*(((double)_myFreq)/50.0));
}

ESP32PWM::ESP32PWM( double freq, uint8_t resolution_bits) {
	_resolutionBits = resolution_bits;
	_myFreq = freq;
	pin = -1;
	_pwmChannel = -1;

}


/* Side effects of frequency changes happen because of shared timers
 *
 * LEDC Chan to Group/Channel/Timer Mapping
 ** ledc: 0  => Group: 0, Channel: 0, Timer: 0
 ** ledc: 1  => Group: 0, Channel: 1, Timer: 0
 ** ledc: 2  => Group: 0, Channel: 2, Timer: 1
 ** ledc: 3  => Group: 0, Channel: 3, Timer: 1
 ** ledc: 4  => Group: 0, Channel: 4, Timer: 2
 ** ledc: 5  => Group: 0, Channel: 5, Timer: 2
 ** ledc: 6  => Group: 0, Channel: 6, Timer: 3
 ** ledc: 7  => Group: 0, Channel: 7, Timer: 3
 ** ledc: 8  => Group: 1, Channel: 0, Timer: 0
 ** ledc: 9  => Group: 1, Channel: 1, Timer: 0
 ** ledc: 10 => Group: 1, Channel: 2, Timer: 1
 ** ledc: 11 => Group: 1, Channel: 3, Timer: 1
 ** ledc: 12 => Group: 1, Channel: 4, Timer: 2
 ** ledc: 13 => Group: 1, Channel: 5, Timer: 2
 ** ledc: 14 => Group: 1, Channel: 6, Timer: 3
 ** ledc: 15 => Group: 1, Channel: 7, Timer: 3
 */

ESP32PWM* CLAppPWM::attach(uint8_t pin, double freq, uint8_t resolution_bits) {

    if(pwms.size() >= NUM_PWM) {
        ESP_LOGW(tag,"Number of available PWM channels exceeded");
        return nullptr;
    }

	if(!isPinSupported(pin)) {
	#if defined(ARDUINO_ESP32S2_DEV)
		ESP_LOGE(AppPwm.getTag(),"PWM available only on: 1-21,26,33-42");
		return nullptr;
	#else

		ESP_LOGE(AppPwm.getTag(),"PWM available only on: 2,4,5,12-19,21-23,25-27,32-33");
		return nullptr;
	#endif
	}

	for(auto pwm : pwms) 
		if(pwm->getPin() == pin) {
			ESP_LOGW(tag,"Pin %d already utilized");
			return nullptr; // pin already used
		}

    ESP32PWM * newpwm = new ESP32PWM(freq, resolution_bits);
    if(!newpwm) {
        ESP_LOGW(tag,"Failed to create PWM"); 
        deallocate(newpwm);
        return nullptr;
    }
    
	configure(newpwm, freq, resolution_bits); 
	newpwm->attachPin(pin);

    if(!newpwm->attached()) {
        ESP_LOGW(tag,"Failed to attach PWM on pin %d", pin);
        deallocate(newpwm);
        return nullptr;
    }

    ESP_LOGI(tag,"Created a new PWM channel %d on pin %d (freq=%.2f, bits=%d)", 
        newpwm->getChannel(), pin, freq, resolution_bits);

    pwms.push_back(newpwm);

    return newpwm; 
}

int CLAppPWM::write(uint8_t pin, int value, int min_v, int max_v) {
    for(auto pwm : pwms) {
        if(pwm->getPin() == pin) {
            if(pwm->attached()) {
                if(min_v > 0) {
                    // treat values less than MIN_PULSE_WIDTH (500) as angles in degrees 
                    // (valid values in microseconds are handled as microseconds)
                    if (value < MIN_PULSE_WIDTH)
                    {
                        if (value < 0)
                            value = 0;
                        else if (value > 180)
                            value = 180;

                        value = map(value, 0, 180, min_v, max_v);

                    }
                    if (value < min_v)          // ensure pulse width is valid
                        value = min_v;
                    else if (value > max_v)
                        value = max_v;

                    value = pwm->usToTicks(value);  // convert to ticks

                }
                // do the actual write
                ESP_LOGD(tag,"Write %d to PWM channel %d pin %d min %d max %d", 
                         value, pwm[i]->getChannel(), pwm[i]->getPin(), min_v, max_v);
                pwm->write(value);
                return OK;
            }
            else {
                ESP_LOGW(tag,"PWM write failed: pin %d is not attached", pin);
                return FAIL;    
            }
        }
    }
    
    ESP_LOGW(tag,"PWM write failed: pin %d is not found", pin);
    return FAIL;
}

void CLAppPWM::reset(uint8_t pin) {
    for(auto pwm : pwms) {
        if(pwm->getPin() == pin || pin == RESET_ALL_PWM)
            pwm->reset();
    }
}

int CLAppPWM::loadFromJson(JsonObject jctx, bool full_set) {

    JsonArray jaPWM = jctx[FPSTR(PWM_TAG)].as<JsonArray>();

    for(JsonObject joPWM : jaPWM) {
        int pin = joPWM[FPSTR(PWM_PIN)] | 0;
        int freq = joPWM[FPSTR(PWM_FREQ)] | 0;
        int resolution = joPWM[FPSTR(PWM_RESOLUTION_BITS)] | 0;
        int def_val = joPWM[FPSTR(PWM_DEFAULT_DUTY)] | 0;

        ESP32PWM* pwm = attach(pin, freq, resolution);
        delay(75); // let the PWM settle

        if(pwm) {
            if(def_val)  {
                pwm->setDefaultDuty(def_val);
                pwm->reset();
            }
        }
        else {
            ESP_LOGW(tag,"Failed to attach PWM to pin %d", pin);
			return FAIL; 
		}
    }
	return OK;
}

int CLAppPWM::saveToJson(JsonObject jctx, bool full_set) {
	if(pwms.size() > 0) {

        JsonArray jaPWM = jctx[FPSTR(PWM_TAG)].to<JsonArray>();
        for(auto pwm : pwms) 
        {
            JsonObject objPWM = jaPWM.add<JsonObject>();
            objPWM[FPSTR(PWM_PIN)] = pwm->getPin();
            objPWM[FPSTR(PWM_FREQ)] = pwm->getFreq();
            objPWM[FPSTR(PWM_RESOLUTION_BITS)] = pwm->getResolutionBits();     

            if(pwm->getDefaultDuty())
                objPWM[FPSTR(PWM_DEFAULT_DUTY)] = pwm->getDefaultDuty();

        }

    }
	return OK;
}

void CLAppPWM::configure(ESP32PWM* pwm, double freq, uint8_t resolution_bits) { 
	pwm->setChannel(allocateChannel(freq));
	ledcSetup(pwm->getChannel(), freq, resolution_bits);
}

int CLAppPWM::allocateChannel(double freq) {
	
	int timer = allocateTimer(freq); 
	if(timer < 0) {
		ESP_LOGE(tag,"All timers allocated! Can't accommodate %f Hz!", freq);
		return -1;
	}

	ESP_LOGD(tag,"Allocating a free channel for the timer %d at freq %f, remaining %d slots", 
		timer, freq, NUM_CHANNELS_PER_TIMER - timerCount[i]);

	for (int index=0; index<NUM_CHANNELS_PER_TIMER; ++index)
	{
		int channel = timerAndIndexToChannel(timer, index);
		if (!isChannelUsed(channel))
		{
			ESP_LOGD(tag,"PWM on ledc channel #%d using timer %d at freq %f Hz", channel, timer, freq);
			return channel;
		}
	}

	return -1;
}

void CLAppPWM::deallocate(ESP32PWM* pwm) {
	if(pwm == nullptr)
		return;	
	if(pwm->attached()) {
		pwm->detachPin(pwm->getPin());
	}
	if(pwm->getChannel() >= 0) {
		ESP_LOGD(tag,"Deallocating channel %d on timer %d", pwm->getChannel(), pwm->getTimer());
		timerCount[pwm->getTimer()]--;
		if (timerCount[pwm->getTimer()] == 0) {
			timerFreqSet[pwm->getTimer()] = 0; // last pwm closed out
			ESP_LOGD(tag,"Timer %d is now free", pwm->getTimer());
		}
	}
	for(auto it = pwms.begin(); it != pwms.end(); ++it) {
		if(*it == pwm) {
			pwms.erase(it);
			break;
		}
	}
	delete pwm;
}

int CLAppPWM::allocateTimer(double freq) {
	for (int i = 0; i < NUM_TIMERS; i++) {

		bool candidate = ((timerFreqSet[i] == freq) || (timerFreqSet[i] == 0));

		if (candidate && (timerCount[i] < NUM_CHANNELS_PER_TIMER)) {
			if (timerFreqSet[i] == 0) {
				ESP_LOGD(tag,"Starting timer %d at freq %f", i, freq);
				timerFreqSet[i] = freq;
			}
			timerCount[i]++;
			return i;
		}
	}
	return -1;
}

bool CLAppPWM::isChannelUsed(int channel) {
	for(auto pwm : pwms) {
		if(pwm->getChannel() == channel)
			return true;
	}
	return false;
}

bool CLAppPWM::isPinSupported(int pin) {
#if defined(ARDUINO_ESP32S2_DEV)
	if ((pin >=1 && pin <= 21) || //20
		(pin == 26) || //1
		(pin >= 33 && pin <= 42)) {
		return true;
	}
#else
	if ((pin == 2) || //1
		(pin == 4) || //1
		(pin == 5) || //1
		((pin >= 12) && (pin <= 19)) || //8
		((pin >= 21) && (pin <= 23)) || //3
		((pin >= 25) && (pin <= 27)) || //3
		(pin == 32) || (pin == 33)) { 
		return true;
	}	
#endif
	return false;
}

int CLAppPWM::timerAndIndexToChannel(int timer, int index) {
	int localIndex = 0;
	for (int j = 0; j < NUM_PWM; j++) {
		if (((j / 2) % 4) == timer) {
			if (localIndex == index) {
				return j;
			}
			localIndex++;
		}
	}
	return -1;
}

CLAppPWM AppPwm;


