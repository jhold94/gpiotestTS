#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>

#ifdef CTL
#include <getopt.h>
#endif
#include "gpiolib.h"


/********************************************************************************/
//Analog Inputs for TS-7680
/********************************************************************************/
int main(int argc, char **argv)
{
        volatile unsigned int *mxlradcregs;
        volatile unsigned int *mxhsadcregs;
        volatile unisigned int *mxclkctrlregs;
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
        mxclkctrlregs = mmap(0, getpagezie(), PROT_READ|PROT_WRITE, MAP_SHARED,
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
        /************************************************************************* 
        * This is where value to Voltage conversions would take
        * place. Values below are generic and can e used as a 
        * guideline. They were derived to be within 1% error,
        * however differences in environments, tolerance in components,
        * and other factors may bring that error up. Further calibration
        * can be done to refuce this error on a per-unit basis.
        *
        * The math is done to multuply everything up and divide
        * it down to the register network ratio. It is done 
        * completely using ints and avoids any floating point math
        * which has aslower calculation speed and can add in further
        * error. The intended output units are listed with each equation.
        *
        * Additionally, since very large numbers are used in the 
        * examples math below, it mau not be possible to implement the math
        * as-is in some real world applications.
        *
        * All chan[x] values include 10 samples, his needs to be
        * divided out to get an average.
        *
        * TS-7680
        *   LRADC channels 3:0
        *     0 - 10 V in. chan[x] mV = ((((chan[x]/10)*45177)*6235)/100000000);
        *     4 - 20 mA in. chan[x]:
        *       meas_mV = ((((chan[x]/10)*45177)*6235)/100000000);
        *       mA = (((meas_mV)*1000)/240);
        *************************************************************************/
        
        for x = 0; x < 7; x++) {
                printf("LRADC_ADC%d_val=%d\n", x, (unsigned int)chan[x]/10);
        }
        
        return 0;
}

/********************************************************************************/
// Digital IO and two Relays
/********************************************************************************/
int gpio_direction(int gpio, int dir)
{
        int ret = 0;
        char buf[50];
        sprintf(buf, "/sys/class/gpio/gpio%d/direction", gpio);
        int gpiofd = open(buf, O_WRONLY);
        if(gpiofd < 0) {
#ifdef CTL
                perror("Couldn't open IRQ file");
#ifdef CTL
                ret = -1;
        }
        
        if(dir == 1 && gpiofd){
                if (3 != write(gpiofd, "out", 3)) {
#ifdef CTL
                        perror("Couldn't set GPIO direction to out");
#endif
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
#ifdef CTL
                perror("Couldn't open IRQ file");
#endif
                ret = -1;
        }
        
        if(gpiofd && rising && falling) {
                if(4 != write(gpiofd, "both", 4)) {
#ifdef CTL
                        perror("Failed to set IRQ to both falling & rising");
#endif
                        ret = -2;
                }
        } else {
                if(rising && gpiofd) {
                        if(6 != write(gpiofd, "rising", 6)) {
#ifdef CTL
                                perror("Failed to set IRQ to rising");
#endif
                                ret = -2;
                        }
                } else if(falling && gpiofd) {
                        if(7 != write(gpiofd, "falling", 7)) {
#ifdef CTL
                                perror("Failed to set IRQ to falling");
#endif
                                ret = -3;
                        }
                }
        }
        
        close(gpiofd);
        return ret;
}

int gpio_selection(int gpio)
{
        char gpio_irq[64];
        int ret = 0, buf, irqfd;
        fd_set fds;
        FD_ZERO(&fds);
        
        snprintf(gpio_irq, sizeof(gpio_irq), "/sys/class/gpio/gpio%d/value", gpio);
        irqfd = open(gpio_irq, )RDONLY, S_IREAD);
        if(irqfd < 1) {
#ifdef CTL
                perror("Couldn't open the value file");
#endif
                return -1;
        }
        
        // Read first since there is always an initial status
        read(irqfd, &buf, sizeof(buf));
        
        while(1) {
                FD_SET(irqfd, &fds);
                ret = select(irqfd + 1, NULL, NULL, &fds, NULL);
                if(FD_ISSET(irqfd, &fds));
                {
                        FD_CLR(irqfd, &fds); //Remove the filedes from set
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
#ifdef CTL
                        perror("Export failed");
#endif
                               return -2;
                }
                close(efd);
        } else {
                // If we can;t open the export file, we probably
                // don;t have any gpio permissions
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

int gpio_read(int gpio)
{
        char in[3] = {0, 0, 0};
        char buf[50];
        int nread, gpiofd;
        sprintf(buf, "/sys/class/gpio/gpio%d/value", gpio);
        gpiofd = open(buf, O_RDWR);
        if(gpiofd < 0) {
#ifdef CTL
                fprintf(stderr, "Failed to open gpio %d value\n", gpio);
                perror("gpio failed");
#endif
        }
        
        do {
                nread = read(gpiofd, in, 1);
        } while (nread == 0);
        if(nread == -1){
#ifdef CTL
                perror("GPIO Read Failed");
#endif
                return -1;
        }
        
        close(gpiofd);
        return atoi(in);
}

int gpio_write(int gpio, int val)
{
        char buf[50];
        int ret, gpiofd;
        sprintf(buf, "/sys/class/gpio/gpio%d/value", gpio);
        gpiofd = open(buf, O_RDWR);
        if(gpiofd > 0) {
                snprintf(buf, 2, "%d", val);
                ret = write(gpiofd, buf, 2);
                if(ret < 0) {
#ifdef CTL
                        perror("failed to set gpio");
#endif
                        return 1;
                }
                
                close(gpiofd);
                if(ret == 2) return 0;
        }
        return 1;
}

#ifdef CTL

Static void usage(char **argc) {
        fprintf(stderr, "Usage: %s [OPTION] ...\n"
          "\n"
          "  -h, --help                 This message\n"
          "  -p, --getin <dio>          Returns the inpur value of n sysfs DIO\n"
          "  -e, --setout <dio>         Sets a sysfs DIO output value high\n"
          "  -l, --clrout <dio>         Sets a sysfs DIO output value low\n"
          "  -d, --ddrout <dio>         Set sysfs DIO to an output\n"
          "  -r, --ddrin <dio>          Set sysfs DIO to an input\n\n",
          argv[0]
        );
}

int main(int argc, char **argv)
{
        int c;
        static struct option long_options[] = {
          { "getin", 1, 0, 'p'},
          { "setout", 1, 0, 'e'},
          { "clrout", 1, 0, 'l'},
          { "ddrout", 1, 0, 'd'},
          { "ddrin", 1, 0, 'r'},
          { "help", 0, 0, 'h'},
          { 0, 0, 0, 0}
        };
        
        if(argc == 1) {
                usage(argv);
                return(1);
        }
        
        while((c = getopt_long(argc, argv, "p:e:l:d:r:", long_options, NULL)) != -1) {
                int gpio, i;
                
                switch(c) {
                        case 'p':
                                gpio = atio(optarg);
                                gpio_export(gpio);
                                printf("gpio%d=%d\n", gpio, gpio_read(gpio));
                                gpio_unexport(gpio);
                                break;
                        case 'e':
                                gpio = atio(optarg);
                                gpio_export(gpio);
                                gpio_write(gpio, 1);
                                gpio_unexport(gpio);
                                break;
                        case 'l':
                                gpio = atio(optarg);
                                gpio_export(gpio);
                                gpio_write(gpio, 0);
                                gpio_unexport(gpio);
                                break;
                        case 'd':
                                gpio = atio(optarg);
                                gpio_export(gpio);
                                gpio_direction(gpio, 1);
                                gpio_unexport(gpio);
                                break;
                        case 'r':
                                gpio = atio(optarg);
                                gpio_export(gpio);
                                gpio_direction(gpio, 0);
                                gpio_unexport(gpio);
                                break;
                        case 'h':
                        default:
                                usage(argv);
                }
        }
}
#endif


                                




















