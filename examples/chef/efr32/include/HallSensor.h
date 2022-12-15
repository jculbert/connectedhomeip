/*
 * HallSensor.h
 *
 *  Created on: Sep. 11, 2022
 *      Author: jeff
 */

#ifndef HALLSENSOR_H_
#define HALLSENSOR_H_

class HallSensor
{
public:
    static sl_status_t  Init();
    static sl_status_t Measure(float *value);

    // Get output state
    // true means "contact" is close
    static bool ContactState();

    // Set the threshold used for the output
    static bool SetThreshold(float threshold);
};

#endif /* HALLSENSOR_H_ */
