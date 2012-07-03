/*
 * Copyright (c) 2012 Hanspeter Portner (agenthp@users.sf.net)
 * 
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 * 
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 * 
 *     1. The origin of this software must not be misrepresented; you must not
 *     claim that you wrote the original software. If you use this software
 *     in a product, an acknowledgment in the product documentation would be
 *     appreciated but is not required.
 * 
 *     2. Altered source versions must be plainly marked as such, and must not be
 *     misrepresented as being the original software.
 * 
 *     3. This notice may not be removed or altered from any source
 *     distribution.
 */

#include <stdint.h>

#include <tube.h>

#include <adc.h>
#include <spi.h>

dma_tube_config adc_tube = {
	.tube_src = &ADC1_BASE->DR,
	.tube_src_size = DMA_SIZE_16BITS,
	.tube_dst = NULL, //set me
	.tube_dst_size = DMA_SIZE_16BITS,
	.tube_nr_xfers = 0, //set me
	.tube_flags = DMA_CFG_DST_INC | DMA_CFG_CIRC | DMA_CFG_CMPLT_IE,
	.target_data = NULL,
	.tube_req_src = DMA_REQ_SRC_ADC1
};

dma_tube_config spi2_rx_tube = {
	.tube_src = &SPI2_BASE->DR,
	.tube_src_size = DMA_SIZE_8BITS,
	.tube_dst = NULL, //set me
	.tube_dst_size = DMA_SIZE_8BITS,
	.tube_nr_xfers = 0, //set me
	.tube_flags = DMA_CFG_DST_INC | DMA_CFG_CMPLT_IE,
	.target_data = NULL,
	.tube_req_src = DMA_REQ_SRC_SPI2_RX
};

dma_tube_config spi2_tx_tube = {
	.tube_src = &SPI2_BASE->DR,
	.tube_src_size = DMA_SIZE_8BITS,
	.tube_dst = NULL, //set me
	.tube_dst_size = DMA_SIZE_8BITS,
	.tube_nr_xfers = 0, //set me
	.tube_flags = DMA_CFG_DST_INC | DMA_CCR_DIR_FROM_MEM,
	.target_data = NULL,
	.tube_req_src = DMA_REQ_SRC_SPI2_TX
};