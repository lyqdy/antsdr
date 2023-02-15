//
// Created by jcc on 23-2-10.
//
#include <uhd/transport/buffer_pool.hpp>
#include <uhd/transport/pcie_riffa_zero_copy.hpp>
#include <uhd/utils/log.hpp>
#include <uhdlib/transport/pcieriffa_common.h>
#include <uhdlib/utils/atomic.hpp>
#include <boost/format.hpp>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>


using namespace uhd;
using namespace uhd::transport;
namespace asio = boost::asio;

class libpcie_zero_copy_asio_mrb : public managed_recv_buffer
{
public:
    libpcie_zero_copy_asio_mrb(fpga_t *fpga,void* mem,int chan,const size_t frame_size)
    : _fpga(fpga), _mem(mem), _chan(chan), _frame_size(frame_size), _len(0)
    {/*NOP*/}

    void release(void) override
    {
        _claimer.release();
    }

    UHD_INLINE sptr get_new(const double timeout, size_t& index)
    {
        if(not _claimer.claim_with_wait(timeout))
            return sptr();
        const int32_t timeout_ms = static_cast<int32_t>(timeout * 1000);
        _len = recv_pcieriffa_packet(_fpga,_chan,_mem,_frame_size / 4,timeout_ms);
//        UHD_LOGGER_INFO("U220")
//        << "get_new:recv_pcieriffa_packet "<<_len * 4;
        if(_len > 0){
            index++;
            return make(this,_mem,size_t(_len) * 4);
        }

        _claimer.release();
        return sptr();
    }

private:
    void* _mem;
    int _chan;
    size_t _frame_size;
    ssize_t _len;
    simple_claimer _claimer;
    fpga_t *_fpga;
};

class libpcie_zero_copy_asio_msb : public managed_send_buffer
{
public:
    libpcie_zero_copy_asio_msb(fpga_t *fpga,int chan,void* mem,const size_t frame_size)
    : _fpga(fpga), _chan(chan), _mem(mem), _frame_size(frame_size)
    {
        /* NOP */
    }

    void release(void) override
    {
        send_pcieriffa_packet(_fpga,_chan,_mem,_frame_size / 4);
        _claimer.release();
    }

    UHD_INLINE sptr get_new(const double timeout, size_t& index)
    {
        if (not _claimer.claim_with_wait(timeout))
            return sptr();
        index++;
        return make(this,_mem,_frame_size);
    }
private:
    fpga_t *_fpga;
    void* _mem;
    int _chan;
    size_t _frame_size;
    simple_claimer _claimer;
};

class libpcie_zero_copy_asio_impl : public pcie_riffa_zero_copy
{
public:
    typedef std::shared_ptr<libpcie_zero_copy_asio_impl> sptr;

    libpcie_zero_copy_asio_impl(fpga_t *fpga,
                                const int& chan,
                                const zero_copy_xport_params& xport_params)
                                : _recv_frame_size(xport_params.recv_frame_size)
                                , _num_recv_frames(xport_params.num_recv_frames)
                                , _send_frame_size(xport_params.send_frame_size)
                                , _num_send_frames(xport_params.num_send_frames)
                                , _recv_buffer_pool(buffer_pool::make(
                                        xport_params.num_recv_frames,xport_params.recv_frame_size))
                                , _send_buffer_pool(buffer_pool::make(
                                        xport_params.num_send_frames,xport_params.send_frame_size))
                                , _next_recv_buff_index(0)
                                , _next_send_buff_index(0)
    {
        UHD_LOGGER_TRACE("PCIE")
        << boost::format("Creating PCIE transport to device chan %d")  % chan;

        for(size_t i = 0;i < get_num_recv_frames(); i++)
        {
            _mrb_pool.push_back(std::make_shared<libpcie_zero_copy_asio_mrb>(
                    fpga,_recv_buffer_pool->at(i),chan,get_recv_frame_size()));

        }

        for(size_t i = 0; i < get_num_send_frames(); i++){
            _msb_pool.push_back(std::make_shared<libpcie_zero_copy_asio_msb>(
                    fpga,chan,_send_buffer_pool->at(i),get_send_frame_size()));
        }

    }

    managed_recv_buffer::sptr get_recv_buff(double timeout) override
    {
        if(_next_recv_buff_index == _num_recv_frames)
            _next_recv_buff_index = 0;
        return _mrb_pool[_next_recv_buff_index]->get_new(timeout,_next_recv_buff_index);
    }

    size_t get_num_recv_frames(void) const override
    {
        return _num_recv_frames;
    }
    size_t get_recv_frame_size(void) const override
    {
        return _recv_frame_size;
    }

    managed_send_buffer::sptr get_send_buff(double timeout) override
    {
        if(_next_send_buff_index == _num_send_frames)
            _next_send_buff_index = 0;
        return _msb_pool[_next_send_buff_index]->get_new(timeout,_next_send_buff_index);
    }

    size_t get_num_send_frames(void) const override
    {
        return _num_send_frames;
    }
    size_t get_send_frame_size(void) const override
    {
        return _send_frame_size;
    }

private:
    const size_t _recv_frame_size, _num_recv_frames;
    const size_t _send_frame_size, _num_send_frames;
    buffer_pool::sptr _recv_buffer_pool, _send_buffer_pool;
    std::vector<std::shared_ptr<libpcie_zero_copy_asio_msb>> _msb_pool;
    std::vector<std::shared_ptr<libpcie_zero_copy_asio_mrb>> _mrb_pool;
    size_t _next_recv_buff_index, _next_send_buff_index;

};

pcie_riffa_zero_copy::sptr pcie_riffa_zero_copy::make(fpga_t *device, const int channel,
                                                      const zero_copy_xport_params &default_buff_args,
                                                      pcie_riffa_zero_copy::buff_params &buff_params_out,
                                                      const device_addr_t &hints) {
    zero_copy_xport_params xport_params = default_buff_args;

    xport_params.recv_frame_size =
            size_t(hints.cast<double>("recv_frame_size",default_buff_args.recv_frame_size));
    xport_params.num_recv_frames =
            size_t(hints.cast<double>("num_recv_frames",default_buff_args.num_recv_frames));
    xport_params.send_frame_size =
            size_t(hints.cast<double>("send_frame_size",default_buff_args.send_frame_size));
    xport_params.num_send_frames =
            size_t(hints.cast<double>("num_send_frames",default_buff_args.num_send_frames));
    xport_params.recv_buff_size =
            size_t(hints.cast<double>("recv_buff_size", default_buff_args.recv_buff_size));
    xport_params.send_buff_size =
            size_t(hints.cast<double>("send_buff_size", default_buff_args.send_buff_size));

    if (xport_params.num_recv_frames == 0) {
        UHD_LOG_TRACE(
                "PCIE", "Default value for num_recv_frames: " << PCIE_DEFAULT_NUM_FRAMES);
        xport_params.num_recv_frames = PCIE_DEFAULT_NUM_FRAMES;
    }
    if (xport_params.num_send_frames == 0) {
        UHD_LOG_TRACE(
                "PCIE", "Default value for no num_send_frames: " << PCIE_DEFAULT_NUM_FRAMES);
        xport_params.num_send_frames = PCIE_DEFAULT_NUM_FRAMES;
    }
    if (xport_params.recv_frame_size == 0) {
        UHD_LOG_TRACE("PCIE",
                      "Using default value for  recv_frame_size: " << PCIE_DEFAULT_FRAME_SIZE);
        xport_params.recv_frame_size = PCIE_DEFAULT_FRAME_SIZE;
    }
    if (xport_params.send_frame_size == 0) {
        UHD_LOG_TRACE(
                "PCIE", "Using default value for send_frame_size, " << PCIE_DEFAULT_FRAME_SIZE);
        xport_params.send_frame_size = PCIE_DEFAULT_FRAME_SIZE;
    }

    UHD_LOG_TRACE("PCIE", "send_frame_size: " << xport_params.send_frame_size);
    UHD_LOG_TRACE("PCIE", "recv_frame_size: " << xport_params.recv_frame_size);

    if(xport_params.recv_buff_size == 0){
        UHD_LOG_TRACE("PCIE", "Using default value for recv_buff_size");
        xport_params.recv_buff_size = std::max(
                PCIE_DEFAULT_BUFF_SIZE, xport_params.num_recv_frames * MAX_PCIE_MTU);
        UHD_LOG_TRACE("PCIE",
                      "Using default value for recv_buff_size" << xport_params.recv_buff_size);
    }
    if (xport_params.send_buff_size == 0) {
        UHD_LOG_TRACE("PCIE", "default_buff_args has no send_buff_size");
        xport_params.send_buff_size = std::max(
                PCIE_DEFAULT_BUFF_SIZE, xport_params.num_send_frames * MAX_PCIE_MTU);
    }



    libpcie_zero_copy_asio_impl::sptr pcie_trans(
            new libpcie_zero_copy_asio_impl(device,channel,xport_params));


    return pcie_trans;
}