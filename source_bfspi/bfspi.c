#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h> /* O_ACCMODE */
#include <asm/system.h> /* cli(), *_flags */
#include <asm/uaccess.h> /* copy_from/to_user */
#include <asm/io.h>
#include <linux/err.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>

#include <asm/blackfin.h>
#include <asm/bfin5xx_spi.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <asm/portmux.h>
#include <asm-generic/io.h>
#include "bfspi.h"

MODULE_LICENSE( "Dual BSD/GPL" );
#define BFIN_SPI_DEBUG
//#undef BFIN_SPI_DEBUG
#define RESET_BIT   4  /* GPIO bit tied to nRESET on Si chips */

#ifdef BFIN_SPI_DEBUG
#define PRINTK(args...) printk(args)
#else
#define PRINTK(args...)
#endif

/* 
   I found these macros from the bfin5xx_spi.c driver by Luke Yang 
   useful - thanks Luke :-) 
*/

#define DEFINE_SPI_REG(reg, off) \
static inline u16 read_##reg(void) \
            { return *(volatile unsigned short*)(SPI0_REGBASE + off); } \
static inline void write_##reg(u16 v) \
            {*(volatile unsigned short*)(SPI0_REGBASE + off) = v;\
             __builtin_bfin_ssync();}

DEFINE_SPI_REG(CTRL, 0x00)
DEFINE_SPI_REG(FLAG, 0x04)
DEFINE_SPI_REG(STAT, 0x08)
DEFINE_SPI_REG(TDBR, 0x0C)
DEFINE_SPI_REG(RDBR, 0x10)
DEFINE_SPI_REG(BAUD, 0x14)
DEFINE_SPI_REG(SHAW, 0x18)

/* Global variables of the driver */
/* Major number */
#define RXS (1<<5)
#define CONS_BFSPI_MAJOR 234
int BFSPI_MAJOR;
#define N_BFSPI_MINORS			32	/* ... up to 256 */
#define MAX_BFSPI_CLIENT 5

static dev_t first; // Global variable for the first device number
static struct cdev c_dev; // Global variable for the character device structure

static DECLARE_BITMAP(minors, N_BFSPI_MINORS);

/* Buffer to store data */
//char *memory_buffer;
int portno;
int cardno;
int spibaud;
int spimode;
int wrcount;
int rdcount;

struct bfspi_data {
	dev_t			devt;
	spinlock_t		spi_lock;
	//struct spi_device	*spi;
	struct cdev cdev;
	struct list_head	device_entry;

	/* buffer is NULL unless this device is open (users > 0) */
	struct mutex		buf_lock;
	unsigned		users;
	u8			*buffer;
	int cardno;
	int portno;
};

struct bfspi_data	*bfspi[MAX_BFSPI_CLIENT];

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);
#define SPI_NCSB GPIO_PF12
#define SPI_NCSA GPIO_PF3
//int bfspi_open( struct inode *inode, struct file *filp );
//int bfspi_release( struct inode *inode, struct file *filp );
//ssize_t bfspi_read( struct file *filp, char *buf, size_t count, loff_t *f_pos);
//ssize_t bfspi_write( struct file *filp, char *buf, size_t count, loff_t *f_pos);
//int bfspi_probe( struct spi_device *spi );
//int bfspi_init( void );
//void bfspi_exit( void );

#define bufsiz 64

u8 bfspi_read_8_bits(u16 chip_select)
{
  u16 flag_enable, flag;
  u8 ret;

  if (chip_select < 8) {
    flag = bfin_read_SPI_FLG();
    flag_enable = flag & ~(1 << (chip_select + 8));
    //PRINTK("read: flag: 0x%04x flag_enable: 0x%04x \n", flag, flag_enable);
  }
  else {
#if (defined(CONFIG_BF533) || defined(CONFIG_BF532))
    bfin_write_FIO_FLAG_C((1<<chip_select)); 
#endif
#if (defined(CONFIG_BF536) || defined(CONFIG_BF537))
        bfin_write_PORTFIO_CLEAR((1<<chip_select));
#endif
    __builtin_bfin_ssync();
  }

  /* drop SPISEL */
  bfin_write_SPI_FLG(flag_enable); 

  /* read kicks off transfer, detect end by polling RXS, we
     read the shadow register to prevent another transfer
     being started 

     While reading we write a dummy tx value, 0xff.  For
     the MMC card, a 0 bit indicates the start of a command 
     sequence therefore an all 1's sequence keeps the MMC
     card in the current state.
  */
  bfin_write_SPI_TDBR(0xff);
  bfin_read_SPI_RDBR(); __builtin_bfin_ssync();
  do { } while (!(bfin_read_SPI_STAT() & RXS)); //hardcode RXS mask
  ret = bfin_read_SPI_SHADOW(); __builtin_bfin_ssync();

  //ret = read_RDBR(); __builtin_bfin_ssync();
  PRINTK("\nkern>> read: 0x%04X\n", ret);	
  /* raise SPISEL */
  if (chip_select < 8) {
    bfin_write_SPI_FLG(flag); 
  }
  else {
#if (defined(CONFIG_BF533) || defined(CONFIG_BF532))
    bfin_write_FIO_FLAG_S((1<<chip_select)); 
#endif
#if (defined(CONFIG_BF536) || defined(CONFIG_BF537))
        bfin_write_PORTFIO_SET((1<<chip_select));
#endif
    __builtin_bfin_ssync();
  }

  return ret;
}

void bfspi_write_8_bits(u16 chip_select, u8 bits)
{
  u16 flag_enable, flag;

  if (chip_select < 8) {
    flag = bfin_read_SPI_FLG();
    flag_enable = flag & ~(1 << (chip_select + 8));
    //PRINTK("kern>> chip_select: %d write: flag: 0x%04x flag_enable: 0x%04x \n", chip_select, flag, flag_enable);

    /* drop SPISEL */
    bfin_write_SPI_FLG(flag_enable); 
  }
  else {
#if (defined(CONFIG_BF533) || defined(CONFIG_BF532))
  	bfin_write_FIO_FLAG_C((1<<chip_select)); 
#endif
#if (defined(CONFIG_BF536) || defined(CONFIG_BF537))
	bfin_write_PORTFIO_CLEAR((1<<chip_select));
#endif
  	__builtin_bfin_ssync();
  }

  /* read kicks off transfer, detect end by polling RXS */
  
  bfin_write_SPI_TDBR(bits);
  bfin_read_SPI_RDBR(); __builtin_bfin_ssync();
  do {} while (!(bfin_read_SPI_STAT() & RXS)); //hardcode RXS mask
  //(void) bfin_read_SPI_SHADOW(); //discard data && clear rxs
  //__builtin_bfin_ssync();
  
  /* raise SPISEL */
  if (chip_select < 8) {
    bfin_write_SPI_FLG(flag); 
  }
  else {
#if (defined(CONFIG_BF533) || defined(CONFIG_BF532))
    bfin_write_FIO_FLAG_S((1<<chip_select)); 
#endif
#if (defined(CONFIG_BF536) || defined(CONFIG_BF537))
        bfin_write_PORTFIO_SET((1<<chip_select));
#endif
    __builtin_bfin_ssync();
  }
}

static int bfspi_open( struct inode *inode, struct file *filp ) 
{
	struct bfspi_data	*mbfspi;
	int			status = -ENXIO;
	int i, msz;
	int mn = iminor(inode);
	int mj = imajor(inode);
	
	if ((mj != BFSPI_MAJOR) || (mn <= 0) || (mn > MAX_BFSPI_CLIENT)) {
		printk(KERN_WARNING "\n[target] "
			"No device found with minor=%d and major=%d\n", 
			mj, mn);
		return -ENODEV; /* No such device */
	}
	//PRINTK("kern>> Open device %d, %d \n", mj, mn);
	mutex_lock(&device_list_lock);
	//msz = sizeof(sizeof(struct bfspi_data));
	//for (i = 0; i < MAX_BFSPI_CLIENT; i++) {
		mbfspi = bfspi[mn-1];
		//PRINTK("Open device %d, %d \n", mj, mn);
		//if (mbfspi->devt == inode->i_rdev) {
			//mbfspi = bfspi[i];
		//	status = 0;
		//	break;
		//}
	//}
	
	if (mbfspi) {
		if (!mbfspi->buffer) {
			mbfspi->buffer = kmalloc(bufsiz, GFP_KERNEL);
			status = 0;
			if (!mbfspi->buffer) {
				printk(KERN_WARNING "\n[target] open(): out of memory\n");
				status = -ENOMEM;
			}
		}
		if (status == 0) {
			//bfspi->users++;
			//PRINTK("kern>> Open device %d, %d Succses!!\n", mj, mn);
			filp->private_data = mbfspi;
			nonseekable_open(inode, filp);
			status = 0;
		}
	} else
		PRINTK("\nkern>> Open: nothing for minor %d\n", iminor(inode));

	mutex_unlock(&device_list_lock);
	return status;
}

static int bfspi_release( struct inode *inode, struct file *filp ) 
{
	struct bfspi_data	*mbfspi;
	int			status = 0;

	mutex_lock(&device_list_lock);
	mbfspi = filp->private_data;
	filp->private_data = NULL;

	/* last close? */
	//bfspi->users--;
	//if (!bfspi->users) 
	{
	//	int		dofree;

		kfree(mbfspi->buffer);
		mbfspi->buffer = NULL;

		/* ... after we unbound from the underlying device? */
		//spin_lock_irq(&bfspi->spi_lock);
		//dofree = (bfspi->spi == NULL);
		//spin_unlock_irq(&bfspi->spi_lock);

		//if (dofree)
		//kfree(mbfspi);
	}
	mutex_unlock(&device_list_lock);

	return status;
}

static ssize_t bfspi_read( struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
	struct bfspi_data	*mbfspi;
	ssize_t			status = 0;
	ssize_t i;
	uint16_t	missing = 0;
	/* chipselect only toggles at start or end of operation */
	if (count > bufsiz)
		return -EMSGSIZE;

	mbfspi = filp->private_data;
	PRINTK("\nkern>> (%d) Read: %d started\n", rdcount, count);
	if (mbfspi) {
		mutex_lock(&mbfspi->buf_lock);
		bfspi_write_8_bits(SPI_NCSB, (u8)mbfspi->portno);
		for (i=0; i < count; i++)
			mbfspi->buffer[i] = bfspi_read_8_bits(SPI_NCSA);
		status = i;
		if (i > 0) {
			missing = copy_to_user(buf, (char *)mbfspi->buffer, i);
			if (missing < 0)
				status = -EFAULT;
			else {
				status = i - missing;
				*f_pos += status;
				//count = status;
			}
		}
		PRINTK("\nkern>> (%d) Read: %d <> %d : %d : %d : %d Finished\n", rdcount, mbfspi->buffer[0], i, count, status, missing);
		mutex_unlock(&mbfspi->buf_lock);
	} else {
		PRINTK("\nkern>> (%d) Read: %d Buffer Error!!\n", rdcount, count);
	}
	rdcount++;
	return status;
}

static ssize_t bfspi_write( struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
	struct bfspi_data	*mbfspi;
	ssize_t			status = 0;
	int		missing = 0;
	int i,cn = 0, cnt;
	u8 dout = 0;
	
	
	//PRINTK("\nkern>> (%d) Write: %d started", wrcount, count);
	/* chipselect only toggles at start or end of operation */
	if (count > bufsiz) {
		PRINTK("\nkern>> (%d) write too much data count %d : %d\n", wrcount, count, bufsiz);
		return -EMSGSIZE;
	}
	cnt = count;
	i = 0;
	mbfspi = filp->private_data;
	if (mbfspi) {
		mutex_lock(&mbfspi->buf_lock);
		missing = copy_from_user(mbfspi->buffer, buf, cnt);
		//PRINTK("\nkern>> Write: %04X Count : %d : %d, CS: %d portno: %04X\n",(int) mbfspi->buffer[0], cnt, missing, SPI_NCSB, mbfspi->portno);
		if (missing >= 0) {
			cn = cnt - missing;
			//PRINTK("\nkern>> (%d) Write: Count : %d : %d, CS: %d portno: %04X\n", wrcount, count, missing, SPI_NCSB, mbfspi->portno);
			bfspi_write_8_bits(SPI_NCSB, (u8)mbfspi->portno);
			while (i < cn) {
				dout = mbfspi->buffer[i];
				//if (dout >= 0x30) dout = dout - 0x30;
				//PRINTK("\nkern>> (%d) Write: count: %d, CS: %d data[%d]: %04X\n", wrcount, cn, SPI_NCSA, i, dout);
				bfspi_write_8_bits(SPI_NCSA, (u8)(dout));
				i++;
				//PRINTK("kern>> b.Write: count: %d, CS: %d data[%d]: %04X\n",cn, SPI_NCSA, i, dout);
			}
			*f_pos += cn;
			status = cn;
		} else
			status = -EFAULT;
		mutex_unlock(&mbfspi->buf_lock);
		
	} else {
		PRINTK("\nkern>> (%d) writing function can not find the data port!!!\n", wrcount);
		status = -EFAULT;
	}
	//PRINTK("\nkern>> ..DONE.. (%d) Write: count: %d, CS: %d data[%d]: %04X\n", wrcount, cn, SPI_NCSA, i, dout);
	wrcount++;
	return status;
}
//static int bfspi_probe( struct bfspi_device *spi);
//static int __devexit bfspi_remove(struct bfspi_device *spi);
#if 0
static struct spi_driver bfspi_spi_driver = {
	.driver = {
		.name =		"bfspi",
		.owner =	THIS_MODULE,
	},
	.probe =	bfspi_probe,
	.remove =	__devexit_p(bfspi_remove),

	/* NOTE: suspend/resume methods are not necessary here.
	 * We don't do anything except pass the requests to/from
	 * the underlying controller.  The refrigerator handles
	 * most issues; the controller driver handles the rest.
	 */
};
#endif

static const struct file_operations bfspi_fops = {
	.owner =	THIS_MODULE,
	/* REVISIT switch to aio primitives, so that userspace
	 * gets more complete API coverage.  It'll simplify things
	 * too, except for the locking.
	 */
	.write =	bfspi_write,
	.read =		bfspi_read,
	.open =		bfspi_open,
	.release =	bfspi_release,
};

static struct class *bfspi_class;

/* 
   new_chip_select_mask: the logical OR of all the chip selects we wish
   to use for SPI, for example if we wish to use SPISEL2 and SPISEL3
   chip_select_mask = (1<<2) | (1<<3).

   baud:  The SPI clk divider value, see Blackfin Hardware data book,
   maximum speed when baud = 2, minimum when baud = 0xffff (0 & 1
   disable SPI port).

   The maximum SPI clk for the Si Labs 3050 is 16.4MHz.  On a 
   100MHz system clock Blackfin this means baud=4 minimum (12.5MHz).

   For the IP04 some extra code needed to be added to the three SPI
   routines to handle the use of PF12 as nCSB.  It's starting to 
   look a bit messy and is perhaps inefficient.
*/
u16 chip_select_mask;

static u16 hz_to_spi_baud(u32 speed_hz)
{
	u_long sclk = get_sclk();
	u16 spi_baud = (sclk / (2 * speed_hz));

	if ((sclk % (2 * speed_hz)) > 0)
		spi_baud++;

	if (spi_baud < MIN_SPI_BAUD_VAL)
		spi_baud = MIN_SPI_BAUD_VAL;

	return spi_baud;
}

void bfspi_hardware_init(int baud, u16 new_chip_select_mask) 
{
	u16 ctl_reg, flag;
	int cs, bit;

  	if (baud < 4) {
    		printk("\nkern>>baud = %d may mean SPI clock too fast for Si labs 3050"
	   		"consider baud == 4 or greater", baud);
  	}

	PRINTK("\nkern>> bfspi_spi_init\n");
	PRINTK("kern>>   new_chip_select_mask = 0x%04x\n", new_chip_select_mask);
#if (defined(CONFIG_BF533) || defined(CONFIG_BF532))
	PRINTK("kern>>   FIOD_DIR = 0x%04x\n", bfin_read_FIO_DIR());
#endif

#if (defined(CONFIG_BF536) || defined(CONFIG_BF537))
	PRINTK("  FIOD_DIR = 0x%04x\n", bfin_read_PORTFIO_DIR());
#endif
	/* grab SPISEL/GPIO pins for SPI, keep level of SPISEL pins H */
	chip_select_mask |= new_chip_select_mask;

	flag = 0xff00 | (chip_select_mask & 0xff);

	/* set up chip selects greater than PF7 */

  	if (chip_select_mask & 0xff00) {
#if (defined(CONFIG_BF533) || defined(CONFIG_BF532))
	  bfin_write_FIO_DIR(bfin_read_FIO_DIR() | (chip_select_mask & 0xff00)); 
#endif
#if (defined(CONFIG_BF536) || defined(CONFIG_BF537))
	bfin_write_PORTFIO_DIR(bfin_read_PORTFIO_DIR() | (chip_select_mask & 0xff00));
#endif
   	  __builtin_bfin_ssync();
	}
#if (defined(CONFIG_BF533) || defined(CONFIG_BF532))
	PRINTK("kern>>   After FIOD_DIR = 0x%04x\n", bfin_read_FIO_DIR());
#endif

#if (defined(CONFIG_BF536) || defined(CONFIG_BF537))
	PRINTK("  After FIOD_DIR = 0x%04x\n",bfin_read_PORTFIO_DIR());

	/* we need to work thru each bit in mask and set the MUX regs */

	for(bit=0; bit<8; bit++) {
	  if (chip_select_mask & (1<<bit)) {
	    PRINTK("SPI CS bit: %d enabled\n", bit);
	    cs = bit;
	    if (cs == 1) {
	      PRINTK("set for chip select 1\n");
	      bfin_write_PORTF_FER(bfin_read_PORTF_FER() | 0x3c00);
	      __builtin_bfin_ssync();

	    } else if (cs == 2 || cs == 3) {
	      PRINTK("set for chip select 2\n");
	      bfin_write_PORT_MUX(bfin_read_PORT_MUX() | PJSE_SPI);
	      __builtin_bfin_ssync();
	      bfin_write_PORTF_FER(bfin_read_PORTF_FER() | 0x3800);
	      __builtin_bfin_ssync();

	    } else if (cs == 4) {
	      bfin_write_PORT_MUX(bfin_read_PORT_MUX() | PFS4E_SPI);
	      __builtin_bfin_ssync();
	      bfin_write_PORTF_FER(bfin_read_PORTF_FER() | 0x3840);
	      __builtin_bfin_ssync();

	    } else if (cs == 5) {
	      bfin_write_PORT_MUX(bfin_read_PORT_MUX() | PFS5E_SPI);
	      __builtin_bfin_ssync();
	      bfin_write_PORTF_FER(bfin_read_PORTF_FER() | 0x3820);
	      __builtin_bfin_ssync();

	    } else if (cs == 6) {
	      bfin_write_PORT_MUX(bfin_read_PORT_MUX() | PFS6E_SPI);
	      __builtin_bfin_ssync();
	      bfin_write_PORTF_FER(bfin_read_PORTF_FER() | 0x3810);
	      __builtin_bfin_ssync();

	    } else if (cs == 7) {
	      bfin_write_PORT_MUX(bfin_read_PORT_MUX() | PJCE_SPI);
	      __builtin_bfin_ssync();
	      bfin_write_PORTF_FER(bfin_read_PORTF_FER() | 0x3800);
	      __builtin_bfin_ssync();
	    }
	  }
	}
#endif

  	/* note TIMOD = 00 - reading SPI_RDBR kicks off transfer */
  	//Undefines flags lets patch it for now. BFSI is kind of obsolate. 
	//Will be replaced in teh future
	ctl_reg = 0xD004;   //0101 1100 0000  0100  SPE | MSTR | CPOL | CPHA | SZ;
	ctl_reg |= (spimode << 10);
  	bfin_write_SPI_FLG(flag);
  	bfin_write_SPI_BAUD(baud);
  	bfin_write_SPI_CTL(ctl_reg);
}

/*-------------------------- RESET FUNCTION ----------------------------*/

void bfspi_reset(int reset_bit) {
	PRINTK("toggle reset\n");
  
#if (defined(CONFIG_BF533) || defined(CONFIG_BF532))
       	PRINTK("set reset to PF%d\n",reset_bit);
  	bfin_write_FIO_DIR(bfin_read_FIO_DIR() | (1<<reset_bit)); 
  	__builtin_bfin_ssync();

  	bfin_write_FIO_FLAG_C((1<<reset_bit)); 
  	__builtin_bfin_ssync();
  	udelay(100);

  	bfin_write_FIO_FLAG_S((1<<reset_bit));
  	__builtin_bfin_ssync();
#endif
  	
#if (defined(CONFIG_BF536) || defined(CONFIG_BF537))
	if (reset_bit == 1) {
       		PRINTK("set reset to PF10\n");
                bfin_write_PORTF_FER(bfin_read_PORTF_FER() & 0xFBFF);
		__builtin_bfin_ssync();
		bfin_write_PORTFIO_DIR(bfin_read_PORTFIO_DIR() | 0x0400);
		__builtin_bfin_ssync();
		bfin_write_PORTFIO_CLEAR(1<<10);
		__builtin_bfin_ssync();
		udelay(100);
		bfin_write_PORTFIO_SET(1<<10);
		__builtin_bfin_ssync();
        } else if (reset_bit == 2)  {
                PRINTK("Error: cannot set reset to PJ11\n");
        } else if (reset_bit == 3) {
                PRINTK("Error: cannot set reset to PJ10\n");
        } else if (reset_bit == 4) {
                PRINTK("set reset to PF6\n");
                bfin_write_PORTF_FER(bfin_read_PORTF_FER() & 0xFFBF);
                __builtin_bfin_ssync();
		bfin_write_PORTFIO_DIR(bfin_read_PORTFIO_DIR() | 0x0040);
		__builtin_bfin_ssync();
		bfin_write_PORTFIO_CLEAR(1<<6);
		__builtin_bfin_ssync();
		udelay(100);
		bfin_write_PORTFIO_SET(1<<6);
		__builtin_bfin_ssync();
        } else if (reset_bit == 5) {
                PRINTK("set reset to PF5\n");
                bfin_write_PORTF_FER(bfin_read_PORTF_FER() & 0xFFDF);
                __builtin_bfin_ssync();
		bfin_write_PORTFIO_DIR(bfin_read_PORTFIO_DIR() | 0x0020);
		__builtin_bfin_ssync();
		bfin_write_PORTFIO_CLEAR(1<<5);
		__builtin_bfin_ssync();
		udelay(100);
		bfin_write_PORTFIO_SET(1<<5);
		__builtin_bfin_ssync();
        } else if (reset_bit == 6) {
                PRINTK("set reset to PF4\n");
                bfin_write_PORTF_FER(bfin_read_PORTF_FER() & 0xFFEF);
                __builtin_bfin_ssync();
		bfin_write_PORTFIO_DIR(bfin_read_PORTFIO_DIR() | 0x0010);
		__builtin_bfin_ssync();
		bfin_write_PORTFIO_CLEAR(1<<4);
		__builtin_bfin_ssync();
		udelay(100);
		bfin_write_PORTFIO_SET(1<<4);
		__builtin_bfin_ssync();
        } else if (reset_bit == 7) {
                PRINTK("Error: cannot set reset to PJ5\n");

		} else if (reset_bit == 8) {

			PRINTK("Using PF8 for reset...\n");
			bfin_write_PORTF_FER(bfin_read_PORTF_FER() & 0xFEFF);
			__builtin_bfin_ssync();
			bfin_write_PORTFIO_DIR(bfin_read_PORTFIO_DIR() | 0x0100);
			__builtin_bfin_ssync();
			bfin_write_PORTFIO_CLEAR(1<<8);
			__builtin_bfin_ssync();
			udelay(100);
			bfin_write_PORTFIO_SET(1<<8);

		} else if ( reset_bit == 9 ) {
			PRINTK("Using PF9 for reset...\n");
			bfin_write_PORTF_FER(bfin_read_PORTF_FER() & 0xFDFF);
			__builtin_bfin_ssync();
			bfin_write_PORTFIO_DIR(bfin_read_PORTFIO_DIR() | 0x0200);
			__builtin_bfin_ssync();
			bfin_write_PORTFIO_CLEAR(1<<9);
			__builtin_bfin_ssync();
			udelay(100);
			bfin_write_PORTFIO_SET(1<<9);


		}

#endif	
  /* 
     p24 3050 data sheet, allow 1ms for PLL lock, with
     less than 1ms (1000us) I found register 2 would have
     a value of 0 rather than 3, indicating a bad reset.
  */
  udelay(1000); 
}

#if 0
static int bfspi_probe( struct bfspi_device *spi) {
	int			status;
	struct bfspi_data	*bfspi;
	unsigned long		minor;

	/* Allocate driver data */
	bfspi = kzalloc(sizeof(*bfspi), GFP_KERNEL);
	if (!bfspi)
		return -ENOMEM;

	/* Initialize the driver data */
	bfspi->spi = spi;
	spin_lock_init(&bfspi->spi_lock);
	mutex_init(&bfspi->buf_lock);

	INIT_LIST_HEAD(&bfspi->device_entry);

	/* If we can allocate a minor number, hook up this device.
	 * Reusing minors is fine so long as udev or mdev is working.
	 */
	mutex_lock(&device_list_lock);
	minor = find_first_zero_bit(minors, N_BFSPI_MINORS);
	if (minor < N_BFSPI_MINORS) {
		struct device *dev;

		bfspi->devt = MKDEV(BFSPI_MAJOR, minor);
		dev = device_create(bfspi_class, &spi->dev, bfspi->devt,
				    bfspi, "bfspi%d.%d",
				    spi->master->bus_num, spi->chip_select);
		status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
	} else {
		PRINTK("no minor number available!\n");
		status = -ENODEV;
	}
	if (status == 0) {
		set_bit(minor, minors);
		list_add(&bfspi->device_entry, &device_list);
	}
	mutex_unlock(&device_list_lock);

	if (status == 0)
		spi_set_drvdata(spi, bfspi);
		//spi->drv_data = bfspi;
	else
		kfree(bfspi);
	
	return status;	
}
#endif

#if 0
static int __devexit bfspi_release()
{
	//struct bfspi_data	*bfspi = (struct bfspi_data	*)spi->drv_data;

	/* make sure ops on existing fds can abort cleanly */
	//spin_lock_irq(&bfspi->spi_lock);
	//bfspi->spi = NULL;
	//spi_set_drvdata(spi, NULL);
	//spin_unlock_irq(&bfspi->spi_lock);

	/* prevent new opens */
	mutex_lock(&device_list_lock);
	//list_del(&bfspi->device_entry);
	device_destroy(bfspi_class, bfspi->devt);
	clear_bit(MINOR(bfspi->devt), minors);
	if (bfspi->users == 0)
		kfree(bfspi);
	mutex_unlock(&device_list_lock);

	return 0;
}

#endif

static int __init bfspi_init( void ) {
	int	status, ret;
	int i, msz;
	dev_t dev = 0;
	struct bfspi_data	*mbfspi;
	int baud;
	#if 1
	
	wrcount = rdcount = 0;
	baud = hz_to_spi_baud((u32)spibaud*1000);
	if (baud <= 4)
		baud = 48;
	if (cardno < 0)
		cardno = 0;
	if (spimode > 3) 
		spimode = 0;
	msz = sizeof( struct bfspi_data);
	/* Allocate driver data */
	for (i=0; i < MAX_BFSPI_CLIENT; i++) {
		bfspi[i] = kzalloc(sizeof(struct bfspi_data), GFP_KERNEL);
		if (!bfspi[i]) {
			while (--i >= 0) {
				kfree(bfspi[i]);
			}
			return -ENOMEM;
		}
	}
		
	//BUILD_BUG_ON(N_BFSPI_MINORS > 256);
	PRINTK("\nkern>> INIT: alloc bfspi device cardno: %d!, baud: %d, mode: %d\n", cardno, spibaud, spimode);
	if ((ret = alloc_chrdev_region(&dev, 1, MAX_BFSPI_CLIENT, "bfspi")) < 0)
	{
		PRINTK("kern>> INIT: alloc bfspi device Failure to alloc chardev!\n");
		return ret;
	}
	
	BFSPI_MAJOR = MAJOR(dev);

	bfspi_class = class_create(THIS_MODULE, "bfspi");
	if (IS_ERR(bfspi_class)) {
		unregister_chrdev_region(dev, 1);
		PRINTK("kern>> INIT: alloc bfspi device Failure to register chardev!\n");
		return PTR_ERR(bfspi_class);
	}
	/* Initialize the driver data */
	//bfspi->spi = spi;
	

	//INIT_LIST_HEAD(&bfspi->device_entry);

	/* If we can allocate a minor number, hook up this device.
	 * Reusing minors is fine so long as udev or mdev is working.
	 */
	mutex_lock(&device_list_lock);
	for (i=0; i < MAX_BFSPI_CLIENT; i++) {
		mbfspi = bfspi[i];
		//minor = find_first_zero_bit(minors, N_BFSPI_MINORS);
		//if (minor < N_BFSPI_MINORS) {
		struct device *device;
		
		spin_lock_init(&mbfspi->spi_lock);
		mutex_init(&mbfspi->buf_lock);
		cdev_init(&mbfspi->cdev, &bfspi_fops);
		mbfspi->cdev.owner = THIS_MODULE;
		mbfspi->cardno = cardno;
		portno = mbfspi->portno = i+1;
		
		mbfspi->devt = MKDEV(BFSPI_MAJOR, i+1);
		
		cdev_add(&mbfspi->cdev, mbfspi->devt, 1);
		
		device = device_create(bfspi_class, NULL, mbfspi->devt, NULL,
							"bfspi%d.%d", cardno, portno);
							
		status = IS_ERR(device) ? PTR_ERR(device) : 0;
		if (status == 0)
			PRINTK("kern>> init: allocate %d bfspi%d.%d Succses!\n", BFSPI_MAJOR, cardno, portno);
	}
	mutex_unlock(&device_list_lock);
	#endif
	
	//if (spibaud <= 0)
	//	spibaud = 10;

	bfspi_hardware_init(baud, ((1 << SPI_NCSA) | (1 << SPI_NCSB)));
	//bfspi_reset(RESET_BIT);
	return status;
}

static void __exit bfspi_exit(void)
{
//	spi_unregister_driver(&bfspi_spi_driver);
	int i;

	for (i=0; i < MAX_BFSPI_CLIENT; i++) {
		device_destroy(bfspi_class, MKDEV(BFSPI_MAJOR, i+1));
		kfree(bfspi[i]->buffer);
		kfree(bfspi[i]);
	}
	class_destroy(bfspi_class);
	unregister_chrdev_region(MKDEV(BFSPI_MAJOR,0), MAX_BFSPI_CLIENT);
}

module_param(cardno, int, 0600);
module_param(portno, int, 0600);
module_param(spibaud, int, 0600);
module_param(spimode, int, 0600);

module_init(bfspi_init);
module_exit(bfspi_exit);

MODULE_AUTHOR("Thomas Triadi, <netson99@gmail.com>");
MODULE_DESCRIPTION("User mode BFSPI device interface");
