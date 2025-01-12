/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

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

#include "tensorflow/lite/micro/kernels/mul.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/kernels/internal/quantization_util.h"
#include "tensorflow/lite/kernels/internal/reference/integer_ops/mul.h"
#include "tensorflow/lite/kernels/internal/reference/mul.h"
#include "tensorflow/lite/kernels/internal/reference/process_broadcast_shapes.h"
#include "tensorflow/lite/kernels/internal/tensor_ctypes.h"
#include "tensorflow/lite/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/xtensa/xtensa.h"
#include "tensorflow/lite/micro/memory_helpers.h"
#include "tensorflow/lite/micro/micro_log.h"

namespace tflite {

TfLiteStatus EvalMulQuantizedHiFi(TfLiteContext* context,
                   TfLiteNode* node, const OpDataMul* data,
                   const TfLiteEvalTensor* input1,
                   const TfLiteEvalTensor* input2, TfLiteEvalTensor* output) {
  tflite::ArithmeticParams op_params = {};
  op_params.quantized_activation_min = data->output_activation_min;
  op_params.quantized_activation_max = data->output_activation_max;
  op_params.float_activation_max = data->output_activation_max_f32;
  op_params.input1_offset = -data->input1_zero_point;
  op_params.input2_offset = -data->input2_zero_point;
  op_params.output_offset = data->output_zero_point;
  op_params.output_multiplier = data->output_multiplier;
  op_params.output_shift = data->output_shift;

  if (output->type == kTfLiteInt8) {
      int err;
      const RuntimeShape extended_input1_shape =
        RuntimeShape::ExtendedShape(4, tflite::micro::GetTensorShape(input1));
      const RuntimeShape extended_input2_shape =
        RuntimeShape::ExtendedShape(4, tflite::micro::GetTensorShape(input2));
      const RuntimeShape extended_output_shape =
        RuntimeShape::ExtendedShape(4, tflite::micro::GetTensorShape(output));

      err = xa_nn_elm_mul_broadcast_4D_asym8sxasym8s_asym8s(
          tflite::micro::GetTensorData<int8_t>(output),
          extended_output_shape.DimsData(), op_params.output_offset,
          op_params.output_shift, op_params.output_multiplier,
          op_params.quantized_activation_min,
          op_params.quantized_activation_max,
          tflite::micro::GetTensorData<int8_t>(input1),
          extended_input1_shape.DimsData(), op_params.input1_offset,
          tflite::micro::GetTensorData<int8_t>(input2),
          extended_input2_shape.DimsData(), op_params.input2_offset);

      TF_LITE_ENSURE(context, err == 0);
  }
  if (output->type == kTfLiteInt16) {
      int err;
      const RuntimeShape extended_input1_shape =
        RuntimeShape::ExtendedShape(4, tflite::micro::GetTensorShape(input1));
      const RuntimeShape extended_input2_shape =
        RuntimeShape::ExtendedShape(4, tflite::micro::GetTensorShape(input2));
      const RuntimeShape extended_output_shape =
        RuntimeShape::ExtendedShape(4, tflite::micro::GetTensorShape(output));

      err = xa_nn_elm_mul_broadcast_4D_sym16sxsym16s_sym16s(
          tflite::micro::GetTensorData<int16_t>(output),
          extended_output_shape.DimsData(),
          op_params.output_shift, op_params.output_multiplier,
          op_params.quantized_activation_min,
          op_params.quantized_activation_max,
          tflite::micro::GetTensorData<int16_t>(input1),
          extended_input1_shape.DimsData(),
          tflite::micro::GetTensorData<int16_t>(input2),
          extended_input2_shape.DimsData());

      TF_LITE_ENSURE(context, err == 0);
  }
  return kTfLiteOk;
}

TfLiteStatus EvalMulFloatHiFi(TfLiteContext* context, TfLiteNode* node,
    TfLiteMulParams* params, const OpDataMul* data,
    const TfLiteEvalTensor* input1, const TfLiteEvalTensor* input2,
    TfLiteEvalTensor* output) {
  tflite::ArithmeticParams op_params = {};
  op_params.float_activation_min = data->output_activation_min_f32;
  op_params.float_activation_max = data->output_activation_max_f32;

  int err;
  const RuntimeShape& input1_shape = tflite::micro::GetTensorShape(input1);
  const RuntimeShape& input2_shape = tflite::micro::GetTensorShape(input2);
  const RuntimeShape& output_shape = tflite::micro::GetTensorShape(output);
  const int flat_size =
    MatchingElementsSize(input1_shape, input2_shape, output_shape);

  err = xa_nn_elm_mul_f32xf32_f32(tflite::micro::GetTensorData<float>(output),
      tflite::micro::GetTensorData<float>(input1),
      tflite::micro::GetTensorData<float>(input2), flat_size);

  TF_LITE_ENSURE(context, err == 0);

  err = xa_nn_vec_activation_min_max_f32_f32(
      tflite::micro::GetTensorData<float>(output), tflite::micro::GetTensorData<float>(output),
      op_params.float_activation_min, op_params.float_activation_max, flat_size);

  TF_LITE_ENSURE(context, err == 0);
  return kTfLiteOk;
}

TfLiteStatus MulEval(TfLiteContext* context, TfLiteNode* node) {
  TFLITE_DCHECK(node->builtin_data != nullptr);
  auto* params = reinterpret_cast<TfLiteMulParams*>(node->builtin_data);

  TFLITE_DCHECK(node->user_data != nullptr);
  const OpDataMul* data = static_cast<const OpDataMul*>(node->user_data);

  const TfLiteEvalTensor* input1 =
      tflite::micro::GetEvalInput(context, node, kMulInput1Tensor);
  const TfLiteEvalTensor* input2 =
      tflite::micro::GetEvalInput(context, node, kMulInput2Tensor);
  TfLiteEvalTensor* output =
      tflite::micro::GetEvalOutput(context, node, kMulOutputTensor);

  bool need_broadcast;
  if(input1->type == kTfLiteInt8 || input1->type == kTfLiteFloat32)
  {
    tflite::ArithmeticParams op_params = {};
    op_params.quantized_activation_min = data->output_activation_min;
    op_params.quantized_activation_max = data->output_activation_max;
    op_params.float_activation_max = data->output_activation_max_f32;
    op_params.input1_offset = -data->input1_zero_point;
    op_params.input2_offset = -data->input2_zero_point;
    op_params.output_offset = data->output_zero_point;
    op_params.output_multiplier = data->output_multiplier;
    op_params.output_shift = data->output_shift;

    need_broadcast = reference_ops::ProcessBroadcastShapes(
        tflite::micro::GetTensorShape(input1),
        tflite::micro::GetTensorShape(input2), &op_params);
  }

  switch (input1->type) {
    case kTfLiteInt8:
    case kTfLiteInt16:
#if defined(HIFI4) || defined(HIFI5)
      EvalMulQuantizedHiFi(context, node, data, input1, input2, output);
#else
      EvalMulQuantizedReference(context, node, data, input1, input2, output);
#endif
      break;
    case kTfLiteInt32:
        EvalMulQuantizedReference(context, node, data, input1, input2, output);
      break;
    case kTfLiteFloat32:
#if HIFI_VFPU 
      if (!need_broadcast) 
        EvalMulFloatHiFi(context, node, params, data, input1, input2, output);
      else
        EvalMulFloatReference(context, node, params, data, input1, input2, output);
#else
      EvalMulFloatReference(context, node, params, data, input1, input2, output);
#endif
      break;
    default:
      MicroPrintf("Type %s (%d) not supported.",
                  TfLiteTypeGetName(input1->type), input1->type);
      return kTfLiteError;
  }

  return kTfLiteOk;
}

TFLMRegistration Register_MUL() {
  return tflite::micro::RegisterOp(MulInit, MulPrepare, MulEval);
}

}  // namespace tflite
