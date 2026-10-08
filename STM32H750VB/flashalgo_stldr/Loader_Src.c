/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <string.h>

#include "main.h"
#include "quadspi.h"
#include "gpio.h"
#include "w25q64jv.h"

/* Private typedef -----------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

extern void SystemClock_Config(void);

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    return HAL_OK;
}

void HAL_Delay(uint32_t Delay)
{
    volatile int i=0;
    for (i=0; i < 4096; i++);
}

uint32_t HAL_GetTick(void)
{
    volatile int i;
    for (i=0; i < 512; i++);
    uwTick++;
    return uwTick;
}

/*******************************************************************************
 Description :																			         
 Write data to the device	 														       
 Inputs :																					           
 				Address 	: Write location  										     
 				Size 		: Length in bytes 										     
 				buffer 		: Address where to get the data to write	 
 outputs :																				           
 				"1" 	        : Operation succeeded								       
 Info :																						           
 Note : Mandatory for all types except SRAM and PSRAM			   
********************************************************************************/
int Write(uint32_t Address, uint32_t Size, uint8_t *buffer)
{
    HAL_QSPI_DeInit(&hqspi);
    MX_QUADSPI_Init();
    QSPI_W25Q64JV_Reset();
    
    if (QSPI_W25Q64JV_Write(buffer, Address & 0x0fffffff, Size) != QSPI_OK)
    {
        return 0;
    }
    for (volatile uint32_t i = 0; i < 999; i++)
    {
        ;
    }
    QSPI_W25Q64JV_EnableMemoryMappedMode();
    return 1;
}

/*******************************************************************************
 Description :																			
 Erase a full sector in the device 									
 Inputs :																					  
 				SectrorAddress	: Start of sector 					
 outputs :																				  
 				"1" : Operation succeeded										
 				"0" : Operation failure											
 Note : Not Mandatory for SRAM PSRAM and NOR_FLASH		
********************************************************************************/
int SectorErase(uint32_t EraseStartAddress, uint32_t EraseEndAddress)
{
    HAL_QSPI_DeInit(&hqspi);
    MX_QUADSPI_Init();
    QSPI_W25Q64JV_Reset();
    
    uint32_t BlockAddr;
    EraseStartAddress = EraseStartAddress - EraseStartAddress % W25Q64JV_BLOCK_SIZE;

    while (EraseEndAddress >= EraseStartAddress)
    {
        BlockAddr = EraseStartAddress & 0x0FFFFFFF;
        if (QSPI_W25Q64JV_EraseBlock_32Kx2(BlockAddr) != QSPI_OK)
        {
            return 0;
        }
        EraseStartAddress += W25Q64JV_BLOCK_SIZE;
    }
    for (volatile uint32_t i = 0; i < 999; i++)
    {
        ;
    }
    QSPI_W25Q64JV_EnableMemoryMappedMode();
    return 1;
}

/*******************************************************************************
  * Description :
  * Erase a full sector in the device
  * Inputs    :
  *     None
  * outputs   :
  *     R0             : "1" : Operation succeeded
  * 			         "0" : Operation failure
  * Note: Not Mandatory for SRAM PSRAM and NOR_FLASH
********************************************************************************/
int MassErase(void)
{
#if 1
    HAL_QSPI_DeInit(&hqspi);
    MX_QUADSPI_Init();
    QSPI_W25Q64JV_Reset();

    if (QSPI_W25Q64JV_EraseChip() != QSPI_OK)
    {
        return 0;
    }
    for (volatile uint32_t i = 0; i < 999; i++)
    {
        ;
    }
    QSPI_W25Q64JV_EnableMemoryMappedMode();
    return 1;
#else
    return 0;
#endif
}

/*******************************************************************************
 Description :																			
 Read data from the device	 														
 Inputs :																					
 				Address 	: Write location  										
 				Size 		: Length in bytes 										
 				buffer 		: Address where to get the data to write		
 outputs :																				
 				"1" 		: Operation succeeded								
 				"0" 		: Operation failure										
 Note : Not Mandatory                               
********************************************************************************/
#if 1
int Read(uint32_t Address, uint32_t Size, uint8_t *Buffer)
{
    //HAL_OSPI_MspDeInit(&hospi1);
    //MX_OCTOSPI1_Init();
    //OSPI_W25Q128JV_Reset();
    //OSPI_W25Q128JV_EnableMemoryMappedMode();

    for (volatile int i = 0; i < Size; i++)
    {
        *(uint8_t *)Buffer++ = *(uint8_t *)Address;
        Address++;
    }

    return 1;
}
#else
int Read(uint32_t Address, uint32_t Size, uint8_t *Buffer)
{
    //OSPI_W25Q128JV_Reset();
    OSPI_W25Q128JV_Read(Buffer, Address & 0x0fffffff, Size);

    return 1;
}
#endif

/*******************************************************************************
 Description :																		
 Verify the data 	 														    
 Inputs :																					
 				MemoryAddr 	: Write location  					
 				RAMBufferAddr 	: RAM Address		          
 				Size 		: Length in bytes 								
 outputs :																				
 				"0" 		: Operation succeeded						
 Note : Not Mandatory                             	
********************************************************************************/
#if 1
int Verify(uint32_t MemoryAddr, uint32_t RAMBufferAddr, uint32_t Size)
{
    uint32_t VerifiedData = 0;
    Size *= 4;

    while (Size > VerifiedData)
    {
        if (*(uint8_t *)MemoryAddr++ != *((uint8_t *)RAMBufferAddr + VerifiedData))
            return (MemoryAddr + VerifiedData);

        VerifiedData++;
    }

    return 0;
}
#endif
/*******************************************************************************
 Description :																		
 System initialization										        
 Inputs 	:																        
 				 None 																		
 outputs 	:																				
 				"1" 		: Operation succeeded						
 				"0" 		: Operation failure							
********************************************************************************/
int Init(void)
{
    SystemInit();
    HAL_Init();

    SystemClock_Config();
    //MX_GPIO_Init();

    QUADSPI->CCR = QUADSPI->CCR & 0xf7ffffff; // Indirect read mode
    QUADSPI->CR = QUADSPI->CR & 0xfffffffe;   // Disable the QUADSPI.
    MX_QUADSPI_Init();

    QSPI_W25Q64JV_Reset();
    QSPI_W25Q64JV_EnableMemoryMappedMode();

    return 1;
}
