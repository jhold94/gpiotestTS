/********************************************************************************/
// USE: gcc -fno-tree-cselim -Wall -O0 -mcpu=arm9 -o ts7680ctl ts7680ctl.c
/********************************************************************************/

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <linux/types.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <getopt.h>

#include "gpiolib.h"
#include "fpga.h"
#include "crossbar-ts7680.h"
#include "i2c-dev.h"



/********************************************************************************/
// fpga dependencies
/********************************************************************************/
int fpga_init(char *path, char adr)
{
        static int fd = -1;

        if(fd != -1)
                return fd;

        if(path == NULL) {
                // Will always be I2C0 on the 7680
                fd = open("/dev/i2c-0", O_RDWR);
        } else {

                fd = open(path, O_RDWR);
        }

        if(!adr) adr = 0x28;

        if(fd != -1) {
                if (ioctl(fd, I2C_SLAVE_FORCE, 0x28) < 0) {
                        perror("FPGA did not ACK 0x28\n");
                        return -1;
                }
        }

        return fd;
}

void fpoke8(int twifd, uint16_t addr, uint8_t value)
{
        uint8_t data[3];
        data[0] = ((addr >> 8) & 0xff);
        data[1] = (addr & 0xff);
        data[2] = value;
        if (write(twifd, data, 3) != 3) {
                perror("I2C Write Failed");
        }
}

uint8_t fpeek8(int twifd, uint16_t addr)
{
        uint8_t data[2];
        data[0] = ((addr >> 8) & 0xff);
        data[1] = (addr & 0xff);
        if (write(twifd, data, 2) != 2) {
                perror("I2C Address set Failed");
        }
        read(twifd, data, 1);

        return data[0];
}


/********************************************************************************/
// Digital IO and two Relays Setup
/********************************************************************************/
int pinMode(int gpio, int dir)
{
        int ret = 0;
        char buf[50];
        sprintf(buf, "/sys/class/gpio/gpio%d/direction", gpio);
        int gpiofd = open(buf, O_WRONLY);
        if(gpiofd < 0) {
                perror("Couldn't open IRQ file");
                ret = -1;
        }
        
        if(dir == 1 && gpiofd){
                if (3 != write(gpiofd, "out", 3)) {
                        perror("Couldn't set GPIO direction to out");
                        ret = -2;
                }
        }
        
        close(gpiofd);
        return ret;
}

int gpio_setedge(int gpio, int rising, int falling)
{
        int ret = 0;
        char buf[50];
        sprintf(buf, "/sys/class/gpio/gpio%d/edge", gpio);
        int gpiofd = open(buf, O_WRONLY);
        if(gpiofd < 0) {
                perror("Couldn't open IRQ file");
                ret = -1;
        }
        
        if(gpiofd && rising && falling) {
                if(4 != write(gpiofd, "both", 4)) {
                        perror("Failed to set IRQ to both falling & rising");
                        ret = -2;
                }
        } else {
                if(rising && gpiofd) {
                        if(6 != write(gpiofd, "rising", 6)) {
                                perror("Failed to set IRQ to rising");
                                ret = -2;
                        }
                } else if(falling && gpiofd) {
                        if(7 != write(gpiofd, "falling", 7)) {
                                perror("Failed to set IRQ to falling");
                                ret = -3;
                        }
                }
        }
        
        close(gpiofd);
        return ret;
}

int gpio_select(int gpio)
{
	char gpio_irq[64];
	int ret = 0, buf, irqfd;
	fd_set fds;
	FD_ZERO(&fds);

	snprintf(gpio_irq, sizeof(gpio_irq), "/sys/class/gpio/gpio%d/value", gpio);
	irqfd = open(gpio_irq, O_RDONLY, S_IREAD);
	if(irqfd < 1) {
		perror("Couldn't open the value file");
		return -1;
	}

	// Read first since there is always an initial status
	read(irqfd, &buf, sizeof(buf));

	while(1) {
		FD_SET(irqfd, &fds);
		ret = select(irqfd + 1, NULL, NULL, &fds, NULL);
		if(FD_ISSET(irqfd, &fds))
		{
			FD_CLR(irqfd, &fds);  //Remove the filedes from set
			// Clear the junk data in the IRQ file
			read(irqfd, &buf, sizeof(buf));
			return 1;
		}
	}
}

int gpio_export(int gpio)
{
        int efd;
        char buf[50];
        int ret;
        efd = open("/sys/class/gpio/export", O_WRONLY);
        
        if(efd != -1) {
                sprintf(buf, "%d", gpio);
                ret = write(efd, buf, strlen(buf));
                if(ret < 0) {
                        perror("Export failed");
                        return -2;
                }
                close(efd);
        } else {
                // If we can't open the export file, we probably
                // don't have any gpio permissions
                return -1;
        }
        return 0;
}

void gpio_unexport(int gpio)
{
        int gpiofd;
        char buf[50];
        gpiofd = open("/sys/class/gpio/unexport", O_WRONLY);
        sprintf(buf, "%d", gpio);
        write(gpiofd, buf, strlen(buf));
        close(gpiofd);
}

int digitalRead(int gpio)
{
        char in[3] = {0, 0, 0};
        char buf[50];
        int nread, gpiofd;
        sprintf(buf, "/sys/class/gpio/gpio%d/value", gpio);
        gpiofd = open(buf, O_RDWR);
        if(gpiofd < 0) {
                fprintf(stderr, "Failed to open gpio %d value\n", gpio);
                perror("gpio failed");
        }
        
        do {
                nread = read(gpiofd, in, 1);
        } while (nread == 0);
        if(nread == -1){
                perror("GPIO Read Failed");
                return -1;
        }
        
        close(gpiofd);
        return atoi(in);
}

int digitalWrite(int gpio, int val)
{
        char buf[50];
        int ret, gpiofd;
        sprintf(buf, "/sys/class/gpio/gpio%d/value", gpio);
        gpiofd = open(buf, O_RDWR);
        if(gpiofd > 0) {
                snprintf(buf, 2, "%d", val);
                ret = write(gpiofd, buf, 2);
                if(ret < 0) {
                        perror("failed to set gpio");
                        return 1;
                }
                
                close(gpiofd);
                if(ret == 2) return 0;
        }
        return 1;
}


/********************************************************************************/
// Analog Outputs for TS-7680
/********************************************************************************/

static int twifd;

int get_model()
{
        FILE *proc;
        char mdl[256];
        char *ptr;
        
        proc = fopen("/proc/device-tree/model", "r");
        if  (!proc) {
                perror("model");
                return 0;
        }
        fread(mdl, 256, 1, proc);
        ptr = strstr(mdl, "TS-");
        return strtoull(ptr+3, NULL, 16);
}

void dac(int dacpin, int value)
{
	value = value * 360;
	switch(dacpin) {
		case '0':
			char buf[2];
        			buf[0] = ((((strtoul(value, NULL, 0) & 0xfff)<<1)|0x1 >> 9) & 0xf);
				buf[1] = ((((strtoul(value, NULL, 0) & 0xfff)<<1)|0x1 >> 1) & 0xff);
				fpoke8(twifd, 0x2E, buf[0]);
				fpoke8(twifd, 0x2F, buf[1]);
		case '1' :
			char buf[2];
                		buf[0] = ((((strtoul(value, NULL, 0) & 0xfff)<<1)|0x1 >> 9) & 0xf);
                		buf[1] = ((((strtoul(value, NULL, 0) & 0xfff)<<1)|0x1 >> 1) & 0xff);
                		fpoke8(twifd, 0x30, buf[0]);
                		fpoke8(twifd, 0x31, buf[1]);
		case '2' :
			char buf[2];
				buf[0] = ((((strtoul(value, NULL, 0) & 0xfff)<<1)|0x1 >> 9) & 0xf);
				buf[1] = ((((strtoul(value, NULL, 0) & 0xfff)<<1)|0x1 >> 1) & 0xff);
				fpoke8(twifd, 0x32, buf[0]);
				fpoke8(twifd, 0x33, buf[1]);
		case '3' :
			char buf[2];
				buf[0] = ((((strtoul(value, NULL, 0) & 0xfff)<<1)|0x1 >> 9) & 0xf);
				buf[1] = ((((strtoul(value, NULL, 0) & 0xfff)<<1)|0x1 >> 1) & 0xff);
				fpoke8(twifd, 0x34, buf[0]);
				fpoke8(twifd, 0x35, buf[1]);
		default :
			return 1;
	}
}
			

/********************************************************************************/
// Analog Inputs
/********************************************************************************/

int analogInMode(int adcpin, int mode)
{
	volatile unsigned int *mxlradcregs;
        volatile unsigned int *mxhsadcregs;
        volatile unsigned int *mxclkctrlregs;
	unsigned int i, x, meas_mV, meas_mA;
        unsigned long long chan[8] = {0,0,0,0,0,0,0,0};
	int devmem;
        
        devmem = open("/dev/mem", O_RDWR|O_SYNC);
	assert(devmem != -1);
        
        // LRADC
	mxlradcregs = (unsigned int *) mmap(0, getpagesize(),
	  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80050000);
        
        mxlradcregs[0x148/4] = 0xfffffff; //Clears LRADC6:0 assignments
        mxlradcregs[0x144/4] = 0x6543210; //set LRADC6:0 to channel 6:0
        mxlradcregs[0x28/4] = 0xff000000; //set 1.8V Range
        for(x = 0; x < 7; x++)
          mxlradcregs[(0x50+(x * 0x10))/4] = 0x0; //Clear LRADCx reg
        
        for(x = 0; x < 10; x++) {
                mxlradcregs[0x18/4] = 0x7f; //Clear interrupt ready
	        mxlradcregs[0x4/4] = 0x7f; //Schedule conversion of chan 6:0
                while(!((mxlradcregs[0x10/4] & 0x7f) == 0x7f)); //wait
                for(i = 0; i < 7; i++)
                  chan[i] += (mxlradcregs[(0x50+(i * 0x10))/4] & 0xffff);
	}
        
        mxhsadcregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
	  devmem, 0x80002000);
	mxclkctrlregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
          devmem, 0x80040000);
        
	//HSADC
	//See if the HSADC needs to be brought out of reset
	if(mxhsadcregs[0x0/4] & 0xc0000000) {
		mxclkctrlregs[0x154/4] = 0x70000000;
	        mxclkctrlregs[0x1c8/4] = 0x8000;
                //ENGR116296 errata workaround
		mxhsadcregs[0x8/4] = 0x80000000;
		mxhsadcregs[0x0/4] = ((mxhsadcregs[0x0/4] | 0x80000000) & (~0x40000000));
	        mxhsadcregs[0x4/4] = 0x40000000;
                mxhsadcregs[0x8/4] = 0x40000000;
	        mxhsadcregs[0x4/4] = 0x40000000;
                
	        usleep(10);
		mxhsadcregs[0x8/4] = 0xc0000000;
	}
        
        mxhsadcregs[0x28/4] = 0x2000; //Clear powerdown
        mxhsadcregs[0x24/4] = 0x31; //Set precharge and SH bypass
        mxhsadcregs[0x30/4] = 0xa; //Set sample num
        mxhsadcregs[0x40/4] = 0x1; //Set seq num
        mxhsadcregs[0x4/4] = 0x40000; //12bit mode
        
        while(!(mxhsadcregs[0x10/4] & 0x20)) {
                mxhsadcregs[0x50/4]; //Empty FIFO
        }
        
        mxhsadcregs[0x50/4]; //An extra read is necessary
        
        mxhsadcregs[0x14/4] = 0xfc000000; //Clear interrupts
        mxhsadcregs[0x4/4] = 0x1; //Set HS_RUN
        usleep(10);
        mxhsadcregs[0x4/4] = 0x08000000; //Start conversion
        while(!(mxhsadcregs[0x10/4] & 0x1)); //Wait for interrupt
        
        for(i = 0; i < 5; i++) {
                x = mxhsadcregs[0x50/4];
                chan[7] += ((x & 0xfff) + ((x >> 16) & 0xfff));
        }
        
	if(mode == mv) {
		meas_mV = ((((chan[pinadc]/10)*45177)*6235)/100000000);
		return meas_mV; }
	if(mode == ma) {
		meas_mV = ((((chan[pinadc]/10)*45177)*6235)/100000000);
		meas_mA = (((meas_mV)*1000)/240); }
	else {
		return 0; }                
}


/********************************************************************************/
// Usage & Main Function 
/********************************************************************************/


void usage(char **argv) {
        fprintf(stderr,
                "Usage: %s [OPTIONS] ...\n"
                "\n"
                "  -h, --help                   Displays this message\n"
                "\n"
                "***********************Board Info and Setup***********************\n"
                "\n"
                "  -i, --info                   Display board information\n"
                "  -t, --cputemp                Print CPU internal Temp\n"
                "  -m, --getmac                 Display ethernet MAC address\n"
                "  -o, --ddrout <dio>           Set sysfs DIO to an output\n"
                "  -e, --ddrin <dio>            Set sysfs DIO to an input\n"
                "\n"
                "*******************Set Digital and Analog Outputs*****************\n"
                "\n"
                "  -j, --sethigh <dio>          Set a sysfs Digout value high\n"
                "  -l, --setlow <dio>           Set a sysds DigIn value low\n"
                "  -a, --dac0 <Vout>            Set DAC0 output value to Vout\n"
                "  -b, --dac1 <Vout>            Set DAC1 output value to Vout\n"
                "  -c, --dac2 <Vout>            Set DAC2 output value to Vout\n"
                "  -d, --dac3 <Vout>            Set DAC3 output value to Vout\n"
                "\n"
                "*******************Set Digital and Analog Inputs******************\n"
                "\n"
                "  -g, --getin <dio>            Return the input value of DIO <n>\n"
                "  -p, --getadcA0               Return the input mA value of ADC0\n"
                "  -q, --getadcA1               Return the input mA value of ADC1\n"
                "  -r, --getadcA2               Return the input mA value of ADC2\n"
                "  -s, --getadcA3               Return the input mA value of ADC3\n"
                "  -w, --getadcV0               Return the input mV value of ADC0\n"
                "  -x, --getadcV1               Return the input mV value of ADC1\n"
                "  -y, --getadcV2               Return the input mV value of ADC2\n"
                "  -z, --getadcV3               Return the input mV value of ADC3\n"
                "\n",
                argv[0]
        );
}

int main(int argc, char **argv)
{
        int c;
        //uint16_t addr = 0x0;
        int opt_info = 0, opt_getmac = 0;
        int opt_cputemp = 0;
        int opt_dac0 = 0, opt_dac1 = 0, opt_dac2 = 0, opt_dac3 = 0;
        int opt_mAadc0 = 0, opt_mAadc1 = 0, opt_mAadc2 = 0, opt_mAadc3 = 0;
        int opt_mVadc0 = 0, opt_mVadc1 = 0, opt_mVadc2 = 0, opt_mVadc3 = 0;
        //char *opt_mac = NULL;
        int model;
        //uint8_t pokeval = 0;
        
        static struct option long_options[] = {
                { "help", 0, 0, 'h' },
                { "info", 0, 0, 'i' },
                { "cputemp", 0, 0, 't' },
                { "getmac", 0, 0, 'm' },
                { "ddrout", 1, 0, 'o' },
                { "ddrin", 1, 0, 'e' },
                { "sethigh", 1, 0, 'j' },
                { "setlow", 1, 0, 'l' },
                { "dac0", 1, 0, 'a' },
                { "dac1", 1, 0, 'b' },
                { "dac2", 1, 0, 'c' },
                { "dac3", 1, 0, 'd' },
                { "getin", 1, 0, 'g' },
                { "getadcA0", 0, 0, 'p' },
                { "getadcA1", 0, 0, 'q' },
                { "getadcA2", 0, 0, 'r' },
                { "getadcA3", 0, 0, 's' },
                { "getadcV0", 0, 0, 'w' },
                { "getadcV1", 0, 0, 'x' },
                { "getadcV2", 0, 0, 'y' },
                { "getadcV3", 0, 0, 'z' },
                { 0, 0, 0, 0 }
        };
                
        while((c = getopt_long(argc, argv, "+o:hitme:j:l:a:b:c:d:pqrswxyzg:", 
          long_options, NULL)) != -1) {
                int gpio;
                
                switch(c) {
                                
                     // Board Info, and Setup
                        case 'i':
                                opt_info = 1;
                                break;
                        case 't':
                                opt_cputemp = 1;
                                break;
                        case 'm':
                                opt_getmac = 1;
                                break;
                        case 'o':
                                gpio = atoi(optarg);
                                gpio_export(gpio);
                                pinMode(gpio, 1);
                                gpio_unexport(gpio);
                                break;
                        case 'e':
                                gpio = atoi(optarg);
                                gpio_export(gpio);
                                pinMode(gpio, 0);
                                gpio_unexport(gpio);
                                break;
                     
                     // Digital and Analog Outputs
                        case 'j':
                                gpio = atoi(optarg);
                                gpio_export(gpio);
                                digitalWrite(gpio, 1);
                                gpio_unexport(gpio);
                                break;
                        case 'l':
                                gpio = atoi(optarg);
                                gpio_export(gpio);
                                digitalWrite(gpio, 0);
                                gpio_unexport(gpio);
                                break;
			case 'a':
                                opt_dac0 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
                                break;
                        case 'b':
                                opt_dac1 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
                                break;
                        case 'c':
                                opt_dac2 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
                                break;
                        case 'd':
                                opt_dac3 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
                                break;
                                
                     // Digital and Analog Inputs
                        case 'g':
                                gpio = atoi(optarg);
                                gpio_export(gpio);
                                printf("gpio%d=%d\n", gpio, digitalRead(gpio));
                                gpio_unexport(gpio);
                                break;
                        case 'p':
                                opt_mAadc0 = 1;
                                break;
                        case 'q':
                                opt_mAadc1 = 1;
                                break;
                        case 'r':
                                opt_mAadc2 = 1;
                                break;
                        case 's':
                                opt_mAadc3 = 1;
                                break;
                        case 'w':
                                opt_mVadc0 = 1;
                                break;
                        case 'x':
                                opt_mVadc1 = 1;
                                break;
                        case 'y':
                                opt_mVadc2 = 1;
                                break;
                        case 'z':
                                opt_mVadc3 = 1;
                                break;
                        default:
                                usage(argv);
                                return 1;
                }
        }
        
        twifd = fpga_init(NULL, 0);
        if(twifd == -1) {
                perror("Can't open FPGA I2C bus");
                return 1;
        }
        
        if(opt_info) {
                model = get_model();
                printf("model=0x%X\n", model);
                gpio_export(44);
                printf("bootmode=0x%X\n", digitalRead(44) ? 1:0);
                printf("fpga_revision=0x%X\n", fpeek8(twifd, 0x7F));
                gpio_unexport(44);
        }
        
        if(opt_cputemp) {
                signed int temp[2] = {0, 0}, x;
                volatile unsigned int *mxlradcregs;
                int devmem;
                
                devmem = open("/dev/mem", O_RDWR|O_SYNC);
                assert(devmem != -1);
                mxlradcregs = (unsigned int *) mmap(0, getpagesize(),
                  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80050000);
                
                mxlradcregs[0x148/4] = 0xff;
                mxlradcregs[0x144/4] = 0x98; //Set to temp sense mode
                mxlradcregs[0x28/4] = 0x8300; //Enable temp sense block
                mxlradcregs[0x50/4] = 0x0; //Clear ch0 reg
                mxlradcregs[0x60/4] = 0x0; //Clear ch1 reg
                temp[0] = temp[2] = 0;
                
                for(x = 0; x < 10; x++) {
                        /* Clear interrupts
                         * Schedule readings
                         * Poll for samples completion
                         * Pull out samples */
                        mxlradcregs[0x18/4] = 0x3;
                        mxlradcregs[0x4/4] = 0x3;
                        while(!((mxlradcregs[0x10/4] & 0x3) == 0x3));
                        temp[0] += mxlradcregs[0x60/4] & 0xFFFF;
                        temp[1] += mxlradcregs[0x50/4] & 0xFFFF;
                }
                temp[0] = (((temp[0] - temp[1]) * (1012/4)) - 2730000);
                printf("internal_temp=%d.%d\n", temp[0] / 10000,
                  abs(temp[0] % 10000));
                
                munmap((void *)mxlradcregs, getpagesize());
                close(devmem);
        }
        
        if(opt_getmac) {
                unsigned char a, b, c;
                unsigned int mac;
                int devmem;
                volatile unsigned int *mxocotpregs;
                
                devmem = open("/dev/mem", O_RDWR|O_SYNC);
                assert(devmem != -1);
                mxocotpregs = (unsigned int *) mmap(0, getpagesize(),
                  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x8002C000);
                
                mxocotpregs[0x08/4] = 0x200;
                mxocotpregs[0x0/4] = 0x1000;
                while(mxocotpregs[0x0/4] & 0x100); //check busy flag
                mac = mxocotpregs[0x20/4] & 0xFFFFFF;
                if(!mac) {
                        mxocotpregs[0x0/4] = 0x0; //close the reg first
                        mxocotpregs[0x08/4] = 0x200;
                        mxocotpregs[0x0/4] = 0x1013;
                        while(mxocotpregs[0x0/4] & 0x100); //check busy flag
                        mac = (unsigned short) mxocotpregs[0x150/4];
                        mac |= 0x4f0000;
                }
                mxocotpregs[0x0/4] = 0x0;
                
                a = mac >> 16;
                b = mac >> 8;
                c = mac;
                
                printf("mac=00:d0:69:%02x:%02x:%02x\n", a, b, c);
                printf("shortmac=%02x:%02x:%02x\n", a, b, c);
                
                munmap((void *)mxocotpregs, getpagesize());
                close(devmem);
        }
        
        if(opt_dac0) {
                char buf[2];
                buf[0] = ((opt_dac0 >> 9) & 0xf);
                buf[1] = ((opt_dac0 >> 1) & 0xff);
                fpoke8(twifd, 0x2E, buf[0]);
                fpoke8(twifd, 0x2F, buf[1]);
        }
        
        if(opt_dac1) {
                char buf[2];
                buf[0] = ((opt_dac1 >> 9) & 0xf);
                buf[1] = ((opt_dac1 >> 1) & 0xff);
                fpoke8(twifd, 0x30, buf[0]);
                fpoke8(twifd, 0x31, buf[1]);
        }
        
        if(opt_dac2) {
                char buf[2];
                buf[0] = ((opt_dac2 >> 9) & 0xf);
                buf[1] = ((opt_dac2 >> 1) & 0xff);
                fpoke8(twifd, 0x32, buf[0]);
                fpoke8(twifd, 0x33, buf[1]);
        }
        
        if(opt_dac3) {
                char buf[2];
                buf[0] = ((opt_dac3 >> 9) & 0xf);
                buf[1] = ((opt_dac3 >> 1) & 0xff);
                fpoke8(twifd, 0x34, buf[0]);
                fpoke8(twifd, 0x35, buf[1]);
        }
        
        if(opt_mAadc0) {
                volatile unsigned int *mxlradcregs;
                volatile unsigned int *mxhsadcregs;
                volatile unsigned int *mxclkctrlregs;
                unsigned int i, x, meas_mV;
                unsigned long long chan[8] = {0,0,0,0,0,0,0,0};
                int devmem;
        
                devmem = open("/dev/mem", O_RDWR|O_SYNC);
                assert(devmem != -1);
        
                // LRADC
                mxlradcregs = (unsigned int *) mmap(0, getpagesize(),
                  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80050000);
        
                mxlradcregs[0x148/4] = 0xfffffff; //Clears LRADC6:0 assignments
                mxlradcregs[0x144/4] = 0x6543210; //set LRADC6:0 to channel 6:0
                mxlradcregs[0x28/4] = 0xff000000; //set 1.8V Range
                for(x = 0; x < 7; x++)
                  mxlradcregs[(0x50+(x * 0x10))/4] = 0x0; //Clear LRADCx reg
        
                for(x = 0; x < 10; x++) {
                        mxlradcregs[0x18/4] = 0x7f; //Clear interrupt ready
                        mxlradcregs[0x4/4] = 0x7f; //Schedule conversion of chan 6:0
                        while(!((mxlradcregs[0x10/4] & 0x7f) == 0x7f)); //wait
                        for(i = 0; i < 7; i++)
                          chan[i] += (mxlradcregs[(0x50+(i * 0x10))/4] & 0xffff);
                }
        
                mxhsadcregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80002000);
                mxclkctrlregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80040000);
        
                //HSADC
                //See if the HSADC needs to be brought out of reset
                if(mxhsadcregs[0x0/4] & 0xc0000000) {
                        mxclkctrlregs[0x154/4] = 0x70000000;
                        mxclkctrlregs[0x1c8/4] = 0x8000;
                        //ENGR116296 errata workaround
                        mxhsadcregs[0x8/4] = 0x80000000;
                        mxhsadcregs[0x0/4] = ((mxhsadcregs[0x0/4] | 0x80000000) & (~0x40000000));
                        mxhsadcregs[0x4/4] = 0x40000000;
                        mxhsadcregs[0x8/4] = 0x40000000;
                        mxhsadcregs[0x4/4] = 0x40000000;
                
                        usleep(10);
                        mxhsadcregs[0x8/4] = 0xc0000000;
                }
        
                mxhsadcregs[0x28/4] = 0x2000; //Clear powerdown
                mxhsadcregs[0x24/4] = 0x31; //Set precharge and SH bypass
                mxhsadcregs[0x30/4] = 0xa; //Set sample num
                mxhsadcregs[0x40/4] = 0x1; //Set seq num
                mxhsadcregs[0x4/4] = 0x40000; //12bit mode
        
                while(!(mxhsadcregs[0x10/4] & 0x20)) {
                        mxhsadcregs[0x50/4]; //Empty FIFO
                }
        
                mxhsadcregs[0x50/4]; //An extra read is necessary
        
                mxhsadcregs[0x14/4] = 0xfc000000; //Clear interrupts
                mxhsadcregs[0x4/4] = 0x1; //Set HS_RUN
                usleep(10);
                mxhsadcregs[0x4/4] = 0x08000000; //Start conversion
                while(!(mxhsadcregs[0x10/4] & 0x1)); //Wait for interrupt
        
                for(i = 0; i < 5; i++) {
                        x = mxhsadcregs[0x50/4];
                        chan[7] += ((x & 0xfff) + ((x >> 16) & 0xfff));
                }
        
                meas_mV = ((((chan[0]/10)*45177)*6235)/100000000);
                printf("ADC0_val=%dmA\n", (unsigned int)(((meas_mV)*1000)/240));
                
        }
        
        if(opt_mAadc1) {
                volatile unsigned int *mxlradcregs;
                volatile unsigned int *mxhsadcregs;
                volatile unsigned int *mxclkctrlregs;
                unsigned int i, x, meas_mV;
                unsigned long long chan[8] = {0,0,0,0,0,0,0,0};
                int devmem;
        
                devmem = open("/dev/mem", O_RDWR|O_SYNC);
                assert(devmem != -1);
        
                // LRADC
                mxlradcregs = (unsigned int *) mmap(0, getpagesize(),
                  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80050000);
        
                mxlradcregs[0x148/4] = 0xfffffff; //Clears LRADC6:0 assignments
                mxlradcregs[0x144/4] = 0x6543210; //set LRADC6:0 to channel 6:0
                mxlradcregs[0x28/4] = 0xff000000; //set 1.8V Range
                for(x = 0; x < 7; x++)
                  mxlradcregs[(0x50+(x * 0x10))/4] = 0x0; //Clear LRADCx reg
        
                for(x = 0; x < 10; x++) {
                        mxlradcregs[0x18/4] = 0x7f; //Clear interrupt ready
                        mxlradcregs[0x4/4] = 0x7f; //Schedule conversion of chan 6:0
                        while(!((mxlradcregs[0x10/4] & 0x7f) == 0x7f)); //wait
                        for(i = 0; i < 7; i++)
                          chan[i] += (mxlradcregs[(0x50+(i * 0x10))/4] & 0xffff);
                }
        
                mxhsadcregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80002000);
                mxclkctrlregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80040000);
        
                //HSADC
                //See if the HSADC needs to be brought out of reset
                if(mxhsadcregs[0x0/4] & 0xc0000000) {
                        mxclkctrlregs[0x154/4] = 0x70000000;
                        mxclkctrlregs[0x1c8/4] = 0x8000;
                        //ENGR116296 errata workaround
                        mxhsadcregs[0x8/4] = 0x80000000;
                        mxhsadcregs[0x0/4] = ((mxhsadcregs[0x0/4] | 0x80000000) & (~0x40000000));
                        mxhsadcregs[0x4/4] = 0x40000000;
                        mxhsadcregs[0x8/4] = 0x40000000;
                        mxhsadcregs[0x4/4] = 0x40000000;
                
                        usleep(10);
                        mxhsadcregs[0x8/4] = 0xc0000000;
                }
        
                mxhsadcregs[0x28/4] = 0x2000; //Clear powerdown
                mxhsadcregs[0x24/4] = 0x31; //Set precharge and SH bypass
                mxhsadcregs[0x30/4] = 0xa; //Set sample num
                mxhsadcregs[0x40/4] = 0x1; //Set seq num
                mxhsadcregs[0x4/4] = 0x40000; //12bit mode
        
                while(!(mxhsadcregs[0x10/4] & 0x20)) {
                        mxhsadcregs[0x50/4]; //Empty FIFO
                }
        
                mxhsadcregs[0x50/4]; //An extra read is necessary
        
                mxhsadcregs[0x14/4] = 0xfc000000; //Clear interrupts
                mxhsadcregs[0x4/4] = 0x1; //Set HS_RUN
                usleep(10);
                mxhsadcregs[0x4/4] = 0x08000000; //Start conversion
                while(!(mxhsadcregs[0x10/4] & 0x1)); //Wait for interrupt
        
                for(i = 0; i < 5; i++) {
                        x = mxhsadcregs[0x50/4];
                        chan[7] += ((x & 0xfff) + ((x >> 16) & 0xfff));
                }
        
                meas_mV = ((((chan[1]/10)*45177)*6235)/100000000);
                printf("ADC1_val=%dmA\n", (unsigned int)(((meas_mV)*1000)/240));
                
        }
        
        if(opt_mAadc2) {
                volatile unsigned int *mxlradcregs;
                volatile unsigned int *mxhsadcregs;
                volatile unsigned int *mxclkctrlregs;
                unsigned int i, x, meas_mV;
                unsigned long long chan[8] = {0,0,0,0,0,0,0,0};
                int devmem;
        
                devmem = open("/dev/mem", O_RDWR|O_SYNC);
                assert(devmem != -1);
        
                // LRADC
                mxlradcregs = (unsigned int *) mmap(0, getpagesize(),
                  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80050000);
        
                mxlradcregs[0x148/4] = 0xfffffff; //Clears LRADC6:0 assignments
                mxlradcregs[0x144/4] = 0x6543210; //set LRADC6:0 to channel 6:0
                mxlradcregs[0x28/4] = 0xff000000; //set 1.8V Range
                for(x = 0; x < 7; x++)
                  mxlradcregs[(0x50+(x * 0x10))/4] = 0x0; //Clear LRADCx reg
        
                for(x = 0; x < 10; x++) {
                        mxlradcregs[0x18/4] = 0x7f; //Clear interrupt ready
                        mxlradcregs[0x4/4] = 0x7f; //Schedule conversion of chan 6:0
                        while(!((mxlradcregs[0x10/4] & 0x7f) == 0x7f)); //wait
                        for(i = 0; i < 7; i++)
                          chan[i] += (mxlradcregs[(0x50+(i * 0x10))/4] & 0xffff);
                }
        
                mxhsadcregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80002000);
                mxclkctrlregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80040000);
        
                //HSADC
                //See if the HSADC needs to be brought out of reset
                if(mxhsadcregs[0x0/4] & 0xc0000000) {
                        mxclkctrlregs[0x154/4] = 0x70000000;
                        mxclkctrlregs[0x1c8/4] = 0x8000;
                        //ENGR116296 errata workaround
                        mxhsadcregs[0x8/4] = 0x80000000;
                        mxhsadcregs[0x0/4] = ((mxhsadcregs[0x0/4] | 0x80000000) & (~0x40000000));
                        mxhsadcregs[0x4/4] = 0x40000000;
                        mxhsadcregs[0x8/4] = 0x40000000;
                        mxhsadcregs[0x4/4] = 0x40000000;
                
                        usleep(10);
                        mxhsadcregs[0x8/4] = 0xc0000000;
                }
        
                mxhsadcregs[0x28/4] = 0x2000; //Clear powerdown
                mxhsadcregs[0x24/4] = 0x31; //Set precharge and SH bypass
                mxhsadcregs[0x30/4] = 0xa; //Set sample num
                mxhsadcregs[0x40/4] = 0x1; //Set seq num
                mxhsadcregs[0x4/4] = 0x40000; //12bit mode
        
                while(!(mxhsadcregs[0x10/4] & 0x20)) {
                        mxhsadcregs[0x50/4]; //Empty FIFO
                }
        
                mxhsadcregs[0x50/4]; //An extra read is necessary
        
                mxhsadcregs[0x14/4] = 0xfc000000; //Clear interrupts
                mxhsadcregs[0x4/4] = 0x1; //Set HS_RUN
                usleep(10);
                mxhsadcregs[0x4/4] = 0x08000000; //Start conversion
                while(!(mxhsadcregs[0x10/4] & 0x1)); //Wait for interrupt
        
                for(i = 0; i < 5; i++) {
                        x = mxhsadcregs[0x50/4];
                        chan[7] += ((x & 0xfff) + ((x >> 16) & 0xfff));
                }
        
                meas_mV = ((((chan[2]/10)*45177)*6235)/100000000);
                printf("ADC2_val=%dmA\n", (unsigned int)(((meas_mV)*1000)/240));
                
        }
        
        if(opt_mAadc3) {
                volatile unsigned int *mxlradcregs;
                volatile unsigned int *mxhsadcregs;
                volatile unsigned int *mxclkctrlregs;
                unsigned int i, x, meas_mV;
                unsigned long long chan[8] = {0,0,0,0,0,0,0,0};
                int devmem;
        
                devmem = open("/dev/mem", O_RDWR|O_SYNC);
                assert(devmem != -1);
        
                // LRADC
                mxlradcregs = (unsigned int *) mmap(0, getpagesize(),
                  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80050000);
        
                mxlradcregs[0x148/4] = 0xfffffff; //Clears LRADC6:0 assignments
                mxlradcregs[0x144/4] = 0x6543210; //set LRADC6:0 to channel 6:0
                mxlradcregs[0x28/4] = 0xff000000; //set 1.8V Range
                for(x = 0; x < 7; x++)
                  mxlradcregs[(0x50+(x * 0x10))/4] = 0x0; //Clear LRADCx reg
        
                for(x = 0; x < 10; x++) {
                        mxlradcregs[0x18/4] = 0x7f; //Clear interrupt ready
                        mxlradcregs[0x4/4] = 0x7f; //Schedule conversion of chan 6:0
                        while(!((mxlradcregs[0x10/4] & 0x7f) == 0x7f)); //wait
                        for(i = 0; i < 7; i++)
                          chan[i] += (mxlradcregs[(0x50+(i * 0x10))/4] & 0xffff);
                }
        
                mxhsadcregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80002000);
                mxclkctrlregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80040000);
        
                //HSADC
                //See if the HSADC needs to be brought out of reset
                if(mxhsadcregs[0x0/4] & 0xc0000000) {
                        mxclkctrlregs[0x154/4] = 0x70000000;
                        mxclkctrlregs[0x1c8/4] = 0x8000;
                        //ENGR116296 errata workaround
                        mxhsadcregs[0x8/4] = 0x80000000;
                        mxhsadcregs[0x0/4] = ((mxhsadcregs[0x0/4] | 0x80000000) & (~0x40000000));
                        mxhsadcregs[0x4/4] = 0x40000000;
                        mxhsadcregs[0x8/4] = 0x40000000;
                        mxhsadcregs[0x4/4] = 0x40000000;
                
                        usleep(10);
                        mxhsadcregs[0x8/4] = 0xc0000000;
                }
        
                mxhsadcregs[0x28/4] = 0x2000; //Clear powerdown
                mxhsadcregs[0x24/4] = 0x31; //Set precharge and SH bypass
                mxhsadcregs[0x30/4] = 0xa; //Set sample num
                mxhsadcregs[0x40/4] = 0x1; //Set seq num
                mxhsadcregs[0x4/4] = 0x40000; //12bit mode
        
                while(!(mxhsadcregs[0x10/4] & 0x20)) {
                        mxhsadcregs[0x50/4]; //Empty FIFO
                }
        
                mxhsadcregs[0x50/4]; //An extra read is necessary
        
                mxhsadcregs[0x14/4] = 0xfc000000; //Clear interrupts
                mxhsadcregs[0x4/4] = 0x1; //Set HS_RUN
                usleep(10);
                mxhsadcregs[0x4/4] = 0x08000000; //Start conversion
                while(!(mxhsadcregs[0x10/4] & 0x1)); //Wait for interrupt
        
                for(i = 0; i < 5; i++) {
                        x = mxhsadcregs[0x50/4];
                        chan[7] += ((x & 0xfff) + ((x >> 16) & 0xfff));
                }
        
                meas_mV = ((((chan[3]/10)*45177)*6235)/100000000);
                printf("ADC3_val=%dmA\n", (unsigned int)(((meas_mV)*1000)/240));
                
        }
        
        if(opt_mVadc0) {
                volatile unsigned int *mxlradcregs;
                volatile unsigned int *mxhsadcregs;
                volatile unsigned int *mxclkctrlregs;
                unsigned int i, x;
                unsigned long long chan[8] = {0,0,0,0,0,0,0,0};
                int devmem;
        
                devmem = open("/dev/mem", O_RDWR|O_SYNC);
                assert(devmem != -1);
        
                // LRADC
                mxlradcregs = (unsigned int *) mmap(0, getpagesize(),
                  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80050000);
        
                mxlradcregs[0x148/4] = 0xfffffff; //Clears LRADC6:0 assignments
                mxlradcregs[0x144/4] = 0x6543210; //set LRADC6:0 to channel 6:0
                mxlradcregs[0x28/4] = 0xff000000; //set 1.8V Range
                for(x = 0; x < 7; x++)
                  mxlradcregs[(0x50+(x * 0x10))/4] = 0x0; //Clear LRADCx reg
        
                for(x = 0; x < 10; x++) {
                        mxlradcregs[0x18/4] = 0x7f; //Clear interrupt ready
                        mxlradcregs[0x4/4] = 0x7f; //Schedule conversion of chan 6:0
                        while(!((mxlradcregs[0x10/4] & 0x7f) == 0x7f)); //wait
                        for(i = 0; i < 7; i++)
                          chan[i] += (mxlradcregs[(0x50+(i * 0x10))/4] & 0xffff);
                }
        
                mxhsadcregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80002000);
                mxclkctrlregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80040000);
        
                //HSADC
                //See if the HSADC needs to be brought out of reset
                if(mxhsadcregs[0x0/4] & 0xc0000000) {
                        mxclkctrlregs[0x154/4] = 0x70000000;
                        mxclkctrlregs[0x1c8/4] = 0x8000;
                        //ENGR116296 errata workaround
                        mxhsadcregs[0x8/4] = 0x80000000;
                        mxhsadcregs[0x0/4] = ((mxhsadcregs[0x0/4] | 0x80000000) & (~0x40000000));
                        mxhsadcregs[0x4/4] = 0x40000000;
                        mxhsadcregs[0x8/4] = 0x40000000;
                        mxhsadcregs[0x4/4] = 0x40000000;
                
                        usleep(10);
                        mxhsadcregs[0x8/4] = 0xc0000000;
                }
        
                mxhsadcregs[0x28/4] = 0x2000; //Clear powerdown
                mxhsadcregs[0x24/4] = 0x31; //Set precharge and SH bypass
                mxhsadcregs[0x30/4] = 0xa; //Set sample num
                mxhsadcregs[0x40/4] = 0x1; //Set seq num
                mxhsadcregs[0x4/4] = 0x40000; //12bit mode
        
                while(!(mxhsadcregs[0x10/4] & 0x20)) {
                        mxhsadcregs[0x50/4]; //Empty FIFO
                }
        
                mxhsadcregs[0x50/4]; //An extra read is necessary
        
                mxhsadcregs[0x14/4] = 0xfc000000; //Clear interrupts
                mxhsadcregs[0x4/4] = 0x1; //Set HS_RUN
                usleep(10);
                mxhsadcregs[0x4/4] = 0x08000000; //Start conversion
                while(!(mxhsadcregs[0x10/4] & 0x1)); //Wait for interrupt
        
                for(i = 0; i < 5; i++) {
                        x = mxhsadcregs[0x50/4];
                        chan[7] += ((x & 0xfff) + ((x >> 16) & 0xfff));
                }
        
                printf("ADC0_val=%dmV\n", (unsigned int)((((chan[0]/10)*45177)*6235)/100000000));
        }
        
        if(opt_mVadc1) {
                volatile unsigned int *mxlradcregs;
                volatile unsigned int *mxhsadcregs;
                volatile unsigned int *mxclkctrlregs;
                unsigned int i, x;
                unsigned long long chan[8] = {0,0,0,0,0,0,0,0};
                int devmem;
        
                devmem = open("/dev/mem", O_RDWR|O_SYNC);
                assert(devmem != -1);
        
                // LRADC
                mxlradcregs = (unsigned int *) mmap(0, getpagesize(),
                  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80050000);
        
                mxlradcregs[0x148/4] = 0xfffffff; //Clears LRADC6:0 assignments
                mxlradcregs[0x144/4] = 0x6543210; //set LRADC6:0 to channel 6:0
                mxlradcregs[0x28/4] = 0xff000000; //set 1.8V Range
                for(x = 0; x < 7; x++)
                  mxlradcregs[(0x50+(x * 0x10))/4] = 0x0; //Clear LRADCx reg
        
                for(x = 0; x < 10; x++) {
                        mxlradcregs[0x18/4] = 0x7f; //Clear interrupt ready
                        mxlradcregs[0x4/4] = 0x7f; //Schedule conversion of chan 6:0
                        while(!((mxlradcregs[0x10/4] & 0x7f) == 0x7f)); //wait
                        for(i = 0; i < 7; i++)
                          chan[i] += (mxlradcregs[(0x50+(i * 0x10))/4] & 0xffff);
                }
        
                mxhsadcregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80002000);
                mxclkctrlregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80040000);
        
                //HSADC
                //See if the HSADC needs to be brought out of reset
                if(mxhsadcregs[0x0/4] & 0xc0000000) {
                        mxclkctrlregs[0x154/4] = 0x70000000;
                        mxclkctrlregs[0x1c8/4] = 0x8000;
                        //ENGR116296 errata workaround
                        mxhsadcregs[0x8/4] = 0x80000000;
                        mxhsadcregs[0x0/4] = ((mxhsadcregs[0x0/4] | 0x80000000) & (~0x40000000));
                        mxhsadcregs[0x4/4] = 0x40000000;
                        mxhsadcregs[0x8/4] = 0x40000000;
                        mxhsadcregs[0x4/4] = 0x40000000;
                
                        usleep(10);
                        mxhsadcregs[0x8/4] = 0xc0000000;
                }
        
                mxhsadcregs[0x28/4] = 0x2000; //Clear powerdown
                mxhsadcregs[0x24/4] = 0x31; //Set precharge and SH bypass
                mxhsadcregs[0x30/4] = 0xa; //Set sample num
                mxhsadcregs[0x40/4] = 0x1; //Set seq num
                mxhsadcregs[0x4/4] = 0x40000; //12bit mode
        
                while(!(mxhsadcregs[0x10/4] & 0x20)) {
                        mxhsadcregs[0x50/4]; //Empty FIFO
                }
        
                mxhsadcregs[0x50/4]; //An extra read is necessary
        
                mxhsadcregs[0x14/4] = 0xfc000000; //Clear interrupts
                mxhsadcregs[0x4/4] = 0x1; //Set HS_RUN
                usleep(10);
                mxhsadcregs[0x4/4] = 0x08000000; //Start conversion
                while(!(mxhsadcregs[0x10/4] & 0x1)); //Wait for interrupt
        
                for(i = 0; i < 5; i++) {
                        x = mxhsadcregs[0x50/4];
                        chan[7] += ((x & 0xfff) + ((x >> 16) & 0xfff));
                }
        
                printf("ADC1_val=%dmV\n", (unsigned int)((((chan[1]/10)*45177)*6235)/100000000));
        }
        
        if(opt_mVadc2) {
                volatile unsigned int *mxlradcregs;
                volatile unsigned int *mxhsadcregs;
                volatile unsigned int *mxclkctrlregs;
                unsigned int i, x;
                unsigned long long chan[8] = {0,0,0,0,0,0,0,0};
                int devmem;
        
                devmem = open("/dev/mem", O_RDWR|O_SYNC);
                assert(devmem != -1);
        
                // LRADC
                mxlradcregs = (unsigned int *) mmap(0, getpagesize(),
                  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80050000);
        
                mxlradcregs[0x148/4] = 0xfffffff; //Clears LRADC6:0 assignments
                mxlradcregs[0x144/4] = 0x6543210; //set LRADC6:0 to channel 6:0
                mxlradcregs[0x28/4] = 0xff000000; //set 1.8V Range
                for(x = 0; x < 7; x++)
                  mxlradcregs[(0x50+(x * 0x10))/4] = 0x0; //Clear LRADCx reg
        
                for(x = 0; x < 10; x++) {
                        mxlradcregs[0x18/4] = 0x7f; //Clear interrupt ready
                        mxlradcregs[0x4/4] = 0x7f; //Schedule conversion of chan 6:0
                        while(!((mxlradcregs[0x10/4] & 0x7f) == 0x7f)); //wait
                        for(i = 0; i < 7; i++)
                          chan[i] += (mxlradcregs[(0x50+(i * 0x10))/4] & 0xffff);
                }
        
                mxhsadcregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80002000);
                mxclkctrlregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80040000);
        
                //HSADC
                //See if the HSADC needs to be brought out of reset
                if(mxhsadcregs[0x0/4] & 0xc0000000) {
                        mxclkctrlregs[0x154/4] = 0x70000000;
                        mxclkctrlregs[0x1c8/4] = 0x8000;
                        //ENGR116296 errata workaround
                        mxhsadcregs[0x8/4] = 0x80000000;
                        mxhsadcregs[0x0/4] = ((mxhsadcregs[0x0/4] | 0x80000000) & (~0x40000000));
                        mxhsadcregs[0x4/4] = 0x40000000;
                        mxhsadcregs[0x8/4] = 0x40000000;
                        mxhsadcregs[0x4/4] = 0x40000000;
                
                        usleep(10);
                        mxhsadcregs[0x8/4] = 0xc0000000;
                }
        
                mxhsadcregs[0x28/4] = 0x2000; //Clear powerdown
                mxhsadcregs[0x24/4] = 0x31; //Set precharge and SH bypass
                mxhsadcregs[0x30/4] = 0xa; //Set sample num
                mxhsadcregs[0x40/4] = 0x1; //Set seq num
                mxhsadcregs[0x4/4] = 0x40000; //12bit mode
        
                while(!(mxhsadcregs[0x10/4] & 0x20)) {
                        mxhsadcregs[0x50/4]; //Empty FIFO
                }
        
                mxhsadcregs[0x50/4]; //An extra read is necessary
        
                mxhsadcregs[0x14/4] = 0xfc000000; //Clear interrupts
                mxhsadcregs[0x4/4] = 0x1; //Set HS_RUN
                usleep(10);
                mxhsadcregs[0x4/4] = 0x08000000; //Start conversion
                while(!(mxhsadcregs[0x10/4] & 0x1)); //Wait for interrupt
        
                for(i = 0; i < 5; i++) {
                        x = mxhsadcregs[0x50/4];
                        chan[7] += ((x & 0xfff) + ((x >> 16) & 0xfff));
                }
        
                printf("ADC2_val=%dmV\n", (unsigned int)((((chan[2]/10)*45177)*6235)/100000000));
        }
        
        if(opt_mVadc3) {
                volatile unsigned int *mxlradcregs;
                volatile unsigned int *mxhsadcregs;
                volatile unsigned int *mxclkctrlregs;
                unsigned int i, x;
                unsigned long long chan[8] = {0,0,0,0,0,0,0,0};
                int devmem;
        
                devmem = open("/dev/mem", O_RDWR|O_SYNC);
                assert(devmem != -1);
        
                // LRADC
                mxlradcregs = (unsigned int *) mmap(0, getpagesize(),
                  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80050000);
        
                mxlradcregs[0x148/4] = 0xfffffff; //Clears LRADC6:0 assignments
                mxlradcregs[0x144/4] = 0x6543210; //set LRADC6:0 to channel 6:0
                mxlradcregs[0x28/4] = 0xff000000; //set 1.8V Range
                for(x = 0; x < 7; x++)
                  mxlradcregs[(0x50+(x * 0x10))/4] = 0x0; //Clear LRADCx reg
        
                for(x = 0; x < 10; x++) {
                        mxlradcregs[0x18/4] = 0x7f; //Clear interrupt ready
                        mxlradcregs[0x4/4] = 0x7f; //Schedule conversion of chan 6:0
                        while(!((mxlradcregs[0x10/4] & 0x7f) == 0x7f)); //wait
                        for(i = 0; i < 7; i++)
                          chan[i] += (mxlradcregs[(0x50+(i * 0x10))/4] & 0xffff);
                }
        
                mxhsadcregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80002000);
                mxclkctrlregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
                  devmem, 0x80040000);
        
                //HSADC
                //See if the HSADC needs to be brought out of reset
                if(mxhsadcregs[0x0/4] & 0xc0000000) {
                        mxclkctrlregs[0x154/4] = 0x70000000;
                        mxclkctrlregs[0x1c8/4] = 0x8000;
                        //ENGR116296 errata workaround
                        mxhsadcregs[0x8/4] = 0x80000000;
                        mxhsadcregs[0x0/4] = ((mxhsadcregs[0x0/4] | 0x80000000) & (~0x40000000));
                        mxhsadcregs[0x4/4] = 0x40000000;
                        mxhsadcregs[0x8/4] = 0x40000000;
                        mxhsadcregs[0x4/4] = 0x40000000;
                
                        usleep(10);
                        mxhsadcregs[0x8/4] = 0xc0000000;
                }
        
                mxhsadcregs[0x28/4] = 0x2000; //Clear powerdown
                mxhsadcregs[0x24/4] = 0x31; //Set precharge and SH bypass
                mxhsadcregs[0x30/4] = 0xa; //Set sample num
                mxhsadcregs[0x40/4] = 0x1; //Set seq num
                mxhsadcregs[0x4/4] = 0x40000; //12bit mode
        
                while(!(mxhsadcregs[0x10/4] & 0x20)) {
                        mxhsadcregs[0x50/4]; //Empty FIFO
                }
        
                mxhsadcregs[0x50/4]; //An extra read is necessary
        
                mxhsadcregs[0x14/4] = 0xfc000000; //Clear interrupts
                mxhsadcregs[0x4/4] = 0x1; //Set HS_RUN
                usleep(10);
                mxhsadcregs[0x4/4] = 0x08000000; //Start conversion
                while(!(mxhsadcregs[0x10/4] & 0x1)); //Wait for interrupt
        
                for(i = 0; i < 5; i++) {
                        x = mxhsadcregs[0x50/4];
                        chan[7] += ((x & 0xfff) + ((x >> 16) & 0xfff));
                }
        
                printf("ADC3_val=%dmV\n", (unsigned int)((((chan[3]/10)*45177)*6235)/100000000));
        }
        
        close(twifd);
        
        return 0;
}


































