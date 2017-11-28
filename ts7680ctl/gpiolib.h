#ifndef _GPIOLIB_H_

// returns -1 or the file descriptor of the gpio value file
int gpio_open(int gpio);
// 1 output, 0 input
int pinMode(int gpio, int dir);
int gpio_export(int gpio);
void gpio_unexport(int gpio);
int digitalRead(int gpio);
int digitalWrite(int gpio, int val);
int gpio_setedge(int gpio, int rising, int falling);
int gpio_select(int gpio);
int dac(int dacpin, int value);
int analogInMode(int adcpin, int mode);
int ts7680Setup(void);
#endif //_GPIOLIB_H_
