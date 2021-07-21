/********************************************************************
Author: Sittisak Sinprem <ssinprem@celestica.com>
    flash_spi_fpga

    user-space appliction to flash SPI FLASH

    As a "root" previledge, this program can run well
    while other user group would report system errors under Linux OS.

*********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>      //for open()
#include <unistd.h>     //for close()
#include <string.h>
#include <stdint.h>

/**
 * The FPGA SPI Flash update application
 * This application read the binary image file and program
 * into flash memory. The erasing time can be long the
 * the WAIT_WRITE_READY_SEC should be more than 30 seconds.
 */

#define WAIT_WRITE_READY_SEC 180
#define WAIT_WRITE_CONTINUE_CYCLE 100000

#define REG_SPI_WR_EN   0x1200
#define REG_SPI_WR_DAT  0x1204
#define REG_SPI_CHK_ID  0x1208
#define REG_SPI_VERIFY  0x120C
#define REG_SPI_STAT    0x1210
#define REG_SPI_RESET   0x1214

#define SPI_STAT_MARK_READY             (1 << 15)
#define SPI_STAT_MARK_DONE              (1 << 14)
#define SPI_STAT_MARK_ERROR_ANY         (1 << 13)
#define SPI_STAT_MARK_ERROR_CHKID       (1 << 12)
#define SPI_STAT_MARK_ERROR_ERASE       (1 << 11)
#define SPI_STAT_MARK_ERROR_PROG        (1 << 10)
#define SPI_STAT_MARK_ERROR_TOUT        (1 << 9)
#define SPI_STAT_MARK_ERROR_CRC         (1 << 8)
#define SPI_STAT_MARK_STG_STARTED       (1 << 7)
#define SPI_STAT_MARK_STG_INITED        (1 << 6)
#define SPI_STAT_MARK_STG_CHECKED_ID    (1 << 5)
#define SPI_STAT_MARK_STG_ERSD_SW       (1 << 4)
#define SPI_STAT_MARK_STG_UP_ERSD_IMG   (1 << 3)
#define SPI_STAT_MARK_STG_UP_PRG_IMG    (1 << 2)
#define SPI_STAT_MARK_STG_VERIFIED      (1 << 1)
#define SPI_STAT_MARK_STG_PRG_CMPT      (1 << 0)

#define debug(fmt,args...)      printf("debug : "fmt"\n",##args)
#define reg_write(reg,value)    func_write(reg,value)
#define reg_read(reg,value)     value = func_read(reg)

#define DEV_CHAR_FILENAME "/dev/fwupgrade"

struct fpga_reg_data {
    uint32_t reg;
    uint32_t value;
};

enum{
    READREG,
    WRITEREG
};

unsigned int func_write(int addr,unsigned long value){
    int fd;
    int ret;
    struct fpga_reg_data fpga_reg;

    fd = open(DEV_CHAR_FILENAME, O_RDWR);

    fpga_reg.reg = addr;
    fpga_reg.value = value;

    ioctl(fd, WRITEREG, (void *)&fpga_reg);

    close(fd);
    return 0;
}

unsigned int func_read(int addr){
    int fd;
    int ret;

    struct fpga_reg_data fpga_reg;

    fd = open(DEV_CHAR_FILENAME, O_RDWR);

    fpga_reg.reg = addr;

    ioctl(fd, READREG, (void *)&fpga_reg);

    close(fd);
    return fpga_reg.value;
}

void dump_status(int Stat){
    debug("#########################");
    debug("%d ready(1)/busy(0)",        (Stat&SPI_STAT_MARK_READY)!=0);
    debug("%d done",                    (Stat&SPI_STAT_MARK_DONE)!=0);
    debug("%d error any",               (Stat&SPI_STAT_MARK_ERROR_ANY)!=0);
    debug("%d error checkId",           (Stat&SPI_STAT_MARK_ERROR_CHKID)!=0);
    debug("%d error erase",             (Stat&SPI_STAT_MARK_ERROR_ERASE)!=0);
    debug("%d error program",           (Stat&SPI_STAT_MARK_ERROR_PROG)!=0);
    debug("%d error timeout",           (Stat&SPI_STAT_MARK_ERROR_TOUT)!=0);
    debug("%d error crc",               (Stat&SPI_STAT_MARK_ERROR_CRC)!=0);
    debug("%d stage started",           (Stat&SPI_STAT_MARK_STG_STARTED)!=0);
    debug("%d stage inited",            (Stat&SPI_STAT_MARK_STG_INITED)!=0);
    debug("%d stage checked id",        (Stat&SPI_STAT_MARK_STG_CHECKED_ID)!=0);
    debug("%d stage erasred",           (Stat&SPI_STAT_MARK_STG_ERSD_SW)!=0);
    debug("%d stage upload erase img",  (Stat&SPI_STAT_MARK_STG_UP_ERSD_IMG)!=0);
    debug("%d stage upload program img",(Stat&SPI_STAT_MARK_STG_UP_PRG_IMG)!=0);
    debug("%d stage verified",          (Stat&SPI_STAT_MARK_STG_VERIFIED)!=0);
    debug("%d stage completed",         (Stat&SPI_STAT_MARK_STG_PRG_CMPT)!=0);
}

int flash_program(char *data,int lens){
    int ctimeout;
    int error =0;
    unsigned long Stat = 0;

    reg_read(REG_SPI_RESET,Stat);
    printf("Read Reset is %x\n",Stat);
    printf("Reset Module \n");
    reg_write(REG_SPI_RESET,0x1);   // reset
    sleep(1);
    reg_write(REG_SPI_RESET,0x0);   // normal mode
    ctimeout=0;
    do{        // wait for done flag
        reg_read(REG_SPI_STAT,Stat);
            if(Stat & SPI_STAT_MARK_ERROR_ANY){
                dump_status(Stat);
                error = Stat;
                break;
            }
            if(ctimeout++ > WAIT_WRITE_READY_SEC){
                error = Stat| SPI_STAT_MARK_ERROR_TOUT;
                debug("wait ready timeout . . .");
                break;
            }
    printf(" waiting status to ready ... %d s.  status = %x\n",ctimeout,Stat);
    sleep(1);
    }while((Stat & 0x80F8) != 0x80F8);
    if(error){
      return -1;
    }
    printf("Ready\n");


    for(int i=0;i<lens;){

        do{     // wait for ready flag
            reg_read(REG_SPI_STAT,Stat);
        }while(Stat & SPI_STAT_MARK_READY == 0);

        uint32_t dbuf=0;

        // first byte is MSB
        dbuf =  (((uint32_t)data[i]  )&0xFF) << 24;
        dbuf |= (((uint32_t)data[i+1])&0xFF) << 16;
        dbuf |= (((uint32_t)data[i+2])&0xFF) << 8;
        dbuf |= (((uint32_t)data[i+3])&0xFF);

        reg_write(REG_SPI_WR_DAT,dbuf); // write data

        reg_write(REG_SPI_WR_EN,0x1);   // write enable

        reg_write(REG_SPI_WR_EN,0x0);

        ctimeout=0;
        do{        // wait for done flag
            reg_read(REG_SPI_STAT,Stat);
            //debug(" Stat %8.8x %d",Stat,ctimeout);
            if(Stat & SPI_STAT_MARK_ERROR_ANY){
                dump_status(Stat);
                error = Stat;
                break;
            }

            if(ctimeout++ > WAIT_WRITE_CONTINUE_CYCLE){
                error = Stat| SPI_STAT_MARK_ERROR_TOUT;
                debug("wait ready timeout . . .");
                break;
            }
        }while((Stat & 0x80F8) != 0x80F8);

        if(error){
            printf("FPGA programing fail at %d/%d\n",i,lens);
            debug("Status = %4.4X",error);
            break;
        }

        i +=4;

        if(i%(lens/40*4)==0){
            printf("FPGA programing . . . %d/%d\n",i,lens);
        }
    }

    dump_status(Stat);
    printf("Status = %4.4X\n",Stat);

    reg_write(REG_SPI_WR_EN,0x0);    // write protect
    reg_write(REG_SPI_RESET,0x1);    // module reset

    return error;
}

int main(int argc,char **argv){
    FILE *pFILE;
    int filesize;
    char *filename;
    char *fpga_buff;
    int status;
    int max_size = 128;
    int current_size = max_size;
    int i = 0;
    int c = EOF;

    printf(" FPGA PROGRAMMNG version 0.1.1 \n");
    printf(" build date : %s %s\n",__DATE__,__TIME__);

    filename = NULL;
    filename = malloc(max_size);
    if(!filename){
        exit(-12); /* Out of memory */
    }

    if(argc<2){
        printf("please enter filename : ");
        while((c = getchar()) != '\n' && c != EOF ){

            filename[i++] = (char)c;
            if(i == current_size){
                current_size += max_size;
                filename = realloc(filename, current_size);
            }
        }
        filename[i] = '\0';
    }else{
        i = strlen(argv[1]) + 1;
        filename = realloc(filename, i);
        strcpy(filename, argv[1]);
    }

    pFILE = fopen(filename,"rb");
    free(filename);
    if (pFILE == NULL)
    {
        printf("Could not open the file %s, exit\n",filename);
        return -5;
    }

    fseek(pFILE , 0 , SEEK_END);
    filesize = ftell (pFILE);
    rewind(pFILE);
    fpga_buff = malloc(filesize);
    if(fpga_buff==NULL){
        printf("Can't Allocate memory \n");
        return -5;
    }

    fread(fpga_buff,1,filesize,pFILE);
    fclose(pFILE);

    printf(" Start FPGA Flash ... \n");

    status = flash_program(fpga_buff,filesize);

    if(status == 0){
        printf(" Programing finish \n");
    }else{
        printf(" Program Error : error code %4.4x \n",status);
    }

    return status;
}
