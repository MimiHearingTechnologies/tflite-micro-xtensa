/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include "tensorflow/lite/kernels/internal/reference/depth_to_space.h"

#include <stdint.h>

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/kernels/internal/types.h"
#include "tensorflow/lite/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/xtensa/xtensa.h"

namespace tflite {
namespace {

constexpr int kInputTensor = 0;
constexpr int kOutputTensor = 0;

// input/output tensor shape rank associations
constexpr int kBatchRank = 0;
constexpr int kHeightRank = 1;
constexpr int kWidthRank = 2;
constexpr int kDepthRank = 3;

TfLiteStatus CalculateOpData(TfLiteContext* context, TfLiteNode* node) {
  auto* params =
      reinterpret_cast<TfLiteDepthToSpaceParams*>(node->builtin_data);

  TF_LITE_ENSURE_EQ(context, NumInputs(node), 1);
  TF_LITE_ENSURE_EQ(context, NumOutputs(node), 1);

  MicroContext* micro_context = GetMicroContext(context);

  TfLiteTensor* input =
      micro_context->AllocateTempInputTensor(node, kInputTensor);
  TF_LITE_ENSURE(context, input != nullptr);
  TfLiteTensor* output =
      micro_context->AllocateTempOutputTensor(node, kOutputTensor);
  TF_LITE_ENSURE(context, output != nullptr);

  TF_LITE_ENSURE_EQ(context, NumDimensions(input), 4);

  auto data_type = output->type;
  TF_LITE_ENSURE(context,
                 data_type == kTfLiteFloat32 || data_type == kTfLiteInt8);
  TF_LITE_ENSURE_TYPES_EQ(context, input->type, output->type);

  const int block_size = params->block_size;
  TF_LITE_ENSURE(context, block_size > 0);
  const int input_height = input->dims->data[kHeightRank];
  const int input_width = input->dims->data[kWidthRank];
  const int input_channels = input->dims->data[kDepthRank];
  int output_height = input_height * block_size;
  int output_width = input_width * block_size;
  int output_channels = input_channels / block_size / block_size;

  TF_LITE_ENSURE_EQ(context, input_height, output_height / block_size);
  TF_LITE_ENSURE_EQ(context, input_width, output_width / block_size);
  TF_LITE_ENSURE_EQ(context, input_channels,
                    output_channels * block_size * block_size);

  // We must update the output tensor dimensions.
  // The dims storage is expected to be the same area in memory
  // for both TfLiteTensor and TfLiteEvalTensor.  This is important
  // because TfLiteTensor in the MicroInterpreter is a temporary
  // allocation.  For the KernelRunner interpreter, TfLiteEvalTensor
  // is a temporary allocation.  We must therefore relocate the dims
  // from the FlatBuffer to the persistant storage arena.
  TfLiteEvalTensor* output_eval =
      tflite::micro::GetEvalOutput(context, node, kOutputTensor);
  TF_LITE_ENSURE_OK(context, tflite::micro::CreateWritableTensorDimsWithCopy(
                                 context, output, output_eval));
  output->dims->data[kBatchRank] = input->dims->data[kBatchRank];
  output->dims->data[kHeightRank] = output_height;
  output->dims->data[kWidthRank] = output_width;
  output->dims->data[kDepthRank] = output_channels;

  micro_context->DeallocateTempTfLiteTensor(input);
  micro_context->DeallocateTempTfLiteTensor(output);

  return kTfLiteOk;
}

TfLiteStatus Prepare(TfLiteContext* context, TfLiteNode* node) {
  return CalculateOpData(context, node);
}

TfLiteStatus Eval(TfLiteContext* context, TfLiteNode* node) {
  auto* params =
      reinterpret_cast<TfLiteDepthToSpaceParams*>(node->builtin_data);

  const TfLiteEvalTensor* input =
      tflite::micro::GetEvalInput(context, node, kInputTensor);
  TfLiteEvalTensor* output =
      tflite::micro::GetEvalOutput(context, node, kOutputTensor);

  tflite::DepthToSpaceParams op_params;
  op_params.block_size = static_cast<int32_t>(params->block_size);

  switch (input->type) {  // Already know in/out types are same.
    case kTfLiteFloat32:
      reference_ops::DepthToSpace(op_params,
                                  tflite::micro::GetTensorShape(input),
                                  tflite::micro::GetTensorData<float>(input),
                                  tflite::micro::GetTensorShape(output),
                                  tflite::micro::GetTensorData<float>(output));
      break;
    case kTfLiteInt8:
#if defined(HIFI5) || defined(HIFI4)
      {
        const RuntimeShape& input_shape = tflite::micro::GetTensorShape(input);
        const RuntimeShape& output_shape = tflite::micro::GetTensorShape(output);
        const int num_batches = input_shape.Dims(0);
        const int input_height = input_shape.Dims(1);
        const int input_width = input_shape.Dims(2);
        const int input_depth = input_shape.Dims(3);
        const int output_height = output_shape.Dims(1);
        const int output_width = output_shape.Dims(2);
        const int output_depth = output_shape.Dims(3);
        int input_cube_size = input_height * input_width * input_depth;
        int output_cube_size = output_height * output_width * output_depth;
        int8_t *p_output;
        const int8_t *p_input;
        p_output = tflite::micro::GetTensorData<int8_t>(output);
        p_input = tflite::micro::GetTensorData<int8_t>(input);
        for(int b = 0; b < num_batches; b++)
        {
          TF_LITE_ENSURE_EQ(
              context,
              xa_nn_depth_to_space_8_8(
                  (p_output + b * output_cube_size),
                  (p_input + b * input_cube_size),
                  input_height, input_width, input_depth,
                  op_params.block_size,
                  output_height, output_width, output_depth, 0, 0),
              0);
        }
      }
#else
      reference_ops::DepthToSpace(op_params,
                                  tflite::micro::GetTensorShape(input),
                                  tflite::micro::GetTensorData<int8_t>(input),
                                  tflite::micro::GetTensorShape(output),
                                  tflite::micro::GetTensorData<int8_t>(output));
#endif // defined(HIFI5) || defined(HIFI4)
      break;
    default:
      TF_LITE_KERNEL_LOG(
          context, "DEPTH_TO_SPACE only supports FLOAT32 and INT8, got %s.",
          TfLiteTypeGetName(output->type));
      return kTfLiteError;
  }

  return kTfLiteOk;
}

}  // namespace

TFLMRegistration Register_DEPTH_TO_SPACE() {
  return tflite::micro::RegisterOp(nullptr, Prepare, Eval);
}

}  // namespace tflite
