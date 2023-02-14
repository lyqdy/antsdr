//
// Created by jcc on 23-2-10.
//

#ifndef ANTSDR_PCIE_RIFFA_ZERO_COPY_HPP
#define ANTSDR_PCIE_RIFFA_ZERO_COPY_HPP
#include <uhd/transport/zero_copy.hpp>
#include <uhd/types/device_addr.hpp>
#include "riffa.h"

namespace uhd{namespace transport{


class UHD_API pcie_riffa_zero_copy : public virtual zero_copy_if
{
public:
    struct buff_params
    {
        size_t recv_buff_size;
        size_t send_buff_size;
    };

    typedef std::shared_ptr<pcie_riffa_zero_copy> sptr;


    static sptr make(fpga_t *device,
                     const int channel,
                     const zero_copy_xport_params& default_buff_args,
                     pcie_riffa_zero_copy::buff_params& buff_params_out,
                     const device_addr_t& hints = device_addr_t());
};
}}


#endif //ANTSDR_PCIE_RIFFA_ZERO_COPY_HPP
