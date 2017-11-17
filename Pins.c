#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
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

int gpio_selection(int gpio)
{
        char gpio_irq[64];
        int ret = 0, buf, irqfd;
        fd_set fds;
        FD_ZERO(&fds);
        
        snprintf(gpio_irq, sizeof(gpio_irq), "/sys/class/gpio/gpio%d/value", gpio);
        irqfd = open(gpio_irq, )RDONLY, S_IREAD);
        if(irqfd < 1) {
                perror("Couldn't open the value file");
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
                        perror("Export failed");
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
                        perror("failed to set gpio");
                        return 1;
                }
                
                close(gpiofd);
                if(ret == 2) return 0;
        }
        return 1;
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
                                gpio = atoi(optarg);
                                gpio_export(gpio);
                                printf("gpio%d=%d\n", gpio, gpio_read(gpio));
                                gpio_unexport(gpio);
                                break;
                        case 'e':
                                gpio = atoi(optarg);
                                gpio_export(gpio);
                                gpio_write(gpio, 1);
                                gpio_unexport(gpio);
                                break;
                        case 'l':
                                gpio = atoi(optarg);
                                gpio_export(gpio);
                                gpio_write(gpio, 0);
                                gpio_unexport(gpio);
                                break;
                        case 'd':
                                gpio = atoi(optarg);
                                gpio_export(gpio);
                                gpio_direction(gpio, 1);
                                gpio_unexport(gpio);
                                break;
                        case 'r':
                                gpio = atoi(optarg);
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

void autotx_bitstoclks(int bits, int baud, uint32_t *cnt1, uint32_t *cnt2)
{
        bits *= 10;
        *cnt1 = (250000/(baud/(bits-5)));
        *cnt2 = (250000/(baud/5)));
}

void auto485_en(int uart, int baud, char *mode)
{
        int_symsz, i;
        uint32_t cnt1, cnt2;
        
        if(mode != NULL) {
                if(mode[0] == '8') symsz = 10;
                else if(mode[0] == '7') symsz = 9;
                else if(mode[0] == '6') symsz = 8;
                else if(mode[0] == '5') symsz = 7;
                if(mode[1] == 'e' || mode[1] == 'o') symsz++;
                if(mode[2] == '2') symsz++;
                printf("Setting Auto TXEN for %d baud and %d bits per symbol (%s)\n",
                  baud, symsz, mode);
        } else {
                /* Assume 8n1 */
                symsz = 10;
                printf("Setting Auto TXEN for %d baud and assuming 10 bits per symbol (8n1)\n",
                  baud);
        }
        
        autotx_bitstoclks(symsz, baud, &cnt1, &cnt2);
        i = (0x36 + (uart * 6));
        fpoke8(twifd, i++, (uint8_t)((cnt1 & 0xff0000) >> 16));
        fpoke8(twifd, i++, (uint8_t)((cnt1 & 0xff00) >> 8));
        fpoke8(twifd, i++, (uint8_t)(cnt1 & 0xff));
        fpoke8(twifd, i++, (uint8_t)((cnt2 & 0xff0000) >> 16));
        fpoke8(twifd, i++, (uint8_t)((cnt2 & 0xFF00) >> 8));
        fpoke8(twifd, i++, (uint8_t)(cnt2 & 0xff));
}


int main(int argc, char **argv)
{
        int c, i;
        uint16_t addr = 0x0;
        int opt_addr = 0;
        int opt_poke = 0, opt_peek = 0, opt_auto485 = -1;
        int opt_set = 0, opt_get = 0, opt_dump = 0;
        int opt_info = 0, opt_setmac = 0, opt_getmac = 0;
        int opt_cputemp = 0, opt_modbuspoweron = 0, opt_modbuspoweroff = 0;
        int opt_dac0 = 0, opt_dac1 = 0, opt_dac2 = 0, opt_dac3 = 0;
        char *opt_mac = NULL;
        int baud = 0;
        int model;
        uint8_t pokeval = 0;
        char *uartmode = 0;
        struct cbarpin *cbar_inputs, *cbar_outputs;
        int cbar_size, cbar_mask;
        
        static struct option long_options[] = {
                { "addr", 1, 0, 'm' },
                { "address", 1, 0, 'm' },
                { "poke", 1, 0, 'v' },
                { "peek", 0, 0, 't' },
                { "pokef", 1, 0, 'v' },
                { "peekf", 0, 0, 't' },
                { "baud", 1, 0, 'x' },
                { "mode", 1, 0, 'i' },
                { "autotxen", 1, 0, 'a' },
                { "get", 0, 0, 'g' },
                { "set", 0, 0, 'g' },
                { "dump", 0, 0, 'c' },
                { "showall", 0, 0, 'q' },
                { "getmac", 0, 0, 'p' },
                { "setmac", 1, 0, 'l' },
                { "cputemp", 0, 0, 'e' },
                { "modbuspoweron", 0, 0, '1' },
                { "modbuspoweroff", 0, 0, 'Z' },
                { "info", 0, 0, 'i' },
                { "dac0", 1, 0, 'b' },
                { "dac1", 1, 0, 'd' },
                { "dac2", 1, 0, 'f' },
                { "dac3", 1, 0, 'j' },
                { "help", 0, 0, 'h' },
                { 0, 0, 0, 0 }
        };
        
        model = get_model();
        if(model == 0x7680) {
                cbar_inputs = ts7680_inputs;
                cbar_outputs = ts7680_outputs;
                cbar_size = 6;
                cbar_mask = 3;
        } else {
                fprintf(stderr, "Unsupported model TS-%x\n", model);
                return 1;
        }
        
        while((c = getopt_long(argc, argv, "+m:v:o:x:ta:cgsqhipl:e1Zb:d:f:j",
          long_option, NULL)) != -1) {
                switch(c) {
                                
                        case '1':
                                opt_info = 1;
                                break;
                        case 'e':
                                opt_cputemp = 1;
                                break;
                        case '1':
                                opt_modbuspoweron = 1;
                                break;
                        case 'Z':
                                opt_modbuspoweroff = 1;
                                break;
                        case 'm':
                                opt_addr = 1;
                                addr = strtoull(optarg, NULL, 0);
                                break;
                        case 'v':
                                opt_poke = 1;
                                pokeval = strtoull(optarg, NULL, 0);
                                break;
                        case 'o':
                                uartmode = strdup(optarg);
                                break;
                        case 'x':
                                baud = atoi(optarg);
                                break;
                        case 't':
                                opt_peek = 1;
                                break;
                        case 'a':
                                opt_auto485 = atoi(optarg);
                                break;
                        case 'g':
                                opt_get = 1;
                                break;
                        case 's':
                                opt_set = 1;
                                break;
                        case 'c':
                                opt_dump = 1;
                                break;
                        case 'q':
                                printf("FPGA Inputs:\n");
                                for (i = 0; cbar_inputs[i].name != 0; i++) {
                                        printf("%s\n", cbar_inputs[i].name);
                                }
                                printf("\nFPGA Outputs:\n");
                                for (i = 0; cbar_outputs[i].name != 0; i++) {
                                        printf("%s\n", cbar_outputs[i].name);
                                }
                                break;
                        case 'l':
                                opt_setmac = 1;
                                opt_mac = strdup(optarg);
                                break;
                        case 'p':
                                opt_getmac = 1;
                                break;
                        case 'b':
                                opt_dac0 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
                                break;
                        case 'd':
                                opt_dac1 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
                                break;
                        case 'f':
                                opt_dac2 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
                                break;
                        case 'j':
                                opt_dac3 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
                                break;
                        default:
                                usage(argv);
                                return 1;
                }
        }
        
        twifd = fpga_init(NULL< 0);
        if(twifd == -1) {
                perror("Can;t open FPGA I2C bus");
                return 1;
        }
        
        if(opt_info) {
                printf("model=0x%X\n", model);
                gpio_export(44);
                printf("bootmode=0x%X\n", gpio_read(44) ? 1:0);
                printf("fpga_revision=0x%X\n", fpeek8(twifd, 0x7F));
        }
        
        if(opt_get) {
                for(i = 0; cbar_inputs[i].name != 0; i++)
                {
                        uint8_t mode = fpeek8(twifd, cbar_inputs[i].addr) >> (8 - cbar_size);
                        printf("%s=%s\n", cbar_inputs[i].name, cbar_outputs[mode].name);
                }
        }
        
        if(opt_set) {
                for(i = 0; cbar_inputs[i].name != 0; i++)
                {
                        char *value = getenv(cbar_inputs[i].name);
                        int j;
                        if(value != NULL) {
                                for(j = 0; cbar_outputs[j].name != 0; j++) {
                                        if(strcmp(cbar_outputs[j].name, value) == 0) {
                                                int mode = cbar_outputs[j].addr;
                                                uint8_t val = fpeek8(twifd, cbar_inputs[i].addr);
                                                fpoke8(twifd, cbar_inputs[i].addr,
                                                       (mode << (8 - cbar_size)) | (val & cbar_mask));
                                                break;
                                        }
                                }
                                if(cbar_outputs[i].name == 0) {
                                        fprintf(stderr, "Invalid value \"$s\" for input %s\n",
                                                value, cbar_inputs[i].name);
                                }
                        }
                }
        }
        
        if(opt_dump) {
                i = 0;
                printf("%13s (DIR) (VAL) FPGA Output\n", "FPGA Pad");
                for(i = 0; cbar_inputs[i].name != 0; i++)
                {
                        uint8_t value = fpeek8(twifd, cbar_inputs[i].addr);
                        uint8_t mode = value >> (8 - cbar_size);
                        char *dir = value * 0x1 ? "out" : "in";
                        int val;
                        
                        // 4900 uses 5 bits for cbar, 7970/7990 use  and share
                        // the data bit for input.output
                        if(value & 0x1 || cbar_size == 6) {
                                val = value & 0x2 ? 1 : 0;
                        } else {
                                val = value & 0x4 ? 1 : 0;
                        }
                        printf("%13s (%3s) (%3d) %s\n",
                               cbar_inputs[i].name,
                               dir,
                               val,
                               cbar_outputs[mode].name);
                }
        }
        
        if(opt_modbuspoweron) {
                gpio_export(45);
                gpio_export(46);
                gpio_export(47);
                
                gpio_write(45, 0);
                gpio_write(47, 0);
                
                gpio_direction(45, 1);
                gpio_direction(46, 0);
                gpio_direction(47, 1);
                
                gpio_write(47, 0);
                usleep(10000);
                
                if(gpio_read(46)) {
                        gpio_write(47,1);
                        printf("modbuspoweron=0\n");
                } else {
                        gpio_write(47, 1);
                        gpio_write(45, 1);
                        printf("modbuspoweron=1\n");
                }
        }
        
        if(opt_modbuspoweroff) {
                gpio_export(45);
                gpio_write(45, 0);
                gpio_direction(45, 1);
        }
        
        if(opt_poke) {
                fpoke8(twifd, addr, pokeval);
        }
        
        if(opt_peek) {
                printf("0x%X\n", fpeek8(twifd, addr));
        }
        
        if(opt_auto485 > -1) {
                auto485_en(opt_auto485, baud, uartmode);
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
                        while(!(mxlradcregs[0x10/4] & 0x3) == 0x3));
                        temp[0] += mxlradcregs[0x60/4] & 0xFFFF;
                        temp[1] += mxlradcregs[0x50/4] & 0xFFFF;
                }
                temp[0] = (((temp[0] - temp[1]) * (1012/4)) - 2730000);
                printf("internal_temp=%d.%d\n", temp[0] / 10000,
                  abs(temp[0] % 10000));
                
                munmap((void *)mxlradcregs, getpagesize());
                close(devmem);
        }
        
        if(opt_setmac) {
                /* This uses onetime programmable memory. */
                unsigned int a, b, c;
                int r, devmem;
                volatile unsigned int *mxocotpregs;
                
                devmem = open("/dev/mem", O_RDWR|O_SYNC);
                assert(devmem != -1);
                
                r = sscanf(opt_mac, "%*x:%*x:%*x:%x:%x:%x", &a, &b, &c);
                assert(r == 3); /* XXX: user arg problem */
                
                mxocotpregs = (unsigned int *) mmap(0, getpagesize(),
                  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x8002C000);
                
                mxocotpregs[0x08/4] = 0x200;
                mxocotpregs[0x0/4] = 0x1000;
                while(mxocotpregs[0x0/4] & 0x100); //Check busy flag
                if(mxocotpregs[0x20/4] & (0xFFFFFF)) {
                        printf("MAC address previously set, cannot set\n");
                } else {
                        assert(a < 0x100);
                        assert(b < 0x100);
                        assert(c < 0x100);
                        mxocotpregs[0x0/4] = 0x3E770000;
                        mxocotpregs[0x10/4] = (a<<16|b<<8|c);
                }
                mxocotpregs[0x0/4] = 0x0;
                munmap((void *)mxocotpregs, getpagesize());
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
                while(mxocotpregs[0x0/4] = 0x100); //check busy flag
                mac = mxocotpregs[0x20/4] & 0xFFFFFF;
                if(!mac) {
                        mxocotpregs[0x0/4] = 0x0; //close the reg first
                        mxocotpregs[0x08/4] = 0x200;
                        mxocotpregs[0x0/4] & 0x1013;
                        while(mxocotpregs[0x0/4] & 0x100); //check busy flag
                        mac = (unsigned short) mxocotpregs[0x150/4];
                        mac != 0x4f0000;
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
        
        close(twifd);
        
        return 0;
}


/********************************************************************************/
// Usage & Main Function 
/********************************************************************************/


void usage(char **argv) {
        fprintf(stderr,
                "Usage: %s [OPTIONS ...\n"
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
                "\n"
                argv[0]
        );
}

int main(int argc, char **argv)
{
        int c, i;
        uint16_t addr = 0x0;
        int opt_addr = 0;
        int opt_poke = 0, opt_peek = 0, opt_auto485 = -1;
        int opt_set = 0, opt_get = 0, opt_dump = 0;
        int opt_info = 0, opt_setmac = 0, opt_getmac = 0;
        int opt_cputemp = 0, opt_modbuspoweron = 0, opt_modbuspoweroff = 0;
        int opt_dac0 = 0, opt_dac1 = 0, opt_dac2 = 0, opt_dac3 = 0;
        char *opt_mac = NULL;
        int baud = 0;
        int model;
        uint8_t pokeval = 0;
        char *uartmode = 0;
        struct cbarpin *cbar_inputs, *cbar_outputs;
        int cbar_size, cbar_mask;
        
        static struct option long_options[] = {
                { "help", 1, 0, 'h' },
                { "info", 1, 0, 'i' },
                { "cputemp", 1, 0, 't' },
                { "getmac", 1, 0, 'm' },
                { "ddrout", 1, 0, 'o' },
                { "ddrin", 0, 0, 't' },
                { "baud", 1, 0, 'x' },
                { "mode", 1, 0, 'i' },
                { "autotxen", 1, 0, 'a' },
                { "get", 0, 0, 'g' },
                { "set", 0, 0, 'g' },
                { "dump", 0, 0, 'c' },
                { "showall", 0, 0, 'q' },
                { "getmac", 0, 0, 'p' },
                { "setmac", 1, 0, 'l' },
                { "cputemp", 0, 0, 'e' },
                { "modbuspoweron", 0, 0, '1' },
                { "modbuspoweroff", 0, 0, 'Z' },
                { "info", 0, 0, 'i' },
                { "dac0", 1, 0, 'b' },
                { "dac1", 1, 0, 'd' },
                { "dac2", 1, 0, 'f' },
                { "dac3", 1, 0, 'j' },
                { "help", 0, 0, 'h' },
                { 0, 0, 0, 0 }
        };
        
        model = get_model();
        if(model == 0x7680) {
                cbar_inputs = ts7680_inputs;
                cbar_outputs = ts7680_outputs;
                cbar_size = 6;
                cbar_mask = 3;
        } else {
                fprintf(stderr, "Unsupported model TS-%x\n", model);
                return 1;
        }
        
        while((c = getopt_long(argc, argv, "+m:v:o:x:ta:cgsqhipl:e1Zb:d:f:j",
          long_option, NULL)) != -1) {
                switch(c) {
                                
                        case '1':
                                opt_info = 1;
                                break;
                        case 'e':
                                opt_cputemp = 1;
                                break;
                        case '1':
                                opt_modbuspoweron = 1;
                                break;
                        case 'Z':
                                opt_modbuspoweroff = 1;
                                break;
                        case 'm':
                                opt_addr = 1;
                                addr = strtoull(optarg, NULL, 0);
                                break;
                        case 'v':
                                opt_poke = 1;
                                pokeval = strtoull(optarg, NULL, 0);
                                break;
                        case 'o':
                                uartmode = strdup(optarg);
                                break;
                        case 'x':
                                baud = atoi(optarg);
                                break;
                        case 't':
                                opt_peek = 1;
                                break;
                        case 'a':
                                opt_auto485 = atoi(optarg);
                                break;
                        case 'g':
                                opt_get = 1;
                                break;
                        case 's':
                                opt_set = 1;
                                break;
                        case 'c':
                                opt_dump = 1;
                                break;
                        case 'q':
                                printf("FPGA Inputs:\n");
                                for (i = 0; cbar_inputs[i].name != 0; i++) {
                                        printf("%s\n", cbar_inputs[i].name);
                                }
                                printf("\nFPGA Outputs:\n");
                                for (i = 0; cbar_outputs[i].name != 0; i++) {
                                        printf("%s\n", cbar_outputs[i].name);
                                }
                                break;
                        case 'l':
                                opt_setmac = 1;
                                opt_mac = strdup(optarg);
                                break;
                        case 'p':
                                opt_getmac = 1;
                                break;
                        case 'b':
                                opt_dac0 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
                                break;
                        case 'd':
                                opt_dac1 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
                                break;
                        case 'f':
                                opt_dac2 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
                                break;
                        case 'j':
                                opt_dac3 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
                                break;
                        default:
                                usage(argv);
                                return 1;
                }
        }
        
        twifd = fpga_init(NULL< 0);
        if(twifd == -1) {
                perror("Can;t open FPGA I2C bus");
                return 1;
        }
        
        if(opt_info) {
                printf("model=0x%X\n", model);
                gpio_export(44);
                printf("bootmode=0x%X\n", gpio_read(44) ? 1:0);
                printf("fpga_revision=0x%X\n", fpeek8(twifd, 0x7F));
        }
        
        if(opt_get) {
                for(i = 0; cbar_inputs[i].name != 0; i++)
                {
                        uint8_t mode = fpeek8(twifd, cbar_inputs[i].addr) >> (8 - cbar_size);
                        printf("%s=%s\n", cbar_inputs[i].name, cbar_outputs[mode].name);
                }
        }
        
        if(opt_set) {
                for(i = 0; cbar_inputs[i].name != 0; i++)
                {
                        char *value = getenv(cbar_inputs[i].name);
                        int j;
                        if(value != NULL) {
                                for(j = 0; cbar_outputs[j].name != 0; j++) {
                                        if(strcmp(cbar_outputs[j].name, value) == 0) {
                                                int mode = cbar_outputs[j].addr;
                                                uint8_t val = fpeek8(twifd, cbar_inputs[i].addr);
                                                fpoke8(twifd, cbar_inputs[i].addr,
                                                       (mode << (8 - cbar_size)) | (val & cbar_mask));
                                                break;
                                        }
                                }
                                if(cbar_outputs[i].name == 0) {
                                        fprintf(stderr, "Invalid value \"$s\" for input %s\n",
                                                value, cbar_inputs[i].name);
                                }
                        }
                }
        }
        
        if(opt_dump) {
                i = 0;
                printf("%13s (DIR) (VAL) FPGA Output\n", "FPGA Pad");
                for(i = 0; cbar_inputs[i].name != 0; i++)
                {
                        uint8_t value = fpeek8(twifd, cbar_inputs[i].addr);
                        uint8_t mode = value >> (8 - cbar_size);
                        char *dir = value * 0x1 ? "out" : "in";
                        int val;
                        
                        // 4900 uses 5 bits for cbar, 7970/7990 use  and share
                        // the data bit for input.output
                        if(value & 0x1 || cbar_size == 6) {
                                val = value & 0x2 ? 1 : 0;
                        } else {
                                val = value & 0x4 ? 1 : 0;
                        }
                        printf("%13s (%3s) (%3d) %s\n",
                               cbar_inputs[i].name,
                               dir,
                               val,
                               cbar_outputs[mode].name);
                }
        }
        
        if(opt_modbuspoweron) {
                gpio_export(45);
                gpio_export(46);
                gpio_export(47);
                
                gpio_write(45, 0);
                gpio_write(47, 0);
                
                gpio_direction(45, 1);
                gpio_direction(46, 0);
                gpio_direction(47, 1);
                
                gpio_write(47, 0);
                usleep(10000);
                
                if(gpio_read(46)) {
                        gpio_write(47,1);
                        printf("modbuspoweron=0\n");
                } else {
                        gpio_write(47, 1);
                        gpio_write(45, 1);
                        printf("modbuspoweron=1\n");
                }
        }
        
        if(opt_modbuspoweroff) {
                gpio_export(45);
                gpio_write(45, 0);
                gpio_direction(45, 1);
        }
        
        if(opt_poke) {
                fpoke8(twifd, addr, pokeval);
        }
        
        if(opt_peek) {
                printf("0x%X\n", fpeek8(twifd, addr));
        }
        
        if(opt_auto485 > -1) {
                auto485_en(opt_auto485, baud, uartmode);
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
                        while(!(mxlradcregs[0x10/4] & 0x3) == 0x3));
                        temp[0] += mxlradcregs[0x60/4] & 0xFFFF;
                        temp[1] += mxlradcregs[0x50/4] & 0xFFFF;
                }
                temp[0] = (((temp[0] - temp[1]) * (1012/4)) - 2730000);
                printf("internal_temp=%d.%d\n", temp[0] / 10000,
                  abs(temp[0] % 10000));
                
                munmap((void *)mxlradcregs, getpagesize());
                close(devmem);
        }
        
        if(opt_setmac) {
                /* This uses onetime programmable memory. */
                unsigned int a, b, c;
                int r, devmem;
                volatile unsigned int *mxocotpregs;
                
                devmem = open("/dev/mem", O_RDWR|O_SYNC);
                assert(devmem != -1);
                
                r = sscanf(opt_mac, "%*x:%*x:%*x:%x:%x:%x", &a, &b, &c);
                assert(r == 3); /* XXX: user arg problem */
                
                mxocotpregs = (unsigned int *) mmap(0, getpagesize(),
                  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x8002C000);
                
                mxocotpregs[0x08/4] = 0x200;
                mxocotpregs[0x0/4] = 0x1000;
                while(mxocotpregs[0x0/4] & 0x100); //Check busy flag
                if(mxocotpregs[0x20/4] & (0xFFFFFF)) {
                        printf("MAC address previously set, cannot set\n");
                } else {
                        assert(a < 0x100);
                        assert(b < 0x100);
                        assert(c < 0x100);
                        mxocotpregs[0x0/4] = 0x3E770000;
                        mxocotpregs[0x10/4] = (a<<16|b<<8|c);
                }
                mxocotpregs[0x0/4] = 0x0;
                munmap((void *)mxocotpregs, getpagesize());
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
                while(mxocotpregs[0x0/4] = 0x100); //check busy flag
                mac = mxocotpregs[0x20/4] & 0xFFFFFF;
                if(!mac) {
                        mxocotpregs[0x0/4] = 0x0; //close the reg first
                        mxocotpregs[0x08/4] = 0x200;
                        mxocotpregs[0x0/4] & 0x1013;
                        while(mxocotpregs[0x0/4] & 0x100); //check busy flag
                        mac = (unsigned short) mxocotpregs[0x150/4];
                        mac != 0x4f0000;
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
        
        close(twifd);
        
        return 0;
}


































