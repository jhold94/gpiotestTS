#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


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
        mxlradcregs = (unsigned 
  







                 


int main(int argc, char **argv)
{
    int c, i;
    uint16_t addr = 0x0;
    int opt_addr = 0;
    int opt_poke = 0, opt_peek = 0;
    int opt_set = 0, opt_get = 0, opt_dump = 0;
    int opt_dac0 = 0, opt_dac1 = 0, opt_dac2 = 0, opt_dac3 = 0;
    uint8_t pokeval = 0;
    struct cbarpin *cbar_inputs, *cbar_outputs;
    int cbar_size, cbar_mask;
  
    static struct option long_options[] = {
        { "dac0", 1, 0, 'd0' },
        { "dac1", 1, 0, 'd1' },
        { "dac2", 1, 0, 'd2' },
        { "dac3", 1, 0, 'd3' },
        { 0, 0, 0, 0}
    }
    
    model = get_model();
    if(model == 0x7860) {
        cbar_inputs = ts7680_inputs;
        cbar_outputs = ts7680_outputs;
        cbar_size = 6;
        cbar_mask = 3;
    } else {
        fprintf(stderr, "Unsuported model TS-%X\n", model);
        return 1;
    }
    
    


