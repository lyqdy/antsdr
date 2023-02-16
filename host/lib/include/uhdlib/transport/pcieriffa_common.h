//
// Created by jcc on 23-2-14.
//

#include <uhd/config.hpp>
#include <uhd/exception.hpp>
#include <uhd/rfnoc/constants.hpp>
#include <uhd/types/device_addr.hpp>
#include <uhd/utils/log.hpp>
#include <uhdlib/transport/links.hpp>
#include <uhdlib/utils/narrow.hpp>
#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <thread>
#include "riffa.h"

namespace uhd {namespace transport{
    constexpr size_t MAX_PCIE_MTU = 9600;
    constexpr size_t PCIE_DEFAULT_NUM_FRAMES = 1;
    constexpr size_t PCIE_DEFAULT_FRAME_SIZE = 8000;
    constexpr size_t PCIE_DEFAULT_BUFF_SIZE = 25000000;

    UHD_INLINE size_t recv_pcieriffa_packet(
            fpga_t *fpga, int chan,void *mem, size_t frame_size, int32_t timeout_ms)
    {
        ssize_t len;
        if(timeout_ms == 0)
            timeout_ms = 10;
        len = uhd::narrow_cast<ssize_t>(fpga_recv(fpga,chan,(char*)mem,frame_size / 4,timeout_ms));

        if(len == 0){
            return 0;
        }
        if(len < 0){
            throw uhd::io_error(
                    str(boost::format("recv error on pcie:%s") % strerror(errno)));
        }
        return len;
    }

    UHD_INLINE void send_pcieriffa_packet(fpga_t *fpga,int chan, void *mem, size_t len)
    {
        while(true){
            const ssize_t ret =
                    uhd::narrow_cast<ssize_t>(fpga_send(fpga,chan,mem,len / 4,0,0,10));
            if((ret*4) == ssize_t(len))
                break;
            if(ret == -1 and errno == ENOBUFS){
                std::this_thread::sleep_for(std::chrono::microseconds(1));
                continue; // try to send again
            }
            if(ret == 0){
                throw uhd::io_error(
                        str(boost::format("send error on pcieriffa: %s") % strerror(errno))
                );
            }
            UHD_ASSERT_THROW((ret*4) == ssize_t(len));
        }
    }
}}
