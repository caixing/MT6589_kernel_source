#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/time.h>
#include "kd_flashlight.h"
#include <asm/io.h>
#include <asm/uaccess.h>
#include "kd_camera_hw.h"
#include <cust_gpio_usage.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/xlog.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
#include <linux/mutex.h>
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#include <linux/semaphore.h>
#else
#include <asm/semaphore.h>
#endif
#endif

//>2013/1/18-20610-jessicatseng, [Pelican] Integrate flashlight IC LM3642 for PRE-MP SW
#include <linux/ctype.h>
#include <linux/i2c.h>
//>2013/1/18-20610-jessicatseng

//>2013/1/18-20610-jessicatseng, [Pelican] Integrate flashlight IC LM3642 for PRE-MP SW
#if defined(FLASHLIGHT_IC_LM3642)
extern struct i2c_client *flashlight_i2c_client;
#endif
//>2013/1/18-20610-jessicatseng

/******************************************************************************
 * Debug configuration
******************************************************************************/
// availible parameter
// ANDROID_LOG_ASSERT
// ANDROID_LOG_ERROR
// ANDROID_LOG_WARNING
// ANDROID_LOG_INFO
// ANDROID_LOG_DEBUG
// ANDROID_LOG_VERBOSE
#define TAG_NAME "leds_strobe.c"
#define PK_DBG_NONE(fmt, arg...)    do {} while (0)
#define PK_DBG_FUNC(fmt, arg...)    xlog_printk(ANDROID_LOG_DEBUG  , TAG_NAME, KERN_INFO  "%s: " fmt, __FUNCTION__ ,##arg)
#define PK_WARN(fmt, arg...)        xlog_printk(ANDROID_LOG_WARNING, TAG_NAME, KERN_WARNING  "%s: " fmt, __FUNCTION__ ,##arg)
#define PK_NOTICE(fmt, arg...)      xlog_printk(ANDROID_LOG_DEBUG  , TAG_NAME, KERN_NOTICE  "%s: " fmt, __FUNCTION__ ,##arg)
#define PK_INFO(fmt, arg...)        xlog_printk(ANDROID_LOG_INFO   , TAG_NAME, KERN_INFO  "%s: " fmt, __FUNCTION__ ,##arg)
#define PK_TRC_FUNC(f)              xlog_printk(ANDROID_LOG_DEBUG  , TAG_NAME,  "<%s>\n", __FUNCTION__);
#define PK_TRC_VERBOSE(fmt, arg...) xlog_printk(ANDROID_LOG_VERBOSE, TAG_NAME,  fmt, ##arg)
#define PK_ERROR(fmt, arg...)       xlog_printk(ANDROID_LOG_ERROR  , TAG_NAME, KERN_ERR "%s: " fmt, __FUNCTION__ ,##arg)


#define DEBUG_LEDS_STROBE
#ifdef  DEBUG_LEDS_STROBE
	#define PK_DBG PK_DBG_FUNC
	#define PK_VER PK_TRC_VERBOSE
	#define PK_ERR PK_ERROR
#else
	#define PK_DBG(a,...)
	#define PK_VER(a,...)
	#define PK_ERR(a,...)
#endif

/******************************************************************************
 * local variables
******************************************************************************/

static DEFINE_SPINLOCK(g_strobeSMPLock); /* cotta-- SMP proection */


static u32 strobe_Res = 0;
static u32 strobe_Timeus = 0;
static BOOL g_strobe_On = 0;
//<2013/05/15-24927-alberthsiao, Update Flash light parameters
static int g_duty=0;
static int g_timeOutTimeMs=0;

//<2013/4/12-23739-jessicatseng, [Pelican] Enable torch mode
static kal_bool g_flash_mode = KAL_FALSE;
//>2013/4/12-23739-jessicatseng

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
static DEFINE_MUTEX(g_strobeSem);
#else
static DECLARE_MUTEX(g_strobeSem);
#endif


#define STROBE_DEVICE_ID 0x60


static struct work_struct workTimeOut;

/*****************************************************************************
Functions
*****************************************************************************/
extern int iWriteRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u16 i2cId);
extern int iReadRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u8 * a_pRecvData, u16 a_sizeRecvData, u16 i2cId);
static void work_timeOutFunc(struct work_struct *data);

//<2013/3/15-22905-jessicatseng, [Pelican] Fix flashlight issues
//<2013/1/18-20610-jessicatseng, [Pelican] Integrate flashlight IC LM3642 for PRE-MP SW
#if defined(FLASHLIGHT_IC_LM3642)
#define GPIO_CAMERA_FLASH_MODE GPIO209
#define GPIO_CAMERA_FLASH_MODE_M_GPIO  GPIO_MODE_00
    /*CAMERA-FLASH-T/F
           H:flash mode
           L:torch mode*/
#define GPIO_CAMERA_FLASH_EN GPIO213
#define GPIO_CAMERA_FLASH_EN_M_GPIO  GPIO_MODE_00
    /*CAMERA-FLASH-EN */

ssize_t gpio_FL_Init(void)
{
	/*set torch mode*/
	if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_MODE,GPIO_CAMERA_FLASH_MODE_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
	if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_MODE,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
	if(mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
	/*Init. to disable*/
	if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_EN,GPIO_CAMERA_FLASH_EN_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
	if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_EN,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
	if(mt_set_gpio_out(GPIO_CAMERA_FLASH_EN,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
	
//<2013/4/12-23739-jessicatseng, [Pelican] Enable torch mode
	INIT_WORK(&workTimeOut, work_timeOutFunc);
//>2013/4/12-23739-jessicatseng

	return 0;
}

ssize_t gpio_FL_Uninit(void)
{
    /*Uninit. to disable*/
	if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_EN,GPIO_CAMERA_FLASH_EN_M_GPIO)){PK_DBG("[constant_flashlight] set gpio mode failed!! \n");}
	if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_EN,GPIO_DIR_OUT)){PK_DBG("[constant_flashlight] set gpio dir failed!! \n");}
	if(mt_set_gpio_out(GPIO_CAMERA_FLASH_EN,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
	return 0;
}

//<2013/4/12-23739-jessicatseng, [Pelican] Enable torch mode
ssize_t gpio_FL_Enable(void)
{
	u8 databuf[2];
	int ret = 0;	

	printk("[gpio_flash] gpio_FL_Enable g_flash_mode = %d\n", g_flash_mode);
	
//<2013/3/8-22644-jessicatseng, [Pelican] Using hwPowerOn()/hwPowerDown() function to enable/disable VGP1 for flashlgith driver
	//pmic_ldo_vol_sel(MT65XX_POWER_LDO_VGP1, VOL_1800);
	//pmic_ldo_enable(MT65XX_POWER_LDO_VGP1, 1);
	hwPowerOn(MT65XX_POWER_LDO_VGP1, VOL_1800, "LM3642_FLASHLIGHT");
//>2013/3/8-22644-jessicatseng	
 
	mdelay(2); 

	//PK_DBG("strobe_width=%d\n",strobe_width);	
	//printk("[constant_flashlight] gpio_FL_Enable strobe_width=%d\n", strobe_width);
//<2013/6/10-25882-alberthsiao,[5860][ATS160881]fix flash enable time issue
	databuf[0] = 0x08;
	databuf[1] = 0x44;//0x47; //0,800ms //ramp :0,time out:500ms
	ret = i2c_master_send(flashlight_i2c_client, (const char*)&databuf, 2);
//>2013/6/10-25882-alberthsiao
	//if(KD_STROBE_HIGH_CURRENT_WIDTH==strobe_width) //switch to flash mode

	if(g_duty==0) //switch to torch mode
	{
		//if(mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ZERO))
		//{
			//PK_DBG("[constant_flashlight] set gpio flash_mode failed!! \n");
		//}
		
		databuf[0] = 0x09;
		databuf[1] = 0x19;
		ret = i2c_master_send(flashlight_i2c_client, (const char*)&databuf, 2);

		databuf[0] = 0x0A;
		databuf[1] = 0x12;//0x02;
		ret = i2c_master_send(flashlight_i2c_client, (const char*)&databuf, 2);

		mt_set_gpio_mode(GPIO_CAMERA_FLASH_MODE,GPIO_CAMERA_FLASH_MODE_M_GPIO);
		mt_set_gpio_dir(GPIO_CAMERA_FLASH_MODE,GPIO_DIR_OUT);
		mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ONE);
	}
	else if(g_duty==1)
	{
		//if(mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ZERO))
		//{
			//PK_DBG("[constant_flashlight] set gpio flash_mode failed!! \n");
		//}

		databuf[0] = 0x09;
		databuf[1] = 0x39;
		ret = i2c_master_send(flashlight_i2c_client, (const char*)&databuf, 2);

		databuf[0] = 0x0A;
		databuf[1] = 0x12;//0x02;
		ret = i2c_master_send(flashlight_i2c_client, (const char*)&databuf, 2);

		mt_set_gpio_mode(GPIO_CAMERA_FLASH_MODE,GPIO_CAMERA_FLASH_MODE_M_GPIO);
		mt_set_gpio_dir(GPIO_CAMERA_FLASH_MODE,GPIO_DIR_OUT);
		mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ONE);
	}
	else //if(g_duty == 2)
	{
		//if(mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ONE))
		//{
			//PK_DBG("[constant_flashlight] set gpio flash_mode failed!! \n");
		//}

		databuf[0] = 0x09;
		databuf[1] = 0x39;//0x03;
		i2c_master_send(flashlight_i2c_client, (const char*)&databuf, 2);

		databuf[0] = 0x0A;
		databuf[1] = 0x03;
		ret = i2c_master_send(flashlight_i2c_client, (const char*)&databuf, 2);

		//g_flash_mode = KAL_FALSE;

		//mt_set_gpio_out(GPIO_CAMERA_FLASH_EN,GPIO_OUT_ONE);
	}

//<2013/3/8-22644-jessicatseng, [Pelican] Using hwPowerOn()/hwPowerDown() function to enable/disable VGP1 for flashlgith driver
	//pmic_ldo_enable(MT65XX_POWER_LDO_VGP1, 0);
	hwPowerDown(MT65XX_POWER_LDO_VGP1, "LM3642_FLASHLIGHT");
//>2013/3/8-22644-jessicatseng	
    
 	/*Enable*/
 	//if(mt_set_gpio_out(GPIO_CAMERA_FLASH_EN,GPIO_OUT_ONE)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
 	return 0;
}
//>2013/4/12-23739-jessicatseng

ssize_t gpio_FL_Disable(void)
{
	u8 databuf[2];
	int ret = 0;	

	printk("[gpio_flash] gpio_FL_Disable\n");

//<2013/3/8-22644-jessicatseng, [Pelican] Using hwPowerOn()/hwPowerDown() function to enable/disable VGP1 for flashlgith driver
	//pmic_ldo_vol_sel(MT65XX_POWER_LDO_VGP1, VOL_1800);
	//pmic_ldo_enable(MT65XX_POWER_LDO_VGP1, 1);
	hwPowerOn(MT65XX_POWER_LDO_VGP1, VOL_1800, "LM3642_FLASHLIGHT");
//>2013/3/8-22644-jessicatseng	

	mdelay(2); 

	databuf[0] = 0x0A;
	databuf[1] = 0x00;
	ret = i2c_master_send(flashlight_i2c_client, (const char*)&databuf, 2);

	mt_set_gpio_out(GPIO_CAMERA_FLASH_EN,GPIO_OUT_ZERO);
	mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ZERO);

//<2013/3/8-22644-jessicatseng, [Pelican] Using hwPowerOn()/hwPowerDown() function to enable/disable VGP1 for flashlgith driver
	//pmic_ldo_enable(MT65XX_POWER_LDO_VGP1, 0);
	hwPowerDown(MT65XX_POWER_LDO_VGP1, "LM3642_FLASHLIGHT");
//>2013/3/8-22644-jessicatseng	

	/*Enable*/
	//if(mt_set_gpio_out(GPIO_CAMERA_FLASH_EN,GPIO_OUT_ZERO)){PK_DBG("[constant_flashlight] set gpio failed!! \n");}
	return 0;
}

ssize_t gpio_FL_Dim_duty(kal_uint8 duty)
{
    PK_DBG("gpio_FL_dim_duty\n");
    /*N/A*/
    if(duty<0)
      duty=0;
    if(duty>2)
      duty=2;
    g_duty = duty;


    return 0;
}

//<2013/4/12-23739-jessicatseng, [Pelican] Enable torch mode
int FL_Flash_Mode(void)
{    
    PK_DBG("FL_Flash_Mode\n");

    g_flash_mode = KAL_TRUE;

    //if(mt_set_gpio_mode(GPIO_CAMERA_FLASH_MODE,GPIO_CAMERA_FLASH_MODE_M_GPIO)){PK_DBG(" set gpio mode failed!! \n");}
    //if(mt_set_gpio_dir(GPIO_CAMERA_FLASH_MODE,GPIO_DIR_OUT)){PK_DBG(" set gpio dir failed!! \n");}
    //if(mt_set_gpio_out(GPIO_CAMERA_FLASH_MODE,GPIO_OUT_ONE)){PK_DBG(" set gpio failed!! \n");}

    //g_WDTTimeout_ms = FLASH_LIGHT_WDT_TIMEOUT_MS;
}
//>2013/4/12-23739-jessicatseng

#define FL_Init 			gpio_FL_Init
#define FL_Uninit	 		gpio_FL_Uninit
#define FL_Enable 		gpio_FL_Enable
#define FL_Disable 		gpio_FL_Disable
#define FL_dim_duty  gpio_FL_Dim_duty

#else

int FL_Enable(void)
{
	char buf[2];
	int reg6;
	int mode_setting=2;
	PK_DBG(" FL_Enable\n");
	if(g_duty>=15)
		mode_setting=3;
	reg6 = 8+mode_setting;
	buf[0]=6;
	buf[1]=reg6;
	iWriteRegI2C(buf , 2, STROBE_DEVICE_ID);
    return 0;
}

int FL_Disable(void)
{

	char buf[2];
	int reg6;
	int mode_setting=2;
	PK_DBG(" FL_Disablexx1\n");
	reg6 = mode_setting;
	buf[0]=6;
	buf[1]=reg6;
	iWriteRegI2C(buf , 2, STROBE_DEVICE_ID);
    return 0;
}

int FL_dim_duty(kal_uint32 duty)
{
	char buf[2];
	int reg1;
    PK_DBG(" strobe duty : %u\n",duty);
    if(duty<0)
    	duty=0;
    if(duty>=31)
    	duty=31;
    g_duty=duty;
    reg1=(g_duty+1)*8;
    if(reg1>255)
    	reg1=255;
    buf[0]=1;
	buf[1]=reg1;
	iWriteRegI2C(buf , 2, STROBE_DEVICE_ID);
    return 0;
}


int FL_Init(void)
{


	//down(&g_strobeSem);
	//up(&g_strobeSem);
	char buf[2];
    //spin_lock(&g_strobeSMPLock);

    PK_DBG(" FL_Init line=%d\n",__LINE__);

    buf[0]=5;
	buf[1]=220;
	iWriteRegI2C(buf , 2, STROBE_DEVICE_ID); //timeout 1s

	buf[0]=7;
	buf[1]=0;
	iWriteRegI2C(buf , 2, STROBE_DEVICE_ID); //strobe_on = 0

	 INIT_WORK(&workTimeOut, work_timeOutFunc);

    //spin_unlock(&g_strobeSMPLock);

    PK_DBG(" FL_Init line=%d\n",__LINE__);
    return 0;
}


int FL_Uninit(void)
{
	FL_Disable();
    return 0;
}
#endif
//>2013/1/18-20610-jessicatseng
//>2013/3/15-22905-jessicatseng
/*****************************************************************************
User interface
*****************************************************************************/

static void work_timeOutFunc(struct work_struct *data)
{
    FL_Disable();
    PK_DBG("ledTimeOut_callback\n");
    //printk(KERN_ALERT "work handler function./n");
}

enum hrtimer_restart ledTimeOutCallback(struct hrtimer *timer)
{
    schedule_work(&workTimeOut);
    return HRTIMER_NORESTART;
}

static struct hrtimer g_timeOutTimer;
void timerInit(void)
{
	g_timeOutTimeMs=1000; //1s
	hrtimer_init( &g_timeOutTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
	g_timeOutTimer.function=ledTimeOutCallback;

}

//<2013/4/12-23739-jessicatseng, [Pelican] Enable torch mode
static int constant_flashlight_ioctl(MUINT32 cmd, MUINT32 arg)
{
	int i4RetValue = 0;
	int ior_shift;
	int iow_shift;
	int iowr_shift;
	ior_shift = cmd - (_IOR(FLASHLIGHT_MAGIC,0, int));
	iow_shift = cmd - (_IOW(FLASHLIGHT_MAGIC,0, int));
	iowr_shift = cmd - (_IOWR(FLASHLIGHT_MAGIC,0, int));
	
	PK_DBG("constant_flashlight_ioctl() line=%d ior_shift=%d, iow_shift=%d iowr_shift=%d arg=%d\n",__LINE__, ior_shift, iow_shift, iowr_shift, arg);
	
    switch(cmd)
    {
    	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
    		PK_DBG("FLASH_IOC_SET_TIME_OUT_TIME_MS: %d\n",arg);
    		g_timeOutTimeMs=arg;
    		break;

    	case FLASH_IOC_SET_DUTY :
    		PK_DBG("FLASHLIGHT_DUTY: %d\n",arg);
    		gpio_FL_Dim_duty(arg);
			   break;


    	case FLASH_IOC_SET_STEP:
    		PK_DBG("FLASH_IOC_SET_STEP: %d\n",arg);
    		break;

    	case FLASH_IOC_SET_ONOFF :
    		PK_DBG("FLASHLIGHT_ONOFF: %d\n",arg);
    		if(arg==1)
    		{
 		       if(g_timeOutTimeMs!=0)
 		       {
	            	ktime_t ktime;
	            	ktime = ktime_set( 0, g_timeOutTimeMs*1000000 );
	            	hrtimer_start( &g_timeOutTimer, ktime, HRTIMER_MODE_REL );
 		       }
 		       FL_Enable();
    		}
    		else
    		{
 		       FL_Disable();
 		       hrtimer_cancel( &g_timeOutTimer );
    		}
    		break;
    		
    	default :
    		PK_DBG(" No such command \n");
    		i4RetValue = -EPERM;
    		break;
    }
    return i4RetValue;
}
//>2013/4/12-23739-jessicatseng
//>2013/05/15-24927-alberthsiao
static int constant_flashlight_open(void *pArg)
{
    int i4RetValue = 0;
    PK_DBG("constant_flashlight_open line=%d\n", __LINE__);

	if (0 == strobe_Res)
	{
	    FL_Init();
		timerInit();
	}
	PK_DBG("constant_flashlight_open line=%d\n", __LINE__);
	spin_lock_irq(&g_strobeSMPLock);


    if(strobe_Res)
    {
        PK_ERR(" busy!\n");
        i4RetValue = -EBUSY;
    }
    else
    {
        strobe_Res += 1;
    }


    spin_unlock_irq(&g_strobeSMPLock);
    PK_DBG("constant_flashlight_open line=%d\n", __LINE__);

    return i4RetValue;

}

static int constant_flashlight_release(void *pArg)
{
    PK_DBG(" constant_flashlight_release\n");

    if (strobe_Res)
    {
        spin_lock_irq(&g_strobeSMPLock);

        strobe_Res = 0;
        strobe_Timeus = 0;

        /* LED On Status */
        g_strobe_On = FALSE;

        spin_unlock_irq(&g_strobeSMPLock);

    	FL_Uninit();
    }

    PK_DBG(" Done\n");

    return 0;

}

FLASHLIGHT_FUNCTION_STRUCT	constantFlashlightFunc=
{
	constant_flashlight_open,
	constant_flashlight_release,
	constant_flashlight_ioctl
};

MUINT32 constantFlashlightInit(PFLASHLIGHT_FUNCTION_STRUCT *pfFunc)
{
    if (pfFunc != NULL)
    {
        *pfFunc = &constantFlashlightFunc;
    }
    return 0;
}


/* LED flash control for high current capture mode*/
ssize_t strobe_VDIrq(void)
{

    return 0;
}

EXPORT_SYMBOL(strobe_VDIrq);


