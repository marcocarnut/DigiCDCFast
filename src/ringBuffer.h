/*******************************************************************************************************************
*
* Ring buffers for DigiCDCFast. Originally LUFA's lightweight ring buffer (see
* http://www.fourwalledcubicle.com/files/LightweightRingBuff.h for its license);
* reduced to 8-bit sizes and indices, and without interrupt locking: DigiCDCFast
* only uses them outside interrupts (V-USB calls usbFunctionWriteOut() from
* usbPoll()).
*
*******************************************************************************************************************/
#include <stdint.h>
/*----------------------------------------------------------------------------------------------------------------*/
typedef struct
{
    uint8_t* Data;  /**< The buffer's storage array. */
    uint8_t  Size;  /**< Its size, 1 to 255 bytes. */
    uint8_t  Out;   /**< Index of the oldest byte. */
    uint8_t  Count; /**< Number of bytes stored. */
} RingBuffer_t;
/*----------------------------------------------------------------------------------------------------------------*/
static inline void RingBuffer_InitBuffer(RingBuffer_t* Buffer, uint8_t* const DataPtr, const uint8_t Size)
{
    Buffer->Data  = DataPtr;
    Buffer->Size  = Size;
    Buffer->Out   = 0;
    Buffer->Count = 0;
}
static inline uint8_t RingBuffer_GetCount(RingBuffer_t* const Buffer)     { return Buffer->Count; }
static inline uint8_t RingBuffer_GetFreeCount(RingBuffer_t* const Buffer) { return Buffer->Size - Buffer->Count; }
static inline uint8_t RingBuffer_IsEmpty(RingBuffer_t* const Buffer)      { return Buffer->Count == 0; }
static inline uint8_t RingBuffer_IsFull(RingBuffer_t* const Buffer)       { return Buffer->Count == Buffer->Size; }
static inline uint8_t RingBuffer_Peek(RingBuffer_t* const Buffer)         { return Buffer->Data[Buffer->Out]; }
/*----------------------------------------------------------------------------------------------------------------*/
static inline void RingBuffer_Insert(RingBuffer_t* Buffer, const uint8_t Data)
{
    uint8_t In = Buffer->Out + Buffer->Count;
    if (In >= Buffer->Size)
        In -= Buffer->Size;
    Buffer->Data[In] = Data;
    Buffer->Count++;
}
/*----------------------------------------------------------------------------------------------------------------*/
static inline uint8_t RingBuffer_Remove(RingBuffer_t* Buffer)
{
    uint8_t Data = Buffer->Data[Buffer->Out];
    if (++Buffer->Out == Buffer->Size)
        Buffer->Out = 0;
    Buffer->Count--;
    return Data;
}
