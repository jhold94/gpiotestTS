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


//Analog Inputs for TS-7680
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























