/*
Copyright (c) 2014, Intel Corporation

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of Intel Corporation nor the names of its contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <immintrin.h>
#include <cmath>
#include <algorithm>
#include "gtest/gtest.h"


#include "../../devices/common/nn_workload_data.h"
#include "../../devices/device_cpu/core/fixedpoint/layer_convolution_pooling_int16_fixedpoint_avx2.h"
#include "../../devices/api/nn_device_interface_0.h"
#include "../../devices/device_cpu/api_internal/nn_device_interface_0_internal.h"

const uint32_t C_simd_width = sizeof(__m256)/sizeof(int32_t);


///////////////////////////////////////////////////////////////////////////////////////////////////
// Helper classess and functions.
static void ult_nn_convolution_initialize_work_item(
    nn_workload_item* &work_item,
    nn_workload_item* &input_item,
    int16_t *const input,
    int32_t*const biases,
    int16_t *const output,
    int16_t *const kernel,
    uint32_t num_output_feature_maps,
    uint32_t num_input_feature_maps,
    uint32_t output_feature_map_width,
    uint32_t output_feature_map_height,
    uint32_t input_feature_map_width,
    uint32_t input_feature_map_height,
    uint32_t kernel_width,
    uint32_t kernel_height,
    uint32_t kernel_stride_x,
    uint32_t kernel_stride_y,
    uint32_t pool_stride_x,
    uint32_t pool_stride_y,
    uint32_t pool_size_x,
    uint32_t pool_size_y,
    uint8_t accumulator_fraction,
    uint8_t output_fraction,
    uint_least32_t center_x,
    uint_least32_t center_y,
    NN_ACTIVATION_FUNCTION activation)
{
    uint32_t IFMBlock = 8;
    uint32_t OFMBlock = 32;
    uint32_t OFMpBlock = 2;
    uint32_t OFMOutBlock = 8;

    if (num_input_feature_maps == 4)
        IFMBlock = 4;

    nn_workload_data_layout_t in_layout = {
        { 0, 0, 0, 0, 0, 0 }, // tile in log2(size)
        { 0, 0, 0, 0, 0, 0 }, // alignment
        { NN_DATA_COORD_p, NN_DATA_COORD_x, NN_DATA_COORD_y, NN_DATA_COORD_z, NN_DATA_COORD_n, NN_DATA_COORD_q }, // ordering
        NN_DATATYPE_INT16
    };
    nn_workload_data_layout_t out_layout = {
        { 0, 0, 0, 0, 0, 0 }, // tile in log2(size)
        { 0, 0, 0, 0, 0, 0 }, // alignment
        { NN_DATA_COORD_p, NN_DATA_COORD_x, NN_DATA_COORD_y, NN_DATA_COORD_z, NN_DATA_COORD_n, NN_DATA_COORD_q }, // ordering
        NN_DATATYPE_INT16
    };

    nn_workload_data_layout_t bias_layout = {
        { 0, 0, 0, 0, 0, 0 }, // tile in log2(size)
        { 0, 0, 0, 0, 0, 0 }, // alignment
        { NN_DATA_COORD_z, NN_DATA_COORD_x, NN_DATA_COORD_y, NN_DATA_COORD_p, NN_DATA_COORD_n, NN_DATA_COORD_q }, // ordering
        NN_DATATYPE_INT32
    };

    nn_workload_data_layout_t weight_layout = {
        { 0, 0, 0, 0, 0, 0 }, // tile in log2(size)
        { 0, 0, 0, 0, 0, 0 }, // alignment
        { NN_DATA_COORD_y, NN_DATA_COORD_p, NN_DATA_COORD_z, NN_DATA_COORD_n, NN_DATA_COORD_x, NN_DATA_COORD_q }, // ordering
        NN_DATATYPE_INT16
    };

    nn_workload_data_coords_t input_coords = 
    { 
        1, // batch size
        input_feature_map_width,
        input_feature_map_height,
        num_input_feature_maps / IFMBlock,
        IFMBlock,
        1, 
    };

    nn_workload_data_coords_t output_coords =
    {
        1, // batch size
        output_feature_map_width + 2 * center_x,
        output_feature_map_height + 2 * center_y,
        num_output_feature_maps / OFMOutBlock,
        OFMOutBlock,
        1,
    };


    nn_workload_data_coords_t nn_view_begin =
    {
        0,
        center_x,
        center_y,
        0,
        0,
        0
    };

    nn_workload_data_coords_t nn_view_end =
    {
        0,
        output_feature_map_width + center_x - 1,
        output_feature_map_height + center_y - 1,
        num_output_feature_maps / OFMOutBlock - 1,
        OFMOutBlock - 1,
        0
    };

    nn_workload_data_coords_t bias_coords =
    {
        1,
        1,
        1,
        num_output_feature_maps,
        1,
        1
    };

    nn_workload_data_coords_t weight_coords = 
    {
        kernel_width,
        kernel_height,
        OFMpBlock,
        num_input_feature_maps / OFMpBlock,
        OFMBlock,
        num_output_feature_maps / OFMBlock
    };

    nn::nn_workload_data_t<int16_t> *output_data = new nn::nn_workload_data_t<int16_t>(output_coords, out_layout);
    nn::nn_workload_data_t<int32_t> *bias_data = new nn::nn_workload_data_t<int32_t>(bias_coords, bias_layout);
    nn::nn_workload_data_t<int16_t> *weight_data = new nn::nn_workload_data_t<int16_t>(weight_coords, weight_layout);

    work_item = new nn_workload_item();
    work_item->type = NN_WORK_ITEM_TYPE_CONVOLUTION_POOLING_MAX_2x2_STRIDE_2x2_INT16_FIXEDPOINT;
    
    auto &arguments = work_item->arguments.forward_convolution_pooling_max_2x2_stride_2x2_fixedpoint;
    arguments.stride[0] = kernel_stride_x;
    arguments.stride[1] = kernel_stride_y;
    arguments.center_offset[0] = center_x;
    arguments.center_offset[1] = center_y;
    arguments.activation.basic_arguments.function = activation;
    arguments.activation.fractions.accumulator = accumulator_fraction;
    arguments.activation.fractions.output = output_fraction;

    work_item->output = output_data;
    arguments.biases = bias_data;
    arguments.weights = weight_data;

    work_item->output = new nn::nn_workload_data_t<int16_t>(*output_data, nn_view_begin, nn_view_end);
    memcpy(work_item->output->parent->data_buffer, output, work_item->output->parent->buffer_size);
    memcpy(arguments.biases->parent->data_buffer, biases, arguments.biases->parent->buffer_size);
    memcpy(arguments.weights->parent->data_buffer, kernel, arguments.weights->parent->buffer_size);

    input_item = new nn_workload_item();
    input_item->type = NN_WORK_ITEM_TYPE_INPUT;

    nn::nn_workload_data_t<int16_t> *input_data = new nn::nn_workload_data_t<int16_t>(input_coords, in_layout);
    memcpy(input_data->parent->data_buffer, input, input_data->parent->buffer_size);
    input_item->output = input_data;

    work_item->input.push_back(input_item);
}

//////////////////////////////////////////////////////////////////////////////////////////////////
static void ult_nn_convolution_deinitialize_work_item(nn_workload_item* &work_item)
{
    auto &arguments = work_item->arguments.forward_convolution_pooling_max_2x2_stride_2x2_fixedpoint;
    delete arguments.biases;
    delete arguments.weights;

    work_item->input.clear();
    delete work_item->output;

    delete work_item;

    work_item = nullptr;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
static bool ult_nn_convolution_interface_run(nn_workload_item* work_item)
{
    bool retvalue = true;
    nn_device_description_t device_description;
    nn_device_interface_0_t device_interface_0;

    nn_device_load(&device_description);
    nn_device_interface_open(0, &device_interface_0);

    int16_fixedpoint::run_multithreaded_convolve_pooling_fixedpoint_work_item(work_item, reinterpret_cast<nn_device_internal*>(device_interface_0.device));

    nn_device_interface_close(&device_interface_0);
    nn_device_unload();

    return retvalue;
}

static bool ult_nn_convolution_interface_run(uint32_t noworkitems, nn_workload_item* work_items[])
{
    bool retvalue = true;


    nn_device_description_t device_description;
    nn_device_interface_0_t device_interface_0;

    nn_device_load(&device_description);
    nn_device_interface_open(0, &device_interface_0);

    for (unsigned int item = 0; item < noworkitems && retvalue; item++)
    {
        int16_fixedpoint::run_multithreaded_convolve_pooling_fixedpoint_work_item(work_items[item], reinterpret_cast<nn_device_internal*>(device_interface_0.device));
    }

    

    nn_device_interface_close(&device_interface_0);
    nn_device_unload();

    return retvalue;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static int16_t ult_nn_convolution_optimized_get_output_value(
    int16_t* output,
    uint_least32_t output_feature_map_width,
    uint_least32_t output_feature_map_height,
    uint_least32_t num_output_feature_maps,
    uint_least32_t output_column,
    uint_least32_t output_row,
    uint_least32_t output_map,
    uint_least32_t& offset)
{
    offset = output_row*output_feature_map_width*num_output_feature_maps + output_column*num_output_feature_maps + output_map;
    return output[offset];
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void ult_nn_convolution_optimized_set_output_value(
    int16_t* output,
    uint_least32_t output_feature_map_width,
    uint_least32_t output_feature_map_height,
    uint_least32_t num_output_feature_maps,
    uint_least32_t output_column,
    uint_least32_t output_row,
    uint_least32_t output_map,
    int16_t value,
    uint_least32_t& offset)
{
    offset = output_row*output_feature_map_width*num_output_feature_maps + output_column*num_output_feature_maps + output_map;
    output[offset] = value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void ult_nn_convolution_optimized_set_input_value(
    int16_t* input,
    uint_least32_t input_feature_map_width,
    uint_least32_t num_input_feature_maps,
    uint_least32_t input_column,
    uint_least32_t input_row,
    uint_least32_t input_map,
    int16_t value,
    uint_least32_t& offset)
{
    offset = input_column*num_input_feature_maps + input_row*num_input_feature_maps*input_feature_map_width + input_map;

    input[offset] = value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void ult_nn_convolution_optimized_set_kernel_value(
    int16_t* kernel,
    uint_least32_t kernel_width,
    uint_least32_t kernel_height,
    uint_least32_t num_input_feature_maps,
    uint_least32_t kernel_column,
    uint_least32_t kernel_row,
    uint_least32_t kernel_input_map,
    uint_least32_t kernel_output_map,
    int16_t value,
    uint_least32_t& offset)
{
    uint_least32_t kernel_output_map_div = kernel_output_map / C_simd_width;
    uint_least32_t kernel_output_map_rem = kernel_output_map % C_simd_width;
    offset = kernel_row*C_simd_width*kernel_width*num_input_feature_maps + kernel_column*C_simd_width + kernel_input_map*C_simd_width*kernel_width + kernel_output_map_div*kernel_width*kernel_height*num_input_feature_maps*C_simd_width + kernel_output_map_rem;
    kernel[offset] = value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static int16_t ult_nn_convolution_naive_get_output_value(
    int16_t* output_ref,
    uint_least32_t output_feature_map_width,
    uint_least32_t output_feature_map_height,
    uint_least32_t num_output_feature_maps,
    uint_least32_t output_column,
    uint_least32_t output_row,
    uint_least32_t output_map,
    uint_least32_t& offset)
{
    offset = output_column + output_row*output_feature_map_width + output_map*output_feature_map_width*output_feature_map_height;
    return output_ref[offset];
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void ult_nn_convolution_naive_set_output_value(
    int16_t* output_ref,
    uint_least32_t output_feature_map_width,
    uint_least32_t output_feature_map_height,
    uint_least32_t num_output_feature_maps,
    uint_least32_t output_column,
    uint_least32_t output_row,
    uint_least32_t output_map,
    int16_t value,
    uint_least32_t& offset)
{
    offset = output_column + output_row*output_feature_map_width + output_map*output_feature_map_width*output_feature_map_height;
    output_ref[offset] = value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void ult_nn_convolution_naive_set_input_value(
    int16_t* input_ref,
    uint_least32_t input_feature_map_width,
    uint_least32_t input_feature_map_height,
    uint_least32_t input_column,
    uint_least32_t input_row,
    uint_least32_t input_map,
    int16_t value,
    uint_least32_t& offset)
{
    offset = input_column + input_row*input_feature_map_width + input_map*input_feature_map_width*input_feature_map_height;

    input_ref[offset] = value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void ult_nn_convolution_naive_set_kernel_value(
    int16_t* kernel_ref,
    uint_least32_t kernel_width,
    uint_least32_t kernel_height,
    uint_least32_t num_input_feature_maps,
    uint_least32_t kernel_column,
    uint_least32_t kernel_row,
    uint_least32_t kernel_input_map,
    uint_least32_t kernel_output_map,
    int16_t value,
    uint_least32_t& offset)
{
    offset = kernel_column + kernel_row*kernel_width + kernel_input_map*kernel_width*kernel_height + kernel_output_map*kernel_width*kernel_height*num_input_feature_maps;
    kernel_ref[offset] = value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void ult_nn_convolution_both_initialize_matrices(
    int16_t* input,
    int16_t* output,
    int32_t* biases,
    int16_t* kernel,
    int16_t* input_ref,
    int16_t* output_ref,
    int32_t* biases_ref,
    int16_t* kernel_ref,
    uint_least32_t num_output_feature_maps,
    uint_least32_t num_input_feature_maps,
    uint_least32_t output_feature_map_width,
    uint_least32_t output_feature_map_height,
    uint_least32_t input_feature_map_width,
    uint_least32_t input_feature_map_height,
    uint_least32_t kernel_width,
    uint_least32_t kernel_height,
    uint_least32_t center_x,
    uint_least32_t center_y
    )
{
    uint_least32_t input_size = input_feature_map_width * input_feature_map_height * num_input_feature_maps * sizeof(int16_t);
    int16_t * inputT = (int16_t*)_mm_malloc(input_size, 64);
    uint_least32_t kernel_size = num_input_feature_maps * num_output_feature_maps * kernel_width * kernel_height * sizeof(int16_t);
    int16_t * weightT = (int16_t*)_mm_malloc(kernel_size, 64);

    uint32_t IFMBlock = 8;
    uint32_t OFMBlock = 32;
    uint32_t OFMpBlock = 2;
    if (num_input_feature_maps == 4) IFMBlock = 4;

    for (uint_least32_t input_map = 0; input_map < num_input_feature_maps; input_map++)
    {
        uint_least32_t element = 0;
        int16_t value = input_map * 0x0100;
        for (uint_least32_t row = 0; row < input_feature_map_height; row++)
        {
            for (uint_least32_t column = 0; column < input_feature_map_width; column++)
            {
                uint_least32_t offset;
                //ult_nn_convolution_optimized_set_input_value(inputT, input_feature_map_width, num_input_feature_maps, column, row, input_map, value, offset);
                ult_nn_convolution_naive_set_input_value(inputT, input_feature_map_width,   input_feature_map_height, column, row, input_map, value, offset);
                ult_nn_convolution_naive_set_input_value(input_ref, input_feature_map_width,input_feature_map_height, column, row, input_map, value, offset);
                value++;
            }
        }
    }

    for (uint_least32_t outmapa = 0; outmapa < num_output_feature_maps; outmapa++)
    {
        for (uint_least32_t input_map = 0; input_map < num_input_feature_maps; input_map++)
        {
            uint_least32_t element = 0;
            int16_t value = input_map * 0x0100 + outmapa * 0x2000;
            for (uint_least32_t row = 0; row < kernel_height; row++)
            {
                for (uint_least32_t column = 0; column < kernel_width; column++)
                {
                    element++;
                    uint_least32_t offset;
                    //ult_nn_convolution_optimized_set_kernel_value
                    ult_nn_convolution_naive_set_kernel_value(weightT, kernel_width, kernel_height, num_input_feature_maps, column, row, input_map, outmapa, value, offset);
                    ult_nn_convolution_naive_set_kernel_value(kernel_ref, kernel_width, kernel_height, num_input_feature_maps, column, row, input_map, outmapa, value, offset);
                    value++;
                }
            }
        }
    }

    for (uint_least32_t outmapa = 0; outmapa < num_output_feature_maps; outmapa++)
    {
        for (uint_least32_t row = 0; row < output_feature_map_height + 2 * center_y; row++)
        {
            for (uint_least32_t column = 0; column < output_feature_map_width + 2 * center_x; column++)
            {
                uint32_t index = column + row * (output_feature_map_width + 2 * center_x) + outmapa * (output_feature_map_width + 2 * center_x) * (output_feature_map_height + 2 * center_y);
                output[index] = 0;
                output_ref[index] = 0;
            }
        }
    }


    for (uint_least32_t outmapa = 0; outmapa < num_output_feature_maps; outmapa++)
    {
        biases[outmapa] = outmapa;
        biases_ref[outmapa] = outmapa;
    }

    //prepare right input layout for naive implementation
    for (uint32_t i = 0; i < num_input_feature_maps / IFMBlock; i++)
    for (uint32_t j = 0; j < input_feature_map_width * input_feature_map_height; j++)
    for (uint32_t n = 0; n < IFMBlock; n++)
        input[n + j * IFMBlock + i * input_feature_map_width * input_feature_map_height * IFMBlock]
            = inputT[n * input_feature_map_width * input_feature_map_height + j + i * input_feature_map_width * input_feature_map_height * IFMBlock];

    const uint32_t ItrIn = num_input_feature_maps / 2;
    const uint32_t ItrOut = num_output_feature_maps / OFMBlock;

    for (uint32_t k = 0; k < ItrOut; k++)
    for (uint32_t i = 0; i < kernel_width * kernel_height; i++)
    for (uint32_t j = 0; j < ItrIn; j++)
    for (uint32_t n = 0; n < OFMBlock; n++)
    for (uint32_t m = 0; m < 2; m++)
        kernel[m + 2 * n + 2 * OFMBlock * j + i * 2 * OFMBlock * ItrIn + k * 2 * OFMBlock * ItrIn  * kernel_width * kernel_height]
        = weightT[m * kernel_width * kernel_height + n * num_input_feature_maps * kernel_width * kernel_height + 2 * j * kernel_width * kernel_height + k * OFMBlock * num_input_feature_maps * kernel_width * kernel_height + i];

    _mm_free(inputT);
    _mm_free(weightT);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static bool ult_nn_convolution_check_outputs(
    nn_workload_data_t* output,
    int16_t* output_ref,
    uint_least32_t num_output_feature_maps,
    uint_least32_t output_feature_map_width,
    uint_least32_t output_feature_map_height,
    uint_least32_t center_x,
    uint_least32_t center_y
    )
{
    int16_t * outputOpt = (int16_t *)output->parent->data_buffer;
    uint32_t OFMOutBlock = 8;

    uint_least32_t output_size = (output_feature_map_width + 2 * center_x) * (output_feature_map_height + 2 * center_y) * num_output_feature_maps * sizeof(int16_t);
    int16_t* outputT = (int16_t*)_mm_malloc(output_size, 64);
    for (uint32_t i = 0; i < num_output_feature_maps / OFMOutBlock; i++)
    for (uint32_t j = 0; j < (output_feature_map_width + 2 * center_x) * (output_feature_map_height + 2 * center_y); j++)
    for (uint32_t n = 0; n < OFMOutBlock; n++)
    {
        outputT[n + j * OFMOutBlock + i * (output_feature_map_width + 2 * center_x) * (output_feature_map_height + 2 * center_y) * OFMOutBlock]
            = output_ref[n * (output_feature_map_width + 2 * center_x) * (output_feature_map_height + 2 * center_y) + j + i * (output_feature_map_width + 2 * center_x) * (output_feature_map_height + 2 * center_y) * OFMOutBlock];
    }

    bool passed = true;
    for (uint_least32_t i = 0; i < (output_size / sizeof(int16_t)) && passed; i++)
    if (outputT[i] != outputOpt[i])
        passed = false;

    _mm_free(outputT);

    return passed;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void ult_nn_convolution_both_alloc(
    int16_t* &input,
    int16_t* &output,
    int32_t* &biases,
    int16_t* &kernel,
    int16_t* &input_ref,
    int16_t* &output_ref,
    int32_t* &biases_ref,
    int16_t* &kernel_ref,
    uint_least32_t num_output_feature_maps,
    uint_least32_t num_input_feature_maps,
    uint_least32_t output_width,
    uint_least32_t output_height,
    uint_least32_t input_width,
    uint_least32_t input_height,
    uint_least32_t kernel_width,
    uint_least32_t kernel_height,
    uint_least32_t center_x,
    uint_least32_t center_y
    )
{
    uint_least32_t input_size = input_width * input_height * num_input_feature_maps * sizeof(int16_t);
    uint_least32_t output_size = (output_width + 2 * center_x) * (output_height + 2 * center_y) * num_output_feature_maps * sizeof(int16_t);
    uint_least32_t bias_size = num_output_feature_maps * sizeof(int32_t);
    uint_least32_t kernel_size = num_input_feature_maps * num_output_feature_maps * kernel_width * kernel_height * sizeof(int16_t);

    input_ref = (int16_t*)_mm_malloc(input_size, 64);
    output_ref = (int16_t*)_mm_malloc(output_size, 64);
    biases_ref = (int32_t*)_mm_malloc(bias_size, 64);
    kernel_ref = (int16_t*)_mm_malloc(kernel_size, 64);

    input_size = input_width * input_height * num_input_feature_maps * sizeof(int16_t);
    output_size = (output_width + 2 * center_x) * (output_height + 2 * center_y) * num_output_feature_maps * sizeof(int16_t);
    kernel_size = num_input_feature_maps * num_output_feature_maps * kernel_width * kernel_height * sizeof(int32_t);

    input = (int16_t*)_mm_malloc(input_size, 64);
    output = (int16_t*)_mm_malloc(output_size, 64);
    biases = (int32_t*)_mm_malloc(bias_size, 64);
    kernel = (int16_t*)_mm_malloc(kernel_size, 64);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void ult_nn_convolution_both_dealloc(
    int16_t* &input,
    int16_t* &output,
    int32_t* &biases,
    int16_t* &kernel,
    int16_t* &input_ref,
    int16_t* &output_ref,
    int32_t* &biases_ref,
    int16_t* &kernel_ref)
{
    if (input != 0)
    {
        _mm_free(input);
        input = 0;
    }

    if (output != 0)
    {
        _mm_free(output);
        output = 0;
    }

    if (biases != 0)
    {
        _mm_free(biases);
        biases = 0;
    }

    if (kernel != 0)
    {
        _mm_free(kernel);
        kernel = 0;
    }

    if (input_ref != 0)
    {
        _mm_free(input_ref);
        input_ref = 0;
    }

    if (output_ref != 0)
    {
        _mm_free(output_ref);
        output_ref = 0;
    }

    if (biases_ref != 0)
    {
        _mm_free(biases_ref);
        biases_ref = 0;
    }

    if (kernel_ref != 0)
    {
        _mm_free(kernel_ref);
        kernel_ref = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void ult_nn_maxpooling_naive(
    int16_t* input_ref,
    int16_t* output_ref,
    int32_t* biases_ref,
    int16_t* kernel_ref,
    uint_least32_t num_output_feature_maps,
    uint_least32_t num_input_feature_maps,
    uint_least32_t output_feature_map_width,
    uint_least32_t output_feature_map_height,
    uint_least32_t output_feature_map_width_int,
    uint_least32_t output_feature_map_height_int,
    uint_least32_t input_feature_map_width,
    uint_least32_t input_feature_map_height,
    uint_least32_t kernel_width,
    uint_least32_t kernel_height,
    uint_least32_t kernel_stride_x,
    uint_least32_t kernel_stride_y,
    uint_least32_t pool_width,
    uint_least32_t pool_height,
    uint_least32_t pool_stride_x,
    uint_least32_t pool_stride_y,
    uint8_t accumulator_fraction,
    uint8_t output_fraction,
    uint_least32_t center_x,
    uint_least32_t center_y,
    NN_ACTIVATION_FUNCTION activation)
{
    bool BiasEn = 1;

    uint_least32_t output_int_size = output_feature_map_width_int * output_feature_map_height_int * num_output_feature_maps * sizeof(int32_t);
    int32_t * output_int = (int32_t*)_mm_malloc(output_int_size, 64);
    memset(output_int, 0, output_int_size);
    uint_least32_t output_ref_size = (output_feature_map_width + 2 * center_x) * (output_feature_map_height + 2 * center_y) * num_output_feature_maps * sizeof(int16_t);
    memset(output_ref, 0, output_ref_size);

    //int32_t * output_ref_temp = (int32_t*)_mm_malloc((output_feature_map_width + 2 * center_x) * (output_feature_map_height + 2 * center_y) * num_output_feature_maps * sizeof(int32_t), 64);
    //memset(output_ref_temp, 0, (output_feature_map_width + 2 * center_x) * (output_feature_map_height + 2 * center_y) * num_output_feature_maps * sizeof(int32_t));

    for (unsigned int ofmItr = 0; ofmItr < num_output_feature_maps; ofmItr++)
    { // For each output feature map
        for (unsigned int ifmItr = 0; ifmItr < num_input_feature_maps; ifmItr++)
        { // Go over all input feature maps

            //for (unsigned int hItr = 0; hItr < output_feature_map_height_int; hItr += kernel_stride_y)
            for (unsigned int hItr = 0; hItr < (input_feature_map_height - kernel_height + 1); hItr += kernel_stride_y)
            { // For each input feature map, go over all locations where the kernel-sized stencil would fit - in both dimensions, y...
               // for (unsigned int wItr = 0; wItr < output_feature_map_width_int; wItr += kernel_stride_x)
                for (unsigned int wItr = 0; wItr < (input_feature_map_width - kernel_width + 1); wItr += kernel_stride_x)
                { // and x...
                    for (unsigned int kH = 0; kH < kernel_height; kH++)
                    {
                        for (unsigned int kW = 0; kW < kernel_width; kW++)
                        { // For each stencil placement, compute 2D convolution at the placement
                            short kernel_pixel = kernel_ref[ofmItr * num_input_feature_maps * kernel_height * kernel_width + ifmItr * kernel_width * kernel_height + kernel_width*kH + kW];
                            short ifm_pixel = input_ref[(ifmItr * input_feature_map_width * input_feature_map_height + (input_feature_map_width*hItr + wItr) + kH*input_feature_map_width + kW)];
                            output_int[(ofmItr * output_feature_map_width_int * output_feature_map_height_int + (hItr / kernel_stride_y) * output_feature_map_height_int + (wItr / kernel_stride_x))] += ifm_pixel*kernel_pixel;
                            /*output_ref_temp[
                                (ofmItr * (output_feature_map_width + 2 * center_x) * (output_feature_map_height + 2 * center_y) 
                                    + (center_y + hItr / kernel_stride_y) * (output_feature_map_width + 2 * center_x)
                                    + (center_x + wItr / kernel_stride_x))]
                                    += ifm_pixel*kernel_pixel;*/
                        }
                    }
                    // Also add bias, but only once for each output feature map - when going over input feature map 0
                    output_int[(ofmItr * output_feature_map_width_int * output_feature_map_height_int + (hItr / kernel_stride_y) * output_feature_map_width_int + (wItr / kernel_stride_x))] += ((ifmItr == 0 && BiasEn) ? 1 : 0) * biases_ref[ofmItr];
                    //output_ref_temp[(ofmItr * (output_feature_map_width + 2 * center_x) * (output_feature_map_height + 2 * center_y)
                    //    + (center_y + hItr / kernel_stride_y) * (output_feature_map_width + 2 * center_x)
                    //    + (center_x + wItr / kernel_stride_x))]
                    //    += (((ifmItr == 0) && BiasEn) ? 1 : 0) * biases_ref[ofmItr];
                }
            }
        }
    }

    const auto acc_shift = accumulator_fraction - output_fraction;

    for (unsigned int ofmItr = 0; ofmItr < num_output_feature_maps; ofmItr++)
    for (uint32_t y = 0; y < output_feature_map_height; y++)
    for (uint32_t x = 0; x < output_feature_map_width; x++)
    {
        int32_t max_t = output_int[ofmItr * output_feature_map_height_int * output_feature_map_width_int + y * pool_stride_y * output_feature_map_height_int + x * pool_stride_x];
        for (uint32_t maxY = 0; maxY < pool_stride_y; maxY++)
        for (uint32_t maxX = 0; maxX < pool_stride_x; maxX++)
        {
            int32_t max_t1 = output_int[ofmItr * output_feature_map_height_int * output_feature_map_width_int + (y * pool_stride_y + maxY) * output_feature_map_height_int + x * pool_stride_x + maxX];
            max_t = std::max(max_t, max_t1);
        }

        switch (activation)
        {
        case NN_ACTIVATION_FUNCTION_RELU:
            max_t = std::max(0, max_t);
            break;
        case NN_ACTIVATION_FUNCTION_NONE:
            break;
        default:
            break;
        }

        if (acc_shift > 0)
        {
            max_t = max_t >> acc_shift;
        }
        else
        {
            max_t = max_t << -acc_shift;
        }

        max_t = std::min(max_t, 32767);
        max_t = std::max(max_t, -32768);
        output_ref[ofmItr * (output_feature_map_height + 2 * center_y) * (output_feature_map_width + 2 * center_x) + (y + center_y) * (output_feature_map_width + 2 * center_x) + x + center_x] = max_t;
    }

    _mm_free(output_int);

}

///////////////////////////////////////////////////////////////////////////////////////////////////
static bool ult_perform_test(
    uint_least32_t num_output_feature_maps,
    uint_least32_t num_input_feature_maps,
    uint_least32_t input_feature_map_width,
    uint_least32_t input_feature_map_height,
    uint_least32_t kernel_width,
    uint_least32_t kernel_height,
    uint_least32_t kernel_stride_x,
    uint_least32_t kernel_stride_y,
    uint_least32_t pool_stride_x,
    uint_least32_t pool_stride_y,
    uint_least32_t pool_size_x,
    uint_least32_t pool_size_y,	
    uint8_t accumulator_fraction,
    uint8_t output_fraction,
    uint_least32_t center_x,
    uint_least32_t center_y,
    NN_ACTIVATION_FUNCTION activation,
    NN_POOLING_MODE mode)

{
    nn_workload_item* work_item = nullptr;
    nn_workload_item* work_items[12];

    nn_workload_item* input_item = nullptr;
    nn_workload_item* input_items[12];

    std::fill_n(work_items, 12, nullptr);

    bool passed = false;

    int16_t* input = 0;
    int16_t* output = 0;
    int32_t* biases = 0;
    int16_t* kernel = 0;

    int16_t* input_ref = 0;
    int16_t* output_ref = 0;
    int32_t* biases_ref = 0;
    int16_t* kernel_ref = 0;

    uint32_t NoWItems = 1;

    uint_least32_t output_feature_map_width = (((input_feature_map_width - kernel_width) / kernel_stride_x + 1) - pool_size_x) / pool_stride_x + 1;
    uint_least32_t output_feature_map_height = (((input_feature_map_height - kernel_height) / kernel_stride_y + 1) - pool_size_y) / pool_stride_y + 1;

    uint_least32_t output_feature_map_width_int = (input_feature_map_width - kernel_width) / kernel_stride_x + 1;
    uint_least32_t output_feature_map_height_int = (input_feature_map_height - kernel_height) / kernel_stride_y + 1;

    num_output_feature_maps += (C_simd_width - (num_output_feature_maps % C_simd_width)) % C_simd_width;

    // Allocate naive and optimized buffers.
    ult_nn_convolution_both_alloc(
        input,
        output,
        biases,
        kernel,
        input_ref,
        output_ref,
        biases_ref,
        kernel_ref,
        num_output_feature_maps,
        num_input_feature_maps,
        output_feature_map_width,
        output_feature_map_height,
        input_feature_map_width,
        input_feature_map_height,
        kernel_width,
        kernel_height,
        center_x,
        center_y
 );

    // Initialize both buffers.    
    ult_nn_convolution_both_initialize_matrices(
        input,
        output,
        biases,
        kernel,
        input_ref,
        output_ref,
        biases_ref,
        kernel_ref,
        num_output_feature_maps,
        num_input_feature_maps,
        output_feature_map_width,
        output_feature_map_height,
        input_feature_map_width,
        input_feature_map_height,
        kernel_width,
        kernel_height,
        center_x,
        center_y
);

        // Naive maxpooling.
    ult_nn_maxpooling_naive(
        input_ref,
        output_ref,
        biases_ref,
        kernel_ref,
        num_output_feature_maps,
        num_input_feature_maps,
        output_feature_map_width,
        output_feature_map_height,
        output_feature_map_width_int,
        output_feature_map_height_int,
        input_feature_map_width,
        input_feature_map_height,
        kernel_width,
        kernel_height,
        kernel_stride_x,
        kernel_stride_y,
        pool_size_x,
        pool_size_y,
        pool_stride_x,
        pool_stride_y,
        accumulator_fraction,
        output_fraction,
        center_x,
        center_y,
        activation);

    {
        // Perform data copy to interface test.
        ult_nn_convolution_initialize_work_item(
            work_item,
            input_item,
            input,
            biases,
            output,
            kernel,
            num_output_feature_maps,
            num_input_feature_maps,
            output_feature_map_width,
            output_feature_map_height,
            input_feature_map_width,
            input_feature_map_height,
            kernel_width,
            kernel_height,
            kernel_stride_x,
            kernel_stride_y,
            pool_stride_x,
            pool_stride_y,
            pool_size_x,
            pool_size_y,
            accumulator_fraction,
            output_fraction,
            center_x,
            center_y,
            activation);

         //Optimized convolution.
         passed = ult_nn_convolution_interface_run(work_item);
    }


    if (passed)
    {
        // Basic check between optimized and naive versions.
        passed = ult_nn_convolution_check_outputs(
            work_item->output,
            output_ref,
            num_output_feature_maps,
            output_feature_map_width,
            output_feature_map_height,
            center_x,
            center_y);
    }

     //Cleanup.
    ult_nn_convolution_deinitialize_work_item(work_item);

    ult_nn_convolution_both_dealloc(
        input,
        output,
        biases,
        kernel,
        input_ref,
        output_ref,
        biases_ref,
        kernel_ref);

    return passed;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Tests.
TEST(cpu_int16_convolution_maxpooling2x2_fixedpoint, cpu_convolution_maxpooling2x2_stride1)
{
 // EXPECT_EQ(true, ult_perform_test(
        // 8,                              // output feature maps
        // 1,                              // input feature maps
        // 5,                              // input width
        // 5,                              // input height
        // 2,                              // kernel width
        // 2,                              // kernel height
        // 1,                              // kernel stride x
        // 1,                              // kernel stride y
        // 2,                              // pool stride x
        // 2,                              // pool stride y
        // 2,                              // pool size x
        // 2,                              // pool size y
        // NN_ACTIVATION_FUNCTION_NONE,    // activation function
        // NN_POOLING_MODE_MAX));          // pooling mode


    //EXPECT_EQ(true, ult_perform_test(32, 4, 231, 231, 11, 11, 4, 4, 2, 2, 2, 2, 16, 0, 0, 0, NN_ACTIVATION_FUNCTION_RELU, NN_POOLING_MODE_MAX));
    EXPECT_EQ(true, ult_perform_test(96, 4, 231, 231, 11, 11, 4, 4, 2, 2, 2, 2, 16, 0, 0, 0, NN_ACTIVATION_FUNCTION_RELU, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(96, 16, 14, 14, 3, 3, 1, 1, 2, 2, 2, 2, false, 16, 0, 0, 0, NN_ACTIVATION_FUNCTION_RELU, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(96, 4, 231, 231, 11, 11, 4, 4, 2, 2, 2, 2, false, 16, 0, 0, 0, NN_ACTIVATION_FUNCTION_RELU, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(256, 96, 28, 28, 5, 5, 1, 1, 2, 2, 2, 2, false, 16, 0, 1, 1, NN_ACTIVATION_FUNCTION_RELU, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(1024, 1024, 14, 14, 3, 3, 1, 1, 2, 2, 2, 2, false, 16, 0, 1, 1, NN_ACTIVATION_FUNCTION_RELU, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(256, 96, 28, 28, 5, 5, 1, 1, 2, 2, 2, 2, false, 16, 0, 2, 2, NN_ACTIVATION_FUNCTION_RELU, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(512, 256, 14, 14, 3, 3, 1, 1, 2, 2, 2, 2, false, 16, 0, 1, 1, NN_ACTIVATION_FUNCTION_RELU, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(256, 256, 14, 14, 3, 3, 1, 1, 2, 2, 2, 2, false, 16, 0, 1, 1, NN_ACTIVATION_FUNCTION_RELU, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(1024, 512, 14, 14, 3, 3, 1, 1, 2, 2, 2, 2, false, 16, 0, 1, 1, NN_ACTIVATION_FUNCTION_RELU, NN_POOLING_MODE_MAX));

    //EXPECT_EQ(true, ult_perform_test(96, 4, 231, 231, 11, 11, 4, 4, 2, 2, 2, 2, false, 16, 0, 1, 1, NN_ACTIVATION_FUNCTION_NONE, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(96, 16, 14, 14, 3, 3, 1, 1, 2, 2, 2, 2, false, 16, 0, 1, 1, NN_ACTIVATION_FUNCTION_NONE, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(96, 4, 231, 231, 11, 11, 4, 4, 2, 2, 2, 2, false, 16, 0, 1, 1, NN_ACTIVATION_FUNCTION_NONE, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(256, 96, 28, 28, 5, 5, 1, 1, 2, 2, 2, 2, false, 16, 0, 1, 1, NN_ACTIVATION_FUNCTION_NONE, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(1024, 1024, 14, 14, 3, 3, 1, 1, 2, 2, 2, 2, false, 16, 0, 1, 1, NN_ACTIVATION_FUNCTION_NONE, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(256, 96, 28, 28, 5, 5, 1, 1, 2, 2, 2, 2, false, 16, 0, 2, 2, NN_ACTIVATION_FUNCTION_NONE, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(512, 256, 14, 14, 3, 3, 1, 1, 2, 2, 2, 2, false, 16, 0, 2, 2, NN_ACTIVATION_FUNCTION_NONE, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(256, 256, 14, 14, 3, 3, 1, 1, 2, 2, 2, 2, false, 16, 0, 0, 0, NN_ACTIVATION_FUNCTION_NONE, NN_POOLING_MODE_MAX));
    //EXPECT_EQ(true, ult_perform_test(1024, 512, 14, 14, 3, 3, 1, 1, 2, 2, 2, 2, false, 16, 0, 0, 0, NN_ACTIVATION_FUNCTION_NONE, NN_POOLING_MODE_MAX));
}

