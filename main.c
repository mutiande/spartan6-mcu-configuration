#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/GPIO.h>

/* TI-RTOS Header files */
#include <ti/drivers/GPIO.h>
#include "inc/hw_types.h"
#include "inc/hw_memmap.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/debug.h"
#include "driverlib/flash.h"
#include "driverlib/crc.h"
#include "driverlib/adc.h"
#include "driverlib/interrupt.h"
#define TARGET_IS_TM4C129_RA2
#include "driverlib/rom.h"

/* Board Header file */
#include "Board.h"

#include "bitstream.h"
#define SPARTAN_FW_SIZE 54664  // in bytes


#define TASKSTACKSIZE   512
Task_Struct task0Struct;
Char task0Stack[TASKSTACKSIZE];



Void configure_spartan(UArg arg0, UArg arg1)
{
    /***********************************************************************************/
    /***************************** [SPARTAN CONFIGURATION] *****************************/
    /***********************************************************************************/

    // Global disable interrupts on programming time
    IntMasterDisable();

    /*
     *  Pin configuration:
     *
     *  PB2 - DIN pin (data)
     *  PL2 - CCLK pin
     *  PE2 - nPROGRAM
     *  PA3 - PROG_B
     *  PL5 - DONE
     */

    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOQ);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOL);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);

    /*
     *  Test configuration uses dedicated pin that activates on successful programming so
     *  we can check it before and after our attempt
     */
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOG);
    GPIOPinTypeGPIOInput(GPIO_PORTG_BASE, GPIO_PIN_0);
    if ( GPIOPinRead(GPIO_PORTG_BASE, GPIO_PIN_0) & (1<<0) ) {
        System_printf("before: PG0 HIGH\n");
    }
    else {
        System_printf("before: PG0 LOW\n");
    }
    System_flush();

    /*
     *  Set configuration pins. They define the source and method of firmware
     *
     *  M0=1 M1=1 M2=1 (internal flash reload)
     */
    GPIOPinTypeGPIOOutput(GPIO_PORTQ_BASE, GPIO_PIN_1);  // M0
    GPIOPinWrite(GPIO_PORTQ_BASE, GPIO_PIN_1, 1<<1);
    GPIOPinTypeGPIOOutput(GPIO_PORTQ_BASE, GPIO_PIN_0);  // M1
    GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_0, 1<<0);
    GPIOPinTypeGPIOOutput(GPIO_PORTQ_BASE, GPIO_PIN_2);  // M2
    GPIOPinWrite(GPIO_PORTQ_BASE, GPIO_PIN_2, 1<<2);

    GPIOPinTypeGPIOOutput(GPIO_PORTA_BASE, GPIO_PIN_3);  // Prog_b
    GPIOPinTypeGPIOInput(GPIO_PORTL_BASE, GPIO_PIN_5);  // Done
    GPIOPadConfigSet(GPIO_PORTL_BASE, GPIO_PIN_5, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);

    GPIOPinTypeGPIOOutput(GPIO_PORTL_BASE, GPIO_PIN_2);  // serial slave clock
    GPIOPinTypeGPIOOutput(GPIO_PORTB_BASE, GPIO_PIN_2);  // serial slave DIN

    // SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    // GPIOPinTypeGPIOOutput(GPIO_PORTE_BASE, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2);
    // GPIOPinTypeGPIOInput(GPIO_PORTE_BASE, GPIO_PIN_3|GPIO_PIN_4);


    uint32_t i;
    uint8_t byte_of_data;
    int8_t j;

    // Holds nPROGRAM LOW
    // GPIOPinWrite(GPIO_PORTE_BASE, GPIO_PIN_2, 0<<2);
    // ROM_SysCtlDelay(40);
    // GPIOPinWrite(GPIO_PORTE_BASE, GPIO_PIN_2, 1<<2);

    // PROG_B pulse (important!)
    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_3, 1<<3);
    ROM_SysCtlDelay(100);
    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_3, 0<<3);
    ROM_SysCtlDelay(100);
    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_3, 1<<3);

    // Wait for nINIT goes HIGH (we can't check this because of no physical routing from TIVA to nINIT)
    ROM_SysCtlDelay(24000);  // delay in CPU cycles (see frequency below)
    // while ( !((1<<3) & GPIOPinRead(GPIO_PORTE_BASE, GPIO_PIN_3)) ) {}
    // System_printf("nINIT went HIGH.\n");
    // System_flush();


    // Flashing loop
    for (i=0; i<SPARTAN_FW_SIZE; ++i) {

        byte_of_data = bitstream_fw[i];

        for (j=0; j<8; ++j) {
            // Set bit of data
            GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_2, (byte_of_data & (1<<(7-j))) ? (1<<2) : (0<<2));

            // Latch bit of data by CCLK impulse
            GPIOPinWrite(GPIO_PORTL_BASE, GPIO_PIN_2, 1<<2);
            ROM_SysCtlDelay(20);
            GPIOPinWrite(GPIO_PORTL_BASE, GPIO_PIN_2, 0<<2);
            ROM_SysCtlDelay(20);
        }
    }

    // Additional cycles (not necessary)
    for (i=0; i<20; ++i) {
        GPIOPinWrite(GPIO_PORTL_BASE, GPIO_PIN_2, 1<<2);
        ROM_SysCtlDelay(20);
        GPIOPinWrite(GPIO_PORTL_BASE, GPIO_PIN_2, 0<<2);
        ROM_SysCtlDelay(20);
    }

    // Not necessary
    // ROM_SysCtlDelay(40000);

    System_printf("Flashing loop complete.\n");
    System_flush();

    // Check DONE pin (should be 1)
    if ( GPIOPinRead(GPIO_PORTL_BASE, GPIO_PIN_5) & (1<<5) ) {
        System_printf("DONE HIGH\n");
        System_flush();
    }

    // This guarantees that configuration complete successfully
    if ( GPIOPinRead(GPIO_PORTG_BASE, GPIO_PIN_0) & (1<<0) )
        System_printf("after: PG0 HIGH\n");
    else
        System_printf("after: PG0 LOW\n");
    System_flush();


    // Enable interrupts back
    IntMasterEnable();

    /***********************************************************************************/
    /**************************** [/SPARTAN CONFIGURATION] *****************************/
    /***********************************************************************************/

    while (1) {
        Task_sleep((unsigned int)arg0);
        // GPIO_toggle(Board_LED0);
    }
}



int main(void)
{
    SysCtlClockFreqSet((SYSCTL_XTAL_25MHZ|SYSCTL_OSC_MAIN|SYSCTL_USE_PLL|SYSCTL_CFG_VCO_480), 120000000);

    /* Call board init functions */
    Board_initGeneral();

    /* Construct programming task */
    Task_Params taskParams;
    Task_Params_init(&taskParams);
    taskParams.arg0 = 1000;
    taskParams.stackSize = TASKSTACKSIZE;
    taskParams.stack = &task0Stack;
    Task_construct(&task0Struct, (Task_FuncPtr)configure_spartan, &taskParams, NULL);

     /* Turn on user LED */
    // GPIO_write(Board_LED0, Board_LED_ON);

    System_printf("Starting the example\nSystem provider is set to SysMin. "
                  "Halt the target to view any SysMin contents in ROV.\n");
    /* SysMin will only print to the console when you call flush or exit */
    System_flush();

    /* Start BIOS */
    BIOS_start();

    return (0);
}
