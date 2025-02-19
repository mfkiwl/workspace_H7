#include "common_inc.h"
#include "ascii_processor.hpp"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#include "usb_device.h"
#include "interface_usb.hpp"

osThreadId_t usbServerTaskHandle;
USBStats_t usb_stats_ = {0};

class USBSender : public PacketSink
{
public:
    USBSender(const osSemaphoreId &sem_usb_tx)
        : sem_usb_tx_(sem_usb_tx)
    {}

    int process_packet(uint8_t *buffer, size_t length) override
    {
        // cannot send partial packets
#if 0
        if (length > CDC_DATA_FS_MAX_PACKET_SIZE)
            return -1;
#endif
        // wait for USB interface to become ready
        if (osSemaphoreAcquire(sem_usb_tx_, PROTOCOL_SERVER_TIMEOUT_MS) != osOK)
        {
            // If the host resets the device it might be that the TX-complete handler is never called
            // and the sem_usb_tx_ semaphore is never released. To handle this we just override the
            // TX buffer if this wait times out. The implication is that the channel is no longer lossless.
            // TODO: handle endpoint reset properly
            usb_stats_.tx_overrun_cnt++;
        }

        // transmit packet
        uint8_t status = CDC_Transmit_FS(const_cast<uint8_t *>(buffer), length);
        if (status != USBD_OK)
        {
            osSemaphoreRelease(sem_usb_tx_);
            return -1;
        }
        usb_stats_.tx_cnt++;

        return 0;
    }

private:
    const osSemaphoreId &sem_usb_tx_;
};

// Note we could have independent semaphores here to allow concurrent transmission
USBSender usb_packet_output_cdc(sem_usb_tx);

class TreatPacketSinkAsStreamSink : public StreamSink
{
public:
    TreatPacketSinkAsStreamSink(PacketSink &output) : output_(output)
    {}

    int process_bytes(uint8_t *buffer, size_t length, size_t *processed_bytes)
    {
#if 0
        // Loop to ensure all bytes get sent
        while (length)
        {
            size_t chunk = length < CDC_DATA_FS_MAX_PACKET_SIZE ? length : CDC_DATA_FS_MAX_PACKET_SIZE;
            if (output_.process_packet(buffer, length) != 0)
                return -1;
            buffer += chunk;
            length -= chunk;
            if (processed_bytes)
                *processed_bytes += chunk;
        }
#else
        if (output_.process_packet(buffer, length) != 0)
            return -1;
#endif
        return 0;
    }

    size_t get_free_space()
    { return SIZE_MAX; }

private:
    PacketSink &output_;

/**
 * 构造 usb_stream_output 对象时构造函数将 usb_packet_output_cdc 对象传给引用类型成员 output_ ，
 * 那么成员函数 process_bytes() 调用的 output_.process_packet ，是 usb_packet_output_cdc 对象的虚函数
 */
} usb_stream_output(usb_packet_output_cdc);


// This is used by the printf feature. Hence the above statics, and below seemingly random ptr (it's externed)
// TODO: less spaghetti code
/**
 * 指向虚基类 StreamSink 的指针指向派生类 TreatPacketSinkAsStreamSink 对象 usb_stream_output
 */
StreamSink *usb_stream_output_ptr = &usb_stream_output;

struct USBInterface
{
    uint8_t *rx_buf = nullptr;
    uint32_t rx_len = 0;
    bool data_pending = false;
    uint8_t out_ep;
    uint8_t in_ep;
    USBSender &usb_sender;
};

// Note: statics make this less modular.
// Note: we use a single rx semaphore and loop over data_pending to allow a single pump loop thread
static USBInterface CDC_interface = {
    .rx_buf = nullptr,
    .rx_len = 0,
    .data_pending = false,
    .out_ep = CDC_OUT_EP,
    .in_ep = CDC_IN_EP,
    .usb_sender = usb_packet_output_cdc,
};

static void UsbServerTask(void *ctx)
{
    (void) ctx;

    for (;;)
    {
        // const uint32_t usb_check_timeout = 1; // ms
        osStatus sem_stat = osSemaphoreAcquire(sem_usb_rx, osWaitForever);
        if (sem_stat == osOK)
        {
            usb_stats_.rx_cnt++;

            // CDC Interface
            if (CDC_interface.data_pending)
            {
                CDC_interface.data_pending = false;
                // 不能使用ASCII_protocol_parse_stream()，因为PPK上位机发送命令末尾无追加，而该函数以'\r'、'\n'或'!'来判断一次接收数据完毕
//                ASCII_protocol_parse_stream(CDC_interface.rx_buf, CDC_interface.rx_len, usb_stream_output);
                ASCII_protocol_process_line(CDC_interface.rx_buf, CDC_interface.rx_len, usb_stream_output);
                USBD_CDC_ReceivePacket(&hUsbDeviceFS);  // Allow next packet
            }
        }
    }
}

// Called from CDC_Receive_FS callback function, this allows the communication
// thread to handle the incoming data
void usb_rx_process_packet(uint8_t *buf, uint32_t len)
{
    // We don't allow the next USB packet until the previous one has been processed completely.
    // Therefore it's safe to write to these vars directly since we know previous processing is complete.
	CDC_interface.rx_buf = buf;
	CDC_interface.rx_len = len;
	CDC_interface.data_pending = true;
    osSemaphoreRelease(sem_usb_rx);
}

const uint32_t usbServerTaskStackSize = 512 * 4;
const osThreadAttr_t usbServerTask_attributes = {
    .name = "UsbServerTask",
    .stack_size = usbServerTaskStackSize,
    .priority = (osPriority_t) osPriorityAboveNormal,
};

void StartUsbServer()
{
    // Start USB communication thread
    usbServerTaskHandle = osThreadNew(UsbServerTask, nullptr, &usbServerTask_attributes);
}
